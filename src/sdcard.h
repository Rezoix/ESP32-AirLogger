#include "FS.h"
#include "SD.h"
#include "SPI.h"

#define OPEN_FAILED -1
#define WR_FAILED 1
#define WR_OK 0

bool fileExists(fs::FS &fs, const char *path);
int8_t writeFile(fs::FS &fs, const char *path, const char *data);
int8_t appendFile(fs::FS &fs, const char *path, const char *data);