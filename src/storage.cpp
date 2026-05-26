#include "storage.h"
#include "config.h"
#include <algorithm>

// Static member initialization
bool StorageManager::initialized = false;
bool StorageManager::timeIsSynced = false;
const char *StorageManager::lastError = nullptr;
const char *StorageManager::currentLogFile = nullptr;

void StorageManager::setError(const char *error)
{
    lastError = error;
    Serial.printf("Storage Error: %s\n", error);
}

void StorageManager::clearError()
{
    lastError = nullptr;
}

bool StorageManager::init()
{
    Serial.println("Initializing storage system...");

    // Try to mount the filesystem
    Serial.println("Mounting LittleFS...");
    if (!LittleFS.begin(true))
    {
        setError("Failed to mount LittleFS");
        initialized = false;
        return false;
    }
    Serial.println("LittleFS mounted successfully");

    // Check available space
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
    size_t freeBytes = totalBytes - usedBytes;

    Serial.printf("Storage Info - Total: %u bytes, Used: %u bytes, Free: %u bytes\n",
                  totalBytes, usedBytes, freeBytes);

    if (totalBytes == 0)
    {
        setError("No storage space available");
        initialized = false;
        return false;
    }

    // Guard against a filesystem larger than the configured maximum
    if (totalBytes > MAX_TOTAL_SPACE)
    {
        setError("Flash size exceeds maximum configured size");
        initialized = false;
        return false;
    }

    if (freeBytes < MIN_FREE_SPACE)
    {
        setError("Insufficient free space");
        initialized = false;
        return false;
    }

    // Clear current log file
    currentLogFile = nullptr;

    // Clean up old files if needed
    Serial.println("Cleaning up old files...");
    if (!maintainFileLimit())
    {
        setError("Failed to maintain file limit");
        initialized = false;
        return false;
    }

    // Check if time is set
    struct tm timeinfo;
    timeIsSynced = getLocalTime(&timeinfo);
    if (!timeIsSynced)
    {
        Serial.println("System time not set, waiting for time sync");
    }

    clearError();
    initialized = true;
    Serial.println("Storage system initialized successfully");
    return true;
}

bool StorageManager::createNewLogFile()
{
    if (!initialized)
    {
        setError("Storage system not initialized");
        return false;
    }

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
        setError("Cannot create file: system time not set");
        return false;
    }

    timeIsSynced = true;
    char timeStr[32];
    strftime(timeStr, sizeof(timeStr), "/%Y%m%d-%H%M%S.json", &timeinfo);
    static char fileNameBuffer[32];
    strcpy(fileNameBuffer, timeStr);
    currentLogFile = fileNameBuffer;

    File file = LittleFS.open(currentLogFile, "w");
    if (file)
    {
        if (file.print("[\n")) // Just opening bracket with newline
        {
            file.close();
            Serial.printf("Created new log file: %s\n", currentLogFile);
            return maintainFileLimit();
        }
        file.close();
    }

    char errorBuffer[64];
    snprintf(errorBuffer, sizeof(errorBuffer), "Failed to create new log file: %s", currentLogFile);
    setError(errorBuffer);
    return false;
}

bool StorageManager::store(const String &data)
{
    if (!initialized)
    {
        setError("Storage manager not initialized");
        return false;
    }

    if (!timeIsSynced)
    {
        struct tm timeinfo;
        if (!getLocalTime(&timeinfo))
        {
            setError("Cannot store data: waiting for time sync");
            return false;
        }
        timeIsSynced = true;
        if (!currentLogFile || strlen(currentLogFile) == 0 && !createNewLogFile())
        {
            return false;
        }
    }

    if (!currentLogFile || strlen(currentLogFile) == 0)
    {
        if (!createNewLogFile())
        {
            return false;
        }
    }

    // Check current file size
    File file = LittleFS.open(currentLogFile, "r");
    if (!file)
    {
        setError("Failed to open file for size check");
        return false;
    }

    size_t fileSize = file.size();
    file.close();

    // Create new file if needed
    if (fileSize >= MAX_FILE_SIZE)
    {
        if (!closeCurrentFile() || !createNewLogFile())
        {
            return false;
        }
    }

    // Append data to file
    file = LittleFS.open(currentLogFile, "r+");
    if (!file)
    {
        setError("Failed to open file for writing");
        return false;
    }

    bool success = true;

    // Check whether the file is empty or only contains "["
    if (fileSize <= 2)
    {
        // Empty or new file: write the first element
        file.seek(1); // Position right after the "["
        success = file.print(data);
    }
    else
    {
        // Existing file: position just before the trailing "]"
        file.seek(fileSize - 2);
        success = file.print(",\n  "); // Add a comma and indentation
        if (success)
        {
            success = file.print(data);
        }
    }

    if (!success)
    {
        setError("Failed to write data");
        file.close();
        return false;
    }

    // Always close with a ]
    success = file.println("\n]");
    file.close();

    if (!success)
    {
        setError("Failed to finalize JSON");
        return false;
    }

    clearError();
    return true;
}

bool StorageManager::closeCurrentFile()
{
    if (currentLogFile && strlen(currentLogFile) > 0)
    {
        File file = LittleFS.open(currentLogFile, "r+");
        if (file)
        {
            size_t fileSize = file.size();
            if (fileSize > 1)
            {
                file.seek(fileSize - 2);
                char lastChar = file.read();
                if (lastChar != ']')
                {
                    file.seek(fileSize - 1);
                    file.println("]");
                }
            }
            else
            {
                file.println("]");
            }
            file.close();
            return true;
        }
        setError("Failed to close current file");
        return false;
    }
    return true;
}

bool StorageManager::resetCurrentFile()
{
    if (currentLogFile && strlen(currentLogFile) > 0)
    {
        if (!closeCurrentFile())
        {
            return false;
        }
    }
    currentLogFile = nullptr;
    clearError();
    return true;
}

std::vector<const char *> StorageManager::listJsonFiles()
{
    static char fileNames[MAX_FILES][32]; // Static buffer holding the names
    static int currentIndex = 0;
    currentIndex = 0;

    std::vector<const char *> files;
    File root = LittleFS.open("/");
    if (!root || !root.isDirectory())
    {
        return files;
    }

    File file = root.openNextFile();
    while (file && currentIndex < MAX_FILES)
    {
        const char *name = file.name();
        // Log files are named YYYYMMDD-HHMMSS.json (they start with a digit).
        // This excludes web assets (manifest.json, …) and interval.json.
        if (!file.isDirectory() && strstr(name, ".json") && name[0] >= '0' && name[0] <= '9')
        {
            snprintf(fileNames[currentIndex], sizeof(fileNames[currentIndex]), "/%s", name);
            files.push_back(fileNames[currentIndex]);
            currentIndex++;
        }
        file = root.openNextFile();
    }
    return files;
}

bool StorageManager::maintainFileLimit()
{
    auto files = listJsonFiles();
    if (files.empty())
    {
        return true; // No files to maintain
    }

    std::sort(files.begin(), files.end()); // Sort by name (which includes date)

    bool success = true;
    while (files.size() > MAX_FILES)
    {
        if (!deleteFile(files[0]))
        { // Remove oldest file
            setError("Failed to remove old file");
            success = false;
            break;
        }
        files.erase(files.begin());
    }

    if (success)
    {
        clearError();
    }
    return success;
}

bool StorageManager::deleteFile(const char *filename)
{
    if (currentLogFile && strcmp(filename, currentLogFile) == 0)
    {
        setError("Cannot delete active file");
        return false;
    }

    if (!LittleFS.remove(filename))
    {
        static char errorBuffer[64];
        snprintf(errorBuffer, sizeof(errorBuffer), "Failed to delete file: %s", filename);
        setError(errorBuffer);
        return false;
    }

    clearError();
    return true;
}