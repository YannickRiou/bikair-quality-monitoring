#include "network.h"
#include "config.h"
#include "storage.h"
#include "sensors.h"
#include "power.h"
#include "gps.h"
#include <WiFi.h>
#include <ArduinoJson.h>

// Static members initialization
AsyncWebServer NetworkManager::server(80);
AsyncWebSocket NetworkManager::ws("/ws");
bool NetworkManager::initialized = false;
const char *NetworkManager::lastError = nullptr;

void NetworkManager::setError(const char *error)
{
    lastError = error;
    Serial.printf("NetworkManager Error: %s\n", error);
}

void NetworkManager::clearError()
{
    lastError = nullptr;
}

bool NetworkManager::init()
{
    if (initialized)
    {
        return true; // Already initialized
    }
    // Configure WiFi
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

    const char *hostname = "bikair";
    WiFi.setHostname(hostname);

    // Configure mDNS
    if (!MDNS.begin(HOSTNAME))
    {
        setError("Failed to setup MDNS responder");
        return false;
    }

    Serial.println("mDNS responder started");
    Serial.printf("You can now access the device at: http://%s.local\n", HOSTNAME);
    MDNS.enableWorkstation(ESP_IF_WIFI_AP);
    delay(100);

    if (!MDNS.addService("http", "tcp", 80))
    {
        setError("Failed to add MDNS service");
        return false;
    }

    // Configure WebSocket
    ws.onEvent(onWebSocketEvent);
    server.addHandler(&ws);

    setupEndpoints();
    server.begin();

    clearError();
    initialized = true;
    return true;
}

void NetworkManager::setupEndpoints()
{
    // Root endpoint
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(LittleFS, "/index.html", "text/html"); });

    // Sleep endpoint
    server.on("/sleep", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        PowerManager::prepareForSleep(true);
        request->send(200, "text/plain", "OK"); });

    // Start/Stop measurement endpoint
    server.on("/startstopmeas", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        extern bool sensorkTaskOn;
        bool previousState = sensorkTaskOn;
        sensorkTaskOn = !sensorkTaskOn;
        
        if (previousState && !sensorkTaskOn) {
            StorageManager::closeCurrentFile();
        } else if (!previousState && sensorkTaskOn) {
            StorageManager::createNewLogFile();
        }
        
        request->send(200, "text/plain", "OK"); });

    // Mode endpoint supprimé car inutile

    // Time synchronization endpoint
    server.on("/set-time", HTTP_POST, [](AsyncWebServerRequest *request)
              { request->send(200); }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
              {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, data, len);
            
            if (error) {
                request->send(400, "text/plain", "Bad JSON");
                return;
            }
            
            struct tm timeinfo;
            timeinfo.tm_sec = doc["second"].as<int>();
            timeinfo.tm_min = doc["minute"].as<int>();
            timeinfo.tm_hour = doc["hour"].as<int>();
            timeinfo.tm_mday = doc["day"].as<int>();
            timeinfo.tm_mon = doc["month"].as<int>() - 1;
            timeinfo.tm_year = doc["year"].as<int>() - 1900;
            timeinfo.tm_isdst = -1;
            
            time_t timestamp = mktime(&timeinfo);
            struct timeval now = { .tv_sec = timestamp };
            settimeofday(&now, NULL);
            
            request->send(200, "text/plain", "Time updated"); });

    // Status endpoint
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        Serial.println("\n=== STATUS REQUEST ===");
        extern bool sensorkTaskOn;
        bool isEnabled = SensorManager::isManualIntervalEnabled();
        uint32_t interval = SensorManager::getManualInterval();
        
        Serial.printf("Measurement state: %s\n", sensorkTaskOn ? "ON" : "OFF");
        Serial.printf("Sleep mode: %s\n", PowerManager::isSleepEnabled() ? "ON" : "OFF");
        Serial.printf("Interval state - Value: %d, Enabled: %s\n", 
                     interval, isEnabled ? "true" : "false");
        
        String json = "{";
        json += "\"measuring\":" + String(sensorkTaskOn ? "true" : "false") + ",";
        json += "\"mode\":" + String(PowerManager::isSleepEnabled() ? "true" : "false") + ",";
        json += "\"measureInterval\":" + String(isEnabled ? interval : 0);
        json += "}";
        
        Serial.printf("Sending response: %s\n", json.c_str());
        request->send(200, "application/json", json);
        Serial.println("=== END STATUS ===\n"); });

    // Get measurement interval
    server.on("/get-interval", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        Serial.println("\n=== GET INTERVAL REQUEST ===");
        bool isEnabled = SensorManager::isManualIntervalEnabled();
        uint32_t currentInterval = SensorManager::getManualInterval();
        Serial.printf("Current state - Interval: %d, Enabled: %s\n", 
                     currentInterval, isEnabled ? "true" : "false");
        
        String json = "{";
        json += "\"interval\":" + String(isEnabled ? currentInterval : 0);
        json += "}";
        Serial.printf("Sending response: %s\n", json.c_str());
        request->send(200, "application/json", json);
        Serial.println("=== END GET INTERVAL ===\n"); });

    // Set measurement interval
    server.on("/set-interval", HTTP_POST, [](AsyncWebServerRequest *request)
              { request->send(200); }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
              {
        Serial.println("\n=== SET INTERVAL REQUEST ===");
        Serial.printf("Received data length: %d bytes\n", len);
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);
        
        if (error) {
            Serial.printf("JSON Parse Error: %s\n", error.c_str());
            request->send(400, "text/plain", "Bad JSON");
            return;
        }
        
        if (!doc.containsKey("interval")) {
            Serial.println("Error: Missing 'interval' field in request");
            request->send(400, "text/plain", "Missing interval field");
            return;
        }

        uint32_t interval = doc["interval"].as<uint32_t>();
        Serial.printf("Client requested interval: %d seconds\n", interval);
        
        uint32_t oldInterval = SensorManager::getManualInterval();
        bool wasEnabled = SensorManager::isManualIntervalEnabled();
        Serial.printf("Previous state - Interval: %d, Enabled: %s\n", 
                     oldInterval, wasEnabled ? "true" : "false");
        
        SensorManager::setManualInterval(interval);
        
        uint32_t newInterval = SensorManager::getManualInterval();
        bool isEnabled = SensorManager::isManualIntervalEnabled();
        Serial.printf("New state - Interval: %d, Enabled: %s\n", 
                     newInterval, isEnabled ? "true" : "false");
        
        String response = "{\"success\":true,\"currentInterval\":" + String(newInterval) + "}";
        Serial.printf("Sending response: %s\n", response.c_str());
        request->send(200, "application/json", response);
        Serial.println("=== END SET INTERVAL ===\n"); });

    // Files endpoint
    server.on("/files", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(LittleFS, "/files.html", "text/html"); });

    // Files API endpoints
    server.on("/api/files", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        auto files = StorageManager::listJsonFiles();
        JsonDocument doc;
        JsonArray filesArray = doc.to<JsonArray>();

        for (const auto& filename : files) {
            File file = LittleFS.open(filename, "r");
            if (file) {
                JsonObject fileObj = filesArray.add<JsonObject>();
                fileObj["name"] = filename;
                fileObj["size"] = file.size();
                file.close();
            }
        }

        String response;
        serializeJson(filesArray, response);
        request->send(200, "application/json", response); });

    // File download endpoint
    server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        if (!request->hasParam("file")) {
            request->send(400, "text/plain", "File parameter is required");
            return;
        }

        String fileName = request->getParam("file")->value();
        if (!fileName.startsWith("/")) fileName = "/" + fileName;

        if (!LittleFS.exists(fileName)) {
            request->send(404, "text/plain", "File not found");
            return;
        }

        request->send(LittleFS, fileName, "application/json", true); });

    // File delete endpoint
    server.on("/delete", HTTP_DELETE, [](AsyncWebServerRequest *request)
              {
        if (!request->hasParam("file")) {
            request->send(400, "text/plain", "File parameter is required");
            return;
        }

        String fileName = request->getParam("file")->value();
        if (!fileName.startsWith("/")) fileName = "/" + fileName;

        if (StorageManager::deleteFile(fileName.c_str())) {
            request->send(200, "text/plain", "File deleted");
        } else {
            request->send(500, "text/plain", "Failed to delete file");
        } });

    // Static files
    server.serveStatic("/", LittleFS, "/");
}

void NetworkManager::notifyClients(const String &data)
{
    // Vérifier la taille des données avant l'envoi
    if (data.length() > 8192)
    { // Limite de 8KB pour les messages broadcast
        Serial.println("Warning: Message too large for broadcast");
        ws.textAll("{\"error\":\"Message too large\"}");
        return;
    }

    // Vérifier l'espace mémoire disponible
    if (ESP.getFreeHeap() < 10000)
    { // Garder au moins 10KB de mémoire libre
        Serial.println("Warning: Low memory, cleaning up clients");
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
    if (!arg || !data)
    {
        Serial.println("Invalid WebSocket message: null pointer");
        return;
    }

    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT)
    {
        Serial.println("Invalid WebSocket message format");
        return;
    }

    // Vérifier la taille maximale du message
    if (len > 1024)
    { // Limite de 1KB pour les messages
        Serial.println("WebSocket message too large");
        return;
    }

    // S'assurer que les données sont terminées par un caractère nul
    if (len > 0)
    {
        char *messageBuffer = (char *)malloc(len + 1);
        if (!messageBuffer)
        {
            Serial.println("Failed to allocate memory for message");
            return;
        }

        memcpy(messageBuffer, data, len);
        messageBuffer[len] = '\0';
        String message(messageBuffer);
        free(messageBuffer);

        if (message == "getReadings")
        {
            Serial.println("Readings requested via WebSocket");
            try
            {
                JsonDocument doc;

                // Read sensors data safely
                doc["co2"] = String(SensorManager::getCO2());
                doc["tvoc"] = String(SensorManager::getTVOC());
                doc["temperature"] = String(SensorManager::getTemperature());
                doc["humidity"] = String(SensorManager::getHumidity());

                // Add measurement interval
                doc["measureInterval"] = SensorManager::isManualIntervalEnabled() ? SensorManager::getManualInterval() : 0;

                // Add GPS data with null checks
                doc["gpsfix"] = GPSManager::hasFix() ? "1" : "0";
                doc["latitude"] = GPSManager::getLatitude().length() > 0 ? GPSManager::getLatitude() : "0";
                doc["longitude"] = GPSManager::getLongitude().length() > 0 ? GPSManager::getLongitude() : "0";
                doc["satellites"] = GPSManager::getSatellites().length() > 0 ? GPSManager::getSatellites() : "0";
                doc["altitude"] = GPSManager::getAltitude().length() > 0 ? GPSManager::getAltitude() : "0";
                doc["speed"] = String(SensorManager::getSpeed());

                String jsonString;
                serializeJson(doc, jsonString);

                if (jsonString.length() > 0)
                {
                    notifyClients(jsonString);
                }
                else
                {
                    notifyClients("{\"error\":\"No sensor data available\"}");
                }
            }
            catch (const std::exception &e)
            {
                Serial.printf("Error reading sensors: %s\n", e.what());
                notifyClients("{\"error\":\"Failed to read sensors\"}");
            }
        }
        else
        {
            Serial.println("Unknown WebSocket command: " + message);
            notifyClients("{\"error\":\"Unknown command\"}");
        }
    }
}

void NetworkManager::onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                                      AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    if (!client)
    {
        Serial.println("Error: null client in WebSocket event");
        return;
    }

    switch (type)
    {
    case WS_EVT_CONNECT:
        Serial.printf("WebSocket client #%u connected from %s\n",
                      client->id(), client->remoteIP().toString().c_str());
        // Envoyer un message initial de connexion seulement
        client->text("{\"status\":\"connected\"}");
        break;

    case WS_EVT_DISCONNECT:
        Serial.printf("WebSocket client #%u disconnected\n", client->id());
        break;

    case WS_EVT_DATA:
        try
        {
            handleWebSocketMessage(arg, data, len);
        }
        catch (const std::exception &e)
        {
            Serial.printf("Error handling WebSocket message from client #%u: %s\n",
                          client->id(), e.what());
            client->text("{\"error\":\"Internal server error\"}");
        }
        break;

    case WS_EVT_ERROR:
        Serial.printf("WebSocket error for client #%u\n", client->id());
        break;

    case WS_EVT_PONG:
        Serial.printf("WebSocket pong received from client #%u\n", client->id());
        break;

    default:
        Serial.printf("Unknown WebSocket event type: %d\n", type);
        break;
    }
}