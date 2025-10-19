#ifndef TASKS_H
#define TASKS_H

#include <Arduino.h>

void taskGPS(void *pvParameters);
void taskSensors(void *pvParameters);

extern TaskHandle_t TaskGPS;
extern TaskHandle_t TaskSensors;
extern bool sensorkTaskOn;
extern bool gpsTaskOn;

#endif // TASKS_H