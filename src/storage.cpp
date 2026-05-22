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

    // Vérifier que nous ne dépassons pas la taille maximale configurée
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

    // Vérifier si le fichier est vide ou contient seulement "["
    if (fileSize <= 2)
    {
        // Fichier vide ou nouveau, on écrit le premier élément
        file.seek(1); // Se positionner après le "["
        success = file.print(data);
    }
    else
    {
        // Fichier existant, on se positionne avant le dernier "]"
        file.seek(fileSize - 2);
        success = file.print(",\n  "); // Ajouter une virgule et une indentation
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

    // On termine toujours par un ]
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
    static char fileNames[MAX_FILES][32]; // Buffer statique pour stocker les noms
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
        if (!file.isDirectory() && strstr(file.name(), ".json"))
        {
            // Exclure les fichiers de configuration connus
            if (strcmp(file.name(), "interval.json") != 0) // Exclure interval.json
            {
                snprintf(fileNames[currentIndex], sizeof(fileNames[currentIndex]), "/%s", file.name());
                files.push_back(fileNames[currentIndex]);
                currentIndex++;
            }
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