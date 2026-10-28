#include "pet_app.h"

#include <math.h>

#include "led.h"
#include "pet_draw.h"
#include "pet_room.h"
#include "pet_stats.h"
#include "strokes.h"
#include "touch.h"
#include "ui.h"

constexpr uint32_t FRAME_MS = 33;
constexpr float GRAVITY = 0.35f, MAX_FALL = 6;
constexpr float WALK_SPEED = 1.1f;
constexpr int STEP_UP = 8;        // walks straight over lines lying on the floor
constexpr int MAX_JUMP_UP = 60;   // the tallest step it'll jump up onto
constexpr int TILE = 80;          // the room is redrawn in pieces up to this size
constexpr int METERS_H = 120, INFO_BUTTON_Y = 120, ART_BUTTON_Y = 160, CLEAR_BUTTON_Y = 200,
              BUTTON_H = 40;

// In the pet's room each palette colour is a tool: what you draw with it becomes that thing.
constexpr uint8_t TOOL_FLOOR = BLACK, TOOL_ERASER = WHITE, TOOL_FRUIT = RED, TOOL_VEG = GREEN,
                  TOOL_WATER = BLUE, TOOL_SWEETS = MAGENTA, TOOL_TOY = ORANGE, TOOL_DOOR = YELLOW;

static bool isFoodTool(uint8_t c) {
  return c == TOOL_FRUIT || c == TOOL_VEG || c == TOOL_WATER || c == TOOL_SWEETS;
}

// ---------------------------------------------------------------------------
// Things in the room
// ---------------------------------------------------------------------------
struct Rect {
  int x, y, w, h;
  bool operator==(const Rect &o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
  bool overlaps(const Rect &o) const {
    return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h;
  }
  bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

static Rect unite(const Rect &a, const Rect &b) {
  if (!a.w) return b;
  if (!b.w) return a;
  int x0 = min(a.x, b.x), y0 = min(a.y, b.y);
  return {x0, y0, max(a.x + a.w, b.x + b.w) - x0, max(a.y + a.h, b.y + b.h) - y0};
}

enum class Act { EGG, IDLE, WANDER, SEEK_FOOD, EAT, PLAY, ADMIRE, FRIEND, SLEEP, RUNAWAY, LEAVE };

// Your pet, and a friend visiting through the door, share movement and drawing code.
struct Pet {
  float x = ROOM_SIZE / 2, y = 30;  // bottom centre, room coordinates
  float vx = 0, vy = 0;
  float walk = 0;  // speed it wants to walk at; also steers it in the air
  int size = 26;
  uint16_t colour = TFT_GREEN;
  bool onGround = false, blocked = false, sliding = false;
  int facing = 1;
  Act act = Act::EGG;
  uint32_t actUntil = 0;
  float targetX = 0;
  int target = -1;            // item or ball slot
  bool targetIsItem = false;  // PLAY: chasing a toy rather than a ball
  float squash = 0, lookX = 0, lookY = 0, glance = 0;
  Face tempFace = Face::NEUTRAL;
  uint32_t tempFaceUntil = 0;
  uint32_t nextBlink = 0, blinkUntil = 0, nextGlance = 0;
  uint32_t jumpReadyAt = 0, nextBite = 0, nextZ = 0, nextTear = 0, nextKick = 0;
  uint32_t awakeUntil = 0;  // woken up by a poke: stays up a while, even at night
  int eggTaps = 0;
  float wobble = 0;
  PetLook look;
  Rect drawn{};
};
static Pet pet;
static Pet visitor;
static bool visiting = false;
static uint32_t visitEndsAt = 0, nextHugAt = 0;

// Food and toys are small bitmaps of the drawing (colour indices, 0xFF transparent).
// Both fall; toys also get pushed around.
constexpr int MAX_ITEMS = 8, ITEM_MAX_SIZE = 80;
struct Item {
  bool used = false, toy = false;
  float x = 0, y = 0, vx = 0, vy = 0;
  int w = 0, h = 0;
  uint8_t *px = nullptr;
  int left = 0, total = 0;
  uint8_t kind = TOOL_FRUIT;  // the tool most of it was drawn with
  float value = 1;            // bigger food is more filling
  bool resting = false, changed = false;
  uint32_t bornAt = 0, ignoreUntil = 0;
  Rect drawn{};
};
static Item items[MAX_ITEMS];

constexpr int MAX_BALLS = 4;
struct Ball {
  bool used = false;
  float x = 0, y = 0, vx = 0, vy = 0;
  int r = 8;
  uint8_t colour = TOOL_TOY;
  bool held = false;
  uint32_t bornAt = 0;
  Rect drawn{};
};
static Ball balls[MAX_BALLS];
static int heldBall = -1;
static int fingerX, fingerY;  // where the held ball should be, room coordinates

// The door is the drawing itself; it shows a dark doorway while open.
constexpr int DOOR_MAX_W = 100, DOOR_MAX_H = 140;
struct Door {
  bool used = false;
  int x = 0, y = 0, w = 0, h = 0;
  uint8_t *px = nullptr;
  int total = 0, left = 0;
  uint32_t lastStrokeAt = 0, knockAt = 0, visitAt = 0, openUntil = 0, nextVisitAllowed = 0;
  bool wasOpen = false, changed = false;
  Rect drawn{};
};
static Door door;

enum class Fx { HEART, Z, CRUMB, SPARK, TEAR };
constexpr int MAX_FX = 24;
struct Particle {
  bool used = false;
  float x = 0, y = 0, vx = 0, vy = 0;
  Fx kind = Fx::HEART;
  uint16_t colour = TFT_RED;
  uint32_t until = 0;
  Rect drawn{};
};
static Particle fx[MAX_FX];

// What each food does (per average-sized piece): fullness, fun, energy
struct Nutrition {
  float fullness, fun, energy;
};
static const Nutrition NUTRITION[NUM_COLOURS] = {
    {0, 0, 0},    // floor
    {15, 10, 0},  // fruit
    {0, 0, 0},    // toy
    {0, 0, 0},    // door
    {30, -3, 0},  // veg: good for it, not very exciting
    {8, 0, 8},    // water
    {5, 20, 0},   // sweets
    {0, 0, 0},    // eraser
};
constexpr float FAVOURITE_FUN = 12;

static float artLove = 0;  // how much of the hung picture is the pet's favourite colour

// The stroke under the finger, in room coordinates
constexpr int MAX_STROKE = 1500;
static Pt *stroke;
static int strokeLen = 0;
static int drawColour = TOOL_FLOOR;

static float rnd(float lo, float hi) { return lo + (hi - lo) * random(1001) / 1000.0f; }

// ---------------------------------------------------------------------------
// Drawing the room: changed areas are collected, then redrawn in tiles
// ---------------------------------------------------------------------------
static TFT_eSprite spr(&tft);
constexpr int MAX_DIRTY = 48;
static Rect dirty[MAX_DIRTY];
static int numDirty = 0;

static void markDirty(Rect r) {
  int x0 = max(0, r.x), y0 = max(0, r.y);
  int x1 = min(ROOM_SIZE, r.x + r.w), y1 = min(ROOM_SIZE, r.y + r.h);
  if (x1 <= x0 || y1 <= y0) return;
  Rect c{x0, y0, x1 - x0, y1 - y0};
  if (numDirty == MAX_DIRTY) {
    dirty[MAX_DIRTY - 1] = unite(dirty[MAX_DIRTY - 1], c);
  } else {
    dirty[numDirty++] = c;
  }
}

// Marks an object's old and new areas when it moved (or always, with force).
static void markMoved(Rect &drawn, Rect now, bool force) {
  if (!force && drawn == now) return;
  Rect both = unite(drawn, now);
  if (drawn.overlaps(now) && both.w <= TILE && both.h <= TILE) {
    markDirty(both);
  } else {
    markDirty(drawn);
    markDirty(now);
  }
  drawn = now;
}

static Rect petRect(const Pet &p) {
  int hw, h;
  petExtent(p.look, hw, h);
  return {(int)p.x - hw, (int)p.y - h, 2 * hw + 1, h + 3};
}
static Rect itemRect(const Item &it) { return {(int)it.x, (int)it.y, it.w + 1, it.h + 1}; }
static Rect ballRect(const Ball &b) {
  return {(int)b.x - b.r - 2, (int)b.y - b.r - 2, 2 * b.r + 5, 2 * b.r + 5};
}
static Rect doorRect() { return {door.x, door.y, door.w, door.h}; }
static Rect fxRect(const Particle &p) { return {(int)p.x - 7, (int)p.y - 7, 15, 15}; }

static void drawParticle(const Particle &p, int ox, int oy) {
  int x = p.x - ox, y = p.y - oy;
  switch (p.kind) {
    case Fx::HEART:
      drawHeart(spr, x, y, 4, p.colour);
      break;
    case Fx::Z:
      drawZ(spr, x - 3, y - 3, TFT_WHITE);
      break;
    case Fx::CRUMB:
      spr.fillCircle(x, y, 1, p.colour);
      break;
    case Fx::SPARK:
      spr.fillCircle(x, y, 2, p.colour);
      break;
    case Fx::TEAR:
      spr.fillEllipse(x, y, 2, 3, TFT_SKYBLUE);
      break;
  }
}

static void blitBitmap(uint16_t *buf, const Rect &tile, int bx, int by, int bw, int bh,
                       const uint8_t *px) {
  for (int j = max(0, tile.y - by); j < bh && by + j < tile.y + tile.h; j++) {
    for (int i = max(0, tile.x - bx); i < bw && bx + i < tile.x + tile.w; i++) {
      uint8_t c = px[j * bw + i];
      if (c == 0xFF) continue;
      uint16_t col = COLOURS[c];
      buf[(by + j - tile.y) * TILE + (bx + i - tile.x)] = (col << 8) | (col >> 8);
    }
  }
}

static Rect segmentRect(const Pt &a, const Pt &b) {
  return {min(a.x, b.x) - BRUSH_R, min(a.y, b.y) - BRUSH_R, abs(a.x - b.x) + 2 * BRUSH_R + 1,
          abs(a.y - b.y) + 2 * BRUSH_R + 1};
}

// The stroke being drawn goes in with everything else, behind the pets, so drawing (or
// rubbing out) over a pet doesn't paint over it.
static void drawLiveStroke(const Rect &tile) {
  for (int k = 0; k < strokeLen; k++) {
    const Pt &a = stroke[k > 0 ? k - 1 : 0], &b = stroke[k];
    if (!segmentRect(a, b).overlaps(tile)) continue;
    int steps = max(1, max(abs(b.x - a.x), abs(b.y - a.y)) / 2);
    for (int i = 0; i <= steps; i++) {
      spr.fillCircle(a.x + (b.x - a.x) * i / steps - tile.x, a.y + (b.y - a.y) * i / steps - tile.y,
                     BRUSH_R, COLOURS[b.c]);
    }
  }
}

static void renderTile(int x, int y, int w, int h) {
  uint16_t *buf = (uint16_t *)spr.getPointer();
  roomComposite(buf, TILE, x, y, w, h);
  Rect tile{x, y, w, h};
  if (door.used && doorRect().overlaps(tile)) {
    blitBitmap(buf, tile, door.x, door.y, door.w, door.h, door.px);
    if (door.wasOpen) {  // dark doorway in the middle of the drawing
      spr.fillRect(door.x + door.w / 4 - x, door.y + door.h / 5 - y, max(4, door.w / 2),
                   door.h - door.h / 5, 0x2104);
    }
  }
  for (const Item &it : items) {
    if (it.used && itemRect(it).overlaps(tile)) {
      blitBitmap(buf, tile, it.x, it.y, it.w, it.h, it.px);
    }
  }
  for (const Ball &b : balls) {
    if (b.used && ballRect(b).overlaps(tile)) {
      drawBall(spr, b.x - x, b.y - y, b.r, COLOURS[b.colour]);
    }
  }
  drawLiveStroke(tile);
  if (visiting && petRect(visitor).overlaps(tile)) {
    drawPet(spr, visitor.x - x, visitor.y - y, visitor.look);
  }
  if (petRect(pet).overlaps(tile)) drawPet(spr, pet.x - x, pet.y - y, pet.look);
  for (const Particle &p : fx) {
    if (p.used && fxRect(p).overlaps(tile)) drawParticle(p, x, y);
  }
  spr.pushSprite(CANVAS_X + x, y, 0, 0, w, h);
}

static void renderRect(Rect r) {
  int x0 = max(0, r.x), y0 = max(0, r.y);
  int x1 = min(ROOM_SIZE, r.x + r.w), y1 = min(ROOM_SIZE, r.y + r.h);
  for (int y = y0; y < y1; y += TILE) {
    for (int x = x0; x < x1; x += TILE) renderTile(x, y, min(TILE, x1 - x), min(TILE, y1 - y));
  }
}

static void flushDirty() {
  for (int i = 0; i < numDirty; i++) renderRect(dirty[i]);
  numDirty = 0;
}

// ---------------------------------------------------------------------------
// Particles
// ---------------------------------------------------------------------------
static void spawnFx(Fx kind, float x, float y, float vx, float vy, uint16_t colour,
                    uint32_t lifeMs) {
  for (Particle &p : fx) {
    if (p.used) continue;
    p = Particle();
    p.used = true;
    p.kind = kind;
    p.x = x;
    p.y = y;
    p.vx = vx;
    p.vy = vy;
    p.colour = colour;
    p.until = millis() + lifeMs;
    return;
  }
}

static int bodyW(const Pet &p) { return p.size - 4; }  // a little inside the drawn outline
static int bodyH(const Pet &p) { return p.size * 0.85f; }

static void spawnHearts(const Pet &p, int n) {
  for (int i = 0; i < n; i++) {
    spawnFx(Fx::HEART, p.x + rnd(-10, 10), p.y - bodyH(p) - 4, rnd(-0.4, 0.4), rnd(-1.2, -0.6),
            TFT_RED, 900 + random(400));
  }
}

static void spawnSparks(float x, float y, int n) {
  for (int i = 0; i < n; i++) {
    spawnFx(Fx::SPARK, x, y, rnd(-2.5, 2.5), rnd(-4, -1.5), COLOURS[random(RED, WHITE)], 900);
  }
}

static void updateFx(uint32_t now) {
  for (Particle &p : fx) {
    if (!p.used) continue;
    if (now >= p.until) {
      markDirty(p.drawn);
      p.used = false;
      continue;
    }
    p.x += p.vx;
    p.y += p.vy;
    if (p.kind == Fx::CRUMB || p.kind == Fx::SPARK || p.kind == Fx::TEAR) p.vy += 0.2f;
  }
}

// ---------------------------------------------------------------------------
// Movement and getting around
// ---------------------------------------------------------------------------
static bool boxHits(float cx, float by, int w, int h) {
  int x0 = (int)floorf(cx) - w / 2, y1 = (int)floorf(by) - 1;
  for (int y = y1 - h + 1; y <= y1; y++) {
    for (int x = x0; x < x0 + w; x++) {
      if (roomSolid(x, y)) return true;
    }
  }
  return false;
}

// Free pixels below the feet at column x, up to 8.
static int groundGap(const Pet &p, float x) {
  int k = 0;
  while (k < 8 && !roomSolid((int)x, (int)p.y + k)) k++;
  return k;
}

// How far up the pet has to get to clear what's in front of it, or -1 if it's too tall.
static int obstacleHeight(const Pet &p, int dir) {
  int w = bodyW(p), h = bodyH(p);
  for (int up = STEP_UP; up <= MAX_JUMP_UP; up += 2) {
    if (!boxHits(p.x + dir * 2, p.y - up, w, h)) return up;
  }
  return -1;
}

// Jumps just high enough to rise `height` pixels (and a bit).
static void jump(Pet &p, uint32_t now, int height) {
  if (!p.onGround || now < p.jumpReadyAt) return;
  p.vy = -sqrtf(2 * GRAVITY * (min(height, MAX_JUMP_UP) + 6));
  p.onGround = false;
  p.squash = -0.25f;
  p.jumpReadyAt = now + 700;
}

static void movePet(Pet &p, bool ghost) {
  if (ghost) {  // running away: walks off through anything in the way
    p.x += p.walk;
    return;
  }
  int w = bodyW(p), h = bodyH(p);

  // Ink drawn over the pet pushes it up out of the way
  if (boxHits(p.x, p.y, w, h)) {
    for (int k = 1; k <= 40; k++) {
      if (!boxHits(p.x, p.y - k, w, h)) {
        p.y -= k;
        break;
      }
    }
  }

  p.sliding = false;
  if (p.onGround) {  // walking, or sliding down slopes
    int dl = groundGap(p, p.x - w / 2 + 1), dr = groundGap(p, p.x + w / 2 - 2);
    if (p.walk == 0 && dl < 8 && dr < 8 && abs(dr - dl) >= 3) {
      p.vx = constrain(p.vx + 0.15f * (dr > dl ? 1 : -1), -3.0f, 3.0f);
      p.sliding = true;
    } else if (p.walk != 0) {
      p.vx = p.walk;
    } else {
      p.vx *= 0.6f;
    }
  } else if (p.walk != 0) {
    p.vx = p.walk;  // steer in the air, so it lands on whatever it jumped up to
  }

  p.blocked = false;
  int steps = (int)ceilf(fabsf(p.vx));
  for (int i = 0; i < steps; i++) {
    float nx = p.x + p.vx / steps;
    if (!boxHits(nx, p.y, w, h)) {
      p.x = nx;
      continue;
    }
    int k = 1;
    while (p.onGround && k <= STEP_UP && boxHits(nx, p.y - k, w, h)) k++;
    if (p.onGround && k <= STEP_UP) {
      p.x = nx;
      p.y -= k;
      continue;
    }
    p.vx = 0;
    p.blocked = true;
    break;
  }

  bool wasOnGround = p.onGround;
  p.vy = min(p.vy + GRAVITY, MAX_FALL);
  float fallSpeed = p.vy;
  steps = max(1, (int)ceilf(fabsf(p.vy)));
  for (int i = 0; i < steps; i++) {
    float ny = p.y + p.vy / steps;
    if (!boxHits(p.x, ny, w, h)) {
      p.y = ny;
      continue;
    }
    if (p.vy > 0 && fallSpeed > 2.5f) p.squash = min(0.35f, fallSpeed * 0.06f);  // landed
    p.vy = 0;
    break;
  }
  p.onGround = boxHits(p.x, p.y + 1, w, h);
  // Follow the ground down slopes and steps instead of hopping off them
  if (!p.onGround && wasOnGround && p.vy >= 0) {
    for (int k = 1; k <= STEP_UP; k++) {
      if (boxHits(p.x, p.y + k + 1, w, h)) {
        p.y += k;
        p.onGround = true;
        break;
      }
    }
  }
  if (p.onGround && p.vy > 0) p.vy = 0;
}

enum class Walk { ARRIVED, WALKING, STUCK };

// Walks towards tx, stepping over lines and jumping up steps on the way.
static Walk walkTo(Pet &p, float tx, uint32_t now) {
  float dx = tx - p.x;
  if (fabsf(dx) < 3) {
    p.walk = 0;
    return Walk::ARRIVED;
  }
  p.facing = dx > 0 ? 1 : -1;
  p.walk = p.facing * WALK_SPEED * (p.size < 26 ? 0.85f : 1);
  if (p.blocked && p.onGround) {
    int up = obstacleHeight(p, p.facing);
    if (up < 0) {  // a wall too tall to get over
      p.walk = 0;
      return Walk::STUCK;
    }
    jump(p, now, up);
  }
  return Walk::WALKING;
}

// Walks to something at (tx, ty), jumping up to it if it's on a ledge overhead.
static Walk goTo(Pet &p, float tx, float ty, uint32_t now) {
  float rise = p.y - ty;
  if (rise > STEP_UP && fabsf(tx - p.x) < 30) {
    if (rise > MAX_JUMP_UP) return Walk::STUCK;
    p.walk = 0;
    p.facing = tx > p.x ? 1 : -1;
    p.lookY = -1;
    if (p.onGround && now >= p.jumpReadyAt) {
      jump(p, now, rise);
      p.vx = p.facing * 1.2f;
    }
    return Walk::WALKING;
  }
  return walkTo(p, tx, now);
}

// ---------------------------------------------------------------------------
// Food, toys and balls
// ---------------------------------------------------------------------------
static void removeItem(int i) {
  markDirty(items[i].drawn);
  free(items[i].px);
  items[i] = Item();
}

static bool itemSupported(const Item &it) {
  int y = (int)it.y + it.h;
  for (int i = 0; i < it.w; i++) {
    if (roomSolid((int)it.x + i, y)) return true;
  }
  return false;
}

static bool itemSideHits(const Item &it, float nx) {
  int col = nx > it.x ? (int)nx + it.w : (int)nx - 1;
  for (int j = 0; j < it.h; j++) {
    if (roomSolid(col, (int)it.y + j)) return true;
  }
  return false;
}

static void updateItem(Item &it) {
  if (it.toy && it.vx != 0) {  // pushed: slides along, bouncing off walls
    float nx = it.x + it.vx;
    if (itemSideHits(it, nx)) {
      it.vx *= -0.4f;
    } else {
      it.x = nx;
    }
    if (it.resting) it.vx *= 0.9f;
    if (fabsf(it.vx) < 0.1f) it.vx = 0;
  }
  if (it.resting) {
    if (itemSupported(it) && it.vy >= 0) return;
    it.resting = false;  // the floor under it was rubbed out, or it was kicked up
  }
  it.vy = min(it.vy + GRAVITY, MAX_FALL);
  if (it.vy < 0) {
    it.y += it.vy;
    return;
  }
  for (float move = it.vy; move > 0; move -= 1) {
    if (itemSupported(it)) {
      it.resting = true;
      it.vy = 0;
      it.y = floorf(it.y);
      return;
    }
    it.y += min(1.0f, move);
  }
}

static float RING_COS[16], RING_SIN[16];

// Whether the ball's edge touches something solid; n points away from it.
static bool ballHit(const Ball &b, float &nx, float &ny) {
  float sx = 0, sy = 0;
  int hits = 0;
  for (int i = 0; i < 16; i++) {
    if (roomSolid(lroundf(b.x + RING_COS[i] * b.r), lroundf(b.y + RING_SIN[i] * b.r))) {
      sx -= RING_COS[i];
      sy -= RING_SIN[i];
      hits++;
    }
  }
  if (!hits) return false;
  float len = sqrtf(sx * sx + sy * sy);
  nx = len > 0.01f ? sx / len : 0;
  ny = len > 0.01f ? sy / len : -1;
  return true;
}

static void updateBall(Ball &b) {
  bool resting = fabsf(b.vy) < 0.5f && roomSolid(lroundf(b.x), lroundf(b.y + b.r + 1));
  if (resting) {
    b.vy = 0;
    b.vx *= 0.985f;
  } else {
    b.vy = min(b.vy + GRAVITY * 0.8f, 8.0f);
  }
  int steps = max(1, (int)ceilf(max(fabsf(b.vx), fabsf(b.vy))));
  for (int i = 0; i < steps; i++) {
    b.x += b.vx / steps;
    b.y += b.vy / steps;
    float nx, ny;
    if (!ballHit(b, nx, ny)) continue;
    for (int k = 0; k < b.r && ballHit(b, nx, ny); k++) {
      b.x += nx * 0.5f;
      b.y += ny * 0.5f;
    }
    float vn = b.vx * nx + b.vy * ny;
    if (vn < 0) {
      float bounce = vn < -0.8f ? 1.6f : 1.0f;  // gentle touches just stop, no jitter
      b.vx -= bounce * vn * nx;
      b.vy -= bounce * vn * ny;
      b.vx *= 0.97f;
      b.vy *= 0.97f;
    }
  }
  if (fabsf(b.vx) < 0.05f) b.vx = 0;
}

static void showFace(Pet &p, Face f, uint32_t now, uint32_t ms) {
  p.tempFace = f;
  p.tempFaceUntil = now + ms;
}

static bool isActive(const Pet &p) {
  return p.act != Act::EGG && p.act != Act::SLEEP && p.act != Act::RUNAWAY;
}

// Kicks any ball or toy the pet is touching. Your pet enjoys it.
static void kickThings(Pet &p, bool isMain, uint32_t now) {
  if (!isActive(p) || now < p.nextKick) return;
  float hw = bodyW(p) / 2.0f, top = p.y - bodyH(p);
  bool kicked = false;
  for (Ball &b : balls) {
    if (!b.used || b.held) continue;
    float cx = constrain(b.x, p.x - hw, p.x + hw), cy = constrain(b.y, top, p.y);
    float dx = b.x - cx, dy = b.y - cy;
    if (dx * dx + dy * dy > b.r * b.r) continue;
    b.vx = (b.x >= p.x ? 1 : -1) * rnd(2.5, 4) + p.vx * 0.5f;
    b.vy = rnd(-4.5, -2.5);
    kicked = true;
  }
  Rect body{(int)(p.x - hw), (int)top, (int)(2 * hw), bodyH(p)};
  for (Item &it : items) {
    if (!it.used || !it.toy || !itemRect(it).overlaps(body)) continue;
    it.vx = (it.x + it.w / 2.0f >= p.x ? 1 : -1) * rnd(2, 3);
    it.vy = -2;
    it.resting = false;
    kicked = true;
  }
  if (!kicked) return;
  p.nextKick = now + 400;
  showFace(p, Face::HAPPY, now, 600);
  if (isMain) stats.fun = min(100.0f, stats.fun + 4);
  if (random(3) == 0) spawnHearts(p, 1);
}

// ---------------------------------------------------------------------------
// Your pet's behaviour
// ---------------------------------------------------------------------------
static void setAct(Pet &p, Act a, uint32_t now, uint32_t ms) {
  p.act = a;
  p.actUntil = now + ms;
  p.target = -1;
}

static float itemCx(const Item &it) { return it.x + it.w / 2.0f; }

static int pickFood(uint32_t now) {
  int best = -1;
  float bestScore = 1e9;
  for (int i = 0; i < MAX_ITEMS; i++) {
    const Item &it = items[i];
    if (!it.used || it.toy || !it.resting || now < it.ignoreUntil) continue;
    float score = fabsf(itemCx(it) - pet.x) - (it.kind == stats.favourite ? 300 : 0);
    if (score < bestScore) bestScore = score, best = i;
  }
  return best;
}

// Nearest ball or toy. Returns the slot and sets isItem for toys.
static int nearestPlaything(const Pet &p, bool &isItem) {
  int best = -1;
  float bestD = 1e9;
  for (int i = 0; i < MAX_BALLS; i++) {
    float d = fabsf(balls[i].x - p.x);
    if (balls[i].used && !balls[i].held && d < bestD) bestD = d, best = i, isItem = false;
  }
  for (int i = 0; i < MAX_ITEMS; i++) {
    float d = fabsf(itemCx(items[i]) - p.x);
    if (items[i].used && items[i].toy && d < bestD) bestD = d, best = i, isItem = true;
  }
  return best;
}

// Where the ball or toy being chased is: centre x and bottom y
static bool playthingAt(const Pet &p, float &x, float &y) {
  if (p.target < 0) return false;
  if (p.targetIsItem) {
    const Item &it = items[p.target];
    if (!it.used || !it.toy) return false;
    x = itemCx(it);
    y = it.y + it.h;
  } else {
    const Ball &b = balls[p.target];
    if (!b.used || b.held) return false;
    x = b.x;
    y = b.y + b.r;
  }
  return true;
}

static bool nightNow(uint32_t now) { return isNight() && now >= pet.awakeUntil; }

static void startPlay(Pet &p, uint32_t now) {
  bool isItem = false;
  int t = nearestPlaything(p, isItem);
  setAct(p, Act::PLAY, now, 8000);
  p.target = t;
  p.targetIsItem = isItem;
}

static bool hasPlaything(const Pet &p) {
  bool isItem;
  return nearestPlaything(p, isItem) >= 0;
}

static void decide(uint32_t now) {
  if (wantsSleep(nightNow(now))) {
    setAct(pet, Act::SLEEP, now, 0);
    stats.asleep = 1;
    return;
  }
  int f = stats.fullness < 90 ? pickFood(now) : -1;
  if (f >= 0) {
    setAct(pet, Act::SEEK_FOOD, now, 12000);
    pet.target = f;
    return;
  }
  if (visiting && random(100) < 35) {
    setAct(pet, Act::FRIEND, now, 8000);
    return;
  }
  if (hasPlaything(pet) && stats.fun < 85 && random(100) < 70) {
    startPlay(pet, now);
    return;
  }
  if (stats.artId && random(100) < 15 + artLove * 40) {
    setAct(pet, Act::ADMIRE, now, 10000);
    pet.targetX = ART_X + ART_OUTER / 2;
    return;
  }
  if (random(100) < 55) {
    setAct(pet, Act::WANDER, now, 8000);
    pet.targetX = random(20, ROOM_SIZE - 20);
    return;
  }
  setAct(pet, Act::IDLE, now, random(1500, 4000));
}

static void feed(const Item &it, float portion) {
  const Nutrition &n = NUTRITION[it.kind];
  float a = portion * it.value;
  float fun = n.fun + (it.kind == stats.favourite ? FAVOURITE_FUN : 0);
  stats.fullness = constrain(stats.fullness + n.fullness * a, 0.0f, 100.0f);
  stats.fun = constrain(stats.fun + fun * a, 0.0f, 100.0f);
  stats.energy = constrain(stats.energy + n.energy * a, 0.0f, 100.0f);
}

// Takes a bite out of the side of the food nearest the pet.
static void bite(int i, uint32_t now) {
  Item &it = items[i];
  bool fromLeft = pet.x < itemCx(it);
  int col = -1, top = 0, bottom = 0;
  for (int k = 0; k < it.w && col < 0; k++) {
    int c = fromLeft ? k : it.w - 1 - k;
    for (int r = 0; r < it.h; r++) {
      if (it.px[r * it.w + c] == 0xFF) continue;
      if (col < 0) col = c, top = r;
      bottom = r;
    }
  }
  if (col < 0) {
    removeItem(i);
    return;
  }
  int cy = (top + bottom) / 2, removed = 0;
  for (int dy = -5; dy <= 5; dy++) {
    for (int dx = -5; dx <= 5; dx++) {
      int x = col + dx, y = cy + dy;
      if (dx * dx + dy * dy > 25 || x < 0 || y < 0 || x >= it.w || y >= it.h) continue;
      uint8_t &p = it.px[y * it.w + x];
      if (p != 0xFF) p = 0xFF, removed++;
    }
  }
  it.left -= removed;
  it.changed = true;
  float portion = (float)removed / it.total;
  for (int k = 0; k < 2; k++) {
    spawnFx(Fx::CRUMB, it.x + col, it.y + cy, rnd(-1, 1), rnd(-2, -1), COLOURS[it.kind], 500);
  }
  if (it.kind == stats.favourite && random(2) == 0) spawnHearts(pet, 1);
  if (it.kind == TOOL_VEG && stats.favourite != TOOL_VEG && random(3) == 0) {
    showFace(pet, Face::YUCK, now, 500);
  }
  if (it.left < it.total * 0.12f) {  // last crumbs: finish it
    portion += (float)it.left / it.total;
    feed(it, portion);
    removeItem(i);
    showFace(pet, Face::HAPPY, now, 1200);
  } else {
    feed(it, portion);
  }
}

static void startRunaway(uint32_t now) {
  setAct(pet, Act::RUNAWAY, now, 0);
  pet.y = ROOM_SIZE;
  pet.vx = pet.vy = 0;
}

static void newEgg() {
  statsNewEgg();
  statsSave();
  markDirty(pet.drawn);
  pet = Pet();
  Serial.println("pet: a new egg appeared");
}

static void hatch(uint32_t now) {
  stats.hatched = 1;
  stats.asleep = 0;
  stats.ageSec = 0;
  stats.neglectSec = 0;
  statsSave();
  setAct(pet, Act::IDLE, now, 1500);
  pet.vy = -4;
  showFace(pet, Face::WOW, now, 1200);
  spawnSparks(pet.x, pet.y - 14, 10);
  spawnHearts(pet, 3);
}

static void think(uint32_t now) {
  pet.lookX = pet.facing * 0.5f;
  pet.lookY = 0;
  if (pet.act != Act::EGG && pet.act != Act::RUNAWAY && stats.neglectSec >= RUNAWAY_SEC) {
    startRunaway(now);
  }
  if (isActive(pet) && (stats.fullness < 15 || stats.fun < 15) && now >= pet.nextTear) {
    spawnFx(Fx::TEAR, pet.x + pet.facing * bodyW(pet) * 0.3f, pet.y - bodyH(pet) * 0.55f, 0,
            0.3, 0, 700);
    pet.nextTear = now + 2500;
  }

  switch (pet.act) {
    case Act::EGG:
      pet.walk = 0;
      return;

    case Act::RUNAWAY:
      pet.walk = 0.8f;
      pet.facing = 1;
      if (pet.x > ROOM_SIZE + 30) newEgg();
      return;

    case Act::SLEEP:
      pet.walk = 0;
      stats.asleep = 1;
      if (now >= pet.nextZ) {
        spawnFx(Fx::Z, pet.x + 8, pet.y - bodyH(pet), 0.25, -0.5, 0, 1600);
        pet.nextZ = now + 1300;
      }
      if (wantsWake(nightNow(now))) {
        stats.asleep = 0;
        setAct(pet, Act::IDLE, now, 1000);
        showFace(pet, Face::HAPPY, now, 1500);
      }
      return;

    case Act::SEEK_FOOD: {
      int i = pet.target;
      if (i < 0 || !items[i].used) {
        setAct(pet, Act::IDLE, now, 500);
        return;
      }
      Item &it = items[i];
      float fcx = itemCx(it), bottom = it.y + it.h;
      pet.lookX = constrain((fcx - pet.x) / 30, -1.0f, 1.0f);
      bool level = bottom >= pet.y - bodyH(pet) && bottom <= pet.y + 6;
      if (fabsf(fcx - pet.x) < bodyW(pet) / 2.0f + it.w / 2.0f - 2 && level && it.resting) {
        pet.walk = 0;
        setAct(pet, Act::EAT, now, 0);
        pet.target = i;
        pet.nextBite = now + 300;
        return;
      }
      if (now >= pet.actUntil || goTo(pet, fcx, bottom, now) == Walk::STUCK) {
        it.ignoreUntil = now + 20000;  // can't get to it
        showFace(pet, Face::SAD, now, 1500);
        setAct(pet, Act::IDLE, now, 1500);
      }
      return;
    }

    case Act::EAT: {
      int i = pet.target;
      if (i < 0 || !items[i].used) {
        setAct(pet, Act::IDLE, now, 1500);
        return;
      }
      Item &it = items[i];
      if (!it.resting) {
        setAct(pet, Act::SEEK_FOOD, now, 8000);
        pet.target = i;
        return;
      }
      pet.walk = 0;
      pet.facing = itemCx(it) > pet.x ? 1 : -1;
      pet.lookX = pet.facing * 0.6f;
      pet.lookY = 0.5f;
      if (stats.fullness >= 100) {  // full up: leaves the rest
        showFace(pet, Face::HAPPY, now, 1500);
        setAct(pet, Act::IDLE, now, 2000);
      } else if (now >= pet.nextBite) {
        bite(i, now);
        pet.nextBite = now + 350;
      }
      return;
    }

    case Act::PLAY: {
      float tx, ty;
      if (!playthingAt(pet, tx, ty) || now >= pet.actUntil) {
        setAct(pet, Act::IDLE, now, 1000);
        return;
      }
      pet.lookX = constrain((tx - pet.x) / 30, -1.0f, 1.0f);
      if (goTo(pet, tx, ty, now) == Walk::STUCK) setAct(pet, Act::IDLE, now, 1000);
      return;  // touching it kicks it (see kickThings)
    }

    case Act::FRIEND:
      if (!visiting || now >= pet.actUntil) {
        setAct(pet, Act::IDLE, now, 1000);
        return;
      }
      pet.lookX = visitor.x > pet.x ? 1 : -1;
      if (walkTo(pet, visitor.x, now) == Walk::STUCK) setAct(pet, Act::IDLE, now, 1000);
      return;  // meeting up gives a hug (see tryHug)

    case Act::ADMIRE: {
      Walk w = walkTo(pet, pet.targetX, now);
      if (w == Walk::ARRIVED) {
        pet.lookX = 0;
        pet.lookY = -1;
        if (pet.target < 0) {  // just arrived
          pet.target = 1;
          pet.actUntil = now + 3000;
          spawnHearts(pet, 1 + artLove * 4);
          stats.fun = min(100.0f, stats.fun + 3 + 12 * artLove);
        }
      }
      if (w == Walk::STUCK || now >= pet.actUntil) setAct(pet, Act::IDLE, now, 1000);
      return;
    }

    case Act::WANDER: {
      Walk w = walkTo(pet, pet.targetX, now);
      if (w == Walk::STUCK) pet.facing = -pet.facing;  // turn back from a tall wall
      if (w != Walk::WALKING || now >= pet.actUntil) setAct(pet, Act::IDLE, now, 1500);
      return;
    }

    case Act::IDLE:
    case Act::LEAVE:
      pet.walk = 0;
      if (now >= pet.nextGlance) {
        pet.glance = rnd(-1, 1);
        pet.nextGlance = now + random(800, 2500);
      }
      pet.lookX = pet.glance;
      if (now >= pet.actUntil) decide(now);
      return;
  }
}

// ---------------------------------------------------------------------------
// Visitors: draw a door and a friend comes through it to play for a while
// ---------------------------------------------------------------------------
static void openDoor(uint32_t now, uint32_t ms) { door.openUntil = max(door.openUntil, now + ms); }

static void requestVisit(uint32_t now) {
  if (!door.used || visiting || door.visitAt || now < door.nextVisitAllowed) return;
  openDoor(now, 1600);
  door.visitAt = now + 700;
}

static void startVisit(uint32_t now) {
  visitor = Pet();
  visitor.size = 24;
  uint8_t c;
  do c = random(NUM_COLOURS);
  while (c == stats.colour);
  visitor.colour = COLOURS[c];
  visitor.x = constrain(door.x + door.w / 2.0f, 14.0f, ROOM_SIZE - 14.0f);
  visitor.y = door.y + door.h;
  setAct(visitor, Act::IDLE, now, 1200);
  showFace(visitor, Face::HAPPY, now, 2000);
  spawnHearts(visitor, 2);
  visiting = true;
  visitEndsAt = now + random(60000, 90000);
  if (isActive(pet)) {
    showFace(pet, Face::WOW, now, 1200);
    stats.fun = min(100.0f, stats.fun + 5);
  }
}

static void endVisit(uint32_t now, bool throughDoor) {
  markDirty(visitor.drawn);
  visiting = false;
  door.nextVisitAllowed = now + 15000;
  if (throughDoor && door.used) {
    openDoor(now, 800);
  } else {
    spawnSparks(visitor.x, visitor.y - 12, 8);  // poof: home another way
  }
}

static void thinkVisitor(uint32_t now) {
  Pet &v = visitor;
  v.lookX = v.facing * 0.5f;
  v.lookY = 0;
  if (!door.used) {
    endVisit(now, false);
    return;
  }
  if (v.act != Act::LEAVE && now >= visitEndsAt) setAct(v, Act::LEAVE, now, 15000);

  switch (v.act) {
    case Act::LEAVE: {
      float dx = door.x + door.w / 2.0f;
      if (fabsf(v.x - dx) < 6 && fabsf(v.y - (door.y + door.h)) < 30) {
        endVisit(now, true);
      } else if (now >= v.actUntil || goTo(v, dx, door.y + door.h, now) == Walk::STUCK) {
        endVisit(now, false);
      } else {
        openDoor(now, 300);  // the door stays open while it heads home
      }
      return;
    }
    case Act::PLAY: {
      float tx, ty;
      if (!playthingAt(v, tx, ty) || now >= v.actUntil ||
          goTo(v, tx, ty, now) == Walk::STUCK) {
        setAct(v, Act::IDLE, now, 800);
      }
      return;
    }
    case Act::FRIEND:
      v.lookX = pet.x > v.x ? 1 : -1;
      if (now >= v.actUntil || walkTo(v, pet.x, now) == Walk::STUCK) {
        setAct(v, Act::IDLE, now, 800);
      }
      return;
    case Act::WANDER:
      if (walkTo(v, v.targetX, now) != Walk::WALKING || now >= v.actUntil) {
        setAct(v, Act::IDLE, now, 1000);
      }
      return;
    default: {
      v.walk = 0;
      if (now < v.actUntil) return;
      int r = random(100);
      if (r < 40 && hasPlaything(v)) {
        startPlay(v, now);
      } else if (r < 75 && pet.act != Act::EGG && pet.act != Act::RUNAWAY) {
        setAct(v, Act::FRIEND, now, 8000);
      } else {
        setAct(v, Act::WANDER, now, 7000);
        v.targetX = random(20, ROOM_SIZE - 20);
      }
      return;
    }
  }
}

// Pet and visitor side by side: a hug.
static void tryHug(uint32_t now) {
  if (!visiting || now < nextHugAt || !isActive(pet) || visitor.act == Act::LEAVE) return;
  if (fabsf(pet.x - visitor.x) > (bodyW(pet) + bodyW(visitor)) / 2.0f + 4) return;
  if (fabsf(pet.y - visitor.y) > 10) return;
  nextHugAt = now + 5000;
  for (Pet *p : {&pet, &visitor}) {
    p->squash = 0.3f;
    showFace(*p, Face::HAPPY, now, 1500);
    spawnHearts(*p, 2);
    if (p->act == Act::FRIEND) setAct(*p, Act::IDLE, now, 1500);
  }
  stats.fun = min(100.0f, stats.fun + 4);
}

// ---------------------------------------------------------------------------
// Faces and looks
// ---------------------------------------------------------------------------
static Face faceFor(const Pet &p, bool isMain, uint32_t now) {
  if (now < p.tempFaceUntil) return p.tempFace;
  switch (p.act) {
    case Act::SLEEP:
      return Face::SLEEP;
    case Act::EAT:
      return (now / 175) % 2 ? Face::CHEW_OPEN : Face::CHEW_SHUT;
    case Act::RUNAWAY:
      return Face::SAD;
    default:
      break;
  }
  if (p.sliding || (!p.onGround && p.vy < -1)) return Face::WOW;
  if (!isMain) return Face::HAPPY;  // visitors are always having a nice time
  if (stats.fullness < 20 || stats.fun < 20) return Face::SAD;
  if (stats.fun > 55 && stats.fullness > 35) return Face::HAPPY;
  return Face::NEUTRAL;
}

static void updateLook(Pet &p, bool isMain, uint32_t now) {
  PetLook &l = p.look;
  l = PetLook();
  l.size = p.size;
  l.colour = p.colour;
  if (p.act == Act::EGG) {
    l.egg = true;
    l.size = 30;
    l.wobble = p.wobble * sinf(now / 50.0f);
    l.cracks = p.eggTaps;
    return;
  }
  float anim;
  if (p.act == Act::SLEEP) {
    anim = 0.15f + 0.03f * sinf(now / 800.0f);  // slow breathing, lying low
  } else if (!p.onGround) {
    anim = -0.12f;
  } else if (p.walk != 0) {
    anim = 0.08f * fabsf(sinf(now / 110.0f));
  } else {
    anim = 0.03f * sinf(now / 500.0f);
  }
  l.squash = constrain(p.squash + anim, -0.3f, 0.4f);
  l.lookX = p.lookX;
  l.lookY = p.lookY;
  l.blink = now < p.blinkUntil;
  l.face = faceFor(p, isMain, now);
}

static void animate(Pet &p, uint32_t now) {
  if (now >= p.nextBlink) {
    p.blinkUntil = now + 120;
    p.nextBlink = now + random(2000, 5000);
  }
  p.squash *= 0.85f;
  p.wobble *= 0.94f;
}

// ---------------------------------------------------------------------------
// The board's RGB LED mirrors the pet: its colour, breathing while it sleeps, pulsing red
// when it needs looking after. Picking a tool shows that tool's colour for a moment.
// ---------------------------------------------------------------------------
static uint32_t toolLedUntil = 0;

static void updateLed(uint32_t now) {
  if (now < toolLedUntil) {
    ledSet(COLOURS[drawColour], 1);
    return;
  }
  uint16_t c = COLOURS[stats.colour];
  float level = 1;
  if (pet.act == Act::RUNAWAY) {
    level = 0.2f;
  } else if (pet.act == Act::EGG) {
    level = 0.6f + 0.4f * sinf(now / 400.0f);
  } else if (pet.act == Act::SLEEP) {
    level = 0.15f + 0.15f * sinf(now / 1200.0f);
  } else if (stats.fullness < 20 || stats.fun < 20) {
    c = TFT_RED;
    level = 0.5f + 0.5f * sinf(now / 200.0f);
  }
  ledSet(c, level);
}

// ---------------------------------------------------------------------------
// One animation step
// ---------------------------------------------------------------------------
static void frame(uint32_t now) {
  pet.size = petSize();
  pet.colour = COLOURS[stats.colour];
  think(now);
  movePet(pet, pet.act == Act::RUNAWAY);
  animate(pet, now);
  kickThings(pet, true, now);

  if (door.knockAt && now >= door.knockAt) {  // a new door: someone's coming
    door.knockAt = 0;
    requestVisit(now);
  }
  if (door.visitAt && now >= door.visitAt) {
    door.visitAt = 0;
    if (door.used) startVisit(now);
  }
  if (visiting) {
    thinkVisitor(now);
    if (visiting) {
      movePet(visitor, false);
      animate(visitor, now);
      kickThings(visitor, false, now);
      tryHug(now);
    }
  }

  for (Item &it : items) {
    if (it.used) updateItem(it);
  }
  for (Ball &b : balls) {
    if (!b.used) continue;
    if (b.held) {  // follows the finger; let go and it keeps the finger's speed
      b.vx = constrain((float)fingerX - b.x, -12.0f, 12.0f);
      b.vy = constrain((float)fingerY - b.y, -12.0f, 12.0f);
      b.x = fingerX;
      b.y = fingerY;
    } else {
      updateBall(b);
    }
  }
  updateFx(now);

  if (door.used) {
    bool open = now < door.openUntil;
    if (open != door.wasOpen) door.wasOpen = open, door.changed = true;
    markMoved(door.drawn, doorRect(), door.changed);
    door.changed = false;
  }
  updateLook(pet, true, now);
  markMoved(pet.drawn, petRect(pet), true);
  if (visiting) {
    updateLook(visitor, false, now);
    markMoved(visitor.drawn, petRect(visitor), true);
  }
  for (Item &it : items) {
    if (!it.used) continue;
    markMoved(it.drawn, itemRect(it), it.changed);
    it.changed = false;
  }
  for (Ball &b : balls) {
    if (b.used) markMoved(b.drawn, ballRect(b), false);
  }
  for (Particle &p : fx) {
    if (p.used) markMoved(p.drawn, fxRect(p), true);
  }
  flushDirty();
  updateLed(now);
}

// ---------------------------------------------------------------------------
// Drawing in the room: each finished stroke becomes what its tool makes
// ---------------------------------------------------------------------------
static bool roomDirty = false;
static uint32_t roomChangedAt = 0;

// A free slot, or if they're all used, the oldest as -(slot + 1).
static int freeSlot(bool (*used)(int), uint32_t (*born)(int), int count) {
  int oldest = 0;
  for (int i = 0; i < count; i++) {
    if (!used(i)) return i;
    if (born(i) < born(oldest)) oldest = i;
  }
  return -oldest - 1;
}

static void spawnBall(int cx, int cy, int r) {
  int i = freeSlot([](int i) { return balls[i].used; }, [](int i) { return balls[i].bornAt; },
                   MAX_BALLS);
  if (i < 0) {
    i = -i - 1;
    markDirty(balls[i].drawn);
  }
  balls[i] = Ball();
  balls[i].used = true;
  balls[i].x = cx;
  balls[i].y = cy;
  balls[i].r = r;
  balls[i].bornAt = millis();
}

struct Bitmap {
  int x, y, w, h;
  uint8_t *px;
  int counts[NUM_COLOURS];
};

// The stroke drawn on top of an optional existing bitmap, trimmed to what was drawn and
// clipped to area (room coordinates). False if out of memory or nothing was drawn.
static bool strokeBitmap(Rect area, const uint8_t *basePx, Rect base, Bitmap &out) {
  uint8_t *px = (uint8_t *)malloc(area.w * area.h);
  if (!px) return false;
  memset(px, 0xFF, area.w * area.h);
  for (int j = 0; basePx && j < base.h; j++) {
    for (int i = 0; i < base.w; i++) {
      int x = base.x + i - area.x, y = base.y + j - area.y;
      uint8_t c = basePx[j * base.w + i];
      if (c != 0xFF && x >= 0 && y >= 0 && x < area.w && y < area.h) px[y * area.w + x] = c;
    }
  }
  for (int k = 0; k < strokeLen; k++) {
    const Pt &a = stroke[k > 0 ? k - 1 : 0], &b = stroke[k];
    forEachBrushPixel(a.x - area.x, a.y - area.y, b.x - area.x, b.y - area.y, BRUSH_R,
                      [&](int x, int y) {
                        if (x >= 0 && y >= 0 && x < area.w && y < area.h) {
                          px[y * area.w + x] = b.c;
                        }
                      });
  }
  int x0 = area.w, y0 = area.h, x1 = -1, y1 = -1;
  memset(out.counts, 0, sizeof(out.counts));
  for (int y = 0; y < area.h; y++) {
    for (int x = 0; x < area.w; x++) {
      uint8_t c = px[y * area.w + x];
      if (c == 0xFF) continue;
      out.counts[c]++;
      x0 = min(x0, x), y0 = min(y0, y), x1 = max(x1, x), y1 = max(y1, y);
    }
  }
  out.px = nullptr;
  if (x1 >= 0) {
    out.w = x1 - x0 + 1;
    out.h = y1 - y0 + 1;
    out.x = area.x + x0;
    out.y = area.y + y0;
    out.px = (uint8_t *)malloc(out.w * out.h);
    for (int y = 0; out.px && y < out.h; y++) {
      memcpy(out.px + y * out.w, px + (y + y0) * area.w + x0, out.w);
    }
  }
  free(px);
  return out.px != nullptr;
}

// The stroke's own area, at most maxW x maxH around its middle.
static Rect strokeArea(int x0, int y0, int x1, int y1, int maxW, int maxH) {
  Rect r{x0 - BRUSH_R, y0 - BRUSH_R, x1 - x0 + 1 + 2 * BRUSH_R, y1 - y0 + 1 + 2 * BRUSH_R};
  if (r.w > maxW) r.x += (r.w - maxW) / 2, r.w = maxW;
  if (r.h > maxH) r.y += (r.h - maxH) / 2, r.h = maxH;
  return r;
}

static void addItemStroke(bool toy, int x0, int y0, int x1, int y1) {
  Rect area = strokeArea(x0, y0, x1, y1, ITEM_MAX_SIZE, ITEM_MAX_SIZE);
  // Strokes over a piece of food (or a toy) add to it: an apple's leaf, a car's wheels
  int merge = -1;
  for (int i = 0; i < MAX_ITEMS; i++) {
    const Item &it = items[i];
    if (!it.used || it.toy != toy || !itemRect(it).overlaps(area)) continue;
    Rect both = unite(area, {(int)it.x, (int)it.y, it.w, it.h});
    if (both.w <= ITEM_MAX_SIZE && both.h <= ITEM_MAX_SIZE) {
      merge = i;
      area = both;
      break;
    }
  }
  Rect base{};
  if (merge >= 0) base = {(int)items[merge].x, (int)items[merge].y, items[merge].w, items[merge].h};
  Bitmap bm;
  if (!strokeBitmap(area, merge >= 0 ? items[merge].px : nullptr, base, bm)) return;

  Item it;
  it.used = true;
  it.toy = toy;
  it.x = bm.x;
  it.y = bm.y;
  it.w = bm.w;
  it.h = bm.h;
  it.px = bm.px;
  for (int c = 0; c < NUM_COLOURS; c++) {
    it.total += bm.counts[c];
    if (bm.counts[c] > bm.counts[it.kind]) it.kind = c;
  }
  it.left = it.total;
  it.value = constrain(it.total / 350.0f, 0.4f, 1.5f);
  it.bornAt = millis();

  if (merge >= 0) removeItem(merge);
  int slot = freeSlot([](int i) { return items[i].used; }, [](int i) { return items[i].bornAt; },
                      MAX_ITEMS);
  if (slot < 0) {
    slot = -slot - 1;
    removeItem(slot);
  }
  items[slot] = it;
}

static void removeDoor() {
  markDirty(door.drawn);
  free(door.px);
  door = Door();
}

static void addDoorStroke(int x0, int y0, int x1, int y1, uint32_t now) {
  Rect area = strokeArea(x0, y0, x1, y1, DOOR_MAX_W, DOOR_MAX_H);
  // Keep adding to the door while it's being drawn; a door drawn elsewhere replaces it
  bool adding = door.used && now - door.lastStrokeAt < 4000 && doorRect().overlaps(area);
  if (adding) {
    Rect both = unite(area, doorRect());
    if (both.w > DOOR_MAX_W || both.h > DOOR_MAX_H) {
      adding = false;
    } else {
      area = both;
    }
  }
  Bitmap bm;
  if (!strokeBitmap(area, adding ? door.px : nullptr, adding ? doorRect() : Rect{}, bm)) return;
  uint32_t nextVisitAllowed = door.nextVisitAllowed;
  if (door.used) removeDoor();
  door.used = true;
  door.x = bm.x;
  door.y = bm.y;
  door.w = bm.w;
  door.h = bm.h;
  door.px = bm.px;
  door.total = door.left = bm.counts[TOOL_DOOR];
  door.lastStrokeAt = now;
  door.knockAt = now + 2500;  // once it's finished, a friend knocks
  door.nextVisitAllowed = nextVisitAllowed;
  door.changed = true;
}

// The eraser rubs out floors, and any food, toys, balls or door it passes over.
static void eraseThings() {
  for (int k = 0; k < strokeLen; k++) {
    const Pt &a = stroke[k > 0 ? k - 1 : 0], &b = stroke[k];
    roomInk(a.x, a.y, b.x, b.y, TOOL_ERASER);
    forEachBrushPixel(a.x, a.y, b.x, b.y, BRUSH_R, [&](int x, int y) {
      for (Item &it : items) {
        if (!it.used || !itemRect(it).contains(x, y)) continue;
        int i = x - (int)it.x, j = y - (int)it.y;
        if (i >= it.w || j >= it.h) continue;
        uint8_t &p = it.px[j * it.w + i];
        if (p != 0xFF) p = 0xFF, it.left--, it.changed = true;
      }
      if (door.used && doorRect().contains(x, y)) {
        uint8_t &p = door.px[(y - door.y) * door.w + (x - door.x)];
        if (p != 0xFF) p = 0xFF, door.left--, door.changed = true;
      }
    });
    for (Ball &ball : balls) {
      if (ball.used && !ball.held && hypotf(b.x - ball.x, b.y - ball.y) <= ball.r + BRUSH_R) {
        markDirty(ball.drawn);
        ball = Ball();
      }
    }
  }
  for (int i = 0; i < MAX_ITEMS; i++) {
    if (items[i].used && items[i].left < items[i].total * 0.12f) removeItem(i);
  }
  if (door.used && door.left < door.total * 0.3f) removeDoor();
}

static void finishStroke() {
  if (!strokeLen) return;
  uint32_t now = millis();
  int x0 = 255, y0 = 255, x1 = 0, y1 = 0;
  float len = 0;
  for (int i = 0; i < strokeLen; i++) {
    const Pt &p = stroke[i];
    x0 = min(x0, (int)p.x), y0 = min(y0, (int)p.y), x1 = max(x1, (int)p.x), y1 = max(y1, (int)p.y);
    if (i) len += hypotf(p.x - stroke[i - 1].x, p.y - stroke[i - 1].y);
  }
  int w = x1 - x0 + 1, h = y1 - y0 + 1, big = max(w, h), small = min(w, h);
  float gap = hypotf(stroke[strokeLen - 1].x - stroke[0].x, stroke[strokeLen - 1].y - stroke[0].y);
  uint8_t tool = stroke[0].c;

  if (tool == TOOL_FLOOR) {
    for (int i = 0; i < strokeLen; i++) {
      const Pt &a = stroke[i > 0 ? i - 1 : 0], &b = stroke[i];
      roomInk(a.x, a.y, b.x, b.y, tool);
    }
  } else if (tool == TOOL_ERASER) {
    eraseThings();
  } else if (tool == TOOL_DOOR) {
    addDoorStroke(x0, y0, x1, y1, now);
  } else if (tool == TOOL_TOY) {
    bool circle = gap <= max(10.0f, big * 0.3f) && len >= 2.2f * big && small >= 10 &&
                  big <= 60 && small * 2 >= big;
    if (circle) {
      spawnBall(x0 + w / 2, y0 + h / 2, constrain((w + h) / 4 + 1, 6, 20));
    } else {
      addItemStroke(true, x0, y0, x1, y1);
    }
  } else if (isFoodTool(tool)) {
    addItemStroke(false, x0, y0, x1, y1);
  }
  if (tool == TOOL_FLOOR || tool == TOOL_ERASER) {
    roomDirty = true;
    roomChangedAt = now;
  }
  strokeLen = 0;
  // Swap the line drawn under the finger for whatever it turned into
  renderRect({x0 - BRUSH_R - 1, y0 - BRUSH_R - 1, w + 2 * BRUSH_R + 3, h + 2 * BRUSH_R + 3});
}

static void strokeAdd(int rx, int ry) {
  Pt p{(uint8_t)constrain(rx, 0, ROOM_SIZE - 1), (uint8_t)constrain(ry, 0, ROOM_SIZE - 1),
       (uint8_t)drawColour};
  if (strokeLen) {
    const Pt &last = stroke[strokeLen - 1];
    if (abs(p.x - last.x) + abs(p.y - last.y) < 2) return;
  }
  if (strokeLen >= MAX_STROKE) finishStroke();
  stroke[strokeLen++] = p;
  markDirty(segmentRect(stroke[strokeLen > 1 ? strokeLen - 2 : 0], p));
  flushDirty();  // straight away, rather than waiting for the next frame
}

// ---------------------------------------------------------------------------
// The tool palette: each swatch carries a little picture of what it makes
// ---------------------------------------------------------------------------
static void drawToolIcon(uint8_t tool, int cx, int cy) {
  uint16_t bg = COLOURS[tool];
  switch (tool) {
    case TOOL_FLOOR: {  // stairs
      const int pts[][2] = {{-9, 6}, {-3, 6}, {-3, 0}, {3, 0}, {3, -6}, {9, -6}};
      for (int i = 0; i < 5; i++) {
        tft.drawWideLine(cx + pts[i][0], cy + pts[i][1], cx + pts[i + 1][0], cy + pts[i + 1][1],
                         2, TFT_WHITE, bg);
      }
      break;
    }
    case TOOL_ERASER:
      tft.fillRoundRect(cx - 9, cy - 5, 18, 10, 2, TFT_PINK);
      tft.fillRect(cx - 9, cy - 5, 6, 10, TFT_SKYBLUE);
      tft.drawRoundRect(cx - 9, cy - 5, 18, 10, 2, TFT_BLACK);
      break;
    case TOOL_FRUIT:  // apple
      tft.fillCircle(cx, cy + 1, 6, TFT_WHITE);
      tft.fillRect(cx, cy - 8, 2, 3, TFT_WHITE);
      tft.fillEllipse(cx + 4, cy - 7, 3, 1, TFT_GREEN);
      break;
    case TOOL_VEG:  // leaf
      tft.fillEllipse(cx, cy, 9, 5, TFT_WHITE);
      tft.drawLine(cx - 8, cy, cx + 8, cy, bg);
      break;
    case TOOL_WATER:  // drop
      tft.fillCircle(cx, cy + 2, 5, TFT_WHITE);
      tft.fillTriangle(cx - 5, cy + 1, cx + 5, cy + 1, cx, cy - 8, TFT_WHITE);
      break;
    case TOOL_SWEETS:  // wrapped sweet
      tft.fillCircle(cx, cy, 4, TFT_WHITE);
      tft.fillTriangle(cx - 3, cy, cx - 10, cy - 5, cx - 10, cy + 5, TFT_WHITE);
      tft.fillTriangle(cx + 3, cy, cx + 10, cy - 5, cx + 10, cy + 5, TFT_WHITE);
      break;
    case TOOL_TOY:  // ball
      tft.fillCircle(cx, cy, 7, TFT_WHITE);
      tft.drawFastHLine(cx - 6, cy, 13, bg);
      break;
    case TOOL_DOOR:
      tft.fillRoundRect(cx - 6, cy - 9, 12, 18, 3, 0xA285);
      tft.fillCircle(cx + 3, cy + 1, 1, TFT_YELLOW);
      break;
  }
}

static void drawToolSwatch(uint8_t tool, int x, int y, int w, int h) {
  tft.fillRect(x, y, w, h, COLOURS[tool]);
  if (tool == TOOL_ERASER) tft.drawRect(x, y, w, h, TFT_LIGHTGREY);
  drawToolIcon(tool, x + w / 2, y + h / 2);
}

static void drawToolPalette() {
  tft.fillRect(0, 0, PALETTE_W, 240, TFT_DARKGREY);
  for (int i = 0; i < NUM_COLOURS; i++) {
    int y = i * SWATCH_H;
    drawToolSwatch(i, 5, y + 4, PALETTE_W - 10, SWATCH_H - 8);
    if (i == drawColour) {
      tft.drawRect(1, y + 1, PALETTE_W - 2, SWATCH_H - 2, TFT_WHITE);
      tft.drawRect(2, y + 2, PALETTE_W - 4, SWATCH_H - 4, TFT_BLACK);
    }
  }
}

// ---------------------------------------------------------------------------
// Screen furniture: needs meters, buttons, and the picture picker
// ---------------------------------------------------------------------------
static void drawMeterIcon(int i, int cx, int cy) {
  switch (i) {
    case 0:  // apple
      tft.fillCircle(cx, cy + 1, 4, TFT_RED);
      tft.fillEllipse(cx + 2, cy - 4, 2, 1, TFT_GREEN);
      break;
    case 1:
      drawHeart(tft, cx, cy, 4, TFT_PINK);
      break;
    case 2:  // lightning bolt
      tft.fillTriangle(cx + 2, cy - 6, cx - 3, cy + 1, cx + 1, cy + 1, TFT_YELLOW);
      tft.fillTriangle(cx - 1, cy - 1, cx + 3, cy - 1, cx - 2, cy + 6, TFT_YELLOW);
      break;
  }
}

static void drawMeters(bool force) {
  constexpr int TOP = 20, H = METERS_H - TOP - 6, W = 9;
  static int shown[3];
  int v[3] = {(int)lroundf(stats.fullness), (int)lroundf(stats.fun), (int)lroundf(stats.energy)};
  if (!stats.hatched) v[0] = v[1] = v[2] = -1;  // eggs don't have needs yet
  if (force) {
    tft.fillRect(TOOLBAR_X, 0, TOOLBAR_W, METERS_H, TFT_DARKGREY);
    for (int i = 0; i < 3; i++) {
      drawMeterIcon(i, TOOLBAR_X + 8 + i * 12, 9);
      shown[i] = -2;
    }
  }
  for (int i = 0; i < 3; i++) {
    if (v[i] == shown[i]) continue;
    shown[i] = v[i];
    int x = TOOLBAR_X + 4 + i * 12;
    tft.fillRect(x, TOP, W, H, TFT_BLACK);
    if (v[i] < 0) continue;
    int fill = v[i] * (H - 2) / 100;
    uint16_t col = v[i] > 50 ? TFT_GREEN : v[i] > 25 ? TFT_ORANGE : TFT_RED;
    tft.fillRect(x + 1, TOP + H - 1 - fill, W - 2, fill, col);
  }
}

static bool confirmClear = false;
static uint32_t confirmClearAt = 0;

static void drawToolbar() {
  drawMeters(true);
  drawButtonAt(INFO_BUTTON_Y, BUTTON_H, Icon::INFO);
  drawButtonAt(ART_BUTTON_Y, BUTTON_H, Icon::VIEW);
  drawButtonAt(CLEAR_BUTTON_Y, BUTTON_H, confirmClear ? Icon::TRASH_OPEN : Icon::TRASH,
               confirmClear ? TFT_RED : TFT_DARKGREY);
}

static void redrawAll() {
  drawToolPalette();
  drawToolbar();
  uint32_t now = millis();
  updateLook(pet, true, now);
  pet.drawn = petRect(pet);
  if (visiting) {
    updateLook(visitor, false, now);
    visitor.drawn = petRect(visitor);
  }
  renderRect({0, 0, ROOM_SIZE, ROOM_SIZE});
}

static bool picking = false;
static std::vector<int> pickList;
static int pickIdx = 0;

static void showPick() {
  tft.fillRect(0, 0, PALETTE_W, 240, TFT_DARKGREY);
  tft.fillRect(CANVAS_X, 0, CANVAS_SIZE, CANVAS_SIZE, TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.drawNumber(pickIdx + 1, PALETTE_W / 2, 105, 4);
  tft.drawString("of", PALETTE_W / 2, 125, 1);
  tft.drawNumber(pickList.size(), PALETTE_W / 2, 140, 2);
  StrokeDrawer d;
  forEachSavedPoint(pickList[pickIdx], [&](Pt p) { d.add(p); });
}

static void openPicker() {
  pickList = listDrawings();
  if (pickList.empty()) {  // nothing saved in the drawing app yet
    drawButtonAt(ART_BUTTON_Y, BUTTON_H, Icon::CROSS, TFT_RED);
    delay(700);
    drawButtonAt(ART_BUTTON_Y, BUTTON_H, Icon::VIEW);
    return;
  }
  picking = true;
  pickIdx = pickList.size() - 1;
  for (int i = 0; i < (int)pickList.size(); i++) {
    if (pickList[i] == stats.artId) pickIdx = i;
  }
  const Icon buttons[] = {Icon::PREV, Icon::NEXT, Icon::TICK, Icon::CROSS};
  for (int i = 0; i < 4; i++) drawButton(i, 4, buttons[i], i == 2 ? TFT_DARKGREEN : TFT_DARKGREY);
  showPick();
}

static void pickerTap(int x, int y, uint32_t now) {
  if (x < TOOLBAR_X) return;
  int n = pickList.size();
  switch (y / 60) {
    case 0:
      pickIdx = (pickIdx + n - 1) % n;
      showPick();
      return;
    case 1:
      pickIdx = (pickIdx + 1) % n;
      showPick();
      return;
    case 2:  // hang it
      stats.artId = pickList[pickIdx];
      roomHangArt(stats.artId);
      artLove = roomArtShare(stats.favourite);
      roomDirty = true;
      roomChangedAt = now;
      statsSave();
      if (isActive(pet)) {  // go and have a look
        setAct(pet, Act::ADMIRE, now, 10000);
        pet.targetX = ART_X + ART_OUTER / 2;
      }
      break;
  }
  picking = false;
  pickList.clear();
  redrawAll();
}

// ---------------------------------------------------------------------------
// Help for grown-ups: what the tools do, how to look after the pet, and settings
// ---------------------------------------------------------------------------
static int infoPage = -1;  // -1 when closed

static bool overlayOpen() { return picking || infoPage >= 0; }

constexpr int INFO_PAGES = 3;
constexpr int INFO_TOP = 32, INFO_ROW_H = 34, INFO_ICON_X = 20, INFO_TEXT_X = 44;

static int infoRowY(int row) { return INFO_TOP + row * INFO_ROW_H; }

static void infoText(int row, const char *line1, const char *line2) {
  int y = infoRowY(row);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.drawString(line1, INFO_TEXT_X, y, 2);
  tft.drawString(line2, INFO_TEXT_X, y + 15, 2);
}

// The real toolbar button, so it's easy to match up
static void infoButton(int row, Icon icon) {
  int cy = infoRowY(row) + 15;
  tft.fillRoundRect(INFO_ICON_X - 16, cy - 16, 32, 32, 4, TFT_DARKGREY);
  drawIcon(icon, INFO_ICON_X, cy, TFT_DARKGREY);
}

static void drawToolsPage() {
  constexpr int TOP = 30, ROW_H = 26;
  struct Row {
    uint8_t tool;
    const char *text;
  };
  static const Row rows[] = {
      {TOOL_FLOOR, "Floors, ramps and stairs"},
      {TOOL_ERASER, "Rubs out floors, food, toys"},
      {TOOL_FRUIT, "Fruit: tasty and filling"},
      {TOOL_VEG, "Veg: very filling, a bit dull"},
      {TOOL_WATER, "Water: gives it energy"},
      {TOOL_SWEETS, "Sweets: great fun, not filling"},
      {TOOL_TOY, "Toys to kick; circles roll"},
      {TOOL_DOOR, "Door: a friend visits (tap it)"},
  };
  tft.drawString("What you can draw", 6, 2, 4);
  for (int i = 0; i < 8; i++) {
    int y = TOP + i * ROW_H;
    drawToolSwatch(rows[i].tool, 6, y + 1, 30, 22);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.drawString(rows[i].text, INFO_TEXT_X, y + 4, 2);
  }
}

static void drawCarePage() {
  char line1[40], line2[40];
  tft.drawString("Looking after it", 6, 4, 4);
  for (int i = 0; i < 3; i++) drawMeterIcon(i, 8 + i * 12, infoRowY(0) + 15);
  infoText(0, "Meters: fullness, fun, energy.", "Green is good, red needs help");

  PetLook look;
  look.size = 24;
  look.colour = COLOURS[stats.colour];
  look.face = Face::HAPPY;
  drawPet(tft, INFO_ICON_X, infoRowY(1) + 31, look);
  infoText(1, "Tap or rub the pet to play.", "Egg: tap a colour, then it 3x");

  infoButton(2, Icon::VIEW);
  infoText(2, "Hang a saved drawing on the", "wall for it to admire");

  infoButton(3, Icon::TRASH);
  infoText(3, "Tap twice to clear the room", "(the picture stays)");

  drawZ(tft, INFO_ICON_X - 10, infoRowY(4) + 4, TFT_WHITE);
  drawZ(tft, INFO_ICON_X + 2, infoRowY(4) + 16, TFT_WHITE);
  snprintf(line1, sizeof(line1), "Sleeps %d:00-%d:00, or if tired.", BEDTIME_HOUR, WAKE_HOUR);
  infoText(4, line1, "A poke wakes it for a minute");

  look.face = Face::SAD;
  drawPet(tft, INFO_ICON_X, infoRowY(5) + 31, look);
  snprintf(line2, sizeof(line2), "for %u hours, it runs away", RUNAWAY_SEC / 3600);
  infoText(5, "Left hungry or bored", line2);
}

constexpr int SWATCH_X = 8, SWATCH_Y = 56, SWATCH_SIZE = 28, SWATCH_STEP = 34;
constexpr int RESTART_X = 8, RESTART_Y = 180, RESTART_W = 264, RESTART_H = 44;
static bool confirmRestart = false;

static void drawSettingsPage() {
  tft.drawString("Settings", 6, 4, 4);
  tft.drawString("Pet colour", SWATCH_X, 36, 2);
  for (int i = 0; i < NUM_COLOURS; i++) {
    int x = SWATCH_X + i * SWATCH_STEP;
    tft.fillRect(x, SWATCH_Y, SWATCH_SIZE, SWATCH_SIZE, COLOURS[i]);
    tft.drawRect(x, SWATCH_Y, SWATCH_SIZE, SWATCH_SIZE, TFT_DARKGREY);
    if (i == stats.colour) {  // pointer under the current colour
      int cx = x + SWATCH_SIZE / 2, ty = SWATCH_Y + SWATCH_SIZE + 3;
      tft.fillTriangle(cx, ty, cx - 6, ty + 8, cx + 6, ty + 8, TFT_BLACK);
    }
  }

  const char *fav = "Favourite food:";
  tft.drawString(fav, SWATCH_X, 104, 2);
  drawToolSwatch(stats.favourite, SWATCH_X + tft.textWidth(fav, 2) + 8, 100, 30, 22);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  if (clockValid()) {
    tft.drawString("Clock set over WiFi: needs keep", SWATCH_X, 130, 2);
    tft.drawString("changing while it's switched off", SWATCH_X, 146, 2);
  } else {
    tft.drawString("No WiFi clock: time only passes", SWATCH_X, 130, 2);
    tft.drawString("while it's switched on", SWATCH_X, 146, 2);
  }

  uint16_t bg = confirmRestart ? TFT_RED : TFT_DARKGREY;
  tft.fillRoundRect(RESTART_X, RESTART_Y, RESTART_W, RESTART_H, 8, bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, bg);
  tft.drawString(confirmRestart ? "Tap again: the pet leaves" : "Start again with a new egg",
                 RESTART_X + RESTART_W / 2, RESTART_Y + RESTART_H / 2, 2);
}

static void showInfo() {
  tft.fillRect(0, 0, TOOLBAR_X, 240, TFT_WHITE);
  drawButton(0, 2, Icon::NEXT);
  drawButton(1, 2, Icon::CROSS);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  if (infoPage == 0) {
    drawToolsPage();
  } else if (infoPage == 1) {
    drawCarePage();
  } else {
    drawSettingsPage();
  }
}

static void openInfo() {
  infoPage = 0;
  showInfo();
}

static void closeInfo() {
  infoPage = -1;
  confirmRestart = false;
  redrawAll();
}

static void infoTap(int x, int y, uint32_t now) {
  if (x >= TOOLBAR_X) {
    confirmRestart = false;
    if (y < 120) {
      infoPage = (infoPage + 1) % INFO_PAGES;
      showInfo();
    } else {
      closeInfo();
    }
    return;
  }
  if (infoPage != 2) return;
  if (y >= SWATCH_Y - 4 && y < SWATCH_Y + SWATCH_SIZE + 12) {
    int i = (x - SWATCH_X + (SWATCH_STEP - SWATCH_SIZE) / 2) / SWATCH_STEP;
    if (i < 0 || i >= NUM_COLOURS) return;
    stats.colour = i;
    statsSave();
    confirmRestart = false;
    showInfo();
  } else if (y >= RESTART_Y && y < RESTART_Y + RESTART_H) {
    if (!confirmRestart) {  // tap twice, so it can't happen by accident
      confirmRestart = true;
      showInfo();
      return;
    }
    // A hatched pet waves goodbye by walking off, so kids see where it went
    if (stats.hatched) {
      startRunaway(now);
    } else {
      newEgg();
    }
    closeInfo();
  }
}

static void clearRoom() {
  roomClearInk();
  for (int i = 0; i < MAX_ITEMS; i++) {
    if (items[i].used) removeItem(i);
  }
  for (Ball &b : balls) b = Ball();
  heldBall = -1;
  if (visiting) endVisit(millis(), false);
  if (door.used) removeDoor();
  numDirty = 0;
  roomDirty = true;
  roomChangedAt = millis();
  renderRect({0, 0, ROOM_SIZE, ROOM_SIZE});
}

static void uiTap(int x, int y, uint32_t now) {
  bool wasConfirming = confirmClear;
  confirmClear = false;
  if (x < PALETTE_W) {
    drawColour = constrain(y / SWATCH_H, 0, NUM_COLOURS - 1);
    drawToolPalette();
    toolLedUntil = now + 1500;
    if (pet.act == Act::EGG) stats.colour = drawColour;  // paint the egg
  } else if (y >= CLEAR_BUTTON_Y) {
    if (wasConfirming) {
      clearRoom();
    } else {
      confirmClear = true;
      confirmClearAt = now;
    }
  } else if (y >= ART_BUTTON_Y) {
    openPicker();
  } else if (y >= INFO_BUTTON_Y) {
    openInfo();
  }
  if (wasConfirming != confirmClear && !overlayOpen()) {
    drawButtonAt(CLEAR_BUTTON_Y, BUTTON_H, confirmClear ? Icon::TRASH_OPEN : Icon::TRASH,
                 confirmClear ? TFT_RED : TFT_DARKGREY);
  }
}

// ---------------------------------------------------------------------------
// Touching the pet, balls and the door
// ---------------------------------------------------------------------------
static bool touchesPet(int rx, int ry) {
  if (pet.act == Act::RUNAWAY) return false;
  int margin = pet.act == Act::EGG ? 12 : 8;  // generous for small fingers
  return fabsf(rx - pet.x) <= bodyW(pet) / 2.0f + margin && ry >= pet.y - bodyH(pet) - margin &&
         ry <= pet.y + 2;
}

static int ballAt(int rx, int ry) {
  for (int i = 0; i < MAX_BALLS; i++) {
    const Ball &b = balls[i];
    if (b.used && hypotf(rx - b.x, ry - b.y) <= b.r + 6) return i;
  }
  return -1;
}

static void petPressed(uint32_t now) {
  if (pet.act == Act::EGG) {
    pet.wobble = 1;
    if (++pet.eggTaps >= 3) hatch(now);
  } else if (pet.act == Act::SLEEP) {  // woken up: grumpy, but stays up for a minute
    stats.asleep = 0;
    stats.fun = max(0.0f, stats.fun - 3);
    pet.awakeUntil = now + 60000;
    setAct(pet, Act::IDLE, now, 2000);
    showFace(pet, Face::SAD, now, 1500);
  }
}

static void boop(uint32_t now) {
  if (!isActive(pet)) return;
  stats.fun = min(100.0f, stats.fun + 2);
  pet.squash = 0.3f;
  showFace(pet, Face::HAPPY, now, 900);
  spawnHearts(pet, 1);
}

static void rub(uint32_t now) {
  if (!isActive(pet)) return;
  stats.fun = min(100.0f, stats.fun + 1);
  pet.squash = rnd(0.1, 0.25);
  showFace(pet, Face::HAPPY, now, 900);
  if (random(2) == 0) spawnHearts(pet, 1);
}

// ---------------------------------------------------------------------------
// App
// ---------------------------------------------------------------------------
void petDrawPreview(int cx, int by) {
  if (!statsLoad()) statsSave();  // keep the new egg's colour for when the app opens
  PetLook l;
  l.size = 64;
  l.colour = COLOURS[stats.colour];
  l.egg = !stats.hatched;
  l.face = stats.asleep ? Face::SLEEP : Face::HAPPY;
  drawPet(tft, cx, by, l);
}

void petAppBegin() {
  tft.fillScreen(TFT_WHITE);
  petDrawPreview(160, 150);  // shown while WiFi sets the clock
  LittleFS.mkdir("/pet");
  statsLoad();
  clockSync();
  statsCatchUp();

  stroke = (Pt *)malloc(MAX_STROKE * sizeof(Pt));
  spr.setColorDepth(16);
  if (!roomBegin() || !stroke || !spr.createSprite(TILE, TILE)) {
    tft.setTextColor(TFT_RED, TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Out of memory", 160, 200, 2);
    while (true) delay(1000);
  }
  for (int i = 0; i < 16; i++) {
    RING_COS[i] = cosf(i * PI / 8);
    RING_SIN[i] = sinf(i * PI / 8);
  }
  randomSeed(esp_random());
  if (stats.artId) roomHangArt(stats.artId);  // rebuilds the copy that rubbing out restores
  artLove = stats.artId ? roomArtShare(stats.favourite) : 0;

  uint32_t now = millis();
  pet.size = petSize();
  pet.colour = COLOURS[stats.colour];
  if (!stats.hatched) {
    pet.act = Act::EGG;
  } else if (stats.neglectSec >= RUNAWAY_SEC) {
    startRunaway(now);
  } else if (stats.asleep) {
    setAct(pet, Act::SLEEP, now, 0);
  } else {
    setAct(pet, Act::IDLE, now, 1000);
  }
  drawColour = TOOL_FLOOR;
  statsSave();
  redrawAll();
  Serial.printf("pet: started, %u bytes of memory free\n", ESP.getFreeHeap());
}

void petAppLoop() {
  // As in the drawing app, a touch landing outside the room straight after a stroke is a
  // finger that slipped, not a button press.
  constexpr uint32_t STROKE_GRACE_MS = 300;
  // A finger that lifts and comes back down near the same spot this quickly is carrying on
  // the same stroke: the gap is joined up, and only the whole stroke becomes something.
  constexpr uint32_t STROKE_JOIN_MS = 250;
  constexpr int STROKE_JOIN_DIST = 30;
  constexpr int DOOR_TAP_SLOP = 8;  // a touch on the door that moves further is drawing
  enum class Hold { NONE, STROKE, BALL, PET, DOOR, BUTTON, IGNORE };
  static Hold hold = Hold::NONE;
  static bool strokeLifted = false;
  static int lastX, lastY, downX, downY;
  static float rubbed, rubbedAtLastRub;
  static uint32_t strokeEndedAt, lastFrame, lastTick = millis(), lastSave = millis();

  uint32_t now = millis();
  int x = 0, y = 0;
  bool touched = readTouch(x, y);
  int rx = x - CANVAS_X, ry = y;
  bool inRoom = touched && x >= CANVAS_X && x < TOOLBAR_X;

  if (!touched) {
    if (hold == Hold::STROKE) {
      strokeLifted = true;
      strokeEndedAt = now;
    }
    if (hold == Hold::BALL && heldBall >= 0) balls[heldBall].held = false;
    if (hold == Hold::PET && rubbed < 8) boop(now);
    if (hold == Hold::DOOR) requestVisit(now);
    hold = Hold::NONE;
    heldBall = -1;
  } else {
    if (hold == Hold::NONE && strokeLifted) {
      strokeLifted = false;
      const Pt &last = stroke[max(0, strokeLen - 1)];
      if (inRoom && strokeLen && abs(rx - last.x) + abs(ry - last.y) <= STROKE_JOIN_DIST) {
        hold = Hold::STROKE;
      } else {
        finishStroke();
      }
    }
    if (hold == Hold::NONE) {
      if (picking) {
        hold = Hold::BUTTON;
        pickerTap(x, y, now);
      } else if (infoPage >= 0) {
        hold = Hold::BUTTON;
        infoTap(x, y, now);
      } else if (!inRoom) {
        if (now - strokeEndedAt < STROKE_GRACE_MS) {
          hold = Hold::IGNORE;
        } else {
          hold = Hold::BUTTON;
          uiTap(x, y, now);
        }
      } else if (touchesPet(rx, ry)) {
        hold = Hold::PET;
        rubbed = rubbedAtLastRub = 0;
        petPressed(now);
      } else if ((heldBall = ballAt(rx, ry)) >= 0) {
        hold = Hold::BALL;
        balls[heldBall].held = true;
        fingerX = rx;
        fingerY = ry;
      } else if (door.used && doorRect().contains(rx, ry)) {
        hold = Hold::DOOR;
      } else {
        hold = Hold::STROKE;
        strokeLen = 0;
      }
      lastX = downX = rx;
      lastY = downY = ry;
    }
    if (hold == Hold::DOOR && abs(rx - downX) + abs(ry - downY) > DOOR_TAP_SLOP) {
      hold = Hold::STROKE;  // drawing over the door, not tapping it
      strokeLen = 0;
      strokeAdd(downX, downY);
    }
    switch (hold) {
      case Hold::STROKE:
        if (inRoom) {
          strokeAdd(rx, ry);
        } else {
          finishStroke();  // slid off the room: that stroke's done
        }
        break;
      case Hold::BALL:
        fingerX = constrain(rx, 0, ROOM_SIZE - 1);
        fingerY = constrain(ry, 0, ROOM_SIZE - 1);
        break;
      case Hold::PET:
        rubbed += abs(rx - lastX) + abs(ry - lastY);
        if (rubbed - rubbedAtLastRub >= 12) {
          rubbedAtLastRub = rubbed;
          rub(now);
        }
        break;
      default:
        break;
    }
    lastX = rx;
    lastY = ry;
  }

  if (strokeLifted && now - strokeEndedAt > STROKE_JOIN_MS) {
    strokeLifted = false;
    finishStroke();
  }
  bool drawing = hold == Hold::STROKE || strokeLifted;

  if (confirmClear && now - confirmClearAt > 3000) {
    confirmClear = false;
    if (!overlayOpen()) drawButtonAt(CLEAR_BUTTON_Y, BUTTON_H, Icon::TRASH);
  }

  if (!overlayOpen() && now - lastFrame >= FRAME_MS) {
    lastFrame = now;
    frame(now);
  }

  while (now - lastTick >= 1000) {
    lastTick += 1000;
    statsTick(1);
    if (!overlayOpen()) drawMeters(false);
  }
  if (now - lastSave >= 60000) {
    lastSave = now;
    statsSave();
  }
  if (roomDirty && !drawing && now - roomChangedAt > 5000) {
    roomDirty = false;
    roomSave();
  }
}
