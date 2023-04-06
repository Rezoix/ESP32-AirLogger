#include "FS.h"
#include "SD.h"
#include "SPI.h"

#include "main.h"
#include "sdcard.h"

bool fileExists(fs::FS &fs, const char *path)
{
    return fs.exists(path);
}

int8_t writeFile(fs::FS &fs, const char *path, const char *data, const char *mode)
{
    File file = fs.open(path, mode);

    if (!file)
    {
        return OPEN_FAILED;
    }
    if (file.print(data))
    {
        file.close();
        return WR_OK;
    }
    else
    {
        file.close();
        return WR_FAILED;
    }
}

int8_t writeFile(fs::FS &fs, const char *path, const char *data)
{
    return writeFile(fs, path, data, FILE_WRITE);
}

int8_t appendFile(fs::FS &fs, const char *path, const char *data)
{
    return writeFile(fs, path, data, FILE_APPEND);
}

/* void createDir(fs::FS &fs, const char *path)
{
    Serial.printf("Creating Dir: %s\n", path);
    if (fs.mkdir(path))
    {
        Serial.println("Dir created");
    }
    else
    {
        Serial.println("mkdir failed");
    }
}

void removeDir(fs::FS &fs, const char *path)
{
    Serial.printf("Removing Dir: %s\n", path);
    if (fs.rmdir(path))
    {
        Serial.println("Dir removed");
    }
    else
    {
        Serial.println("rmdir failed");
    }
}
*/