#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <RunningMedian.h>
#include <ArduinoJson.h>
#include <driver/rtc_cntl.h>
#include <soc/rtc_cntl_reg.h>
#include "utils.h"
#include "tasks.h"
#include "config.h"
#include "sensors.h"
#include "network.h"
#include "storage.h"
#include "gps.h"
#include "power.h"
#include "led_manager.h"
#include "sensor_task_manager.h"

// Network variables
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;
IPAddress local_IP = LOCAL_IP;
IPAddress gateway = GATEWAY;
IPAddress subnet = SUBNET;

// Web Server and WebSocket
AsyncWebSocket ws("/ws");

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

// Task handles
TaskHandle_t TaskGPS = NULL;
TaskHandle_t TaskSensors = NULL;

// System state
bool sensorkTaskOn = false;
bool gpsTaskOn = true;
unsigned long measurementStart = 0;

uint8_t ledVal = 0;
HardwareSerial gpsSerial(2);

void setup()
{
    //-------------------------------------------------------------------------
    // System Configuration
    //-------------------------------------------------------------------------
    // Configure brownout detector to be less sensitive
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    // Set CPU frequency for stability
    setCpuFrequencyMhz(160);

    // Initialize Serial and wait for stability
    Serial.begin(115200);
    delay(100);

    Serial.println("Starting initialization sequence...");

    //-------------------------------------------------------------------------
    // Critical Systems Initialization
    //-------------------------------------------------------------------------
    // Initialize Storage
    Serial.println("Initializing storage...");
    if (!StorageManager::init())
    {
        Serial.println("Storage initialization failed: " + String(StorageManager::getLastError()));
        ESP.restart();
        return;
    }
    Serial.println("Storage initialized successfully");

    // Initialize Network
    Serial.println("Initializing network...");
    if (!NetworkManager::init())
    {
        Serial.println("Network initialization failed: " + String(NetworkManager::getLastError()));
        ESP.restart();
        return;
    }
    Serial.print("Access point IP address: ");
    Serial.println(WiFi.softAPIP());

    //-------------------------------------------------------------------------
    // Sensor Initialization
    //-------------------------------------------------------------------------
    // Initialize LED pin
    pinMode(GPIO_NUM_2, OUTPUT);
    digitalWrite(GPIO_NUM_2, LOW);

    // Initialize Sensors
    Serial.println("Initializing sensors...");
    if (!SensorManager::init())
    {
        Serial.println("Sensor initialization failed: " + String(SensorManager::getLastError()));
        ESP.restart();
        return;
    }
    Serial.println("Sensors initialized successfully");

    // Initialize and start SPS30 sensor
    if (!SPS30Manager::begin())
    {
        Serial.println("SPS30 initialization failed: " + String(SPS30Manager::getLastError()));
        // Continue anyway, we'll try to recover later
    }
    else
    {
        Serial.println(F("SPS30 initialized successfully"));
    }

    // Initialize GPS
    if (!GPSManager::init())
    {
        Serial.println("GPS initialization failed: " + String(GPSManager::getLastError()));
        // Continue anyway, GPS is not critical
    }

    // Initialize LED manager
    LEDManager::init();

    // Initialize and start task managers
    SensorTaskManager::init();
    SensorTaskManager::start();

    // Create GPS task
    xTaskCreatePinnedToCore(taskGPS, "TaskGPS", 2048, NULL, 1, &TaskGPS, 0);

    // Create and start sensor task
    sensorkTaskOn = true;
    xTaskCreatePinnedToCore(taskSensors, "TaskSensors", 4096, NULL, 2, &TaskSensors, 1);

    Serial.println("Web server started");
}

void taskSensors(void *pvParameters)
{
    static bool storeData = false;
    static uint8_t dataCounter = 0;
    static uint8_t ledVal = 0;

    while (true)
    {
        if (sensorkTaskOn)
        {
            String sensorReadings;
            if (SensorManager::readAllSensors(storeData, sensorReadings))
            {
                NetworkManager::notifyClients(sensorReadings);

                if (dataCounter < 10)
                {
                    storeData = false;
                    dataCounter++;
                    digitalWrite(LED_PIN, LOW);
                }
                else
                {
                    storeData = true;
                    digitalWrite(LED_PIN, HIGH);
                }
            }
            else
            {
                Serial.println("Failed to read sensors: " + String(SensorManager::getLastError()));
            }
        }
        else
        {
            storeData = false;
        }

        NetworkManager::cleanupClients();
        vTaskDelay(pdMS_TO_TICKS(SensorTaskManager::getPeriod()));
    }
}

void notifyClients(String sensorReadings)
{
    ws.textAll(sensorReadings);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
    {
        String sensorReadings;
        if (SensorManager::readAllSensors(true, sensorReadings))
        {
            notifyClients(sensorReadings);
        }
        else
        {
            String errorMsg = "{\"error\": \"Failed to read sensors: ";
            errorMsg += SensorManager::getLastError();
            errorMsg += "\"}";
            notifyClients(errorMsg);
        }
    }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    switch (type)
    {
    case WS_EVT_CONNECT:
        Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
        break;
    case WS_EVT_DISCONNECT:
        Serial.printf("WebSocket client #%u disconnected\n", client->id());
        break;
    case WS_EVT_DATA:
        handleWebSocketMessage(arg, data, len);
        break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
        break;
    }
}

void taskGPS(void *pvParameters)
{
    while (true)
    {
        if (gpsTaskOn)
        {
            GPSManager::process();
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void loop()
{
    static unsigned long wakeTime = millis();
    static bool fixAcquired = false;

    // Mettre à jour l'état de la LED
    LEDManager::update();

    // Phase 1: Wait for GPS fix or timeout
    while ((millis() - wakeTime) < FIX_TIMEOUT)
    {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        delay(25);

        if (GPSManager::hasFix())
        {
            fixAcquired = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    // Phase 2: Measurement period
    Serial.println("Starting measurements");
    measurementStart = millis();

    while ((millis() - measurementStart) < (SensorTaskManager::getPeriod() * NUMBER_OF_MEASUREMENTS))
    {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        delay(25);

        if (!GPSManager::hasFix())
        {
            Serial.println("Lost GPS fix during measurement!");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // Phase 3: Sleep management
    if (PowerManager::isSleepEnabled())
    {
        PowerManager::prepareForSleep(false);
    }
    else
    {
        delay(TIME_TO_SLEEP * 1000);
    }
}