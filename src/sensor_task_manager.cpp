#include "sensor_task_manager.h"
#include "storage.h"
#include "led_manager.h"
#include <stdint.h>

TaskHandle_t SensorTaskManager::taskHandle = NULL;
bool SensorTaskManager::isTaskRunning = false;
String SensorTaskManager::lastError = "";
SemaphoreHandle_t SensorTaskManager::resourceMutex = NULL;
uint16_t SensorTaskManager::measurePeriod = DEFAULT_MEASURE_PERIOD;

void SensorTaskManager::init()
{
    // Create mutex for resource protection
    resourceMutex = xSemaphoreCreateMutex();
    if (!resourceMutex)
    {
        setError("Failed to create resource mutex");
        return;
    }

    // Initialize measurement period
    measurePeriod = DEFAULT_MEASURE_PERIOD;
}

void SensorTaskManager::start()
{
    if (isTaskRunning)
    {
        return;
    }

    // Create task
    BaseType_t result = xTaskCreatePinnedToCore(
        taskFunction,
        "TaskSensors",
        STACK_SIZE,
        NULL,
        TASK_PRIORITY,
        &taskHandle,
        CORE_ID);

    if (result != pdPASS)
    {
        setError("Failed to create sensor task");
        return;
    }

    isTaskRunning = true;
    LEDManager::startBlinking();
    clearError();
}

void SensorTaskManager::stop()
{
    if (!isTaskRunning)
    {
        return;
    }

    if (taskHandle != NULL)
    {
        vTaskDelete(taskHandle);
        taskHandle = NULL;
    }

    // Reset current log file when stopping measurements
    StorageManager::resetCurrentFile();

    isTaskRunning = false;
    LEDManager::stopBlinking();
    clearError();
}

void SensorTaskManager::taskFunction(void *parameter)
{
    static bool storeData = false;
    static uint8_t dataCounter = 0;
    static uint8_t ledVal = 0;
    struct sps_values spsValues;

    while (true)
    {
        if (!acquireResources(pdMS_TO_TICKS(1000)))
        {
            // Si on ne peut pas acquérir les ressources, on attend un peu
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Lecture sécurisée des capteurs
        bool success = true;
        String sensorReadings;

        try
        {
            // Lecture du SPS30
            if (!SPS30Manager::measure(spsValues))
            {
                Serial.println("SPS30 measurement failed: " + SPS30Manager::getLastError());
                success = false;
            }

            // Autres lectures de capteurs...
            // Chaque opération doit avoir sa propre gestion d'erreur

            // Traitement des données et notification seulement si tout s'est bien passé
            if (success)
            {
                // Notification des clients
                // NetworkManager::notifyClients(sensorReadings);

                // Gestion du stockage périodique
                if (dataCounter < 10)
                {
                    storeData = false;
                    dataCounter++;
                    // LED status update removed
                }
                else
                {
                    storeData = true;
                    dataCounter = 0;
                    // LED status update removed
                }
            }
        }
        catch (...)
        {
            // Capture de toute exception non gérée
            setError("Unexpected error in sensor task");
            success = false;
        }

        // Libération des ressources
        releaseResources();

        // Délai avant la prochaine lecture
        vTaskDelay(pdMS_TO_TICKS(measurePeriod));
    }
}

void SensorTaskManager::setError(const char *error)
{
    lastError = error;
}

void SensorTaskManager::clearError()
{
    lastError = "";
}

bool SensorTaskManager::acquireResources(TickType_t timeout)
{
    if (xSemaphoreTake(resourceMutex, timeout) != pdTRUE)
    {
        setError("Failed to acquire resources");
        return false;
    }
    return true;
}

void SensorTaskManager::releaseResources()
{
    xSemaphoreGive(resourceMutex);
}