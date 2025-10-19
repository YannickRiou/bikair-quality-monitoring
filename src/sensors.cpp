#include "sensors.h"
#include "config.h"
#include "storage.h"
#include "gps.h"
#include "sensor_task_manager.h"
#include <Wire.h>
#include <ArduinoJson.h>

// Static member initialization
bool SensorManager::initialized = false;
const char *SensorManager::lastError = nullptr;

RunningMedian SensorManager::temperatureMeas(TEMPERATURE_SAMPLES);
RunningMedian SensorManager::humidityMeas(HUMIDITY_SAMPLES);
RunningMedian SensorManager::co2Meas(CO2_SAMPLES);
RunningMedian SensorManager::tvocMeas(TVOC_SAMPLES);
RunningMedian SensorManager::speedMeas(SPEED_SAMPLES);
float SensorManager::lastSpeedMeasurement = 0;

ScioSense_ENS160 SensorManager::ens160(0x53);
AHT10 SensorManager::aht20(AHT10_ADDRESS_0X38, AHT20_SENSOR);
SPS30 SensorManager::sps30;

void SensorManager::setError(const char *error)
{
    lastError = error;
    Serial.printf("SensorManager Error: %s\n", error);
}

void SensorManager::clearError()
{
    lastError = nullptr;
}

bool SensorManager::init()
{
    if (initialized)
    {
        return true; // Already initialized
    }

    // Load saved interval settings
    if (LittleFS.exists("/interval.json"))
    {
        File file = LittleFS.open("/interval.json", "r");
        if (file)
        {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, file);
            file.close();

            if (!error)
            {
                useManualInterval = doc["enabled"] | false;
                manualMeasurementInterval = doc["interval"] | DEFAULT_MANUAL_INTERVAL;

                if (useManualInterval)
                {
                    uint16_t newPeriod = manualMeasurementInterval * 1000;
                    SensorTaskManager::setPeriod(newPeriod);
                    Serial.printf("Loaded saved interval: %d ms\n", newPeriod);
                }
            }
        }
    }

    // Initialize I2C once
    if (!Wire.begin(I2C_SDA, I2C_SCL))
    {
        setError("I2C initialization failed");
        initialized = false;
        return false;
    }
    Wire.setClock(100000); // Reduced speed for stability

    // Initialize ENS160
    if (!ens160.begin())
    {
        setError("ENS160 not found");
        initialized = false;
        return false;
    }
    if (!ens160.setMode(ENS160_OPMODE_STD))
    {
        setError("ENS160 mode setting failed");
        initialized = false;
        return false;
    }
    Serial.println("ENS160 initialized successfully");

    // Initialize AHT20
    if (!aht20.begin())
    {
        setError("AHT20 not found");
        initialized = false;
        return false;
    }
    Serial.println("AHT20 initialized successfully");

    // Initialize SPS30
    sps30.SetSerialPin(SPS30_RX, SPS30_TX);
    if (!sps30.begin(SERIALPORT1))
    {
        setError("SPS30 initialization failed");
        initialized = false;
        return false;
    }
    if (!sps30.probe())
    {
        setError("SPS30 probe failed");
        initialized = false;
        return false;
    }
    if (!sps30.reset())
    {
        setError("SPS30 reset failed");
        initialized = false;
        return false;
    }
    if (!sps30.start())
    {
        setError("SPS30 measurement start failed");
        initialized = false;
        return false;
    }
    Serial.println("SPS30 initialized successfully");

    clearError();
    initialized = true;
    return true;
}

bool SensorManager::readAllSensors(bool store, String &jsonData)
{
    if (!initialized)
    {
        setError("Sensor manager not initialized");
        return false;
    }

    JsonDocument readings;
    struct sps_values spsValues;

    // Read ENS160 sensor
    if (!ens160.measure())
    {
        setError("ENS160 measurement failed");
        return false;
    }
    float tvoc = ens160.getTVOC();
    float co2 = ens160.geteCO2();
    uint8_t AQI = ens160.getAQI();

    // Read AHT20 sensor
    float temperature = aht20.readTemperature();
    float humidity = aht20.readHumidity();

    // Read SPS30 sensor
    sps30.GetValues(&spsValues);

    // Update running medians
    humidityMeas.add(humidity);
    temperatureMeas.add(temperature);
    co2Meas.add(co2);
    tvocMeas.add(tvoc);
    speedMeas.add(GPSManager::getSpeed().toFloat());
    lastSpeedMeasurement = GPSManager::getSpeed().toFloat();

    // Prepare JSON document with system time
    struct tm timeinfo;
    if (getLocalTime(&timeinfo))
    {
        char timeStr[32];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
        readings["time_utc"] = String(timeStr);
    }
    else
    {
        readings["time_utc"] = "unknown";
    }
    readings["co2"] = String(co2Meas.getMedian());
    readings["tvoc"] = String(tvocMeas.getMedian());
    readings["humidity"] = String(humidityMeas.getMedian());
    readings["temperature"] = String(temperatureMeas.getMedian());
    readings["aqi"] = String(AQI);
    readings["pm1"] = String(spsValues.MassPM1);
    readings["pm2"] = String(spsValues.MassPM2);
    readings["gpsfix"] = GPSManager::getFixStatus();
    readings["latitude"] = GPSManager::getLatitude();
    readings["longitude"] = GPSManager::getLongitude();
    readings["satellites"] = GPSManager::getSatellites();
    readings["altitude"] = GPSManager::getAltitude();
    readings["speed"] = String(speedMeas.getAverage());

    serializeJson(readings, jsonData);

    if (store)
    {
        if (!StorageManager::store(jsonData))
        {
            setError("Failed to store sensor data");
            return false;
        }
    }

    clearError();
    return true;
}

// Initialize static members
uint32_t SensorManager::manualMeasurementInterval = DEFAULT_MANUAL_INTERVAL;
bool SensorManager::useManualInterval = false;

void SensorManager::setManualInterval(uint32_t interval)
{
    Serial.printf("Setting manual interval: %d (current: %d, enabled: %d)\n",
                  interval, manualMeasurementInterval, useManualInterval);

    if (interval == 0)
    {
        useManualInterval = false;
        Serial.println("Switching to automatic interval mode");
    }
    else
    {
        if (interval < MIN_MANUAL_INTERVAL)
        {
            Serial.printf("Interval %d too low, setting to minimum: %d\n",
                          interval, MIN_MANUAL_INTERVAL);
            interval = MIN_MANUAL_INTERVAL;
        }
        if (interval > MAX_MANUAL_INTERVAL)
        {
            Serial.printf("Interval %d too high, setting to maximum: %d\n",
                          interval, MAX_MANUAL_INTERVAL);
            interval = MAX_MANUAL_INTERVAL;
        }
        manualMeasurementInterval = interval;
        useManualInterval = true;
        Serial.printf("Manual interval set to: %d seconds\n", interval);
    }

    // Save settings to LittleFS
    JsonDocument doc;
    doc["enabled"] = useManualInterval;
    doc["interval"] = manualMeasurementInterval;

    File file = LittleFS.open("/interval.json", "w");
    if (file)
    {
        serializeJson(doc, file);
        file.close();
        Serial.println("Interval settings saved to flash");
    }

    // Apply new interval immediately
    if (useManualInterval)
    {
        uint16_t newPeriod = manualMeasurementInterval * 1000; // Convert seconds to milliseconds
        SensorTaskManager::setPeriod(newPeriod);
        Serial.printf("Measure period set to %d ms\n", newPeriod);
    }
}

void SensorManager::adjustMeasurementPeriod()
{
    extern uint16_t measurePeriod;

    if (useManualInterval)
    {
        measurePeriod = manualMeasurementInterval * 1000; // Convert seconds to milliseconds
        return;
    }

    if (speedMeas.getAverage() > 5)
    {
        if (speedMeas.getAverage() > lastSpeedMeasurement)
        {
            measurePeriod = max(1000, measurePeriod - 100);
        }
        else if (speedMeas.getAverage() < lastSpeedMeasurement)
        {
            measurePeriod = min(10000, measurePeriod + 100);
        }
    }
}

void SensorManager::prepareForSleep()
{
    sps30.stop();
    ens160.setMode(ENS160_OPMODE_DEP_SLEEP);
    Wire.end();
}