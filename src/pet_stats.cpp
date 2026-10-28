#include "pet_stats.h"

#include <LittleFS.h>
#include <WiFi.h>
#include <time.h>

#include "ui.h"

// WiFi details live in include/wifi_config.h (not committed). Without it the clock
// isn't set, so time only passes while the board is on.
#if __has_include("wifi_config.h")
#include "wifi_config.h"
#endif
#ifndef PET_TIMEZONE
#define PET_TIMEZONE "GMT0BST,M3.5.0/1,M10.5.0"  // UK
#endif

constexpr uint32_t STATS_MAGIC = 0x50455431;  // "PET1"
static const char *const STATS_PATH = "/pet/state.bin";

// Change per hour of real time
constexpr float AWAKE_FULLNESS = -12, AWAKE_FUN = -14, AWAKE_ENERGY = -8;
constexpr float ASLEEP_FULLNESS = -4, ASLEEP_FUN = -2, ASLEEP_ENERGY = 30;

PetState stats;

// Colours that make food; the favourite is always one of these.
static const uint8_t FOOD_TOOLS[] = {RED, ORANGE, YELLOW, GREEN, BLUE, MAGENTA};

static uint8_t randomFood() { return FOOD_TOOLS[random(sizeof(FOOD_TOOLS))]; }

static bool isFood(uint8_t c) {
  for (uint8_t f : FOOD_TOOLS) {
    if (c == f) return true;
  }
  return false;
}

void statsNewEgg() {
  int16_t artId = stats.magic == STATS_MAGIC ? stats.artId : 0;  // the room keeps its art
  stats = {};
  stats.magic = STATS_MAGIC;
  stats.colour = random(RED, WHITE);
  stats.favourite = randomFood();
  stats.fullness = 70;
  stats.fun = 70;
  stats.energy = 80;
  stats.artId = artId;
}

bool statsLoad() {
  File f = LittleFS.open(STATS_PATH, "r");
  bool ok = f && f.read((uint8_t *)&stats, sizeof(stats)) == sizeof(stats) &&
            stats.magic == STATS_MAGIC;
  if (f) f.close();
  if (!ok) {
    stats.magic = 0;
    statsNewEgg();
  } else if (!isFood(stats.favourite)) {  // not a food colour (from an older version)
    stats.favourite = randomFood();
  }
  return ok;
}

void statsSave() {
  stats.savedAt = clockValid() ? time(nullptr) : 0;
  File f = LittleFS.open(STATS_PATH, "w");
  if (!f) return;
  f.write((const uint8_t *)&stats, sizeof(stats));
  f.close();
}

void statsTick(float sec) {
  if (!stats.hatched) return;
  float h = sec / 3600;
  bool z = stats.asleep;
  stats.fullness = constrain(stats.fullness + h * (z ? ASLEEP_FULLNESS : AWAKE_FULLNESS), 0, 100);
  stats.fun = constrain(stats.fun + h * (z ? ASLEEP_FUN : AWAKE_FUN), 0, 100);
  stats.energy = constrain(stats.energy + h * (z ? ASLEEP_ENERGY : AWAKE_ENERGY), 0, 100);
  stats.ageSec += sec;
  if (stats.fullness <= 0 || stats.fun <= 0) {
    stats.neglectSec += sec;
  } else if (stats.fullness > 10 && stats.fun > 10) {
    stats.neglectSec = 0;
  }
}

bool wantsSleep(bool night) { return night || stats.energy < 15; }
bool wantsWake(bool night) { return !night && stats.energy >= 95; }

static bool isNightAt(time_t t) {
  struct tm tm;
  localtime_r(&t, &tm);
  return tm.tm_hour >= BEDTIME_HOUR || tm.tm_hour < WAKE_HOUR;
}

bool clockValid() { return time(nullptr) > 1700000000; }

bool isNight() { return clockValid() && isNightAt(time(nullptr)); }

void statsCatchUp() {
  if (!stats.hatched || !stats.savedAt || !clockValid()) return;
  constexpr uint32_t MAX_CATCH_UP = 30 * 24 * 3600;
  uint32_t now = time(nullptr);
  if (now <= stats.savedAt) return;
  uint32_t from = max(stats.savedAt, now - MAX_CATCH_UP);
  Serial.printf("pet: catching up %u minutes\n", (now - from) / 60);
  for (uint32_t t = from; t < now && stats.neglectSec < RUNAWAY_SEC; t += 60) {
    bool night = isNightAt(t);
    if (!stats.asleep && wantsSleep(night)) stats.asleep = 1;
    else if (stats.asleep && wantsWake(night)) stats.asleep = 0;
    statsTick(min<uint32_t>(60, now - t));
  }
}

int petSize() {
  uint32_t days = stats.ageSec / (24 * 3600);
  return days < 1 ? 22 : days < 3 ? 26 : 30;
}

bool clockSync() {
  setenv("TZ", PET_TIMEZONE, 1);
  tzset();
#ifdef WIFI_SSID
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  for (uint32_t start = millis(); WiFi.status() != WL_CONNECTED && millis() - start < 10000;) {
    delay(100);
  }
  if (WiFi.status() == WL_CONNECTED) {
    configTzTime(PET_TIMEZONE, "pool.ntp.org", "time.google.com");
    for (uint32_t start = millis(); !clockValid() && millis() - start < 6000;) delay(100);
  }
  Serial.printf("clock: wifi %s, time %s\n", WiFi.status() == WL_CONNECTED ? "ok" : "failed",
                clockValid() ? "set" : "unknown");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
#else
  Serial.println("clock: no include/wifi_config.h, time only passes while switched on");
#endif
  return clockValid();
}
