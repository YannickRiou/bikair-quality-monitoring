#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>

class NetworkManager
{
public:
    static bool init();
    static bool setupWebServer();
    static void notifyClients(const String &data);
    static void cleanupClients();
    static const char *getLastError() { return lastError; }
    static bool isInitialized() { return initialized; }

private:
    static bool initialized;
    static const char *lastError;
    static AsyncWebServer server;
    static AsyncWebSocket ws;

    static void setError(const char *error);
    static void clearError();
    static void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
    static void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                                 AwsEventType type, void *arg, uint8_t *data, size_t len);
    static void setupEndpoints();
};

#endif // NETWORK_H