// Drawing the pet and its things. Everything takes a target (the screen or a sprite)
// and target coordinates, so the same code draws into the room and the start screen.
#pragma once
#include <TFT_eSPI.h>

enum class Face { NEUTRAL, HAPPY, SAD, CHEW_OPEN, CHEW_SHUT, WOW, YUCK, SLEEP };

struct PetLook {
  int size = 26;
  uint16_t colour = TFT_GREEN;
  float squash = 0;  // >0 wider and flatter, <0 taller and thinner
  float lookX = 0, lookY = 0;  // -1..1, where the pupils point
  Face face = Face::NEUTRAL;
  bool blink = false;
  bool egg = false;
  float wobble = 0;  // egg rocking, -1..1
  int cracks = 0;    // egg cracks, 0-3
};

// cx = centre, by = bottom (where the feet touch the floor)
void drawPet(TFT_eSPI &g, int cx, int by, const PetLook &look);
// Half-width and height that drawPet can cover
void petExtent(const PetLook &look, int &halfW, int &h);

void drawBall(TFT_eSPI &g, int cx, int cy, int r, uint16_t colour);
void drawHeart(TFT_eSPI &g, int cx, int cy, int s, uint16_t colour);
void drawZ(TFT_eSPI &g, int x, int y, uint16_t bg);
