#include "pet_draw.h"

static uint16_t shade(uint16_t c, float t, uint16_t towards) {
  int r = ((c >> 11) & 31) * (1 - t) + ((towards >> 11) & 31) * t;
  int g = ((c >> 5) & 63) * (1 - t) + ((towards >> 5) & 63) * t;
  int b = (c & 31) * (1 - t) + (towards & 31) * t;
  return (r << 11) | (g << 5) | b;
}

// Outlined ellipse: black rim, then the fill.
static void blob(TFT_eSPI &g, int cx, int cy, int rx, int ry, uint16_t col) {
  g.fillEllipse(cx, cy, rx + 1, ry + 1, TFT_BLACK);
  g.fillEllipse(cx, cy, max(1, rx), max(1, ry), col);
}

// Curved mouth: positive bend smiles, negative frowns.
static void mouthCurve(TFT_eSPI &g, int cx, int cy, int halfW, float bend) {
  for (int i = -halfW; i <= halfW; i++) {
    float t = (float)i / halfW;
    int y = cy + bend * (1 - t * t);
    g.fillRect(cx + i, y, 1, 2, TFT_BLACK);
  }
}

static void drawEgg(TFT_eSPI &g, int cx, int by, const PetLook &p) {
  int rx = p.size * 0.42, ry = p.size * 0.56;
  int cy = by - ry - 1;
  int tilt = p.wobble * p.size * 0.15;
  blob(g, cx + tilt / 2, cy, rx, ry, p.colour);
  uint16_t spot = shade(p.colour, 0.35, TFT_WHITE);
  g.fillCircle(cx + tilt / 2 - rx / 3, cy - ry / 4, max(2, rx / 4), spot);
  g.fillCircle(cx + tilt / 2 + rx / 3, cy + ry / 5, max(2, rx / 5), spot);
  g.fillCircle(cx + tilt / 2 - rx / 6, cy + ry / 2, max(1, rx / 7), spot);
  // Zig-zag cracks across the middle, one more per tap
  int x0 = cx + tilt / 2 - rx + 2, step = max(2, rx / 3);
  for (int c = 0; c < p.cracks; c++) {
    int y = cy - ry / 3 + c * ry / 3;
    for (int i = 0, x = x0; x < cx + tilt / 2 + rx - 2; i++, x += step) {
      g.drawLine(x, y + (i % 2 ? 3 : -3), x + step, y + (i % 2 ? -3 : 3), TFT_BLACK);
    }
  }
}

void petExtent(const PetLook &p, int &halfW, int &h) {
  halfW = p.size * 0.5 * (1 + fabsf(p.squash)) + 4;
  h = p.size * 1.25 + 4;
}

void drawPet(TFT_eSPI &g, int cx, int by, const PetLook &p) {
  if (p.egg) {
    drawEgg(g, cx, by, p);
    return;
  }
  float w = p.size * (1 + p.squash), h = p.size * 0.85f * (1 - p.squash);
  uint16_t dark = shade(p.colour, 0.45, TFT_BLACK);

  // Feet, then body over them
  int footRx = max(2, (int)(w * 0.15)), footY = by - 3;
  blob(g, cx - w * 0.22, footY, footRx, 2, dark);
  blob(g, cx + w * 0.22, footY, footRx, 2, dark);
  int bodyCy = by - 2 - h / 2;
  blob(g, cx, bodyCy, w / 2, h / 2, p.colour);
  if (p.colour != TFT_WHITE) {
    g.fillEllipse(cx - w * 0.2, bodyCy - h * 0.25, max(1, (int)(w * 0.09)),
                  max(1, (int)(h * 0.07)), shade(p.colour, 0.6, TFT_WHITE));
  }

  // Eyes
  int ex = w * 0.2, ey = bodyCy - h * 0.1, er = max(3, (int)(w * 0.13));
  bool closed = p.blink || p.face == Face::SLEEP;
  for (int side = -1; side <= 1; side += 2) {
    int x = cx + side * ex;
    if (closed) {
      g.fillRect(x - er, ey, er * 2 + 1, 2, TFT_BLACK);
    } else if (p.face == Face::YUCK) {  // squeezed shut: > <
      g.drawLine(x - side * er, ey - er + 1, x + side * er, ey, TFT_BLACK);
      g.drawLine(x + side * er, ey, x - side * er, ey + er - 1, TFT_BLACK);
    } else {
      g.fillCircle(x, ey, er + 1, TFT_BLACK);
      g.fillCircle(x, ey, er, TFT_WHITE);
      int pr = max(1, (int)(er * 0.55));
      g.fillCircle(x + p.lookX * (er - pr), ey + p.lookY * (er - pr), pr, TFT_BLACK);
    }
  }
  if (p.face == Face::SAD) {  // worried eyebrows
    for (int side = -1; side <= 1; side += 2) {
      int x = cx + side * ex;
      g.drawLine(x - side * er, ey - er - 2, x + side * er, ey - er - 5, TFT_BLACK);
    }
  }

  // Mouth
  int my = bodyCy + h * 0.2, mw = max(2, (int)(w * 0.13));
  switch (p.face) {
    case Face::HAPPY:
      mouthCurve(g, cx, my, mw, mw * 0.7);
      g.fillCircle(cx - w * 0.33, my - 1, max(1, (int)(w * 0.07)), TFT_PINK);
      g.fillCircle(cx + w * 0.33, my - 1, max(1, (int)(w * 0.07)), TFT_PINK);
      break;
    case Face::SAD:
      mouthCurve(g, cx, my + 2, mw, -mw * 0.6);
      break;
    case Face::CHEW_OPEN:
    case Face::WOW:
      g.fillEllipse(cx, my + 1, max(2, (int)(w * 0.09)), max(2, (int)(h * 0.1)), TFT_BLACK);
      if (p.face == Face::CHEW_OPEN) g.fillCircle(cx, my + 2, 1, TFT_RED);
      break;
    case Face::CHEW_SHUT:
      mouthCurve(g, cx, my, mw / 2 + 1, 1);
      break;
    case Face::YUCK:
      for (int i = -mw; i <= mw; i++) g.fillRect(cx + i, my + ((i / 2) % 2 ? 1 : -1), 1, 2, TFT_BLACK);
      break;
    case Face::SLEEP:
      g.drawCircle(cx, my + 1, 2, TFT_BLACK);
      break;
    case Face::NEUTRAL:
      g.fillRect(cx - mw / 2, my, mw + 1, 2, TFT_BLACK);
      break;
  }
}

void drawBall(TFT_eSPI &g, int cx, int cy, int r, uint16_t colour) {
  g.fillCircle(cx, cy, r, TFT_BLACK);
  g.fillCircle(cx, cy, r - 1, colour);
  // A stripe and a shine
  g.drawFastHLine(cx - r + 2, cy, 2 * r - 3, shade(colour, 0.4, TFT_BLACK));
  if (colour != TFT_WHITE) {
    g.fillCircle(cx - r / 3, cy - r / 3, max(1, r / 4), shade(colour, 0.6, TFT_WHITE));
  }
}

void drawHeart(TFT_eSPI &g, int cx, int cy, int s, uint16_t colour) {
  g.fillCircle(cx - s / 2, cy - s / 4, s / 2, colour);
  g.fillCircle(cx + s / 2, cy - s / 4, s / 2, colour);
  g.fillTriangle(cx - s, cy - s / 8, cx + s, cy - s / 8, cx, cy + s, colour);
}

void drawZ(TFT_eSPI &g, int x, int y, uint16_t bg) {
  g.drawWideLine(x, y, x + 6, y, 2, TFT_NAVY, bg);
  g.drawWideLine(x + 6, y, x, y + 6, 2, TFT_NAVY, bg);
  g.drawWideLine(x, y + 6, x + 6, y + 6, 2, TFT_NAVY, bg);
}
