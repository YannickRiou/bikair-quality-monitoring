#include <Arduino.h>
#include <driver/rtc_cntl.h>
#include <soc/rtc_cntl_reg.h>

#include "config.h"
#include "tasks.h"
#include "sensors.h"
#include "network.h"
#include "storage.h"
#include "gps.h"
#include "power.h"
#include "led_manager.h"
#include "sensor_task_manager.h"
#include "sps30_manager.h"

// Task handles
TaskHandle_t TaskGPS = NULL;
TaskHandle_t TaskSensors = NULL;

// System state
bool sensorkTaskOn = false;
bool gpsTaskOn = true;
unsigned long measurementStart = 0;

// Set by the /sleep endpoint. Sleep is deferred to taskSensors so the HTTP
// response is flushed before WiFi is shut down (otherwise the client request
// fails and the dashboard reports a sleep error).
volatile bool sleepRequested = false;

HardwareSerial gpsSerial(2);

static void initOrRestart(const char *name, bool ok, const char *err)
{
    if (ok) {
        Serial.printf("[OK] %s\n", name);
    } else {
        Serial.printf("[FAIL] %s: %s\n", name, err ? err : "unknown");
        ESP.restart();
    }
}

void setup()
{
    // Less sensitive brownout detector & stable CPU clock
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    setCpuFrequencyMhz(CPU_FREQUENCY);

    Serial.begin(115200);
    delay(BOOT_DELAY_MS);
    Serial.println("\nBik'air boot");

    initOrRestart("Storage", StorageManager::init(), StorageManager::getLastError());
    initOrRestart("Network", NetworkManager::init(), NetworkManager::getLastError());

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    initOrRestart("Sensors", SensorManager::init(), SensorManager::getLastError());

    // Non-critical inits
    if (!SPS30Manager::begin()) {
        Serial.printf("[WARN] SPS30: %s\n", SPS30Manager::getLastError().c_str());
    }
    if (!GPSManager::init()) {
        Serial.printf("[WARN] GPS: %s\n", GPSManager::getLastError().c_str());
    }

    LEDManager::init();
    // SensorTaskManager keeps the measurement period but its own task is NOT started:
    // taskSensors below already reads every sensor. Starting both caused two
    // independent tasks to drive the SPS30 SoftwareSerial in parallel — UART
    // corruption + heap pressure that eventually killed the WiFi AP.
    SensorTaskManager::init();

    xTaskCreatePinnedToCore(taskGPS,     "TaskGPS",     2048, NULL, 1, &TaskGPS,     0);
    sensorkTaskOn = true;
    xTaskCreatePinnedToCore(taskSensors, "TaskSensors", 4096, NULL, 2, &TaskSensors, 1);

    Serial.println("Setup complete");
}

// Reboot if free heap drops below this many bytes — the WiFi stack needs
// ~10 KB to allocate buffers, so falling under that risks a silent AP crash.
static const uint32_t MIN_SAFE_HEAP = 12000;

void taskSensors(void *)
{
    static bool storeData = false;
    static uint8_t dataCounter = 0;
    static uint8_t heapLogTick = 0;

    while (true) {
        // Deferred sleep: the /sleep endpoint has already answered the client,
        // so we can now bring WiFi down and enter deep sleep safely.
        if (sleepRequested) {
            sleepRequested = false;
            vTaskDelay(pdMS_TO_TICKS(300)); // let the HTTP response flush
            PowerManager::prepareForSleep(true);
        }

        if (sensorkTaskOn) {
            String readings;
            if (SensorManager::readAllSensors(storeData, readings)) {
                NetworkManager::notifyClients(readings);

                if (dataCounter < 10) {
                    storeData = false;
                    dataCounter++;
                    digitalWrite(LED_PIN, LOW);
                } else {
                    storeData = true;
                    digitalWrite(LED_PIN, HIGH);
                }
            } else {
                Serial.printf("[ERR] sensors: %s\n", SensorManager::getLastError());
            }
        } else {
            storeData = false;
        }

        NetworkManager::cleanupClients();

        // Heap watchdog — reboot before the WiFi stack starves
        uint32_t freeHeap = ESP.getFreeHeap();
        if (++heapLogTick >= 30) {
            heapLogTick = 0;
            Serial.printf("[HEAP] free=%u min=%u\n", freeHeap, ESP.getMinFreeHeap());
        }
        if (freeHeap < MIN_SAFE_HEAP) {
            Serial.printf("[FATAL] heap exhausted (%u), restarting\n", freeHeap);
            StorageManager::closeCurrentFile();
            delay(200);
            ESP.restart();
        }

        vTaskDelay(pdMS_TO_TICKS(SensorTaskManager::getPeriod()));
    }
}

void taskGPS(void *)
{
    while (true) {
        if (gpsTaskOn) {
            GPSManager::process();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void loop()
{
    static unsigned long wakeTime = millis();
    static bool fixAcquired = false;

    LEDManager::update();

    // Phase 1: wait for GPS fix or timeout
    while ((millis() - wakeTime) < FIX_TIMEOUT) {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        if (GPSManager::hasFix()) {
            fixAcquired = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    // Phase 2: measurement window
    measurementStart = millis();
    Serial.println("Measurements running");

    const uint32_t window = SensorTaskManager::getPeriod() * NUMBER_OF_MEASUREMENTS;
    while ((millis() - measurementStart) < window) {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        if (!GPSManager::hasFix()) {
            Serial.println("[WARN] GPS fix lost");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // Phase 3: sleep
    if (PowerManager::isSleepEnabled()) {
        PowerManager::prepareForSleep(false);
    } else {
        delay(TIME_TO_SLEEP * 1000);
    }
}
