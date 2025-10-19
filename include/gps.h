#ifndef GPS_H
#define GPS_H

#include <Arduino.h>
#include <HardwareSerial.h>

class GPSManager
{
public:
    static bool init();
    static void process();
    static bool hasFix();
    static bool isInitialized() { return initialized; }
    static const String &getLastError() { return lastError; }

    // GPS data accessors
    static const String &getLatitude() { return latitude; }
    static const String &getLongitude() { return longitude; }
    static const String &getAltitude() { return altitude; }
    static const String &getSpeed() { return speed; }
    static const String &getFixStatus() { return fixStatus; }
    static const String &getSatellites() { return satellites; }
    static const String &getTimeUTC() { return timeUTC; }
    static HardwareSerial &getSerial() { return gpsSerial; }

private:
    static bool initialized;
    static String lastError;
    static HardwareSerial gpsSerial;
    static String timeUTC;
    static String latitude;
    static String longitude;
    static String altitude;
    static String speed;
    static String fixStatus;
    static String satellites;

    static void parseGPGGA(const String &sentence);
    static void parseGPVTG(const String &sentence);
    static double convertLatLonToDecimal(const String &gga_coord);
};

#endif // GPS_H