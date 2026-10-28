#include "draw_app.h"

#include "led.h"
#include "strokes.h"
#include "touch.h"
#include "ui.h"

// The drawing being edited. Allocated when the app starts so the pet app doesn't pay for it.
constexpr size_t MAX_PTS = 16000;
static Pt *pts;
static size_t numPts = 0;
static int colour = 0;
static int savedId = -1;  // file the current drawing was last saved to, or -1

static StrokeDrawer live;

static void clearCanvas() { tft.fillRect(CANVAS_X, 0, CANVAS_SIZE, CANVAS_SIZE, TFT_WHITE); }

static void addPoint(int x, int y) {
  if (numPts >= MAX_PTS - 1) return;  // keep room for the final PEN_UP
  Pt p{(uint8_t)constrain(x - CANVAS_X, 0, CANVAS_SIZE - 1),
       (uint8_t)constrain(y, 0, CANVAS_SIZE - 1), (uint8_t)colour};
  if (live.hasPrev && abs(p.x - live.prev.x) + abs(p.y - live.prev.y) < 2) return;
  pts[numPts++] = p;
  live.add(p);
}

static void endStroke() {
  if (numPts && pts[numPts - 1].c != PEN_UP) pts[numPts++] = {0, 0, PEN_UP};
  live.hasPrev = false;
}

static bool saveDrawing() {
  endStroke();
  if (numPts == 0) return false;
  int id = savedId;
  if (id < 0) {
    std::vector<int> ids = listDrawings();
    id = ids.empty() ? 1 : ids.back() + 1;
  }
  File f = LittleFS.open(pathFor(id), "w");
  if (!f) return false;
  size_t bytes = numPts * sizeof(Pt);
  bool ok = f.write((const uint8_t *)pts, bytes) == bytes;
  f.close();
  if (!ok) {
    LittleFS.remove(pathFor(id));
    return false;
  }
  savedId = id;
  Serial.printf("saved %s (%u bytes)\n", pathFor(id).c_str(), bytes);
  return true;
}

// Replays a saved drawing slowly enough to watch it being drawn. The app loop calls
// stepReplay() to draw the points that are due, so buttons keep working meanwhile.
struct Replay {
  File file;
  StrokeDrawer drawer;
  Pt buf[128];
  size_t bufLen = 0, bufPos = 0;
  uint32_t pointDelay = 0, nextAt = 0;
  bool active = false;
};
static Replay replay;

static void stopReplay() {
  if (replay.file) replay.file.close();
  replay.active = false;
}

static void startReplay(int id) {
  constexpr uint32_t MAX_POINT_DELAY_US = 4000;
  constexpr uint32_t MAX_REPLAY_US = 8000000;  // big drawings speed up to finish in ~8s
  stopReplay();
  replay.file = LittleFS.open(pathFor(id), "r");
  if (!replay.file) return;
  size_t total = replay.file.size() / sizeof(Pt);
  replay.pointDelay = total ? min(MAX_POINT_DELAY_US, MAX_REPLAY_US / total) : 0;
  replay.drawer = StrokeDrawer();
  replay.bufLen = replay.bufPos = 0;
  replay.nextAt = micros();
  replay.active = true;
}

static void stepReplay() {
  uint32_t start = millis();  // never hog the loop for long, even when behind schedule
  while (replay.active && (int32_t)(micros() - replay.nextAt) >= 0 && millis() - start < 10) {
    if (replay.bufPos == replay.bufLen) {
      replay.bufLen = replay.file.read((uint8_t *)replay.buf, sizeof(replay.buf)) / sizeof(Pt);
      replay.bufPos = 0;
      if (replay.bufLen == 0) {
        stopReplay();
        return;
      }
    }
    replay.drawer.add(replay.buf[replay.bufPos++]);
    replay.nextAt += replay.pointDelay;
  }
}

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------
enum class Mode { EDITOR, VIEWER };
static Mode mode = Mode::EDITOR;

static const Icon EDITOR_BUTTONS[] = {Icon::NEW, Icon::SAVE, Icon::VIEW};
static const Icon VIEWER_BUTTONS[] = {Icon::PREV, Icon::NEXT, Icon::PLAY, Icon::TRASH,
                                      Icon::PENCIL};
constexpr int NUM_VIEWER_BUTTONS = 5;
constexpr int DEL_BUTTON = 3;

static void showEditor() {
  stopReplay();
  mode = Mode::EDITOR;
  drawPalette(colour);
  for (int i = 0; i < 3; i++) drawButton(i, 3, EDITOR_BUTTONS[i]);
  clearCanvas();
  StrokeDrawer drawer;
  for (size_t i = 0; i < numPts; i++) drawer.add(pts[i]);
}

static std::vector<int> gallery;
static int galleryIdx = 0;
static bool confirmDelete = false;

static void showViewerImage() {
  tft.fillRect(0, 0, PALETTE_W, 240, TFT_DARKGREY);
  clearCanvas();
  tft.setTextDatum(MC_DATUM);
  if (gallery.empty()) {
    stopReplay();
    tft.setTextColor(TFT_DARKGREY, TFT_WHITE);
    tft.drawString("No drawings yet", CANVAS_X + CANVAS_SIZE / 2, 120, 2);
    return;
  }
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.drawNumber(galleryIdx + 1, PALETTE_W / 2, 105, 4);
  tft.drawString("of", PALETTE_W / 2, 125, 1);
  tft.drawNumber(gallery.size(), PALETTE_W / 2, 140, 2);
  startReplay(gallery[galleryIdx]);
}

static void showViewer() {
  mode = Mode::VIEWER;
  confirmDelete = false;
  gallery = listDrawings();
  galleryIdx = gallery.empty() ? 0 : gallery.size() - 1;  // newest first
  for (int i = 0; i < NUM_VIEWER_BUTTONS; i++) {
    drawButton(i, NUM_VIEWER_BUTTONS, VIEWER_BUTTONS[i]);
  }
  showViewerImage();
}

// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------
static void editorTap(int x, int y) {
  if (x < PALETTE_W) {
    colour = constrain(y / SWATCH_H, 0, NUM_COLOURS - 1);
    drawPalette(colour);
    ledSet(COLOURS[colour]);  // the LED on the back shows the colour you're drawing with
  } else if (x >= TOOLBAR_X) {
    switch (y / 80) {
      case 0:  // NEW
        numPts = 0;
        savedId = -1;
        clearCanvas();
        break;
      case 1:  // SAVE
        if (saveDrawing()) {
          flashButton(1, 3, Icon::TICK, TFT_DARKGREEN, Icon::SAVE);
        } else {
          flashButton(1, 3, Icon::CROSS, TFT_RED, Icon::SAVE);  // empty or full
        }
        break;
      case 2:  // VIEW
        endStroke();
        showViewer();
        break;
    }
  }
}

static void viewerTap(int x, int y) {
  if (x < TOOLBAR_X) return;
  int btn = min(y / (240 / NUM_VIEWER_BUTTONS), NUM_VIEWER_BUTTONS - 1);
  if (btn != DEL_BUTTON && confirmDelete) {
    confirmDelete = false;
    drawButton(DEL_BUTTON, NUM_VIEWER_BUTTONS, Icon::TRASH);
  }
  int n = gallery.size();
  switch (btn) {
    case 0:  // <
      if (n) galleryIdx = (galleryIdx + n - 1) % n, showViewerImage();
      break;
    case 1:  // >
      if (n) galleryIdx = (galleryIdx + 1) % n, showViewerImage();
      break;
    case 2:  // PLAY: watch it draw again
      if (n) showViewerImage();
      break;
    case DEL_BUTTON:  // tap twice
      if (!n) break;
      if (!confirmDelete) {
        confirmDelete = true;
        drawButton(DEL_BUTTON, NUM_VIEWER_BUTTONS, Icon::TRASH_OPEN, TFT_RED);
        break;
      }
      stopReplay();
      LittleFS.remove(pathFor(gallery[galleryIdx]));
      if (gallery[galleryIdx] == savedId) savedId = -1;
      gallery.erase(gallery.begin() + galleryIdx);
      galleryIdx = max(0, min(galleryIdx, (int)gallery.size() - 1));
      confirmDelete = false;
      drawButton(DEL_BUTTON, NUM_VIEWER_BUTTONS, Icon::TRASH);
      showViewerImage();
      break;
    case 4:  // BACK
      showEditor();
      break;
  }
}

void drawAppBegin() {
  pts = (Pt *)malloc(MAX_PTS * sizeof(Pt));
  ledSet(COLOURS[colour]);
  showEditor();
}

void drawAppLoop() {
  // A finger lifting for a moment mid-stroke looks like a new touch. If that lands on a
  // button straight after drawing, it's ignored rather than changing colour or screen.
  constexpr uint32_t STROKE_GRACE_MS = 300;
  enum class Hold { NONE, CANVAS, BUTTON, IGNORE };
  static Hold hold = Hold::NONE;
  static uint32_t strokeEndedAt = 0;

  stepReplay();
  int x, y;
  if (!readTouch(x, y)) {
    if (hold == Hold::CANVAS) {
      endStroke();
      strokeEndedAt = millis();
    }
    hold = Hold::NONE;
    if (!replay.active) delay(5);
    return;
  }

  bool onCanvas = x >= CANVAS_X && x < TOOLBAR_X;
  if (hold == Hold::NONE) {
    if (mode == Mode::EDITOR && onCanvas) {
      hold = Hold::CANVAS;
    } else if (mode == Mode::EDITOR && millis() - strokeEndedAt < STROKE_GRACE_MS) {
      hold = Hold::IGNORE;
    } else {
      hold = Hold::BUTTON;  // buttons fire once on press, then wait for release
      mode == Mode::EDITOR ? editorTap(x, y) : viewerTap(x, y);
    }
  }
  if (hold == Hold::CANVAS) {
    if (onCanvas) {
      addPoint(x, y);
    } else {
      endStroke();  // lift the pen off the edge; drawing resumes if the finger comes back
    }
  }
}
