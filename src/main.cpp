//    Red - 3.3VDC Power
//    Black - Ground
//    Blue - I2C SDA Data
//    Yellow - I2C SCL Clock
#include <EEPROM.h>
#include <Wire.h>
#include <Arduino.h>
#include <Adafruit_SSD1327.h>
#include <bsec.h>
#include <WiFiMulti.h>
#include <InfluxDbClient.h>

#include "secrets.h"

WiFiMulti wifiMulti;
InfluxDBClient influxdb(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN);

Point datapoint("measurement");

#define OLED_RESET -1
Adafruit_SSD1327 display(128, 128, &Wire, OLED_RESET, 1000000);

Bsec bme;
uint8_t bsecState[BSEC_MAX_STATE_BLOB_SIZE] = {0};
uint16_t stateUpdateCounter = 0;
const uint8_t bsec_config[] = {
#include "config/generic_33v_3s_28d/bsec_iaq.txt"
};
#define STATE_SAVE_PERIOD UINT32_C(360 * 60 * 1000)

void checkBME();
void updateState();
void loadState();

void setup(void)
{
  Serial.begin(115200);
  Serial.println("started");

  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

  while (wifiMulti.run() != WL_CONNECTED)
  {
    Serial.println("Waiting for WiFi connection");
    delay(100);
  }
  Serial.println("WiFi connected to " + String(WIFI_SSID));

  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");

  datapoint.addTag("device", "logger1");

  if (influxdb.validateConnection())
  {
    Serial.println("Connected to influxdb server: " + String(influxdb.getServerUrl()));
  }
  else
  {
    Serial.println("Failed to connect to influxdb: " + String(influxdb.getLastErrorMessage()));
  }

  if (!display.begin(0x3D))
  {
    Serial.println("Unable to initialize OLED");
    while (1)
      yield();
  }

  display.clearDisplay();
  display.display();

  display.setTextSize(1);
  display.setTextWrap(true);
  display.setTextColor(SSD1327_WHITE);

  bme.begin(BME680_I2C_ADDR_SECONDARY, Wire);
  Serial.println(String(bme.version.major) + "." + String(bme.version.minor));

  bme.setConfig(bsec_config);
  checkBME();

  loadState();

  bsec_virtual_sensor_t sensorList[10] = {
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
  };
  bme.updateSubscription(sensorList, 10, BSEC_SAMPLE_RATE_LP);

  checkBME();
}

void loop(void)
{
  unsigned long time_trigger = millis();
  if (!bme.run())
  {
    checkBME();
    return;
  }

  float temperature = bme.temperature;
  float humidity = bme.humidity;
  float pressure_hPA = bme.pressure / 100;
  float iaq = bme.iaq;
  float co2 = bme.co2Equivalent;
  float voc = bme.breathVocEquivalent;
  uint8_t iaq_acc = bme.iaqAccuracy;
  float gas_perc = bme.gasPercentage;
  float run_in = bme.runInStatus;

  // Only upload data to influxdb if power-on stabilization is done
  if (iaq_acc != 0)
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
  }

  display.setCursor(0, 0);
  display.clearDisplay();

  display.println("Temperature: " + String(temperature) + " C");
  display.println("Humidity: " + String(humidity) + "%");
  display.println("Pressure: " + String(pressure_hPA) + " hPA");
  display.println("IAQ: " + String(iaq) + "(" + String(iaq_acc) + "/3)");
  display.println("CO2: " + String(co2));
  display.println("VOC: " + String(voc));
  display.println("RUN-IN: " + String(run_in));
  display.display();

  updateState();

  delay(5000);
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
        delay(100);
    }
    else
    {
      display.setCursor(0, 0);
      display.println("BSEC warning code: " + String(bme.status));
      display.display();
      delay(2000);
    }
  }

  if (bme.bme680Status != BME680_OK)
  {
    if (bme.bme680Status < BME680_OK)
    {
      display.setCursor(0, 0);
      display.println("BME680 error code: " + String(bme.bme680Status));
      display.display();
      while (1)
        delay(100);
    }
    else
    {
      display.setCursor(0, 0);
      display.println("BME680 warning code: " + String(bme.bme680Status));
      display.display();
      delay(2000);
    }
  }
}

void loadState(void)
{
  if (EEPROM.read(0) == BSEC_MAX_STATE_BLOB_SIZE)
  {
    Serial.println("Loading state from EEPROM");
    for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++)
    {
      bsecState[i] = EEPROM.read(i + 1);
    }

    bme.setState(bsecState);
    Serial.println("State loaded from EEPROM");
    checkBME();
  }
  else
  {
    Serial.println("Clearing EEPROM");
    for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE + 1; i++)
    {
      EEPROM.write(i, 0);
    }

    EEPROM.commit();
    Serial.println("EEPROM Cleared");
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

    Serial.println("Updating EEPROM state at " + String(millis()));

    for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++)
    {
      EEPROM.write(i + 1, bsecState[i]);
    }

    EEPROM.write(0, BSEC_MAX_STATE_BLOB_SIZE);
    EEPROM.commit();
    Serial.println("EEPROM state updated");
  }
}