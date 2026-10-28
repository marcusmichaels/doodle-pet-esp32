// The board's RGB LED (pins in platformio.ini).
#pragma once
#include <Arduino.h>

void ledBegin();
// Shows an RGB565 colour at a brightness from 0 to 1. Black turns it off.
void ledSet(uint16_t colour, float level = 1);
void ledOff();
