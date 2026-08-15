#pragma once
// MERGE — tilt the whole board and equal tiles fuse. Each board sets a bigger
// goal tile; the run ends when the grid jams with no legal move left. The
// goal line replaces the shell's control hint, which is why this puzzle
// registers no hint of its own.
//
// A move settles the grid immediately and then plays back how it got there:
// shift() records where every tile came from, and the next tenth of a second
// draws those tiles sliding rather than the settled grid. Without it the board
// teleports and you cannot see which tiles fused.
#include "shared.h"

namespace pz_merge {

using namespace pt;
using namespace pz;

constexpr float SLIDE_TIME = 0.10f;
constexpr float FLASH_TIME = 0.14f;

// One tile's journey across a single move. A fusing pair records two of these
// onto the same destination, so both halves visibly travel there.
struct Slide {
  uint8_t from, to;
  uint16_t value;  // value on the way, before any fuse lands
};

uint16_t cell[16];
Slide slides[16];
bool fused[16];  // destinations that fused this move, for the landing flash
int slideCount;
int target, bestTile;
float t, slideT, flashT;
Board board;
Stepper step;

void spawn() {
  int empty[16], n = 0;
  for (int i = 0; i < 16; i++)
    if (!cell[i]) empty[n++] = i;
  if (!n) return;
  cell[empty[randRange(0, n - 1)]] = randRange(0, 9) == 0 ? 4 : 2;
}

// dir: 0 up, 1 right, 2 down, 3 left. Lines are gathered destination-edge
// first so the compact-then-merge pass is the same for all four directions.
bool shift(int dir) {
  bool changed = false;
  slideCount = 0;
  for (int i = 0; i < 16; i++) fused[i] = false;
  for (int i = 0; i < 4; i++) {
    int idx[4];
    for (int k = 0; k < 4; k++) {
      switch (dir) {
        case 0:  idx[k] = k * 4 + i; break;
        case 2:  idx[k] = (3 - k) * 4 + i; break;
        case 3:  idx[k] = i * 4 + k; break;
        default: idx[k] = i * 4 + (3 - k); break;
      }
    }
    uint16_t packed[4];
    int src[4];
    int n = 0;
    for (int k = 0; k < 4; k++) {
      if (!cell[idx[k]]) continue;
      src[n] = idx[k];
      packed[n] = cell[idx[k]];
      n++;
    }
    uint16_t out[4];
    int m = 0;
    for (int k = 0; k < n; k++) {
      int dest = idx[m];
      bool fuse = k + 1 < n && packed[k] == packed[k + 1];
      slides[slideCount++] = {(uint8_t)src[k], (uint8_t)dest, packed[k]};
      if (fuse) {
        slides[slideCount++] = {(uint8_t)src[k + 1], (uint8_t)dest, packed[k]};
        fused[dest] = true;
        out[m++] = (uint16_t)(packed[k] * 2);
        k++;
      } else {
        out[m++] = packed[k];
      }
    }
    while (m < 4) out[m++] = 0;
    for (int k = 0; k < 4; k++) {
      if (cell[idx[k]] != out[k]) changed = true;
      cell[idx[k]] = out[k];
    }
  }
  return changed;
}

bool jammed() {
  for (int i = 0; i < 16; i++) {
    if (!cell[i]) return false;
    int x = i % 4, y = i / 4;
    if (x < 3 && cell[i] == cell[i + 1]) return false;
    if (y < 3 && cell[i] == cell[i + 4]) return false;
  }
  return true;
}

void start(int stage) {
  for (int i = 0; i < 16; i++) {
    cell[i] = 0;
    fused[i] = false;
  }
  target = 32 << mini(stage, 4);
  bestTile = 0;
  slideCount = 0;
  t = 0;
  slideT = 0;
  flashT = 0;
  spawn();
  spawn();
  board = fitBoard(4, 4, 12);
  stepperInit(step, 0.38f, 0.20f);
  beginBoard(target * 3 / 4);
}

// 2..512 fit the cell as digits; past that they get a K suffix.
void tileLabel(int v, char* buf) {
  if (v < 1024) {
    numToStr(v, buf);
    return;
  }
  int k = numToStr(v / 1024, buf);
  buf[k] = 'K';
  buf[k + 1] = 0;
}

void drawTile(int px, int py, int v, bool flash) {
  int s = board.cell - 1;
  int rank = 0;
  while ((2 << rank) < v) rank++;  // 2 -> 0, 4 -> 1, ...
  Color c = hsv(rank * 32.0f, 0.9f, 1.0f);
  fillRect(px, py, s, s, dim(c, 1, 4));
  rect(px, py, s, s, flash ? WHITE : dim(c, 1, 2));
  char buf[6];
  tileLabel(v, buf);
  text(px + (s - textWidth(buf)) / 2, py + (s - 5) / 2, buf, c);
}

Status update(float dt) {
  t += dt;
  if (flashT > 0) flashT -= dt;

  if (slideT > 0) {
    slideT -= dt;
    if (slideT <= 0) {  // tiles have landed: now the new one turns up
      spawn();
      flashT = FLASH_TIME;
      sfx(SFX_BLIP, 1.2f);
    }
  } else {
    int dx = 0, dy = 0;
    if (stepTilt(step, dt, dx, dy)) {
      int dir = dx > 0 ? 1 : dx < 0 ? 3 : dy > 0 ? 2 : 0;
      if (shift(dir)) {
        slideT = SLIDE_TIME;
        moves++;
      }
    }
  }

  bestTile = 0;
  for (int i = 0; i < 16; i++)
    if (cell[i] > bestTile) bestTile = cell[i];

  clear();
  for (int i = 0; i < 16; i++)
    fillRect(board.cx(i % 4), board.cy(i / 4), board.cell - 1, board.cell - 1,
             rgb(12, 12, 24));

  if (slideT > 0) {
    // Mid-move: draw the journeys, not the settled grid, or fused tiles would
    // pop into their doubled value before their halves arrive.
    float k = 1.0f - slideT / SLIDE_TIME;
    k = k * (2.0f - k);  // ease out
    for (int i = 0; i < slideCount; i++) {
      const Slide& s = slides[i];
      float px = lerpf((float)board.cx(s.from % 4), (float)board.cx(s.to % 4), k);
      float py = lerpf((float)board.cy(s.from / 4), (float)board.cy(s.to / 4), k);
      drawTile((int)(px + 0.5f), (int)(py + 0.5f), s.value, false);
    }
  } else {
    for (int i = 0; i < 16; i++)
      if (cell[i])
        drawTile(board.cx(i % 4), board.cy(i / 4), cell[i],
                 flashT > 0 && fused[i]);
  }

  // Goal readout, greening once the board has produced the tile. It shares the
  // bottom row with the shell's control hint, which gets there first.
  if (bottomRowFree) {
    char goal[8];
    tileLabel(target, goal);
    labelText(SCREEN_H - 6, "GOAL", goal, rgb(70, 70, 90),
              bestTile >= target ? GREEN : rgb(150, 150, 180));
  }

  if (slideT > 0) return PLAYING;  // let the move finish before judging it
  if (bestTile >= target) return SOLVED;
  return jammed() ? FAILED : PLAYING;
}

}  // namespace pz_merge
