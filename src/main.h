#include <Preferences.h>
#include <Wire.h>
#include <Arduino.h>
#include <Adafruit_SSD1327.h>
#include <bsec.h>
#include <WiFiMulti.h>
#include <InfluxDbClient.h>
#include <map>
#include <nvs_flash.h>

struct configs
{
    uint32_t SDSaveInterval;
    int8_t GMTOff;
    char InfluxDBUrl[100];
    char InfluxDBOrg[30];
    char InfluxDBBucket[30];
    char InfluxDBToken[100];
    char WiFiSSID[32];
    char WifiPassword[63];
    char TZInfo[30];
};

#define CONFIGSIZE sizeof(configs)

struct errorStates
{
    int8_t SDError;
    int8_t WiFiError;
    int8_t DBError;
    int8_t BMEError;
    int8_t BSECError;
};

void printTable(std::map<String, std::array<float, 3>> values);
void checkBME();
void updateState();
void loadState();
bool loadConfig();
void saveConfig();