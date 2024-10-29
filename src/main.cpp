//    Red - 3.3VDC Power
//    Black - Ground
//    Blue - I2C SDA Data
//    Yellow - I2C SCL Clock

#include "main.h"
#include "secrets.h"
#include "sdcard.h"

Preferences preferences;

WiFiMulti wifiMulti;
InfluxDBClient influxdb; //(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN);

Point datapoint("measurement");

#define OLED_RESET -1
// Adafruit_SSD1327 display(128, 128, &Wire, OLED_RESET, 1000000);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, 255, 22, 21);

// U8G2_SSD1327_MIDAS_128X128_1_HW_I2C display(U8G2_R0, 255, 22, 21);
uint8_t row_n;

Bsec2 sensor;
static uint8_t bsecState[BSEC_MAX_STATE_BLOB_SIZE];
static uint16_t stateUpdateCounter = 0;
const uint8_t bsec_config[] = {
#include "config/bme680/bme680_iaq_33v_3s_4d/bsec_iaq.txt"
};
#define STATE_SAVE_PERIOD UINT32_C(180 * 60 * 1000)

#define TMSIZE sizeof(tm)

bool WiFiConnected = false;
bool DBConnected = false;

uint32_t lastUpload = 0;
struct tm lastStateUpdate;

struct configs config;
struct errorStates errors = {0, 0, 0, 0, 0};
struct outputData outData = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
bool newData = false;

void newDataCallback(const bme68xData data, const bsecOutputs, Bsec2 bsec);
void checkBsecStatus(Bsec2 bsec);
void updateBsecState(Bsec2 bsec);
bool loadState(Bsec2 bsec);
bool saveState(Bsec2 bsec);

void setup(void)
{
    Serial.begin(115200);
    Serial.println("started");

    uint8_t row = 1;
    row_n = 7;
    char buf[64];

    // Display initialization
    {
        if (!display.begin())
        {
            Serial.println("Unable to initialize OLED");
            while (1)
                yield();
        }

        display.clearBuffer();
        display.setFont(u8g2_font_5x7_mf);
        display.drawStr(0, row * row_n, "Display Initialized");
        row++;
        display.sendBuffer();

        delay(2000);
    }

    // SD-Card initialization
    {
        while (!SD.begin())
        {
            Serial.println("Failed to mount SD card");
            display.clearDisplay();
            display.setCursor(0, 0);
            // display.println("SD Card not found!\nMake sure that a SD card is inserted!");
            display.drawStr(0, row * row_n, "SD Card not found!");
            display.sendBuffer();

            delay(2000);
        }

        uint64_t cardSize = SD.cardSize() / (1024 * 1024);
        Serial.printf("SD Card Size: %lluMB\n", cardSize);
        std::snprintf(buf, 64, "SD Card size: %lluMB", cardSize);
        display.drawStr(0, row * row_n, buf);
        row++;
        display.sendBuffer();

        // Initialize csv file
        if (!fileExists(SD, "/data.csv"))
        {
            // display.println("Initializing CSV file");
            display.drawStr(0, row * row_n, "Initializing CSV file");
            row++;
            display.sendBuffer();
            const char *dataHeader = "Timestamp;Temperature;Pressure;Humidity;IAQ;IAQAcc;CO2;VOC;Gas%\n";
            if (writeFile(SD, "/data.csv", dataHeader) != WR_OK)
            {
                Serial.println("Failed to create data file");
            }
        }
    }

    // Preferences (NVS) initialization
    preferences.begin("config");

    // display.println("Loading config");
    display.drawStr(0, row * row_n, "Loading user config");
    row++;
    display.sendBuffer();
    // Try to load config from preferences. If no config is found, reinitialize config to defaults
    if (!loadConfig())
    {
        struct configs tmpCfg = {5, 2, INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, WIFI_SSID, WIFI_PASSWORD};
        config = tmpCfg;
        saveConfig();
    }

    // WiFi initialization
    {
        // display.print("Connecting to WiFi");
        display.drawStr(0, row * row_n, "Connecting to WiFi");
        display.sendBuffer();

        WiFi.mode(WIFI_STA);
        wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);
        uint8_t j = display.getStrWidth("Connecting to WiFi");
        int i = 0;
        while (wifiMulti.run() != WL_CONNECTED && i < 9)
        {
            // display.print(".");
            display.drawStr(j, row * row_n, ".");
            j += 2;
            display.sendBuffer();
            Serial.println("Waiting for WiFi connection");
            delay(1000);
            i++;
        }
        // display.println();
        row++;
        // TODO: Add to display.
        if (i < 9)
        {
            Serial.println("WiFi connected to \n" + String(WIFI_SSID));
            WiFiConnected = true;
        }
        else
        {
            Serial.println("failed to connect to \n" + String(WIFI_SSID));
        }
    }

    // Time Syncing
    if (WiFiConnected)
    {
        Serial.print("Syncing time");
        // display.print("Syncing time");
        display.drawStr(0, row * row_n, "Syncing time");
        display.sendBuffer();
        configTime(config.GMTOff * 60 * 60, 3600, "pool.ntp.org", "time.nis.gov");

        uint8_t j = display.getStrWidth("Syncing time");
        int i = 0;
        while (time(nullptr) < 1000000000l && i < 20)
        {
            Serial.print(".");
            // display.print(".");
            display.drawStr(j, row * row_n, ".");
            j += 2;
            display.sendBuffer();
            delay(500);
            i++;
        }
        Serial.println();
        // display.println();
        row++;

        time_t now = time(nullptr);
        Serial.print("Synchronized time: ");
        Serial.println(ctime(&now));
    }

    // Database connection initialization
    if (WiFiConnected)
    {
        // display.println("Connecting to InfluxDB");
        display.drawStr(0, row * row_n, "Connecting to InfluxDB");
        row++;
        display.sendBuffer();
        influxdb.setConnectionParams(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN);
        datapoint.addTag("device", "logger1");
        if (influxdb.validateConnection())
        {
            Serial.println("Connected to influxdb server: " + String(influxdb.getServerUrl()));
            DBConnected = true;
        }
        else
        {
            Serial.println("Failed to connect to influxdb: " + String(influxdb.getLastErrorMessage()));
            display.drawStr(0, row * row_n, "InfluxDB connection failed!");
            row++;
            display.sendBuffer();
        }
    }

    // BME680 Initialization
    {
        // display.println("Initializing BME");
        display.drawStr(0, row * row_n, "Initializing BME");
        row++;
        display.sendBuffer();

        bsecSensor sensorList[] = {
            BSEC_OUTPUT_RAW_TEMPERATURE,
            BSEC_OUTPUT_RAW_PRESSURE,
            BSEC_OUTPUT_RAW_HUMIDITY,
            BSEC_OUTPUT_RAW_GAS,
            BSEC_OUTPUT_IAQ,
            BSEC_OUTPUT_STATIC_IAQ,
            BSEC_OUTPUT_CO2_EQUIVALENT,
            BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
            BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
            BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
            BSEC_OUTPUT_GAS_PERCENTAGE,
        };

        // Wire.begin();

        if (!sensor.begin(BME68X_I2C_ADDR_LOW, Wire))
        {
            checkBsecStatus(sensor);
        }

        if (!sensor.setConfig(bsec_config))
        {
            checkBsecStatus(sensor);
        }

        if (!loadState(sensor))
        {
            checkBsecStatus(sensor);
        }

        if (!sensor.updateSubscription(sensorList, ARRAY_LEN(sensorList), BSEC_SAMPLE_RATE_LP))
        {
            checkBsecStatus(sensor);
        }

        sensor.attachCallback(newDataCallback);

        Serial.println("BME Version: " + String(sensor.version.major) + "." + String(sensor.version.minor));
    }

    // display.println("Setup done!");
    display.drawStr(0, row * row_n, "Setup done!");
    row++;
    display.sendBuffer();
}

void loop(void)
{
    // Data Collection
    if (!sensor.run())
    {
        checkBsecStatus(sensor);
    }

    if (!newData)
    {
        return;
    }
    newData = false;

    struct tm ts;
    getLocalTime(&ts);

    // Write to SD
    // "Timestamp;Temperature;Pressure;Humidity;IAQ;IAQ_Static;stabilization;CO2;VOC;raw_Gas;Gas%\n"
    char *line;
    asprintf(&line, "%04d-%02d-%02dT%02d:%02d:%02d;%.3f;%.3f;%.3f;%.3f;%.3f;%hu;%.3f;%.3f;%.3f;%.3f\n",
             ts.tm_year + 1900, ts.tm_mon, ts.tm_mday, ts.tm_hour, ts.tm_min, ts.tm_sec,
             outData.compensated_temperature, outData.raw_pressure, outData.compensated_humidity,
             outData.iaq, outData.static_iaq, outData.stabilization_status, outData.co2_equivalent,
             outData.bvoc_equivalent, outData.raw_gas, outData.gas_percentage);
    int8_t status = appendFile(SD, "/data.csv", line);
    free(line);
    if (status != WR_OK)
    {
        Serial.println("Failed to append to file");
        errors.SDError = status;
    }
    else
    {
        errors.SDError = 0;
    }

    // Only upload data to influxdb if power-on stabilization is done
    // Also only upload max once per 10sec
    if (DBConnected && (millis() - lastUpload) > 10 * 1000)
    {
        if (wifiMulti.run() != WL_CONNECTED)
        {
            Serial.println("WiFi connection lost");
        }

        datapoint.clearFields();

        datapoint.addField("temperature", outData.compensated_temperature);
        datapoint.addField("humidity", outData.compensated_humidity);
        datapoint.addField("pressure", outData.raw_pressure);
        datapoint.addField("iaq", outData.iaq);
        datapoint.addField("iaq_static", outData.static_iaq);
        datapoint.addField("stabilization", outData.stabilization_status);
        datapoint.addField("co2", outData.co2_equivalent);
        datapoint.addField("voc", outData.bvoc_equivalent);
        datapoint.addField("gas_perc", outData.gas_percentage);

        if (!influxdb.writePoint(datapoint))
        {
            Serial.println("Failed to connect to influxdb: " + String(influxdb.getLastErrorMessage()));
        }
        else
        {
            lastUpload = millis();
        }
    }

    // Display
    /* display.clearBuffer();

    std::map<String, std::array<float, 3>> history_values;

    std::array<float, 3> temp_hist = {-2, -1, temperature};
    history_values.insert(std::pair<String, std::array<float, 3>>(String((char)247) + "C", temp_hist));

    std::array<float, 3> rh_hist = {-2, -1, humidity};
    history_values.insert(std::pair<String, std::array<float, 3>>("Rh", rh_hist));

    std::array<float, 3> co2_hist = {-2, -1, co2};
    history_values.insert(std::pair<String, std::array<float, 3>>("Co2", co2_hist));

    printTable(history_values);
    display.sendBuffer(); */

    display.clearBuffer();
    uint8_t row = 1;
    char buf[64];

    std::snprintf(buf, 64, "Temperature: %6.2f C", outData.compensated_temperature);
    display.drawStr(0, row * row_n, buf);
    row++;

    std::snprintf(buf, 64, "Humidity: %6.2f %%", outData.compensated_humidity);
    display.drawStr(0, row * row_n, buf);
    row++;

    std::snprintf(buf, 64, "Pressure: %6.2f hPA", outData.raw_pressure);
    display.drawStr(0, row * row_n, buf);
    row++;

    std::snprintf(buf, 64, "CO2: %6.2f ppm", outData.co2_equivalent);
    display.drawStr(0, row * row_n, buf);
    row++;

    std::snprintf(buf, 64, "VOC: %6.2f ppm", outData.bvoc_equivalent);
    display.drawStr(0, row * row_n, buf);
    row++;

    std::snprintf(buf, 64, "IAQ: %6.2f/%6.2f", outData.iaq, outData.static_iaq);
    display.drawStr(0, row * row_n, buf);
    row++;

    std::snprintf(buf, 64, "GAS: %6.2f/%6.2f%%", outData.raw_gas, outData.gas_percentage);
    display.drawStr(0, row * row_n, buf);
    row++;

    display.sendBuffer();
}

void printTable(std::map<String, std::array<float, 3>> values)
{
    uint8_t row = 1;
    for (std::map<String, std::array<float, 3>>::iterator it = values.begin(); it != values.end(); ++it)
    {
        auto v = it->second;
        // display.printf("%.3s|%3.1f|%3.1f|%3.1f\n", it->first, v[0], v[1], v[2]);
        char buf[64];
        std::snprintf(buf, 64, "%3s|%6.1f|%6.1f|%6.1f\n", it->first, v[0], v[1], v[2]);

        display.drawStr(0, row * row_n, buf);
        row++;
    }
}

void newDataCallback(const bme68xData data, const bsecOutputs outputs, Bsec2 bsec)
{
    if (!outputs.nOutputs)
        return;

    uint8_t idx = 0;
    newData = true;

    for (uint8_t i = 0; i < outputs.nOutputs; i++)
    {
        const bsecData output = outputs.output[i];

        if (i == 0)
        {
            outData.time_stamp = output.time_stamp;
        }

        switch (output.sensor_id)
        {
        case BSEC_OUTPUT_RAW_TEMPERATURE:
            outData.raw_temperature = output.signal;
            break;
        case BSEC_OUTPUT_RAW_PRESSURE:
            outData.raw_pressure = output.signal / 100;
            break;
        case BSEC_OUTPUT_RAW_HUMIDITY:
            outData.raw_humidity = output.signal;
            break;
        case BSEC_OUTPUT_RAW_GAS:
            outData.raw_gas = output.signal;
            break;
        case BSEC_OUTPUT_IAQ:
            outData.iaq = output.signal;
            break;
        case BSEC_OUTPUT_STATIC_IAQ:
            outData.static_iaq = output.signal;
            break;
        case BSEC_OUTPUT_CO2_EQUIVALENT:
            outData.co2_equivalent = output.signal;
            break;
        case BSEC_OUTPUT_BREATH_VOC_EQUIVALENT:
            outData.bvoc_equivalent = output.signal;
            break;
        case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE:
            outData.compensated_temperature = output.signal;
            break;
        case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY:
            outData.compensated_humidity = output.signal;
            break;
        case BSEC_OUTPUT_GAS_PERCENTAGE:
            outData.gas_percentage = output.signal;
            break;
        case BSEC_OUTPUT_STABILIZATION_STATUS:
            outData.stabilization_status = output.signal;
            break;
        default:
            break;
        }
    }
    updateBsecState(sensor);
}

void checkBsecStatus(Bsec2 bsec)
{
    errors.BSECError = bsec.status;
    errors.BMEError = bsec.sensor.status;

    if (bsec.status != BSEC_OK)
    {
        char buf[64];
        display.clearDisplay();
        display.setCursor(0, 0);

        while (bsec.status < BSEC_OK)
        {
            std::snprintf(buf, 64, "BSEC error code: %llu", bsec.status);
            display.drawStr(0, 7, buf);
            display.sendBuffer();
            Serial.println("BSEC error code: " + String(bsec.status));
            delay(1000);
        }

        if (bsec.status > BSEC_OK)
        {
            std::snprintf(buf, 64, "BSEC warning code: %llu", bsec.status);
            display.drawStr(0, 7, buf);
            display.sendBuffer();
            Serial.println("BSEC warning code: " + String(bsec.status));
            delay(30000);
        }
    }

    if (bsec.sensor.status != BME68X_OK)
    {
        char buf[64];
        display.clearDisplay();
        display.setCursor(0, 0);

        while (bsec.sensor.status < BME68X_OK)
        {
            std::snprintf(buf, 64, "BME error code: %llu", bsec.sensor.status);
            display.drawStr(0, 7, buf);
            display.sendBuffer();
            Serial.println("BME error code: " + String(bsec.sensor.status));
            delay(1000);
        }

        if (bsec.sensor.status > BME68X_OK)
        {
            std::snprintf(buf, 64, "BME warning code: %llu", bsec.sensor.status);
            display.drawStr(0, 7, buf);
            display.sendBuffer();
            Serial.println("BME warning code: " + String(bsec.sensor.status));
            delay(30000);
        }
    }
}

void updateBsecState(Bsec2 bsec)
{
    bool update = false;

    if (!stateUpdateCounter || (stateUpdateCounter * STATE_SAVE_PERIOD) < millis())
    {
        update = true;
        stateUpdateCounter++;
    }

    if (update && !saveState(bsec))
    {
        checkBsecStatus(bsec);
    }
}

bool loadState(Bsec2 bsec)
{
    if (preferences.getBytesLength("BSEC") == BSEC_MAX_STATE_BLOB_SIZE)
    {
        Serial.println("Loading state from NVS");
        preferences.getBytes("BSEC", bsecState, BSEC_MAX_STATE_BLOB_SIZE);
        preferences.getBytes("BSECTime", &lastStateUpdate, TMSIZE);

        Serial.println(&lastStateUpdate, "State loaded from NVS saved at %A, %B %d %Y %H:%M:%S");
        if (!bsec.setState(bsecState))
        {
            return false;
        }

        return true;
    }

    return false;
}

bool saveState(Bsec2 bsec)
{
    if (!bsec.getState(bsecState))
        return false;

    getLocalTime(&lastStateUpdate);
    Serial.println(&lastStateUpdate, "Updating NVS state at %A, %B %d %Y %H:%M:%S");

    preferences.putBytes("BSEC", bsecState, BSEC_MAX_STATE_BLOB_SIZE);
    preferences.putBytes("BSECTime", &lastStateUpdate, TMSIZE);
    Serial.println("NVS state updated");

    return true;
}

bool loadConfig()
{
    struct configs newConfig; // = {5, "", "", "", "", "", ""};
    size_t ret = preferences.getBytes("config", &newConfig, CONFIGSIZE);
    if (ret != CONFIGSIZE)
    {
        Serial.println("Failed to load config from NVS");
        return false;
    }

    Serial.println("Config loaded from NVS");
    config = newConfig;
    return true;
}

void saveConfig()
{
    preferences.putBytes("config", &config, CONFIGSIZE);
    return;
}