#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <Arduino.h>

class LEDManager
{
public:
    static void init();
    static void startBlinking();
    static void stopBlinking();
    static void update();

private:
    static bool isBlinking;
    static unsigned long lastToggle;
    static const int BLINK_INTERVAL = 250; // Intervalle de clignotement en ms
    static const int LED_PIN = 2;          // GPIO2 - LED bleue intégrée
};

#endif // LED_MANAGER_H