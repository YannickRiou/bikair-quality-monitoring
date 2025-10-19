#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>

void Errorloop(const char *mess, uint8_t r = 0);
void ErrtoMess(const char *mess, uint8_t r);

#endif // UTILS_H