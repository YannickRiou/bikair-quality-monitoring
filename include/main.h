#ifndef MAIN_H
#define MAIN_H

#include <Arduino.h>
// enjoyneering/AHT10@^1.1.0
#include <AHT10.h>

// adafruit/ENS160 - Adafruit Fork@^3.0.1
#include "ScioSense_ENS160.h" // ENS160 library

// paulvha/sps30@^1.4.17
#include "sps30.h"

#include <Wire.h>
#include <SoftwareSerial.h>

#include <LittleFS.h>

#include <RunningMedian.h>

#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 10          /* Time ESP32 will go to sleep (in seconds) */

// Tâches FreeRTOS
TaskHandle_t TaskGPS;
TaskHandle_t TaskSensors;
//  Déclarations des fonctions de tâches
void taskGPS(void *pvParameters);
void taskSensors(void *pvParameters);
// function prototypes (sometimes the pre-processor does not create prototypes themself on ESPxx)
void serialTrigger(char *mess);
void ErrtoMess(char *mess, uint8_t r);
void Errorloop(char *mess, uint8_t r);
void GetDeviceInfo();
// bool readSPS30();
String readSensors();
void processGPS(char data);
void parseGPGGA(String sentence);
void parseGPRMC(String sentence);
void parseGPVTG(String sentence);
void prepareForSleep();

void initLittleFS();

#endif