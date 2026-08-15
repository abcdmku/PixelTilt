#pragma once
// LIGHTS — Lights Out. Pressing a cell flips it and its four neighbours; get
// the whole board dark. Boards are scrambled from a solved grid, so every one
// is solvable and the scramble count doubles as par.
#include "shared.h"

namespace pz_lights {

using namespace pt;
using namespace pz;

constexpr int MAXN = 5;

uint8_t on[MAXN * MAXN];
int n, cx, cy;
Board board;
Stepper step;
float t;

void press(int x, int y) {
  const int DX[5] = {0, 1, -1, 0, 0};
  const int DY[5] = {0, 0, 0, 1, -1};
  for (int i = 0; i < 5; i++) {
    int tx = x + DX[i], ty = y + DY[i];
    if (tx >= 0 && tx < n && ty >= 0 && ty < n) on[ty * n + tx] ^= 1;
  }
}

bool anyLit() {
  for (int i = 0; i < n * n; i++)
    if (on[i]) return true;
  return false;
}

void start(int stage) {
  n = stage == 0 ? 4 : 5;
  int scramble = mini(4 + stage * 2, 12);
  for (int i = 0; i < MAXN * MAXN; i++) on[i] = 0;
  for (int i = 0; i < scramble; i++)
    press(randRange(0, n - 1), randRange(0, n - 1));
  // Presses cancel, so a scramble can land back on a dark board.
  while (!anyLit()) press(randRange(0, n - 1), randRange(0, n - 1));
  cx = n / 2;
  cy = n / 2;
  t = 0;
  board = fitBoard(n, n, 11);
  stepperInit(step);
  beginBoard(scramble);
}

Status update(float dt) {
  t += dt;
  int dx = 0, dy = 0;
  if (stepTilt(step, dt, dx, dy)) {
    int nx = clampi(cx + dx, 0, n - 1), ny = clampi(cy + dy, 0, n - 1);
    if (nx != cx || ny != cy) sfx(SFX_BLIP, 1.5f);
    cx = nx;
    cy = ny;
  }
  if (input.justDown(BTN_CLICK)) {
    press(cx, cy);
    moves++;
    sfx(SFX_SELECT, on[cy * n + cx] ? 1.2f : 0.9f);
  }

  clear();
  for (int y = 0; y < n; y++) {
    for (int x = 0; x < n; x++) {
      int px = board.cx(x), py = board.cy(y);
      bool lit = on[y * n + x] != 0;
      fillRect(px + 1, py + 1, board.cell - 2, board.cell - 2,
               lit ? rgb(255, 190, 30) : rgb(18, 24, 70));
      if (lit) {
        // A darker core keeps the lit cells from smearing into one block.
        fillRect(px + 3, py + 3, board.cell - 6, board.cell - 6,
                 rgb(255, 240, 140));
      }
    }
  }
  rect(board.cx(cx), board.cy(cy), board.cell, board.cell, pulse(t));

  return anyLit() ? PLAYING : SOLVED;
}

}  // namespace pz_lights
