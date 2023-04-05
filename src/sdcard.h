#include "FS.h"
#include "SD.h"
#include "SPI.h"

#define OPEN_FAILED -1
#define WRITE_FAILED 1
#define WRITE_OK 0

void readFile(fs::FS &fs, const char *path);
bool fileExists(fs::FS &fs, const char *path);
int8_t writeFile(fs::FS &fs, const char *path, const char *data);
int8_t appendFile(fs::FS &fs, const char *path, const char *data);