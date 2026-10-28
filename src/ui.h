// Screen layout, the colour palette and the icon buttons shared by both apps.
#pragma once
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

// Layout (landscape 320x240): palette | 240x240 canvas | toolbar
constexpr int PALETTE_W = 40;
constexpr int CANVAS_X = 40;
constexpr int CANVAS_SIZE = 240;
constexpr int TOOLBAR_X = 280;
constexpr int TOOLBAR_W = 40;

constexpr uint16_t COLOURS[] = {TFT_BLACK, TFT_RED,  TFT_ORANGE,  TFT_YELLOW,
                                TFT_GREEN, TFT_BLUE, TFT_MAGENTA, TFT_WHITE};
constexpr int NUM_COLOURS = sizeof(COLOURS) / sizeof(COLOURS[0]);
constexpr int SWATCH_H = 240 / NUM_COLOURS;
// Colour indices with special meanings
constexpr uint8_t BLACK = 0, RED = 1, ORANGE = 2, YELLOW = 3, GREEN = 4, BLUE = 5, MAGENTA = 6,
                  WHITE = 7;

void drawPalette(int selected);

// Toolbar icons are drawn from shapes (no bitmaps), each about 28px across.
enum class Icon {
  NEW, SAVE, VIEW, TICK, CROSS, PREV, NEXT, PLAY, TRASH, TRASH_OPEN, PENCIL, INFO
};

void drawIcon(Icon icon, int cx, int cy, uint16_t bg);
void drawPencil(int cx, int cy, float scale, uint16_t bg);

// Button `i` of `count` stacked down the toolbar.
void drawButton(int i, int count, Icon icon, uint16_t bg = TFT_DARKGREY);
// A toolbar button at any height.
void drawButtonAt(int y, int h, Icon icon, uint16_t bg = TFT_DARKGREY);
// Shows `icon` on `bg` briefly, then puts `restore` back.
void flashButton(int i, int count, Icon icon, uint16_t bg, Icon restore);
