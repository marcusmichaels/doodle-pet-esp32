// Strokes: points in canvas coordinates, PEN_UP separates strokes. Saved drawings
// are raw Pt arrays in LittleFS: /d/0001.bin, /d/0002.bin, ...
#pragma once
#include <Arduino.h>
#include <LittleFS.h>

#include <vector>

#include "ui.h"

struct Pt {
  uint8_t x, y, c;
};
constexpr uint8_t PEN_UP = 0xFF;
constexpr int BRUSH_R = 3;

// Brush line on screen, in canvas coordinates.
void brushLine(int x0, int y0, int x1, int y1, uint16_t col);

// Calls plot(x, y) for every pixel a brush line of radius r covers (may repeat pixels).
template <typename F>
void forEachBrushPixel(int x0, int y0, int x1, int y1, int r, F plot) {
  int steps = max(1, max(abs(x1 - x0), abs(y1 - y0)) / 2);
  for (int i = 0; i <= steps; i++) {
    int cx = x0 + (x1 - x0) * i / steps, cy = y0 + (y1 - y0) * i / steps;
    for (int dy = -r; dy <= r; dy++) {
      for (int dx = -r; dx <= r; dx++) {
        if (dx * dx + dy * dy <= r * r + r) plot(cx + dx, cy + dy);
      }
    }
  }
}

// Draws a stream of points on screen, joining each to the previous one in its stroke.
struct StrokeDrawer {
  bool hasPrev = false;
  Pt prev;
  void add(Pt p) {
    if (p.c == PEN_UP || p.c >= NUM_COLOURS) {
      hasPrev = false;
      return;
    }
    const Pt &from = hasPrev ? prev : p;
    brushLine(from.x, from.y, p.x, p.y, COLOURS[p.c]);
    prev = p;
    hasPrev = true;
  }
};

String pathFor(int id);
std::vector<int> listDrawings();

// Calls fn(Pt) for every point of saved drawing `id`.
template <typename F>
void forEachSavedPoint(int id, F fn) {
  fs::File f = LittleFS.open(pathFor(id), "r");
  if (!f) return;
  Pt buf[128];
  size_t n;
  while ((n = f.read((uint8_t *)buf, sizeof(buf)) / sizeof(Pt)) > 0) {
    for (size_t i = 0; i < n; i++) fn(buf[i]);
  }
  f.close();
}
