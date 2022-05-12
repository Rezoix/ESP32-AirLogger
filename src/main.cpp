#include <Wire.h>
#include <Arduino.h>
#include <Adafruit_SSD1327.h>
#include <bsec.h>
#include <WiFiMulti.h>
#include "secrets.h"
//    Red - 3.3VDC Power
//    Black - Ground
//    Blue - I2C SDA Data
//    Yellow - I2C SCL Clock

#define OLED_RESET -1

Adafruit_SSD1327 display(128, 128, &Wire, OLED_RESET, 1000000);

Bsec bme;
String output;

void checkBME();

void setup()
{
  Serial.begin(115200);
  Serial.println("started");

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

  checkBME();

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

void loop()
{
  display.setCursor(0, 0);
  display.clearDisplay();

  unsigned long time_trigger = millis();
  if (!bme.run())
  {
    checkBME();
    return;
  }

  display.println("Temperature: " + String(bme.temperature) + " C");
  display.println("Humidity: " + String(bme.humidity) + "%");
  display.println("Pressure: " + String(bme.pressure / 100) + " hPA");
  display.println("IAQ: " + String(bme.iaq));
  display.println("CO2: " + String(bme.co2Equivalent));
  display.println("VOC: " + String(bme.breathVocEquivalent));
  display.display();

  delay(1000);
}

void checkBME()
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