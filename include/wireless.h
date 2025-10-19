#ifndef WIRELESS_H
#define WIRELESS_H

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// Configuration WiFi
extern const char *ssid;
extern const char *password;
extern IPAddress local_IP;
extern IPAddress gateway;
extern IPAddress subnet;

// Variables WebServer et WebSocket
extern AsyncWebServer server;
extern AsyncWebSocket ws;

// Variables JSON
extern JsonDocument readings;

class WirelessManager
{
public:
    static bool init();
    static bool connect();
    static void disconnect();
    static bool isConnected();
    static String getIP();
    static void handleOTA();
    static bool setupWebServer();
    static bool uploadData(const String &data);
    static void prepareForSleep();
    static const char *getLastError() { return lastError; }
    static bool isInitialized() { return initialized; }

private:
    static bool initialized;
    static const char *lastError;

    static bool handleFileRead(String path);
    static void handleNotFound();
    static void handleRoot();
    static void handleData();
    static void handleUpload();
    static void reboot();

    static void setError(const char *error);
    static void clearError();

    // WebSocket functions
    static void initWebSocket();
    static void notifyClients(String sensorReadings);
    static void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
    static void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
                        void *arg, uint8_t *data, size_t len);
};

#endif