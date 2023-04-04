#include "FS.h"
#include "SD.h"
#include "SPI.h"

void listDir(fs::FS &fs, const char *dirname, uint8_t levels);
void readFile(fs::FS &fs, const char *path);
bool fileExists(fs::FS &fs, const char *path);
void writeFile(fs::FS &fs, const char *path, const char *data);
void appendFile(fs::FS &fs, const char *path, const char *data);