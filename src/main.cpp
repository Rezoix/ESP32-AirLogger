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
Adafruit_SSD1327 display(128, 128, &Wire, OLED_RESET, 1000000);

Bsec bme;
uint8_t bsecState[BSEC_MAX_STATE_BLOB_SIZE] = {0};
uint16_t stateUpdateCounter = 0;
const uint8_t bsec_config[] = {
#include "config/generic_33v_3s_4d/bsec_iaq.txt"
};
#define STATE_SAVE_PERIOD UINT32_C(360 * 60 * 1000)

#define TMSIZE sizeof(tm)

uint32_t lastUpload = 0;
struct tm lastStateUpdate;

struct configs config;
struct errorStates errors = {0, 0, 0, 0, 0};

void setup(void)
{
    Serial.begin(115200);
    Serial.println("started");

    // Display initialization
    {
        if (!display.begin(0x3D))
        {
            Serial.println("Unable to initialize OLED");
            while (1)
                yield();
        }

        display.setCursor(0, 0);
        display.clearDisplay();
        display.display();

        display.setTextSize(1);
        display.setTextWrap(true);
        display.setTextColor(SSD1327_GRAYTABLE);
        display.println("Display Initialized");
        display.display();
    }

    // SD-Card initialization
    {
        display.println("Initializing SD-Card");
        display.display();
        while (!SD.begin())
        {
            Serial.println("Failed to mount SD card");
            display.clearDisplay();
            display.setCursor(0, 0);
            display.println("SD Card not found!\nMake sure that a SD card is inserted!");
            display.display();

            delay(2000);
        }
        uint64_t cardSize = SD.cardSize() / (1024 * 1024);
        Serial.printf("SD Card Size: %lluMB\n", cardSize);

        // Initialize csv file
        if (!fileExists(SD, "/data.csv"))
        {
            display.println("Initializing CSV file");
            display.display();
            const char *dataHeader = "Timestamp;Temperature;Pressure;Humidity;IAQ;IAQAcc;CO2;VOC;Gas%\n";
            if (writeFile(SD, "/data.csv", dataHeader) != WR_OK)
            {
                Serial.println("Failed to create data file");
            }
        }
    }

    // nvs_flash_erase();
    // nvs_flash_init();

    // Preferences (NVS) initialization
    preferences.begin("config");

    display.println("Loading config");
    display.display();
    // Try to load config from preferences. If no config is found, reinitialize config to defaults
    if (!loadConfig())
    {
        struct configs tmpCfg = {5, 2, INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, WIFI_SSID, WIFI_PASSWORD};
        config = tmpCfg;
        saveConfig();
    }

    // WiFi initialization
    {
        display.print("Connecting to WiFi");
        display.display();
        WiFi.mode(WIFI_STA);
        wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

        while (wifiMulti.run() != WL_CONNECTED)
        {
            display.print(".");
            display.display();
            Serial.println("Waiting for WiFi connection");
            delay(100);
        }
        display.println();
        Serial.println("WiFi connected to " + String(WIFI_SSID));
        // timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");
        Serial.print("Syncing time");
        display.print("Syncing time");
        configTime(config.GMTOff * 60 * 60, 3600, "pool.ntp.org", "time.nis.gov");

        int i = 0;
        while (time(nullptr) < 1000000000l && i < 40)
        {
            Serial.print(".");
            display.print(".");
            display.display();
            delay(500);
            i++;
        }
        Serial.println();
        display.println();

        time_t now = time(nullptr);
        Serial.print("Synchronized time: ");
        Serial.println(ctime(&now));
    }

    // Database connection initialization
    {
        display.println("Connecting to InfluxDB");
        display.display();
        influxdb.setConnectionParams(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN);
        datapoint.addTag("device", "logger1");
        if (influxdb.validateConnection())
        {
            Serial.println("Connected to influxdb server: " + String(influxdb.getServerUrl()));
        }
        else
        {
            Serial.println("Failed to connect to influxdb: " + String(influxdb.getLastErrorMessage()));
        }
    }

    // BME680 Initialization
    {
        display.println("Initializing BME");
        display.display();
        bme.begin(BME680_I2C_ADDR_SECONDARY, Wire);
        Serial.println("BME Version: " + String(bme.version.major) + "." + String(bme.version.minor));

        bme.setConfig(bsec_config);
        checkBME();

        loadState();

        bsec_virtual_sensor_t sensorList[11] = {
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
        bme.updateSubscription(sensorList, 11, BSEC_SAMPLE_RATE_LP);

        checkBME();
    }

    display.println("Setup done!");
    display.display();
}

void loop(void)
{
    // Data Collection
    if (!bme.run())
    {
        checkBME();
        delay(1000);
        return;
    }
    struct tm ts;
    getLocalTime(&ts);

    float temperature = bme.temperature;
    float humidity = bme.humidity;
    float pressure_hPA = bme.pressure / 100;
    float iaq = bme.iaq;
    float co2 = bme.co2Equivalent;
    float voc = bme.breathVocEquivalent;
    uint8_t iaq_acc = bme.iaqAccuracy;
    float gas_perc = bme.gasPercentage;

    // Write to SD
    // "Timestamp;Temperature;Pressure;Humidity;IAQ;IAQAcc;CO2;VOC;Gas%\n"
    char *line;
    asprintf(&line, "%04d-%02d-%02dT%02d:%02d:%02d;%.3f;%.3f;%.3f;%.3f;%hu;%.3f;%.3f;%.3f\n",
             ts.tm_year + 1900, ts.tm_mon, ts.tm_mday, ts.tm_hour, ts.tm_min, ts.tm_sec,
             temperature, pressure_hPA, humidity, iaq, iaq_acc, co2, voc, gas_perc);
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
    if ((millis() - lastUpload) > 10 * 1000)
    {
        if (wifiMulti.run() != WL_CONNECTED)
        {
            Serial.println("WiFi connection lost");
        }

        datapoint.clearFields();

        datapoint.addField("temperature", temperature);
        datapoint.addField("humidity", humidity);
        datapoint.addField("pressure", pressure_hPA);
        datapoint.addField("iaq", iaq);
        datapoint.addField("iaq_acc", iaq_acc);
        datapoint.addField("co2", co2);
        datapoint.addField("voc", voc);
        datapoint.addField("gas_perc", gas_perc);

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
    display.setCursor(0, 0);
    display.clearDisplay();

    std::map<String, std::array<float, 3>> history_values;

    std::array<float, 3> temp_hist = {-2, -1, temperature};
    history_values.insert(std::pair<String, std::array<float, 3>>(String((char)247) + "C", temp_hist));

    std::array<float, 3> rh_hist = {-2, -1, humidity};
    history_values.insert(std::pair<String, std::array<float, 3>>("Rh", rh_hist));

    std::array<float, 3> co2_hist = {-2, -1, co2};
    history_values.insert(std::pair<String, std::array<float, 3>>("Co2", co2_hist));

    printTable(history_values);
    display.display();

    // display.printf("%7s%7s%7s%7s%7s%7s\n", "Temp", "RH%", "hPA", "Co2", "VoC", "IaQ");
    // display.printf("%3s|%3s|%4s|%4s|%3s|%3s\n", "Tmp", "RH%", "hP A", "Co 2", "VoC", "IaQ");

    /* display.println("Temperature: " + String(temperature) + " C");
    display.println("Humidity: " + String(humidity) + "%");
    display.println("Pressure: " + String(pressure_hPA) + " hPA");
    display.println("IAQ: " + String(iaq) + "(" + String(iaq_acc) + "/3)");
    display.println("CO2: " + String(co2));
    display.println("VOC: " + String(voc));
    display.display(); */

    updateState();

    delay(1000);
}

void printTable(std::map<String, std::array<float, 3>> values)
{
    for (std::map<String, std::array<float, 3>>::iterator it = values.begin(); it != values.end(); ++it)
    {
        auto v = it->second;
        display.printf("%.3s|%3.1f|%3.1f|%3.1f\n", it->first, v[0], v[1], v[2]);
    }
}

void checkBME(void)
{

    if (bme.status != BSEC_OK)
    {
        if (bme.status < BSEC_OK)
        {
            display.setCursor(0, 0);
            display.println("BSEC error code: " + String(bme.status));
            display.display();
            while (1)
                delay(1000);
        }
        else
        {
            display.setCursor(0, 0);
            display.println("BSEC warning code: " + String(bme.status));
            display.display();
            delay(10000);
        }
    }
    else
    {
        Serial.println("BSEC OK");
    }

    if (bme.bme680Status != BME680_OK)
    {
        if (bme.bme680Status < BME680_OK)
        {
            display.setCursor(0, 0);
            display.println("BME680 error code: " + String(bme.bme680Status));
            display.display();
            while (1)
                delay(1000);
        }
        else
        {
            display.setCursor(0, 0);
            display.println("BME680 warning code: " + String(bme.bme680Status));
            display.display();
            delay(10000);
        }
    }
    else
    {
        Serial.println("BME680 OK");
    }

    bme.status = BSEC_OK;
}

void loadState(void)
{
    if (preferences.getBytesLength("BSEC") == BSEC_MAX_STATE_BLOB_SIZE)
    {
        Serial.println("Loading state from NVS");
        preferences.getBytes("BSEC", bsecState, BSEC_MAX_STATE_BLOB_SIZE);
        preferences.getBytes("BSECTime", &lastStateUpdate, TMSIZE);

        Serial.println(&lastStateUpdate, "State loaded from NVS saved at %A, %B %d %Y %H:%M:%S");
        bme.setState(bsecState);
        checkBME();
    }
}

void updateState(void)
{
    bool update = false;

    if (stateUpdateCounter == 0)
    {
        if (bme.iaqAccuracy >= 3)
        {
            update = true;
            stateUpdateCounter++;
        }
    }
    else
    {
        if ((stateUpdateCounter * STATE_SAVE_PERIOD) < millis())
        {
            update = true;
            stateUpdateCounter++;
        }
    }

    if (update)
    {
        bme.getState(bsecState);
        checkBME();
        getLocalTime(&lastStateUpdate);
        Serial.println(&lastStateUpdate, "Updating NVS state at %A, %B %d %Y %H:%M:%S");

        preferences.putBytes("BSEC", bsecState, BSEC_MAX_STATE_BLOB_SIZE);
        preferences.putBytes("BSECTime", &lastStateUpdate, TMSIZE);
        Serial.println("NVS state updated");
    }
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