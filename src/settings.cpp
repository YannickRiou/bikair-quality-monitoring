#include "settings.h"

const char *SettingsManager::SETTINGS_FILE = "/settings.json";
bool SettingsManager::initialized = false;
bool SettingsManager::measuringEnabled = true; // Par défaut actif
uint32_t SettingsManager::measureInterval = 0; // Par défaut auto

bool SettingsManager::init()
{
    if (initialized)
    {
        return true;
    }

    if (!loadSettings())
    {
        // Si le chargement échoue, on sauve les paramètres par défaut
        saveSettings();
    }

    initialized = true;
    return true;
}

bool SettingsManager::saveSettings()
{
    JsonDocument doc;
    doc["measuring"] = measuringEnabled;
    doc["interval"] = measureInterval;

    File file = LittleFS.open(SETTINGS_FILE, "w");
    if (!file)
    {
        Serial.println("Failed to open settings file for writing");
        return false;
    }

    if (serializeJson(doc, file) == 0)
    {
        Serial.println("Failed to write settings to file");
        file.close();
        return false;
    }

    file.close();
    Serial.println("Settings saved successfully");
    return true;
}

bool SettingsManager::loadSettings()
{
    if (!LittleFS.exists(SETTINGS_FILE))
    {
        Serial.println("No settings file found");
        return false;
    }

    File file = LittleFS.open(SETTINGS_FILE, "r");
    if (!file)
    {
        Serial.println("Failed to open settings file for reading");
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error)
    {
        Serial.println("Failed to parse settings file");
        return false;
    }

    measuringEnabled = doc["measuring"] | true;
    measureInterval = doc["interval"] | 0;

    Serial.println("Settings loaded successfully");
    return true;
}

void SettingsManager::setMeasuringEnabled(bool enabled)
{
    if (measuringEnabled != enabled)
    {
        measuringEnabled = enabled;
        saveSettings();
    }
}

void SettingsManager::setMeasureInterval(uint32_t interval)
{
    if (measureInterval != interval)
    {
        measureInterval = interval;
        saveSettings();
    }
}