#include <Arduino.h>
// enjoyneering/AHT10@^1.1.0
#include <AHT10.h>
#include <Wire.h>

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Arduino_JSON.h>

// adafruit/ENS160 - Adafruit Fork@^3.0.1
#include "ScioSense_ENS160.h" // ENS160 library

// paulvha/sps30@^1.4.17
#include "sps30.h"

#include <RunningMedian.h>

#include <SoftwareSerial.h>
#include <Wire.h>

RunningMedian temperatureMeas = RunningMedian(100);
RunningMedian humidityMeas = RunningMedian(100);
RunningMedian co2Meas = RunningMedian(100);

HardwareSerial gpsSerial(2);

SoftwareSerial openLog(16, 17);

ScioSense_ENS160 ens160(0x53);
AHT10 myAHT20(AHT10_ADDRESS_0X38, AHT20_SENSOR);

const char *ssid = "bikeair";
const char *password = "madlicorne";
IPAddress local_IP(192, 168, 1, 1);
// We set a Gateway IP address
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

AsyncWebServer server(80);
// Create a WebSocket object
AsyncWebSocket ws("/ws");
// Json Variable to Hold Sensor Readings
uint8_t readStatus = 0;
JSONVar readings;

#define SP30_COMMS SERIALPORT1

#define TX_PIN 9
#define RX_PIN 10

#define DEBUG 0

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
void initWebSocket();
void initLittleFS();
void notifyClients(String sensorReadings);
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

// Tâches FreeRTOS
TaskHandle_t TaskGPS;
TaskHandle_t TaskSensors;
//  Déclarations des fonctions de tâches
void taskGPS(void *pvParameters);
void taskSensors(void *pvParameters);

String timeUTC = "";
String latitude = "";
String longitude = "";
String altitude = "";
String speed = "";
String fixStatus = "";
String satellites = "";

// create constructor
SPS30 sps30;

void setup()
{
    Serial.begin(115200);
    Serial.println();
    openLog.begin(9600); // Should be the speed specified in config.txt on the sd card divided by 2
    Wire.begin(21, 22);  // SDA, SCL
    myAHT20.begin(21, 22);
    Serial.println(F("AHT21 OK"));

    initWebSocket();
    initLittleFS();

    Serial.println("Setting AP (Access Point)…");
    WiFi.softAPConfig(local_IP, gateway, subnet);

    // Remove the password parameter, if you want the AP (Access Point) to be open
    WiFi.softAP(ssid, password);

    // Web Server Root URL
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(LittleFS, "/index.html", "text/html"); });

    server.serveStatic("/", LittleFS, "/");

    // Start server
    server.begin();

    if (!ens160.begin())
    {
        Serial.println("ENS160 not found. Check connections!");
    }
    Serial.println("ENS160 initialized successfully.");
    ens160.setMode(ENS160_OPMODE_STD);

    // set pins to use for softserial and Serial1 on ESP32
    sps30.SetSerialPin(RX_PIN, TX_PIN);

    // Begin communication channel;
    if (!sps30.begin(SP30_COMMS))
        Errorloop((char *)"could not initialize communication channel.", 0);

    // check for SPS30 connection
    if (!sps30.probe())
        Errorloop((char *)"could not probe / connect with SPS30.", 0);
    else
        Serial.println(F("Detected SPS30."));

    // reset SPS30 connection
    if (!sps30.reset())
        Errorloop((char *)"could not reset.", 0);

    // read device info
    GetDeviceInfo();

    // start measurement
    if (sps30.start())
        Serial.println(F("Measurement started"));
    else
        Errorloop((char *)"Could NOT start measurement", 0);

    gpsSerial.begin(9600, SERIAL_8N1, 32, 12);

    // connect at 115200 so we can read the GPS fast enough and echo without dropping chars
    // also spit it out
    delay(5000);

    xTaskCreate(taskGPS, "TaskGPS", 2048, NULL, 1, &TaskGPS);
    xTaskCreate(taskSensors, "TaskSensors", 2048, NULL, 1, &TaskSensors);
}
uint32_t timer = millis();

void taskSensors(void *pvParameters)
{
    (void)pvParameters;

    while (true)
    {

        String sensorReadings = readSensors();
        notifyClients(sensorReadings);
        ws.cleanupClients();
        vTaskDelay(pdMS_TO_TICKS(10000)); // Lire les capteurs toutes les 10 secondes
    }
}

void notifyClients(String sensorReadings)
{
    ws.textAll(sensorReadings);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
    {
        String sensorReadings = readSensors();
        notifyClients(sensorReadings);
        //}
    }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    switch (type)
    {
    case WS_EVT_CONNECT:
        Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
        break;
    case WS_EVT_DISCONNECT:
        Serial.printf("WebSocket client #%u disconnected\n", client->id());
        break;
    case WS_EVT_DATA:
        handleWebSocketMessage(arg, data, len);
        break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
        break;
    }
}

// Initialize LittleFS
void initLittleFS()
{
    if (!LittleFS.begin(true))
    {
        Serial.println("An error has occurred while mounting LittleFS");
    }
    Serial.println("LittleFS mounted successfully");
}

void initWebSocket()
{
    ws.onEvent(onEvent);
    server.addHandler(&ws);
}

String readSensors()
{
    static bool header = true;
    uint8_t ret, error_cnt = 0;
    struct sps_values val;

    // Lire les capteurs
    float temperature = myAHT20.readTemperature();
    temperatureMeas.add(temperature);
    float humidity = myAHT20.readHumidity();
    humidityMeas.add(humidity);

    if (ens160.measure())
    {
        float tvoc = ens160.getTVOC();
        float co2 = ens160.geteCO2();
        co2Meas.add(co2);

        // Afficher les données des capteurs
        openLog.print("Time (UTC):");
        openLog.println(timeUTC);
        openLog.print("Temperature: ");
        openLog.print(temperature);
        readings["temperature"] = String(temperatureMeas.getMedian());
        openLog.println(" C");
        openLog.print("Humidity: ");
        openLog.print(humidity);
        readings["humidity"] = String(humidityMeas.getMedian());
        openLog.println(" %");
        openLog.print("TVOC: ");
        readings["tvoc"] = String(tvoc);
        openLog.print(tvoc);
        openLog.print(" ppb, CO2: ");
        readings["co2"] = String(co2);
        openLog.print(co2Meas.getMedian());
        openLog.println(" ppm");
    }

    sps30.GetValues(&val);
    openLog.print("PM1: ");
    readings["pm1"] = String(val.MassPM1);
    openLog.println(val.MassPM1);
    readings["pm2"] = String(val.MassPM2);
    openLog.print("PM2: ");
    openLog.println(val.MassPM2);

    readings["gpsfix"] = String(fixStatus);

    readings["latitude"] = latitude;
    readings["longitude"] = longitude;
    readings["satellites"] = satellites;
    readings["altitude"] = altitude;

    String jsonString = JSON.stringify(readings);
    return jsonString;
}

void taskGPS(void *pvParameters)
{
    (void)pvParameters;
    String gpsSentence = "";

    while (true)
    {
        while (gpsSerial.available() > 0)
        {
            char gpsData = gpsSerial.read();
            gpsSentence += gpsData;

            if (gpsData == '\n')
            {
                if (gpsSentence.startsWith("$GPGGA"))
                {
                    parseGPGGA(gpsSentence);
                }
                // else if (gpsSentence.startsWith("$GPRMC"))
                // {
                //     parseGPRMC(gpsSentence);
                // }
                // else if (gpsSentence.startsWith("$GPVTG"))
                // {
                //     parseGPVTG(gpsSentence);
                // }
                gpsSentence = ""; // Réinitialiser le buffer de la phrase
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // Petit délai pour éviter de monopoliser le CPU
    }
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000)); // Petit délai pour éviter de monopoliser le CPU
}

void processGPS(char data)
{
    static String gpsSentence = "";

    // Ajouter le caractère au buffer de la phrase
    gpsSentence += data;

    // Vérifier si nous avons une phrase complète
    if (data == '\n')
    {
        if (gpsSentence.startsWith("$GPGGA"))
        {
            parseGPGGA(gpsSentence);
        }
        else if (gpsSentence.startsWith("$GPRMC"))
        {
            parseGPRMC(gpsSentence);
        }
        else if (gpsSentence.startsWith("$GPVTG"))
        {
            parseGPVTG(gpsSentence);
        }
        // Réinitialiser le buffer de la phrase
        gpsSentence = "";
    }
}

void parseGPGGA(String sentence)
{
    // Exemple de phrase $GPGGA : $GPGGA,hhmmss.ss,llll.ll,a,yyyyy.yy,a,x,xx,x.x,x.x,M,x.x,M,,*xx
    int commaIndex = 0;
    int commaPos[15]; // GPGGA a généralement 15 champs

    // Trouver les positions des virgules
    for (int i = 0; i < 15; i++)
    {
        commaPos[i] = sentence.indexOf(',', commaIndex);
        commaIndex = commaPos[i] + 1;
    }

    // Extraire les données
    latitude = sentence.substring(commaPos[1] + 1, commaPos[2]);
    longitude = sentence.substring(commaPos[3] + 1, commaPos[4]);
    fixStatus = sentence.substring(commaPos[5] + 1, commaPos[6]);
    satellites = sentence.substring(commaPos[6] + 1, commaPos[7]);
    altitude = sentence.substring(commaPos[8] + 1, commaPos[9]);
    timeUTC = sentence.substring(commaPos[0] + 1, commaPos[1]);

    // openLog.print("Latitude: ");
    // openLog.println(latitude);
    // openLog.print("Longitude: ");
    // openLog.println(longitude);
    // openLog.print("Fix Status: ");
    // openLog.println(fixStatus);
    // openLog.print("Satellites: ");
    // openLog.println(satellites);
    // openLog.print("Altitude: ");
    // openLog.println(altitude);
}

void parseGPRMC(String sentence)
{
    // Exemple de phrase $GPRMC : $GPRMC,hhmmss.ss,A,llll.ll,a,yyyyy.yy,a,x.x,x.x,ddmmyy,,,A*hh
    int commaIndex = 0;
    int commaPos[13]; // GPRMC a généralement 13 champs

    // Trouver les positions des virgules
    for (int i = 0; i < 13; i++)
    {
        commaPos[i] = sentence.indexOf(',', commaIndex);
        commaIndex = commaPos[i] + 1;
    }

    // Extraire les données
    speed = sentence.substring(commaPos[6] + 1, commaPos[7]);

    openLog.print("Speed: ");
    openLog.println(speed);
}

void parseGPVTG(String sentence)
{
    // Exemple de phrase $GPVTG : $GPVTG,x.x,T,x.x,M,x.x,N,x.x,K,A*hh
    int commaIndex = 0;
    int commaPos[10]; // GPVTG a généralement 10 champs

    // Trouver les positions des virgules
    for (int i = 0; i < 10; i++)
    {
        commaPos[i] = sentence.indexOf(',', commaIndex);
        commaIndex = commaPos[i] + 1;
    }

    // Extraire les données
    speed = sentence.substring(commaPos[4] + 1, commaPos[5]);

    openLog.print("Speed (km/h): ");
    openLog.println(speed);
}

/**
 * @brief : read and display device info
 */
void GetDeviceInfo()
{
    char buf[32];
    uint8_t ret;
    SPS30_version v;

    // try to read serial number
    ret = sps30.GetSerialNumber(buf, 32);
    if (ret == SPS30_ERR_OK)
    {
        Serial.print(F("Serial number : "));
        if (strlen(buf) > 0)
            Serial.println(buf);
        else
            Serial.println(F("not available"));
    }
    else
        ErrtoMess((char *)"could not get serial number", ret);

    // try to get product name
    ret = sps30.GetProductName(buf, 32);
    if (ret == SPS30_ERR_OK)
    {
        Serial.print(F("Product name  : "));

        if (strlen(buf) > 0)
            Serial.println(buf);
        else
            Serial.println(F("not available"));
    }
    else
        ErrtoMess((char *)"could not get product name.", ret);

    // try to get version info
    ret = sps30.GetVersion(&v);
    if (ret != SPS30_ERR_OK)
    {
        Serial.println(F("Can not read version info"));
        return;
    }

    Serial.print(F("Firmware level: "));
    Serial.print(v.major);
    Serial.print(".");
    Serial.println(v.minor);

    if (SP30_COMMS != I2C_COMMS)
    {
        Serial.print(F("Hardware level: "));
        Serial.println(v.HW_version);

        Serial.print(F("SHDLC protocol: "));
        Serial.print(v.SHDLC_major);
        Serial.print(".");
        Serial.println(v.SHDLC_minor);
    }

    Serial.print(F("Library level : "));
    Serial.print(v.DRV_major);
    Serial.print(".");
    Serial.println(v.DRV_minor);
}

/**
 * @brief : read and display all values
 */
// bool readSPS30()
// {
//     static bool header = true;
//     uint8_t ret, error_cnt = 0;
//     struct sps_values val;

//     // loop to get data
//     do
//     {

//         ret = sps30.GetValues(&val);

//         // data might not have been ready
//         if (ret == SPS30_ERR_DATALENGTH)
//         {

//             if (error_cnt++ > 3)
//             {
//                 ErrtoMess((char *)"Error during reading values: ", ret);
//                 return (false);
//             }
//             delay(1000);
//         }

//         // if other error
//         else if (ret != SPS30_ERR_OK)
//         {
//             ErrtoMess((char *)"Error during reading values: ", ret);
//             return (false);
//         }

//     } while (ret != SPS30_ERR_OK);

//     openLog.println(F("-------------Mass -----------    ------------- Number --------------   -Average-"));
//     openLog.println(F("     Concentration [μg/m3]             Concentration [#/cm3]             [μm]"));
//     openLog.println(F("P1.0\tP2.5\tP4.0\tP10\tP0.5\tP1.0\tP2.5\tP4.0\tP10\tPartSize\n"));

//     openLog.print(val.MassPM1);
//     openLog.print(F("\t"));
//     openLog.print(val.MassPM2);
//     openLog.print(F("\t"));
//     openLog.print(val.MassPM4);
//     openLog.print(F("\t"));
//     openLog.print(val.MassPM10);
//     openLog.print(F("\t"));
//     openLog.print(val.NumPM0);
//     openLog.print(F("\t"));
//     openLog.print(val.NumPM1);
//     openLog.print(F("\t"));
//     openLog.print(val.NumPM2);
//     openLog.print(F("\t"));
//     openLog.print(val.NumPM4);
//     openLog.print(F("\t"));
//     openLog.print(val.NumPM10);
//     openLog.print(F("\t"));
//     openLog.print(val.PartSize);
//     openLog.print(F("\n"));

//     return (true);
// }

/**
 *  @brief : continued loop after fatal error
 *  @param mess : message to display
 *  @param r : error code
 *
 *  if r is zero, it will only display the message
 */
void Errorloop(char *mess, uint8_t r)
{
    if (r)
        ErrtoMess(mess, r);
    else
        Serial.println(mess);
    Serial.println(F("Program on hold"));
    for (;;)
        delay(100000);
}

/**
 *  @brief : display error message
 *  @param mess : message to display
 *  @param r : error code
 *
 */
void ErrtoMess(char *mess, uint8_t r)
{
    char buf[80];

    Serial.print(mess);

    sps30.GetErrDescription(r, buf, 80);
    Serial.println(buf);
}

/**
 * serialTrigger prints repeated message, then waits for enter
 * to come in from the serial port.
 */
void serialTrigger(char *mess)
{
    Serial.println();

    while (!Serial.available())
    {
        Serial.println(mess);
        delay(2000);
    }

    while (Serial.available())
        Serial.read();
}