#pragma once
// SLIDE — the sliding-tile classic. Tilt and the tiles fall toward the low
// edge, so the gap travels the other way. Boards are shuffled with legal
// moves only, which keeps every one solvable and makes the shuffle length a
// fair par.
#include "shared.h"

namespace pz_slide {

using namespace pt;
using namespace pz;

constexpr int MAXN = 4;
constexpr int DX[4] = {0, 1, 0, -1};  // up, right, down, left
constexpr int DY[4] = {-1, 0, 1, 0};

uint8_t tile[MAXN * MAXN];
int n, blank;
Board board;
Stepper step;
float t;

bool solved() {
  int last = n * n - 1;
  for (int i = 0; i < last; i++)
    if (tile[i] != i + 1) return false;
  return tile[last] == 0;
}

// Slide the tile that sits `dir` away from the gap into it.
bool slideBlank(int dir) {
  int bx = blank % n + DX[dir], by = blank / n + DY[dir];
  if (bx < 0 || bx >= n || by < 0 || by >= n) return false;
  int from = by * n + bx;
  tile[blank] = tile[from];
  tile[from] = 0;
  blank = from;
  return true;
}

void start(int stage) {
  n = stage == 0 ? 3 : 4;
  for (int i = 0; i < n * n - 1; i++) tile[i] = (uint8_t)(i + 1);
  tile[n * n - 1] = 0;
  blank = n * n - 1;

  int shuffle = mini(10 + stage * 8, 44);
  int last = -1;
  for (int i = 0; i < shuffle; i++) {
    int pick[4], np = 0;
    for (int d = 0; d < 4; d++) {
      if (last >= 0 && d == (last + 2) % 4) continue;
      int bx = blank % n + DX[d], by = blank / n + DY[d];
      if (bx >= 0 && bx < n && by >= 0 && by < n) pick[np++] = d;
    }
    int d = pick[randRange(0, np - 1)];
    slideBlank(d);
    last = d;
  }
  while (solved()) slideBlank(randRange(0, 3));

  t = 0;
  board = fitBoard(n, n, 16);
  stepperInit(step, 0.30f, 0.14f);
  beginBoard(shuffle);
}

Status update(float dt) {
  t += dt;
  int dx = 0, dy = 0;
  if (stepTilt(step, dt, dx, dy)) {
    // Tiles chase the tilt, so the gap walks against it.
    int dir = dx > 0 ? 3 : dx < 0 ? 1 : dy > 0 ? 0 : 2;
    if (slideBlank(dir)) {
      moves++;
      sfx(SFX_BLIP, 1.1f);
    } else {
      sfx(SFX_HURT, 1.8f);
    }
  }

  clear();
  char buf[4];
  for (int i = 0; i < n * n; i++) {
    int px = board.cx(i % n), py = board.cy(i / n);
    int s = board.cell - 1;
    int v = tile[i];
    if (v == 0) {
      rect(px, py, s, s, rgb(26, 26, 40));
      continue;
    }
    bool home = v == i + 1;
    fillRect(px, py, s, s, home ? rgb(10, 34, 22) : rgb(16, 16, 38));
    rect(px, py, s, s, home ? rgb(24, 90, 50) : rgb(34, 34, 64));
    numToStr(v, buf);
    int w = textWidth(buf);
    text(px + (s - w) / 2, py + (s - 5) / 2, buf,
         hsv(v * 300.0f / (n * n - 1), 0.85f, 1.0f));
  }
  // The gap gets the pulse so you can always find it in a busy board.
  rect(board.cx(blank % n), board.cy(blank / n), board.cell - 1, board.cell - 1,
       dim(pulse(t), 1, 3));

  return solved() ? SOLVED : PLAYING;
}

}  // namespace pz_slide
