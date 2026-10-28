#include "led.h"

// The LED is very bright, so full brightness here is a fraction of what it can do.
constexpr float MAX_LEVEL = 0.35f;
constexpr int CHANNELS[3] = {4, 5, 6};  // LEDC PWM channels
constexpr int PINS[3] = {LED_RED, LED_GREEN, LED_BLUE};

void ledBegin() {
  for (int i = 0; i < 3; i++) {
    ledcSetup(CHANNELS[i], 5000, 8);
    ledcAttachPin(PINS[i], CHANNELS[i]);
  }
  ledOff();
}

void ledSet(uint16_t c, float level) {
  float s = constrain(level, 0.0f, 1.0f) * MAX_LEVEL;
  int rgb[3] = {((c >> 11) & 31) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31};
  for (int i = 0; i < 3; i++) {
    int duty = rgb[i] * s;
    ledcWrite(CHANNELS[i], LED_ACTIVE_LOW ? 255 - duty : duty);
  }
}

void ledOff() { ledSet(0, 0); }
