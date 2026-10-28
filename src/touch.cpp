#include "touch.h"

#include <LittleFS.h>

#include <algorithm>

#include "ui.h"

constexpr uint16_t TOUCH_Z_MIN = 80;

void touchBegin() {
  pinMode(XPT_CLK, OUTPUT);
  pinMode(XPT_MOSI, OUTPUT);
  pinMode(XPT_MISO, INPUT);
  pinMode(XPT_CS, OUTPUT);
  digitalWrite(XPT_CS, HIGH);
  digitalWrite(XPT_CLK, LOW);
}

static uint8_t xptXfer(uint8_t out) {
  uint8_t in = 0;
  for (int i = 7; i >= 0; i--) {
    digitalWrite(XPT_MOSI, (out >> i) & 1);
    delayMicroseconds(1);
    digitalWrite(XPT_CLK, HIGH);
    in = (in << 1) | digitalRead(XPT_MISO);
    delayMicroseconds(1);
    digitalWrite(XPT_CLK, LOW);
  }
  return in;
}

static uint16_t xptRead(uint8_t cmd) {
  xptXfer(cmd);
  uint16_t v = (xptXfer(0) << 8) | xptXfer(0);
  return v >> 3;
}

bool touchRaw(int &rx, int &ry, uint16_t *zOut) {
  digitalWrite(XPT_CS, LOW);
  uint16_t z = xptRead(0xB1);
  bool ok = z > TOUCH_Z_MIN;
  if (ok) {
    int xs[3], ys[3];
    for (int i = 0; i < 3; i++) {
      xs[i] = xptRead(0xD1);
      ys[i] = xptRead(0x91);
    }
    std::sort(xs, xs + 3);
    std::sort(ys, ys + 3);
    ok = xs[2] - xs[0] < 60 && ys[2] - ys[0] < 60;
    rx = xs[1];
    ry = ys[1];
  }
  xptRead(0x90);  // power down with pen IRQ enabled
  digitalWrite(XPT_CS, HIGH);
  if (zOut) *zOut = z;
  return ok;
}

// Calibration maps raw readings to screen pixels; `swap` means screen x follows raw y.
struct Cal {
  uint32_t magic;
  uint8_t swap;
  int16_t a1, a2, b1, b3;
};
constexpr uint32_t CAL_MAGIC = 0x54434131;  // "TCA1"
static Cal cal;

static void rawToScreen(int rx, int ry, int &x, int &y) {
  int a = cal.swap ? ry : rx;
  int b = cal.swap ? rx : ry;
  x = 20 + (a - cal.a1) * 280 / (cal.a2 - cal.a1);
  y = 20 + (b - cal.b1) * 200 / (cal.b3 - cal.b1);
  x = constrain(x, 0, 319);
  y = constrain(y, 0, 239);
}

bool readTouch(int &x, int &y) {
  // Light or slow strokes drop out briefly; a sample takes well under 1ms, so this is
  // measured in time rather than missed samples.
  constexpr uint32_t MAX_DROPOUT_MS = 25;
  static int lastX, lastY;
  static bool down = false;
  static uint32_t lastSeen;
  int rx, ry;
  if (touchRaw(rx, ry)) {
    rawToScreen(rx, ry, lastX, lastY);
    down = true;
    lastSeen = millis();
  } else if (!down || millis() - lastSeen > MAX_DROPOUT_MS) {
    down = false;
    return false;
  }
  x = lastX;
  y = lastY;
  return true;
}

void waitForRelease() {
  for (int quiet = 0; quiet < 10;) {
    int x, y;
    quiet = touchRaw(x, y) ? 0 : quiet + 1;
    delay(10);
  }
}

// Waits for a tap and returns the averaged raw position.
static void waitTapRaw(int &rx, int &ry) {
  uint32_t lastLog = 0;
  long sx = 0, sy = 0, n = 0;
  while (n < 8) {
    int x, y;
    uint16_t z;
    if (touchRaw(x, y, &z)) {
      sx += x;
      sy += y;
      n++;
    } else if (n == 0 && millis() - lastLog > 2000) {
      Serial.printf("waiting for touch (raw z=%u)\n", z);
      lastLog = millis();
    }
    delay(10);
  }
  rx = sx / n;
  ry = sy / n;
  waitForRelease();
}

static bool calibrate() {
  const int tx[3] = {20, 300, 20};
  const int ty[3] = {20, 20, 220};
  int rx[3], ry[3];
  for (int i = 0; i < 3; i++) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Touch calibration", 160, 100, 4);
    tft.drawString("Tap the centre of the cross", 160, 135, 2);
    tft.drawFastHLine(tx[i] - 10, ty[i], 21, TFT_RED);
    tft.drawFastVLine(tx[i], ty[i] - 10, 21, TFT_RED);
    waitTapRaw(rx[i], ry[i]);
    Serial.printf("cal point %d: raw x=%d y=%d\n", i, rx[i], ry[i]);
  }
  cal.magic = CAL_MAGIC;
  cal.swap = abs(ry[1] - ry[0]) > abs(rx[1] - rx[0]);
  cal.a1 = cal.swap ? ry[0] : rx[0];
  cal.a2 = cal.swap ? ry[1] : rx[1];
  cal.b1 = cal.swap ? rx[0] : ry[0];
  cal.b3 = cal.swap ? rx[2] : ry[2];
  return abs(cal.a2 - cal.a1) > 300 && abs(cal.b3 - cal.b1) > 300;
}

static bool loadCal() {
  File f = LittleFS.open("/cal.bin", "r");
  if (!f) return false;
  bool ok = f.read((uint8_t *)&cal, sizeof(cal)) == sizeof(cal) && cal.magic == CAL_MAGIC;
  f.close();
  return ok;
}

void calibrateTouch() {
  while (!calibrate()) Serial.println("calibration rejected, retrying");
  File f = LittleFS.open("/cal.bin", "w");
  f.write((const uint8_t *)&cal, sizeof(cal));
  f.close();
}

void loadOrCalibrate() {
  if (!loadCal()) calibrateTouch();
}
