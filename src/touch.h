// XPT2046 touch, bit-banged on its own pins (see platformio.ini).
#pragma once
#include <Arduino.h>

void touchBegin();

// Raw 12-bit reading. Returns false if not touched or the samples disagree.
bool touchRaw(int &rx, int &ry, uint16_t *zOut = nullptr);

// Screen coordinates while touched; rides out brief dropouts mid-stroke.
bool readTouch(int &x, int &y);

// Waits until nothing has touched the screen for a moment.
void waitForRelease();

// Loads the saved calibration, running calibration first if there isn't one.
void loadOrCalibrate();

// Runs the three-cross calibration and saves it.
void calibrateTouch();
