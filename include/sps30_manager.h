#ifndef SPS30_MANAGER_H
#define SPS30_MANAGER_H

#include <Arduino.h>
#include "sps30.h"

#define SP30_COMMS SERIALPORT1

class SPS30Manager
{
public:
    static bool init();
    static bool begin();
    static void stop();
    static bool measure(struct sps_values &values);
    static bool isConnected() { return initialized && connected; }
    static String getLastError() { return lastError; }

    enum
    {
        SPS30_TX_PIN = 9,
        SPS30_RX_PIN = 10
    };

private:
    static bool initialized;
    static bool connected;
    static String lastError;
    static const uint8_t MAX_RETRIES = 3;
    static const uint16_t RETRY_DELAY_MS = 1000;

    static SPS30 sps30;
    static bool initCommunication();
    static bool setupSensor();
    static void setError(const char *error, uint8_t errorCode = 0);
    static void clearError();
};

#endif