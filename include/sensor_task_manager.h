#ifndef SENSOR_TASK_MANAGER_H
#define SENSOR_TASK_MANAGER_H

#include <Arduino.h>
#include <mutex>
#include <stdint.h>
#include "sps30_manager.h"
#include "RunningMedian.h"
#include "ArduinoJson.h"
#include "config.h"

class SensorTaskManager
{
public:
    static void init();
    static void start();
    static void stop();
    static bool isRunning() { return isTaskRunning; }
    static String getLastError() { return lastError; }
    static void setPeriod(uint16_t period) { measurePeriod = period; }
    static uint16_t getPeriod() { return measurePeriod; }

private:
    static TaskHandle_t taskHandle;
    static bool isTaskRunning;
    static String lastError;
    static SemaphoreHandle_t resourceMutex;
    static const uint16_t STACK_SIZE = 4096;
    static const UBaseType_t TASK_PRIORITY = 2;
    static const BaseType_t CORE_ID = 1;

    static uint16_t measurePeriod;
    static const uint16_t DEFAULT_MEASURE_PERIOD = 1000;

    static void taskFunction(void *parameter);
    static void setError(const char *error);
    static void clearError();
    static bool acquireResources(TickType_t timeout = portMAX_DELAY);
    static void releaseResources();
};

#endif