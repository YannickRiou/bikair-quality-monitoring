#include <main.h>
#include <wireless.h>

RunningMedian temperatureMeas = RunningMedian(5);
RunningMedian humidityMeas = RunningMedian(5);
RunningMedian co2Meas = RunningMedian(5);

bool sensorkTaskOn = false; // Start inactive until GPS fix is acquired
bool gpsTaskOn = true;
unsigned long measurementStart = 0;
const unsigned long FIX_TIMEOUT = 300000;         // 5-minute GPS fix timeout (ms)
const unsigned long MEASUREMENT_DURATION = 20000; // 20-second active period

uint8_t ledVal = 0;

HardwareSerial gpsSerial(2);

SoftwareSerial openLog(16, 17);

ScioSense_ENS160 ens160(0x53);
AHT10 myAHT20(AHT10_ADDRESS_0X38, AHT20_SENSOR);

String timeUTC = "";
String latitude = "";
String longitude = "";
String altitude = "";
String speed = "";
String fixStatus = "";
String satellites = "";

// create constructor
SPS30 sps30;
#define SP30_COMMS SERIALPORT1

#define TX_PIN 9
#define RX_PIN 10

void setup()
{
    Serial.begin(115200);

    openLog.begin(9600); // Should be the speed specified in config.txt on the sd card divided by 2

    Wire.begin(21, 22); // SDA, SCL

    pinMode(GPIO_NUM_2, OUTPUT);
    digitalWrite(GPIO_NUM_2, HIGH);
    delay(25);

    myAHT20.begin(21, 22);

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

    // start measurement
    if (sps30.start())
        Serial.println(F("Measurement started"));
    else
        Errorloop((char *)"Could NOT start measurement", 0);

    gpsSerial.begin(9600, SERIAL_8N1, GPIO_NUM_4, GPIO_NUM_32);
    delay(1000);
    gpsSerial.println("$PMTK101*32"); // GPS wakeup command

    xTaskCreate(taskGPS, "TaskGPS", 2048, NULL, 1, &TaskGPS);
    xTaskCreate(taskSensors, "TaskSensors", 2048, NULL, 1, &TaskSensors);

    openLog.println("Time (UTC),lat,lon,Temperature,Humidity,TVOC,CO2,PM1,PM2");
}

void taskSensors(void *pvParameters)
{
    (void)pvParameters;

    while (true)
    {
        if (sensorkTaskOn)
        {
            String sensorReadings = readSensors();
            notifyClients(sensorReadings);
            ws.cleanupClients();
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
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

double convert_latlon_to_decimal(String gga_coord)
{
    // Extraire les degrés et les minutes
    int degrees = gga_coord.substring(0, gga_coord.length() - 7).toInt();
    double minutes = gga_coord.substring(gga_coord.length() - 7).toDouble();

    // Convertir en format décimal
    return degrees + (minutes / 60.0);
}

String convert_utc_to_readable(String utc_time)
{
    // Extraire les heures, minutes et secondes
    String hours = utc_time.substring(0, 2);
    String minutes = utc_time.substring(2, 4);
    String seconds = utc_time.substring(4, 6);

    // Construire la chaîne formatée
    return hours + ":" + minutes + ":" + seconds;
}

String readSensors()
{
    static bool header = true;
    uint8_t ret, error_cnt = 0;
    struct sps_values val;

    float temperature = myAHT20.readTemperature();
    temperatureMeas.add(temperature);
    float humidity = myAHT20.readHumidity();
    humidityMeas.add(humidity);

    if (ens160.measure())
    {
        float tvoc = ens160.getTVOC();
        float co2 = ens160.geteCO2();
        uint8_t AQI = ens160.getAQI();
        co2Meas.add(co2);

        // Afficher les données des capteurs
        openLog.print(timeUTC);
        openLog.print(",");
        openLog.print(latitude);
        openLog.print(",");
        openLog.print(longitude);
        openLog.print(",");
        openLog.print(temperature);
        openLog.print(",");
        openLog.print(humidity);
        openLog.print(",");
        openLog.print(tvoc);
        openLog.print(",");
        openLog.print(co2Meas.getAverage());
        openLog.print(",");
        openLog.print(AQI);

        // Store for websockets
        readings["co2"] = String(co2);
        readings["tvoc"] = String(tvoc);
        readings["humidity"] = String(humidityMeas.getAverage());
        readings["temperature"] = String(temperatureMeas.getAverage());
        readings["aqi"] = String(AQI);
    }

    sps30.GetValues(&val);
    openLog.print(",");
    openLog.print(val.MassPM1);
    openLog.print(",");
    openLog.println(val.MassPM2);

    // Store for websockets
    readings["pm1"] = String(val.MassPM1);
    readings["pm2"] = String(val.MassPM2);

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
        if (gpsTaskOn)
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
}

void prepareForSleep()
{
    sensorkTaskOn = false;
    gpsTaskOn = false;

    // Allow tasks to finish current operations
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Power down sensors
    sps30.stop();
    ens160.setMode(ENS160_OPMODE_DEP_SLEEP);
    gpsSerial.println("$PMTK161,0*28"); // GPS standby mode

    // Disable peripherals
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    gpsSerial.end();
    openLog.end();
    Wire.end();
    digitalWrite(GPIO_NUM_2, LOW);
    delay(25);
    // Configure wakeup and sleep
    esp_sleep_enable_timer_wakeup(20 * 1000000); // 10-second sleep
    esp_deep_sleep_start();
}

void loop()
{
    unsigned long wakeTime = millis();
    bool fixAcquired = false;

    // Phase 1: Wait for GPS fix with timeout
    while (millis() - wakeTime < FIX_TIMEOUT)
    {
        if (fixStatus != "" && fixStatus != "0")
        {
            fixAcquired = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    sensorkTaskOn = true;

    if (!fixAcquired)
    {
        Serial.println("No GPS fix within timeout - returning to sleep");
        prepareForSleep();
        return;
    }
    // Phase 2: 20-second measurement period
    Serial.println("GPS fix acquired - starting measurements");
    measurementStart = millis();

    while (millis() - measurementStart < MEASUREMENT_DURATION)
    {
        ledVal = !ledVal;
        digitalWrite(GPIO_NUM_2, ledVal);
        delay(25);
        // Monitor GPS fix status during measurement
        if (fixStatus == "0")
        {
            Serial.println("Lost GPS fix during measurement!");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Phase 3: Prepare for sleep
    prepareForSleep();
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
    latitude = convert_latlon_to_decimal(sentence.substring(commaPos[1] + 1, commaPos[2]));
    longitude = convert_latlon_to_decimal(sentence.substring(commaPos[3] + 1, commaPos[4]));
    fixStatus = sentence.substring(commaPos[5] + 1, commaPos[6]);
    satellites = sentence.substring(commaPos[6] + 1, commaPos[7]);
    altitude = sentence.substring(commaPos[8] + 1, commaPos[9]);
    timeUTC = convert_utc_to_readable(sentence.substring(commaPos[0] + 1, commaPos[1]));

    // Serial.print("Latitude: ");
    // Serial.println(latitude);
    // Serial.print("Longitude: ");
    // Serial.println(longitude);
    // Serial.print("Fix Status: ");
    // Serial.println(fixStatus);
    // Serial.print("Satellites: ");
    // Serial.println(satellites);
    // Serial.print("Altitude: ");
    // Serial.println(altitude);
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