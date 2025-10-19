#ifndef MAIN_H
#define MAIN_H

#include <Arduino.h>
#include <Wire.h>
#include <SoftwareSerial.h>
#include <vector>
#include <LittleFS.h>
#include "wireless.h"
#include "sensors.h"

// Constantes
#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 20          /* Time ESP32 will go to sleep (in seconds) */

// Tâches FreeRTOS
extern TaskHandle_t TaskGPS;
extern TaskHandle_t TaskSensors;

// Classes de gestion
class StorageManager
{
public:
    static bool init();
    static bool store(const String &data);
    static bool maintainFileLimit();
    static const char *getLastError() { return lastError; }
    static bool isInitialized() { return initialized; }

private:
    static bool initialized;
    static const char *lastError;
    static void setError(const char *error);
    static void clearError();
};

class GPSManager
{
public:
    static bool init();
    static void processTask(void *pvParameters);
    static double getLatitude();
    static double getLongitude();
    static float getSpeed();
    static String getTime();
    static const char *getLastError() { return lastError; }
    static bool isInitialized() { return initialized; }

private:
    static bool initialized;
    static const char *lastError;

    static double convert_latlon_to_decimal(String gga_coord);
    static String convert_utc_to_readable(String utc_time);
    static void parseGPGGA(String sentence);
    static void parseGPRMC(String sentence);
    static void parseGPVTG(String sentence);

    static void setError(const char *error);
    static void clearError();
};

class PowerManager
{
public:
    static void prepareForSleep(bool deepSleep);
    static void enterDeepSleep();
};

// Fonctions de tâche principales
void taskGPS(void *pvParameters);
void taskSensors(void *pvParameters);

#endif