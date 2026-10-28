#include "ui.h"

void drawPalette(int selected) {
  tft.fillRect(0, 0, PALETTE_W, 240, TFT_DARKGREY);
  for (int i = 0; i < NUM_COLOURS; i++) {
    int y = i * SWATCH_H;
    tft.fillRect(5, y + 4, PALETTE_W - 10, SWATCH_H - 8, COLOURS[i]);
    if (i == selected) {
      tft.drawRect(1, y + 1, PALETTE_W - 2, SWATCH_H - 2, TFT_WHITE);
      tft.drawRect(2, y + 2, PALETTE_W - 4, SWATCH_H - 4, TFT_BLACK);
    }
  }
}

// Chunky arrow pointing left (dir -1) or right (dir 1).
static void drawArrow(int cx, int cy, int dir, uint16_t col) {
  tft.fillRect(dir > 0 ? cx - 11 : cx - 1, cy - 4, 12, 9, col);
  tft.fillTriangle(cx + dir * 13, cy, cx, cy - 11, cx, cy + 11, col);
}

static void drawBin(int cx, int cy, bool lidOpen, uint16_t bg) {
  tft.fillRect(cx - 9, cy - 5, 18, 17, TFT_WHITE);
  for (int dx = -4; dx <= 4; dx += 4) tft.drawFastVLine(cx + dx, cy - 2, 11, bg);
  if (lidOpen) {
    tft.drawWideLine(cx - 11, cy - 8, cx + 8, cy - 16, 3, TFT_WHITE, bg);
  } else {
    tft.fillRect(cx - 11, cy - 9, 22, 3, TFT_WHITE);
    tft.fillRect(cx - 3, cy - 12, 6, 3, TFT_WHITE);
  }
}

// Diagonal pencil, tip at bottom-left. scale 1 fits a toolbar button.
void drawPencil(int cx, int cy, float s, uint16_t bg) {
  tft.drawWideLine(cx - 6 * s, cy + 6 * s, cx + 6 * s, cy - 6 * s, 7 * s, TFT_YELLOW, bg);
  tft.drawWideLine(cx + 6 * s, cy - 6 * s, cx + 9 * s, cy - 9 * s, 7 * s, TFT_PINK, bg);
  tft.fillTriangle(cx - 11 * s, cy + 11 * s, cx - 3 * s, cy + 9 * s, cx - 9 * s, cy + 3 * s,
                   0xFE73);  // wood
  tft.fillTriangle(cx - 11 * s, cy + 11 * s, cx - 8 * s, cy + 10 * s, cx - 10 * s, cy + 8 * s,
                   TFT_BLACK);
}

void drawIcon(Icon icon, int cx, int cy, uint16_t bg) {
  switch (icon) {
    case Icon::NEW:  // plus
      tft.drawWideLine(cx, cy - 11, cx, cy + 11, 7, TFT_WHITE, bg);
      tft.drawWideLine(cx - 11, cy, cx + 11, cy, 7, TFT_WHITE, bg);
      break;
    case Icon::SAVE:  // floppy disk: body with a clipped corner, metal shutter, label
      tft.fillRect(cx - 12, cy - 12, 24, 24, TFT_BLUE);
      tft.fillTriangle(cx + 7, cy - 12, cx + 12, cy - 12, cx + 12, cy - 7, bg);
      tft.fillRect(cx - 6, cy - 12, 12, 8, TFT_LIGHTGREY);
      tft.fillRect(cx + 1, cy - 11, 3, 6, TFT_BLUE);
      tft.fillRect(cx - 8, cy + 1, 16, 9, TFT_WHITE);
      break;
    case Icon::VIEW:  // framed picture: hills and a sun
      tft.fillRect(cx - 13, cy - 11, 26, 22, TFT_ORANGE);
      tft.fillRect(cx - 10, cy - 8, 20, 16, TFT_SKYBLUE);
      tft.fillCircle(cx + 5, cy - 3, 2, TFT_YELLOW);
      tft.fillTriangle(cx - 10, cy + 7, cx - 3, cy - 3, cx + 4, cy + 7, TFT_DARKGREEN);
      tft.fillTriangle(cx - 2, cy + 7, cx + 4, cy, cx + 9, cy + 7, TFT_GREEN);
      break;
    case Icon::TICK:
      tft.drawWideLine(cx - 10, cy, cx - 3, cy + 8, 5, TFT_WHITE, bg);
      tft.drawWideLine(cx - 3, cy + 8, cx + 10, cy - 9, 5, TFT_WHITE, bg);
      break;
    case Icon::CROSS:
      tft.drawWideLine(cx - 9, cy - 9, cx + 9, cy + 9, 5, TFT_WHITE, bg);
      tft.drawWideLine(cx - 9, cy + 9, cx + 9, cy - 9, 5, TFT_WHITE, bg);
      break;
    case Icon::PREV:
      drawArrow(cx, cy, -1, TFT_WHITE);
      break;
    case Icon::NEXT:
      drawArrow(cx, cy, 1, TFT_WHITE);
      break;
    case Icon::PLAY:
      tft.fillCircle(cx, cy, 13, TFT_DARKGREEN);
      tft.fillTriangle(cx - 4, cy - 7, cx - 4, cy + 7, cx + 7, cy, TFT_WHITE);
      break;
    case Icon::TRASH:
      drawBin(cx, cy, false, bg);
      break;
    case Icon::TRASH_OPEN:
      drawBin(cx, cy, true, bg);
      break;
    case Icon::PENCIL:  // "back to drawing"
      drawPencil(cx, cy, 1, bg);
      break;
    case Icon::INFO:  // (i)
      tft.fillCircle(cx, cy, 13, TFT_BLUE);
      tft.fillCircle(cx, cy - 6, 2, TFT_WHITE);
      tft.fillRect(cx - 2, cy - 2, 5, 11, TFT_WHITE);
      break;
  }
}

void drawButtonAt(int y, int h, Icon icon, uint16_t bg) {
  tft.fillRect(TOOLBAR_X, y, TOOLBAR_W, h, bg);
  tft.drawRect(TOOLBAR_X, y, TOOLBAR_W, h, TFT_BLACK);
  drawIcon(icon, TOOLBAR_X + TOOLBAR_W / 2, y + h / 2, bg);
}

void drawButton(int i, int count, Icon icon, uint16_t bg) {
  int h = 240 / count;
  drawButtonAt(i * h, h, icon, bg);
}

void flashButton(int i, int count, Icon icon, uint16_t bg, Icon restore) {
  drawButton(i, count, icon, bg);
  delay(700);
  drawButton(i, count, restore);
}
