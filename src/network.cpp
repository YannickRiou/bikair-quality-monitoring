#include "network.h"
#include "config.h"
#include "storage.h"
#include "sensors.h"
#include "power.h"
#include "gps.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

// Static members initialization
AsyncWebServer NetworkManager::server(80);
AsyncWebSocket NetworkManager::ws("/ws");
bool NetworkManager::initialized = false;
const char *NetworkManager::lastError = nullptr;

// Optional verbose logging — kept off in release builds to save flash & CPU
#ifndef NET_VERBOSE
#define NET_VERBOSE 0
#endif
#if NET_VERBOSE
#define NET_LOG(fmt, ...) Serial.printf("[NET] " fmt "\n", ##__VA_ARGS__)
#else
#define NET_LOG(fmt, ...) ((void)0)
#endif

void NetworkManager::setError(const char *error)
{
    lastError = error;
    Serial.printf("NetworkManager Error: %s\n", error);
}

void NetworkManager::clearError() { lastError = nullptr; }

bool NetworkManager::init()
{
    if (initialized) return true;

    WiFi.mode(WIFI_AP);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);

    if (!WiFi.softAPConfig(LOCAL_IP, GATEWAY, SUBNET))
    {
        setError("Failed to configure WiFi AP");
        return false;
    }
    if (!WiFi.softAP(WIFI_SSID, WIFI_PASSWORD))
    {
        setError("Failed to start WiFi AP");
        return false;
    }

    WiFi.setHostname(HOSTNAME);

    if (!MDNS.begin(HOSTNAME))
    {
        setError("Failed to setup MDNS responder");
        return false;
    }
    MDNS.enableWorkstation(ESP_IF_WIFI_AP);
    delay(100);
    MDNS.addService("http", "tcp", 80);

    ws.onEvent(onWebSocketEvent);
    server.addHandler(&ws);

    setupEndpoints();
    server.begin();

    clearError();
    initialized = true;
    Serial.printf("Network ready — http://%s.local (%s)\n",
                  HOSTNAME, WiFi.softAPIP().toString().c_str());
    return true;
}

// Build the live readings JSON document
static void buildReadingsDoc(JsonDocument &doc)
{
    doc["co2"]         = SensorManager::getCO2();
    doc["tvoc"]        = SensorManager::getTVOC();
    doc["temperature"] = SensorManager::getTemperature();
    doc["humidity"]    = SensorManager::getHumidity();
    doc["measureInterval"] = SensorManager::isManualIntervalEnabled()
                                ? SensorManager::getManualInterval() : 0;
    doc["gpsfix"]    = GPSManager::hasFix() ? "1" : "0";
    doc["latitude"]  = GPSManager::getLatitude();
    doc["longitude"] = GPSManager::getLongitude();
    doc["satellites"]= GPSManager::getSatellites();
    doc["altitude"]  = GPSManager::getAltitude();
    doc["speed"]     = SensorManager::getSpeed();
}

void NetworkManager::setupEndpoints()
{
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(LittleFS, "/index.html", "text/html");
    });

    server.on("/sleep", HTTP_GET, [](AsyncWebServerRequest *req) {
        PowerManager::prepareForSleep(true);
        req->send(200, "text/plain", "OK");
    });

    server.on("/startstopmeas", HTTP_GET, [](AsyncWebServerRequest *req) {
        extern bool sensorkTaskOn;
        bool previousState = sensorkTaskOn;
        sensorkTaskOn = !sensorkTaskOn;

        if (previousState && !sensorkTaskOn) {
            StorageManager::closeCurrentFile();
        } else if (!previousState && sensorkTaskOn) {
            StorageManager::createNewLogFile();
        }
        req->send(200, "text/plain", "OK");
    });

    // Time sync
    server.on("/set-time", HTTP_POST,
        [](AsyncWebServerRequest *req) { /* handled in body cb */ },
        nullptr,
        [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) {
                req->send(400, "text/plain", "Bad JSON");
                return;
            }
            struct tm t = {};
            t.tm_sec  = doc["second"] | 0;
            t.tm_min  = doc["minute"] | 0;
            t.tm_hour = doc["hour"]   | 0;
            t.tm_mday = doc["day"]    | 1;
            t.tm_mon  = (doc["month"] | 1) - 1;
            t.tm_year = (doc["year"]  | 1970) - 1900;
            t.tm_isdst = -1;
            time_t ts = mktime(&t);
            struct timeval now = { .tv_sec = ts };
            settimeofday(&now, NULL);
            req->send(200, "text/plain", "Time updated");
        });

    // Combined status
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *req) {
        extern bool sensorkTaskOn;
        bool enabled  = SensorManager::isManualIntervalEnabled();
        uint32_t intv = SensorManager::getManualInterval();

        JsonDocument doc;
        doc["measuring"] = sensorkTaskOn;
        doc["mode"]      = PowerManager::isSleepEnabled();
        doc["measureInterval"] = enabled ? intv : 0;
        doc["freeHeap"]  = (uint32_t)ESP.getFreeHeap();
        doc["uptime"]    = (uint32_t)(millis() / 1000);

        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    server.on("/get-interval", HTTP_GET, [](AsyncWebServerRequest *req) {
        bool enabled  = SensorManager::isManualIntervalEnabled();
        uint32_t intv = SensorManager::getManualInterval();
        JsonDocument doc;
        doc["interval"] = enabled ? intv : 0;
        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    server.on("/set-interval", HTTP_POST,
        [](AsyncWebServerRequest *req) { /* handled in body cb */ },
        nullptr,
        [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) {
                req->send(400, "text/plain", "Bad JSON");
                return;
            }
            if (!doc["interval"].is<uint32_t>() && !doc["interval"].is<int>()) {
                req->send(400, "text/plain", "Missing interval field");
                return;
            }
            uint32_t interval = doc["interval"].as<uint32_t>();
            SensorManager::setManualInterval(interval);

            JsonDocument resp;
            resp["success"] = true;
            resp["currentInterval"] = SensorManager::getManualInterval();
            String out;
            serializeJson(resp, out);
            req->send(200, "application/json", out);
        });

    // Files page
    server.on("/files", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(LittleFS, "/files.html", "text/html");
    });

    server.on("/api/files", HTTP_GET, [](AsyncWebServerRequest *req) {
        auto files = StorageManager::listJsonFiles();
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (const auto &name : files) {
            File f = LittleFS.open(name, "r");
            if (f) {
                JsonObject o = arr.add<JsonObject>();
                o["name"] = name;
                o["size"] = f.size();
                f.close();
            }
        }
        String out;
        serializeJson(arr, out);
        req->send(200, "application/json", out);
    });

    // Legacy alias used by older clients
    server.on("/list-files", HTTP_GET, [](AsyncWebServerRequest *req) {
        auto files = StorageManager::listJsonFiles();
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (const auto &name : files) arr.add(name);
        String out;
        serializeJson(arr, out);
        req->send(200, "application/json", out);
    });

    // Return the last N points from the current log so the dashboard can rebuild
    // its sparklines / trace on reload.
    server.on("/history", HTTP_GET, [](AsyncWebServerRequest *req) {
        const char *current = StorageManager::getCurrentLogFile();
        if (!current || !LittleFS.exists(current)) {
            req->send(200, "application/json", "[]");
            return;
        }
        // Stream the current file as-is; it already is a JSON array.
        req->send(LittleFS, current, "application/json");
    });

    server.on("/download", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (!req->hasParam("file")) {
            req->send(400, "text/plain", "File parameter is required");
            return;
        }
        String fileName = req->getParam("file")->value();
        if (!fileName.startsWith("/")) fileName = "/" + fileName;
        if (!LittleFS.exists(fileName)) {
            req->send(404, "text/plain", "File not found");
            return;
        }
        // Whether downloaded for save or previewed — we want correct MIME
        const char *mime = fileName.endsWith(".json") ? "application/json" : "text/plain";
        // The third arg controls Content-Disposition (false = inline -> preview works)
        req->send(LittleFS, fileName, mime, false);
    });

    server.on("/delete", HTTP_DELETE, [](AsyncWebServerRequest *req) {
        if (!req->hasParam("file")) {
            req->send(400, "text/plain", "File parameter is required");
            return;
        }
        String fileName = req->getParam("file")->value();
        if (!fileName.startsWith("/")) fileName = "/" + fileName;
        if (StorageManager::deleteFile(fileName.c_str())) {
            req->send(200, "text/plain", "File deleted");
        } else {
            req->send(500, "text/plain", "Failed to delete file");
        }
    });

    // Static files (style.css, script.js, manifest.json, sw.js, icon.svg, …)
    server.serveStatic("/", LittleFS, "/");
}

void NetworkManager::notifyClients(const String &data)
{
    if (data.length() > 8192) {
        Serial.println("WS: message too large for broadcast");
        return;
    }
    if (ESP.getFreeHeap() < 10000) {
        cleanupClients();
    }
    ws.textAll(data);
}

void NetworkManager::cleanupClients()
{
    ws.cleanupClients();
}

void NetworkManager::handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
    if (!arg || !data) return;
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT) return;
    if (len == 0 || len > 256) return; // command frames are tiny

    // Treat the data as text — frames are not necessarily null-terminated
    // but we can safely compare prefixes byte-wise.
    if (len == 11 && memcmp(data, "getReadings", 11) == 0) {
        JsonDocument doc;
        buildReadingsDoc(doc);
        String out;
        serializeJson(doc, out);
        notifyClients(out);
    } else {
        notifyClients("{\"error\":\"Unknown command\"}");
    }
}

void NetworkManager::onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                                      AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    if (!client) return;

    switch (type) {
    case WS_EVT_CONNECT:
        NET_LOG("WS connect #%u from %s", client->id(), client->remoteIP().toString().c_str());
        client->text("{\"status\":\"connected\"}");
        break;

    case WS_EVT_DISCONNECT:
        NET_LOG("WS disconnect #%u", client->id());
        break;

    case WS_EVT_DATA:
        handleWebSocketMessage(arg, data, len);
        break;

    case WS_EVT_ERROR:
        NET_LOG("WS error #%u", client->id());
        break;

    case WS_EVT_PONG:
    default:
        break;
    }
}
