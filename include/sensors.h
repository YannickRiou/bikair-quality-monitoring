#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <RunningMedian.h>
#include "ScioSense_ENS160.h"
#include "AHT10.h"
#include "sps30.h"

class SensorManager
{
public:
    static bool init();
    static bool readAllSensors(bool store, String &jsonData);
    static void adjustMeasurementPeriod();
    static void prepareForSleep();
    static const char *getLastError() { return lastError; }

    // Sensor data accessors
    static float getTemperature() { return temperatureMeas.getMedian(); }
    static float getHumidity() { return humidityMeas.getMedian(); }
    static float getCO2() { return co2Meas.getMedian(); }
    static float getTVOC() { return tvocMeas.getMedian(); }
    static float getSpeed() { return speedMeas.getMedian(); }
    static SPS30 &getSPS30() { return sps30; }
    static bool isInitialized() { return initialized; }

    // Manual interval management
    static void setManualInterval(uint32_t interval);
    static uint32_t getManualInterval() { return manualMeasurementInterval; }
    static bool isManualIntervalEnabled() { return useManualInterval; }

private:
    static bool initialized;
    static uint32_t manualMeasurementInterval; // in seconds
    static bool useManualInterval;
    static const char *lastError;

    static RunningMedian temperatureMeas;
    static RunningMedian humidityMeas;
    static RunningMedian co2Meas;
    static RunningMedian tvocMeas;
    static RunningMedian speedMeas;
    static float lastSpeedMeasurement;

    static ScioSense_ENS160 ens160;
    static AHT10 aht20;
    static SPS30 sps30;

    static void setError(const char *error);
    static void clearError();
};

#endif // SENSORS_H