#include "strokes.h"

#include <algorithm>

void brushLine(int x0, int y0, int x1, int y1, uint16_t col) {
  tft.setViewport(CANVAS_X, 0, CANVAS_SIZE, CANVAS_SIZE);
  int steps = max(1, max(abs(x1 - x0), abs(y1 - y0)) / 2);
  for (int i = 0; i <= steps; i++) {
    tft.fillCircle(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, BRUSH_R, col);
  }
  tft.resetViewport();
}

String pathFor(int id) {
  char buf[20];
  snprintf(buf, sizeof(buf), "/d/%04d.bin", id);
  return buf;
}

std::vector<int> listDrawings() {
  std::vector<int> ids;
  File dir = LittleFS.open("/d");
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String name = f.name();
    int id = name.substring(name.lastIndexOf('/') + 1).toInt();
    if (id > 0) ids.push_back(id);
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}
