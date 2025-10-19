#include "led_manager.h"

bool LEDManager::isBlinking = false;
unsigned long LEDManager::lastToggle = 0;

void LEDManager::init()
{
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
}

void LEDManager::startBlinking()
{
    isBlinking = true;
    digitalWrite(LED_PIN, HIGH);
    lastToggle = millis();
}

void LEDManager::stopBlinking()
{
    isBlinking = false;
    digitalWrite(LED_PIN, LOW);
}

void LEDManager::update()
{
    if (!isBlinking)
    {
        return;
    }

    unsigned long currentMillis = millis();
    if (currentMillis - lastToggle >= BLINK_INTERVAL)
    {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        lastToggle = currentMillis;
    }
}