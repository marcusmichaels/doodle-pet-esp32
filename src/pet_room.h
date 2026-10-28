// The pet's room: a 240x240 image at 4 bits per pixel. It's both what's on screen
// behind the pet and what the pet collides with. Pixel values:
//   0-6   solid ink in COLOURS 0-6: floors, ramps and walls
//   7     background (white, same as the drawing canvas)
//   8-14  hanging art: pale, non-solid versions of COLOURS 0-6
//   15    picture frame (non-solid)
#pragma once
#include <Arduino.h>

constexpr int ROOM_SIZE = 240;
constexpr uint8_t ROOM_BG = 7;
constexpr uint8_t ART_BASE = 8;
constexpr uint8_t ART_FRAME = 15;

// Where the hung picture goes: an 80x80 frame near the top-left of the room.
constexpr int ART_X = 8, ART_Y = 8, ART_OUTER = 80, ART_BORDER = 4;
constexpr int ART_INNER = ART_OUTER - 2 * ART_BORDER;

bool roomBegin();  // allocates the buffer and loads the saved room
void roomSave();

uint8_t roomGet(int x, int y);
bool roomSolid(int x, int y);  // walls at the sides and the ground below count as solid

// A brush stroke segment of solid ink (colour 0-6), or with WHITE, an eraser for ink only.
void roomInk(int x0, int y0, int x1, int y1, uint8_t colour);
void roomClearInk();

// Replaces the hung picture with saved drawing `id`, scaled into the frame (0 takes it down).
// Also call at start-up to rebuild the copy that rubbing out restores.
void roomHangArt(int id);
// How much of the picture's colour is `colour`, 0-1 (ignoring black outlines).
float roomArtShare(uint8_t colour);

// Writes the RGB565 pixels of a room rectangle in sprite byte order (byte-swapped).
void roomComposite(uint16_t *dst, int stride, int x, int y, int w, int h);
