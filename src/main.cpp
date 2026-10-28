// Start screen: tap the drawing app or the pet. There's no way back here without
// restarting, so kids stay in the app they picked. Holding the BOOT button for two
// seconds on this screen recalibrates touch (a grown-up job).
#include <Arduino.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>

#include "draw_app.h"
#include "led.h"
#include "pet_app.h"
#include "strokes.h"
#include "touch.h"
#include "ui.h"

TFT_eSPI tft;

constexpr int BOOT_BUTTON = 0;
constexpr uint32_t RECALIBRATE_HOLD_MS = 2000;

enum class App { LAUNCHER, DRAW, PET };
static App app = App::LAUNCHER;

constexpr int TILE_Y = 8, TILE_W = 148, TILE_H = 224, DRAW_TILE_X = 8, PET_TILE_X = 164;

static void drawLauncher() {
  tft.fillScreen(TFT_DARKGREY);
  // Drawing: rainbow squiggles and a big pencil
  tft.fillRoundRect(DRAW_TILE_X, TILE_Y, TILE_W, TILE_H, 14, TFT_WHITE);
  for (int c = RED; c <= MAGENTA; c++) {
    for (int t = 0; t <= 100; t += 2) {
      tft.fillCircle(32 + t, 36 + c * 14 + 6 * sinf(t / 9.0f + c), 3, COLOURS[c]);
    }
  }
  drawPencil(DRAW_TILE_X + TILE_W / 2, 178, 2.4, TFT_WHITE);
  // Pet: your pet (or its egg) standing on the floor
  tft.fillRoundRect(PET_TILE_X, TILE_Y, TILE_W, TILE_H, 14, TFT_SKYBLUE);
  tft.fillRect(PET_TILE_X + 10, 190, TILE_W - 20, 4, TFT_DARKGREEN);
  petDrawPreview(PET_TILE_X + TILE_W / 2, 190);
}

void setup() {
  Serial.begin(115200);
  touchBegin();
  ledBegin();
  pinMode(BOOT_BUTTON, INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  if (!LittleFS.begin(true)) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Storage failed to mount", 160, 120, 2);
    Serial.println("LittleFS mount failed");
    while (true) delay(1000);
  }
  LittleFS.mkdir("/d");
  LittleFS.mkdir("/pet");
  Serial.printf("storage: %u / %u bytes used, %u drawings\n", LittleFS.usedBytes(),
                LittleFS.totalBytes(), listDrawings().size());

  loadOrCalibrate();
  drawLauncher();
}

void loop() {
  if (app == App::DRAW) return drawAppLoop();
  if (app == App::PET) return petAppLoop();

  static uint32_t bootHeldSince = 0;
  if (digitalRead(BOOT_BUTTON) == LOW) {
    if (!bootHeldSince) bootHeldSince = millis();
    if (millis() - bootHeldSince >= RECALIBRATE_HOLD_MS) {
      calibrateTouch();
      drawLauncher();
      bootHeldSince = 0;
    }
  } else {
    bootHeldSince = 0;
  }

  int x, y;
  if (!readTouch(x, y)) {
    delay(10);
    return;
  }
  app = x < 160 ? App::DRAW : App::PET;
  int tileX = app == App::DRAW ? DRAW_TILE_X : PET_TILE_X;
  for (int i = 0; i < 4; i++) {
    tft.drawRoundRect(tileX + i, TILE_Y + i, TILE_W - 2 * i, TILE_H - 2 * i, 14, TFT_YELLOW);
  }
  waitForRelease();
  app == App::DRAW ? drawAppBegin() : petAppBegin();
}
