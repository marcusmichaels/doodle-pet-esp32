// The pet's needs, saved to flash, and the real-time clock (set over WiFi) that lets
// them keep changing while the board is switched off.
#pragma once
#include <Arduino.h>

// A need sitting at zero this long and the pet runs away (a new egg appears).
constexpr uint32_t RUNAWAY_SEC = 24 * 3600;
constexpr int BEDTIME_HOUR = 19, WAKE_HOUR = 7;  // local time; the pet sleeps in between

struct PetState {
  uint32_t magic;
  uint8_t hatched;    // 0 while it's still an egg
  uint8_t colour;     // body (or egg) colour, index into COLOURS
  uint8_t favourite;  // favourite food colour: extra happy eating it or seeing it in art
  uint8_t asleep;
  float fullness, fun, energy;  // 0-100
  uint32_t ageSec;
  uint32_t neglectSec;  // how long a need has been at zero
  uint32_t savedAt;     // unix time of the last save, 0 if the clock wasn't set
  int16_t artId;        // drawing hanging on the wall, 0 for none
};

extern PetState stats;

void statsNewEgg();
bool statsLoad();  // false if there's no saved pet (stats is then a new egg)
void statsSave();

// Advances needs by `sec` of real time, asleep or awake per stats.asleep.
void statsTick(float sec);
// Applies the time that passed while the board was off. Call after clockSync().
void statsCatchUp();
bool wantsSleep(bool night);
bool wantsWake(bool night);
int petSize();  // grows over its first few days

// Connects to WiFi (see include/wifi_config.h), sets the clock, then turns WiFi off.
bool clockSync();
bool clockValid();
bool isNight();
