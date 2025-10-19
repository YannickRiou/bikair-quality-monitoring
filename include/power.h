#ifndef POWER_H
#define POWER_H

#include <Arduino.h>

class PowerManager
{
public:
    static void init();
    static void prepareForSleep(bool deepSleep);
    static void enableSleepMode(bool enable);
    static bool isSleepEnabled() { return sleepEnabled; }

private:
    static bool sleepEnabled;
    static void configureWakeupSources();
    static void shutdownPeripherals();
};

#endif // POWER_H