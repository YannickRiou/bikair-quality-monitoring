#include "gps.h"
#include "config.h"

// Static member initialization
HardwareSerial GPSManager::gpsSerial(2);
bool GPSManager::initialized = false;
String GPSManager::lastError;
String GPSManager::timeUTC;
String GPSManager::latitude;
String GPSManager::longitude;
String GPSManager::altitude;
String GPSManager::speed;
String GPSManager::fixStatus;
String GPSManager::satellites;

bool GPSManager::init()
{
    if (initialized)
    {
        lastError = "Already initialized";
        return false;
    }

    gpsSerial.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_RX, GPS_TX);
    delay(1000);
    gpsSerial.println("$PMTK101*32"); // GPS wakeup command
    gpsSerial.println("$PMTK313,0");  // Disable SBAS

    initialized = true;
    lastError = "";
    return true;
}

void GPSManager::process()
{
    static String gpsSentence;

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
            else if (gpsSentence.startsWith("$GPVTG"))
            {
                parseGPVTG(gpsSentence);
            }
            gpsSentence = "";
        }
    }
}

bool GPSManager::hasFix()
{
    return fixStatus == "1";
}

void GPSManager::parseGPGGA(const String &sentence)
{
    int commaPos[15];
    int commaIndex = 0;

    // Find comma positions
    for (int i = 0; i < 15; i++)
    {
        commaPos[i] = sentence.indexOf(',', commaIndex);
        commaIndex = commaPos[i] + 1;
    }

    // Extract time and data
    timeUTC = sentence.substring(commaPos[0] + 1, commaPos[1]);

    // Extract position data
    latitude = String(convertLatLonToDecimal(
                          sentence.substring(commaPos[1] + 1, commaPos[2])),
                      6);
    longitude = String(convertLatLonToDecimal(
                           sentence.substring(commaPos[3] + 1, commaPos[4])),
                       6);
    fixStatus = sentence.substring(commaPos[5] + 1, commaPos[6]);
    satellites = sentence.substring(commaPos[6] + 1, commaPos[7]);
    altitude = sentence.substring(commaPos[8] + 1, commaPos[9]);
}

void GPSManager::parseGPVTG(const String &sentence)
{
    int commaPos[10];
    int commaIndex = 0;

    // Find comma positions
    for (int i = 0; i < 10; i++)
    {
        commaPos[i] = sentence.indexOf(',', commaIndex);
        commaIndex = commaPos[i] + 1;
    }

    // Extract speed in km/h (field 7)
    speed = sentence.substring(commaPos[6] + 1, commaPos[7]);
}

double GPSManager::convertLatLonToDecimal(const String &gga_coord)
{
    if (gga_coord.length() == 0)
        return 0.0;

    // Extract degrees and minutes
    int degrees = gga_coord.substring(0, gga_coord.length() - 7).toInt();
    double minutes = gga_coord.substring(gga_coord.length() - 7).toDouble();

    // Convert to decimal format
    return degrees + (minutes / 60.0);
}
