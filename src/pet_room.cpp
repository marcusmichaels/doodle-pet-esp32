#include "pet_room.h"

#include <LittleFS.h>

#include "strokes.h"
#include "ui.h"

constexpr int ROW_BYTES = ROOM_SIZE / 2;
constexpr size_t ROOM_BYTES = ROW_BYTES * ROOM_SIZE;
static const char *const ROOM_PATH = "/pet/room.bin";

static uint8_t *room;
// The hung picture and its frame, one byte per pixel, so rubbing out ink drawn over it
// brings the picture back.
static uint8_t *art;
static uint16_t palSwapped[16];  // byte-swapped RGB565, ready to write into a sprite

static uint16_t blend565(uint16_t a, uint16_t b, float t) {
  int r = ((a >> 11) & 31) * (1 - t) + ((b >> 11) & 31) * t;
  int g = ((a >> 5) & 63) * (1 - t) + ((b >> 5) & 63) * t;
  int bl = (a & 31) * (1 - t) + (b & 31) * t;
  return (r << 11) | (g << 5) | bl;
}

static void roomSet(int x, int y, uint8_t v) {
  uint8_t &b = room[y * ROW_BYTES + (x >> 1)];
  b = (x & 1) ? (b & 0xF0) | v : (b & 0x0F) | (v << 4);
}

uint8_t roomGet(int x, int y) {
  uint8_t b = room[y * ROW_BYTES + (x >> 1)];
  return (x & 1) ? b & 15 : b >> 4;
}

bool roomSolid(int x, int y) {
  if (x < 0 || x >= ROOM_SIZE || y >= ROOM_SIZE) return true;
  if (y < 0) return false;
  return roomGet(x, y) < ROOM_BG;
}

bool roomBegin() {
  uint16_t pal[16];
  for (int i = 0; i < ROOM_BG; i++) {
    pal[i] = COLOURS[i];
    pal[ART_BASE + i] = blend565(COLOURS[i], TFT_WHITE, 0.45);
  }
  pal[ROOM_BG] = TFT_WHITE;
  pal[ART_FRAME] = 0xA285;  // wood brown
  for (int i = 0; i < 16; i++) palSwapped[i] = (pal[i] << 8) | (pal[i] >> 8);

  room = (uint8_t *)malloc(ROOM_BYTES);
  if (!room) return false;
  File f = LittleFS.open(ROOM_PATH, "r");
  if (!f || f.read(room, ROOM_BYTES) != ROOM_BYTES) {
    memset(room, ROOM_BG << 4 | ROOM_BG, ROOM_BYTES);
  }
  if (f) f.close();
  return true;
}

void roomSave() {
  File f = LittleFS.open(ROOM_PATH, "w");
  if (!f) return;
  f.write(room, ROOM_BYTES);
  f.close();
}

// What shows at (x, y) once the ink there is rubbed out: the picture, or the wall.
static uint8_t underInk(int x, int y) {
  int ax = x - ART_X, ay = y - ART_Y;
  if (art && ax >= 0 && ay >= 0 && ax < ART_OUTER && ay < ART_OUTER) return art[ay * ART_OUTER + ax];
  return ROOM_BG;
}

void roomInk(int x0, int y0, int x1, int y1, uint8_t colour) {
  forEachBrushPixel(x0, y0, x1, y1, BRUSH_R, [&](int x, int y) {
    if (x < 0 || y < 0 || x >= ROOM_SIZE || y >= ROOM_SIZE) return;
    if (colour == WHITE) {
      if (roomGet(x, y) < ROOM_BG) roomSet(x, y, underInk(x, y));
    } else {
      roomSet(x, y, colour);
    }
  });
}

void roomClearInk() {
  for (int y = 0; y < ROOM_SIZE; y++) {
    for (int x = 0; x < ROOM_SIZE; x++) {
      if (roomGet(x, y) < ROOM_BG) roomSet(x, y, underInk(x, y));
    }
  }
}

static bool inArtInner(int x, int y) {
  return x >= ART_X + ART_BORDER && x < ART_X + ART_BORDER + ART_INNER &&
         y >= ART_Y + ART_BORDER && y < ART_Y + ART_BORDER + ART_INNER;
}

void roomHangArt(int id) {
  // Take down the old picture
  for (int y = 0; y < ROOM_SIZE; y++) {
    for (int x = 0; x < ROOM_SIZE; x++) {
      if (roomGet(x, y) >= ART_BASE) roomSet(x, y, ROOM_BG);
    }
  }
  if (!art) art = (uint8_t *)malloc(ART_OUTER * ART_OUTER);
  if (!art) return;
  memset(art, ROOM_BG, ART_OUTER * ART_OUTER);
  if (id <= 0) return;

  // Frame and picture into the art copy...
  for (int y = ART_Y; y < ART_Y + ART_OUTER; y++) {
    for (int x = ART_X; x < ART_X + ART_OUTER; x++) {
      if (!inArtInner(x, y)) art[(y - ART_Y) * ART_OUTER + x - ART_X] = ART_FRAME;
    }
  }
  constexpr float S = (float)ART_INNER / CANVAS_SIZE;
  bool hasPrev = false;
  int px = 0, py = 0;
  forEachSavedPoint(id, [&](Pt p) {
    if (p.c == PEN_UP || p.c >= NUM_COLOURS) {
      hasPrev = false;
      return;
    }
    int x = ART_X + ART_BORDER + p.x * S, y = ART_Y + ART_BORDER + p.y * S;
    uint8_t v = p.c == WHITE ? ROOM_BG : ART_BASE + p.c;
    forEachBrushPixel(hasPrev ? px : x, hasPrev ? py : y, x, y, 1, [&](int bx, int by) {
      if (inArtInner(bx, by)) art[(by - ART_Y) * ART_OUTER + bx - ART_X] = v;
    });
    px = x;
    py = y;
    hasPrev = true;
  });

  // ...then onto the wall. Ink already there stays in front of it.
  for (int y = ART_Y; y < ART_Y + ART_OUTER; y++) {
    for (int x = ART_X; x < ART_X + ART_OUTER; x++) {
      if (roomGet(x, y) >= ROOM_BG) roomSet(x, y, underInk(x, y));
    }
  }
}

float roomArtShare(uint8_t colour) {
  int total = 0, match = 0;
  for (int y = ART_Y + ART_BORDER; y < ART_Y + ART_BORDER + ART_INNER; y++) {
    for (int x = ART_X + ART_BORDER; x < ART_X + ART_BORDER + ART_INNER; x++) {
      uint8_t v = roomGet(x, y);
      if (v <= ART_BASE || v >= ART_FRAME) continue;  // skip background, black and frame
      total++;
      if (v == ART_BASE + colour) match++;
    }
  }
  return total ? (float)match / total : 0;
}

void roomComposite(uint16_t *dst, int stride, int x0, int y0, int w, int h) {
  for (int y = 0; y < h; y++) {
    const uint8_t *row = room + (y0 + y) * ROW_BYTES;
    uint16_t *out = dst + y * stride;
    for (int x = 0; x < w; x++) {
      int rx = x0 + x;
      uint8_t b = row[rx >> 1];
      out[x] = palSwapped[(rx & 1) ? (b & 15) : (b >> 4)];
    }
  }
}
