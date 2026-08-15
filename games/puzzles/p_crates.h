#pragma once
// CRATES — Sokoban. Tilt to walk, shove crates onto the green pads, UP to take
// a move back, and tap CLICK to deal the level again from the start. The eight
// boards are hand-built and their par is the optimal solution length from a
// breadth-first search, so par really is perfect play. A crate wedged in a
// corner turns red: nothing is lost, but only UP fixes it.
#include "shared.h"

namespace pz_crates {

using namespace pt;
using namespace pz;

constexpr int MAXCELLS = 80;
constexpr int MAXHIST = 128;

struct Level {
  uint8_t w, h;
  uint16_t par;
  const char* cells;
};

constexpr char L1[] = "#######"
                      "#     #"
                      "# $ . #"
                      "#  @  #"
                      "#######";
constexpr char L2[] = "#########"
                      "#       #"
                      "# ##### #"
                      "# #...# #"
                      "# #$$$# #"
                      "#   @   #"
                      "#########";
constexpr char L3[] = "#######"
                      "# .   #"
                      "# $   #"
                      "# .$  #"
                      "#   @ #"
                      "#######";
constexpr char L4[] = "########"
                      "#      #"
                      "# $ $  #"
                      "# . .  #"
                      "#  @   #"
                      "########";
constexpr char L5[] = "##########"
                      "#  ....  #"
                      "#  $$$$  #"
                      "#   @    #"
                      "#        #"
                      "##########";
constexpr char L6[] = "#########"
                      "#. .    #"
                      "# $$    #"
                      "#   @   #"
                      "# $$    #"
                      "#. .    #"
                      "#########";
constexpr char L7[] = "########"
                      "#      #"
                      "# .$.  #"
                      "# $@$  #"
                      "# .$.  #"
                      "#      #"
                      "########";
constexpr char L8[] = "#########"
                      "#   #   #"
                      "# $ # . #"
                      "#  $$.  #"
                      "# . #$  #"
                      "#  @# . #"
                      "#########";

static_assert(sizeof(L1) - 1 == 7 * 5, "L1 is not w*h cells");
static_assert(sizeof(L2) - 1 == 9 * 7, "L2 is not w*h cells");
static_assert(sizeof(L3) - 1 == 7 * 6, "L3 is not w*h cells");
static_assert(sizeof(L4) - 1 == 8 * 6, "L4 is not w*h cells");
static_assert(sizeof(L5) - 1 == 10 * 6, "L5 is not w*h cells");
static_assert(sizeof(L6) - 1 == 9 * 7, "L6 is not w*h cells");
static_assert(sizeof(L7) - 1 == 8 * 7, "L7 is not w*h cells");
static_assert(sizeof(L8) - 1 == 9 * 7, "L8 is not w*h cells");

// Ordered by the search's optimal move count: 5, 8, 8, 9, 11, 12, 23, 43.
const Level LEVELS[8] = {
    {7, 5, 5, L1},   {9, 7, 8, L2},   {7, 6, 8, L3},  {8, 6, 9, L4},
    {10, 6, 11, L5}, {9, 7, 12, L6},  {8, 7, 23, L7}, {9, 7, 43, L8},
};
constexpr int LEVEL_COUNT = 8;

bool wall[MAXCELLS], goal[MAXCELLS], crate[MAXCELLS];
int w, h, player;
int8_t histDir[MAXHIST];
bool histPush[MAXHIST];
int hist, board_;  // board_ = the board number this level came from, for reset
Board board;
Stepper step;
Tap tap;
float t;

void start(int stage) {
  board_ = stage;
  const Level& lv = LEVELS[stage % LEVEL_COUNT];
  w = lv.w;
  h = lv.h;
  player = 0;
  for (int i = 0; i < w * h; i++) {
    char c = lv.cells[i];
    wall[i] = c == '#';
    goal[i] = c == '.' || c == '*' || c == '+';
    crate[i] = c == '$' || c == '*';
    if (c == '@' || c == '+') player = i;
  }
  hist = 0;
  t = 0;
  board = fitBoard(w, h, 9);
  stepperInit(step, 0.30f, 0.13f);
  tapInit(tap);
  beginBoard(lv.par);
}

bool solved() {
  for (int i = 0; i < w * h; i++)
    if (crate[i] && !goal[i]) return false;
  return true;
}

// A crate off its pad with walls on two perpendicular sides can never move
// again. Flagging it is friendlier than letting the board quietly die.
bool stuck(int i) {
  if (goal[i]) return false;
  int x = i % w, y = i / w;
  bool up = y == 0 || wall[i - w], down = y == h - 1 || wall[i + w];
  bool left = x == 0 || wall[i - 1], right = x == w - 1 || wall[i + 1];
  return (up || down) && (left || right);
}

void walk(int dx, int dy) {
  int nx = player % w + dx, ny = player / w + dy;
  if (nx < 0 || nx >= w || ny < 0 || ny >= h) return;
  int next = ny * w + nx;
  if (wall[next]) return;
  bool pushed = false;
  if (crate[next]) {
    int bx = nx + dx, by = ny + dy;
    if (bx < 0 || bx >= w || by < 0 || by >= h) return;
    int beyond = by * w + bx;
    if (wall[beyond] || crate[beyond]) return;
    crate[next] = false;
    crate[beyond] = true;
    pushed = true;
  }
  if (hist == MAXHIST) {  // oldest move falls off the end of the undo trail
    for (int i = 1; i < MAXHIST; i++) {
      histDir[i - 1] = histDir[i];
      histPush[i - 1] = histPush[i];
    }
    hist--;
  }
  histDir[hist] = (int8_t)(dy * w + dx);
  histPush[hist] = pushed;
  hist++;
  player = next;
  moves++;
  sfx(pushed ? SFX_BOUNCE : SFX_BLIP, pushed ? 0.8f : 1.6f);
}

void undo() {
  if (hist == 0) {
    sfx(SFX_HURT, 1.8f);
    return;
  }
  hist--;
  int d = histDir[hist];
  if (histPush[hist]) {
    crate[player + d] = false;
    crate[player] = true;
  }
  player -= d;
  if (moves > 0) moves--;
  sfx(SFX_BLIP, 0.7f);
}

Status update(float dt) {
  t += dt;
  int dx = 0, dy = 0;
  if (stepTilt(step, dt, dx, dy)) walk(dx, dy);
  if (input.justDown(BTN_UP)) undo();
  // A tap deals the same level again. It has to be the release rather than
  // the press, or holding CLICK for the pause menu would wipe the board on
  // the way in.
  if (tapped(tap, dt)) {
    start(board_);
    sfx(SFX_SELECT, 0.7f);
  }

  clear();
  int s = board.cell;
  for (int i = 0; i < w * h; i++) {
    int px = board.cx(i % w), py = board.cy(i / w);
    if (wall[i]) {
      fillRect(px, py, s, s, rgb(58, 58, 74));
      fillRect(px + 1, py + 1, s - 2, s - 2, rgb(34, 34, 46));
      continue;
    }
    if (goal[i]) {
      // Outline rather than a dark fill: anything under about code 24 lands
      // in the panel's black hole.
      rect(px + 1, py + 1, s - 2, s - 2, rgb(30, 140, 60));
      fillRect(px + s / 2 - 1, py + s / 2 - 1, 2, 2, rgb(60, 220, 110));
    }
    if (crate[i]) {
      Color c = goal[i] ? rgb(60, 230, 110) : stuck(i) ? rgb(230, 40, 40)
                                                       : rgb(230, 140, 30);
      fillRect(px + 1, py + 1, s - 2, s - 2, c);
      fillRect(px + 2, py + 2, s - 4, s - 4, dim(c, 1, 3));
    }
  }
  int px = board.cx(player % w), py = board.cy(player / w);
  fillRect(px + 2, py + 1, s - 4, s - 2, rgb(60, 190, 255));
  fillRect(px + 1, py + 2, s - 2, s - 4, rgb(60, 190, 255));
  fillRect(px + 2, py + 2, s - 4, s - 4, rgb(200, 245, 255));

  return solved() ? SOLVED : PLAYING;
}

}  // namespace pz_crates
