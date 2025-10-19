#include "utils.h"
#include "sensors.h"

void Errorloop(const char *mess, uint8_t r)
{
    if (r)
        ErrtoMess(mess, r);
    else
        Serial.println(mess);
    Serial.println(F("Program on hold"));
    for (;;)
        delay(100000);
}

void ErrtoMess(const char *mess, uint8_t r)
{
    char buf[80];
    Serial.print(mess);
    SensorManager::getSPS30().GetErrDescription(r, buf, 80);
    Serial.println(buf);
}