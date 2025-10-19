#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

class SettingsManager
{
public:
    static bool init();
    static bool saveSettings();
    static bool loadSettings();

    // Getters
    static bool getMeasuringEnabled() { return measuringEnabled; }
    static uint32_t getMeasureInterval() { return measureInterval; }

    // Setters
    static void setMeasuringEnabled(bool enabled);
    static void setMeasureInterval(uint32_t interval);

private:
    static const char *SETTINGS_FILE;
    static bool initialized;
    static bool measuringEnabled;
    static uint32_t measureInterval;
};

#endif // SETTINGS_H