#include "power.h"
#include "config.h"
#include "sensors.h"
#include "gps.h"
#include "storage.h"
#include <WiFi.h>
#include "driver/rtc_cntl.h"
#include "esp32/clk.h"
#include "soc/rtc_cntl_reg.h"

bool PowerManager::sleepEnabled = false;

void PowerManager::init()
{
    // Configure brownout detector to be less sensitive
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    // Set CPU frequency for stability
    setCpuFrequencyMhz(160);

    // Configure status LED
    pinMode(LED_PIN, OUTPUT);
}

void PowerManager::prepareForSleep(bool deepSleep)
{
    extern bool sensorkTaskOn;
    extern bool gpsTaskOn;

    // Stop ongoing tasks
    sensorkTaskOn = false;
    gpsTaskOn = false;

    // Allow tasks to finish
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Shutdown peripherals
    shutdownPeripherals();

    if (deepSleep)
    {
        // Manual standby ("Veille"): no wake source, so the device stays asleep
        // at minimum power until the user presses reset.
        esp_deep_sleep_start();
    }
    else
    {
        // Light sleep: arm a timer so the measurement cycle resumes on its own.
        esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
        esp_light_sleep_start();
    }
}

void PowerManager::enableSleepMode(bool enable)
{
    sleepEnabled = enable;
}

void PowerManager::shutdownPeripherals()
{
    // Close file system
    StorageManager::closeCurrentFile();
    LittleFS.end();

    // Shutdown sensors
    SensorManager::prepareForSleep();

    // Put GPS to sleep
    GPSManager::getSerial().println("$PMTK161,0*28");
    GPSManager::getSerial().end();

    // Disable WiFi
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // Turn off status LED
    digitalWrite(LED_PIN, LOW);
}