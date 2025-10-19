#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include <LittleFS.h>
#include <vector>

class StorageManager
{
public:
    static bool init();
    static bool store(const String &jsonData);
    static bool createNewLogFile();
    static bool closeCurrentFile();
    static bool maintainFileLimit();
    static bool deleteFile(const char *filename); // Utilise char* au lieu de String
    static bool resetCurrentFile();               // Nouvelle méthode pour réinitialiser le fichier courant
    static const char *getCurrentLogFile() { return currentLogFile; }
    static std::vector<const char *> listJsonFiles();
    static bool isInitialized() { return initialized; }
    static const char *getLastError() { return lastError; }

private:
    static bool initialized;
    static bool timeIsSynced;
    static const char *currentLogFile; // Utilise char* au lieu de String
    static const char *lastError;      // Utilise char* au lieu de String

    static void setError(const char *error);
    static void clearError();
    static bool checkStorageSpace(size_t additionalSpace = 0);
};

#endif // STORAGE_H