#pragma once
// Shared furniture for the PUZZLES collection: play-area geometry, the tilt
// cursor stepper, the HUD strip and the per-board bookkeeping the shell scores
// from. Each p_*.h next to this file is compiled exactly once, as part of
// games/puzzles/game.cpp, so these are plain definitions rather than a
// header/TU split.
#include "pixeltilt/pixeltilt.h"

namespace pz {

using namespace pt;

// What a puzzle reports back to the shell every frame.
enum Status : uint8_t { PLAYING, SOLVED, FAILED };

// Bookkeeping for the board in progress: `moves` is what the player spent,
// `par` what a tidy solution costs. game.cpp turns the ratio into the
// efficiency half of the board's award.
int moves = 0;
int par = 1;

inline void beginBoard(int parMoves) {
  moves = 0;
  par = parMoves < 1 ? 1 : parMoves;
}

// The shell sets this false while the control hint owns the bottom row, so a
// puzzle with its own readout down there can wait its turn.
bool bottomRowFree = true;

// --- play area ---------------------------------------------------------------
// The HUD keeps the top 8 rows; the hint line, while it is up, keeps the
// bottom 6. Puzzles draw between the two.
constexpr int PLAY_TOP = 9;
constexpr int PLAY_BOT = 57;
constexpr int PLAY_H = PLAY_BOT - PLAY_TOP;

struct Board {
  int x, y, cell, cols, rows;
  int cx(int c) const { return x + c * cell; }
  int cy(int r) const { return y + r * cell; }
};

// Largest square cell that fits the play area, board centered in it.
inline Board fitBoard(int cols, int rows, int maxCell = 64) {
  int cell = mini(PLAY_H / rows, (SCREEN_W - 2) / cols);
  cell = mini(cell, maxCell);
  if (cell < 3) cell = 3;
  Board b;
  b.cols = cols;
  b.rows = rows;
  b.cell = cell;
  b.x = (SCREEN_W - cols * cell) / 2;
  b.y = PLAY_TOP + (PLAY_H - rows * cell) / 2;
  return b;
}

// --- tilt stepping -----------------------------------------------------------
// Tilt to a discrete grid step. A lean past ~0.45 fires immediately, then
// auto-repeats while you hold it over so long traverses don't need ten
// separate leans; coming back near level re-arms. A sloppy diagonal resolves
// to its dominant axis rather than being thrown away, which reads as
// forgiving on a hand-held board.
struct Stepper {
  float first;  // delay before the first auto-repeat
  float next;   // delay between repeats; 0 = one step per lean
  float hold;
  int lastX, lastY;
  bool repeating;
};

inline void stepperInit(Stepper& s, float first = 0.30f, float next = 0.11f) {
  s.first = first;
  s.next = next;
  s.hold = 0;
  s.lastX = 0;
  s.lastY = 0;
  s.repeating = false;
}

inline bool stepTilt(Stepper& s, float dt, int& dx, int& dy) {
  float ax = fabsf_(input.tiltX), ay = fabsf_(input.tiltY);
  float lean = fmaxf_(ax, ay);
  if (lean < 0.25f) {  // level again: the next lean starts a fresh step
    s.lastX = 0;
    s.lastY = 0;
    s.hold = 0;
    s.repeating = false;
    return false;
  }
  if (lean < 0.45f) return false;  // hysteresis band, hold whatever we had

  int nx = 0, ny = 0;
  if (ax >= ay) nx = input.tiltX > 0 ? 1 : -1;
  else          ny = input.tiltY > 0 ? 1 : -1;

  if (nx != s.lastX || ny != s.lastY) {
    s.lastX = nx;
    s.lastY = ny;
    s.hold = 0;
    s.repeating = false;
    dx = nx;
    dy = ny;
    return true;
  }
  if (s.next <= 0.0f) return false;  // one step per lean
  s.hold += dt;
  if (s.hold < (s.repeating ? s.next : s.first)) return false;
  s.hold = 0;
  s.repeating = true;
  dx = nx;
  dy = ny;
  return true;
}

// --- tap on CLICK ------------------------------------------------------------
// The engine's pause gesture is a ~0.7s CLICK hold, and the game sees that
// press too, so anything destructive bound to CLICK has to wait for a quick
// release instead. Same idiom as the Sand II toys' tap-to-reset.
struct Tap {
  float held;  // seconds down, or -1 while idle
};

inline void tapInit(Tap& tap) { tap.held = -1.0f; }

inline bool tapped(Tap& tap, float dt, float maxHold = 0.35f) {
  if (input.justDown(BTN_CLICK)) tap.held = 0.0f;
  if (tap.held >= 0.0f && input.held(BTN_CLICK)) tap.held += dt;
  if (!input.justUp(BTN_CLICK)) return false;
  bool quick = tap.held >= 0.0f && tap.held < maxHold;
  tap.held = -1.0f;
  return quick;
}

// --- text --------------------------------------------------------------------
inline int numToStr(int32_t n, char* buf) {
  int i = 0;
  if (n < 0) { buf[i++] = '-'; n = -n; }
  char tmp[12];
  int t = 0;
  do { tmp[t++] = (char)('0' + n % 10); n /= 10; } while (n > 0);
  while (t > 0) buf[i++] = tmp[--t];
  buf[i] = 0;
  return i;
}

inline void drawNum(int x, int y, int32_t n, Color c) {
  char buf[14];
  numToStr(n, buf);
  text(x, y, buf, c);
}

inline void textRight(int rightX, int y, const char* s, Color c) {
  text(rightX - textWidth(s), y, s, c);
}

inline void numRight(int rightX, int y, int32_t n, Color c) {
  char buf[14];
  numToStr(n, buf);
  textRight(rightX, y, buf, c);
}

// --- chrome ------------------------------------------------------------------
// Difficulty tint, 1..4. Green reads as "warm up", magenta as "bring a pen".
const Color TIER_TINT[4] = {
    rgb(60, 220, 90), rgb(40, 210, 255), rgb(255, 200, 40), rgb(255, 70, 190),
};

inline Color tierTint(int tier) { return TIER_TINT[clampi(tier - 1, 0, 3)]; }

// Dim a color toward black without losing its hue — the panel collapses
// low-value pixels to one flat block, so unlit states stay saturated.
inline Color dim(Color c, int num, int den) {
  return rgb((uint8_t)(c.r * num / den), (uint8_t)(c.g * num / den),
             (uint8_t)(c.b * num / den));
}

// Cursor outline: pulses so it stays findable against a busy board.
inline Color pulse(float t) {
  float k = 0.65f + 0.35f * sinf_(t * 7.0f);
  return rgb((uint8_t)(255 * k), (uint8_t)(255 * k), (uint8_t)(255 * k));
}

// Name and board number on the left, moves spent on the right.
inline void hudTop(const char* name, int board, Color tint) {
  fillRect(0, 0, SCREEN_W, 7, rgb(8, 8, 18));
  hline(0, 7, SCREEN_W, rgb(26, 26, 44));
  text(2, 1, name, tint);
  drawNum(4 + textWidth(name), 1, board, rgb(90, 90, 110));
  numRight(SCREEN_W - 2, 1, moves, WHITE);
}

// One-line control reminder along the bottom, faded out once you have had a
// few seconds to read it.
inline void hudHint(const char* hint, float age) {
  if (age <= 0.0f || !hint) return;
  float k = age > 1.0f ? 1.0f : age;
  Color c = rgb((uint8_t)(90 * k), (uint8_t)(90 * k), (uint8_t)(110 * k));
  textCentered(SCREEN_H - 6, hint, c);
}

// "LABEL 1234" centred as one unit, with the label and the value tinted
// separately so the number is what the eye lands on.
inline void labelText(int y, const char* label, const char* value, Color lc,
                      Color vc) {
  int w = textWidth(label) + 3 + textWidth(value);
  int x = (SCREEN_W - w) / 2;
  text(x, y, label, lc);
  text(x + textWidth(label) + 3, y, value, vc);
}

inline void labelNum(int y, const char* label, int32_t n, Color lc, Color nc) {
  char buf[14];
  numToStr(n, buf);
  labelText(y, label, buf, lc, nc);
}

// Corner-tick cursor, for boards whose art runs right up to the cell edge and
// would be cut by a full outline.
inline void cursorCorners(const Board& b, int c, int r, Color col) {
  int x = b.cx(c), y = b.cy(r), s = b.cell;
  hline(x, y, 2, col);
  vline(x, y, 2, col);
  hline(x + s - 2, y, 2, col);
  vline(x + s - 1, y, 2, col);
  hline(x, y + s - 1, 2, col);
  vline(x, y + s - 2, 2, col);
  hline(x + s - 2, y + s - 1, 2, col);
  vline(x + s - 1, y + s - 2, 2, col);
}

// Panel used by the solved / failed banners.
inline void banner(int y, int h, Color edge) {
  fillRect(6, y, SCREEN_W - 12, h, rgb(6, 6, 14));
  rect(6, y, SCREEN_W - 12, h, edge);
}

}  // namespace pz
