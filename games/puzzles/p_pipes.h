#pragma once
// PIPES — turn every tile until the whole network hangs off the source. The
// layout is a random spanning tree, so a perfect solution always exists, and
// because rotation preserves each tile's stub count, "every tile reachable
// from the source" is exactly the same thing as "no loose ends anywhere".
#include "shared.h"

namespace pz_pipes {

using namespace pt;
using namespace pz;

constexpr int MAXCELLS = 36;
constexpr int DX[4] = {0, 1, 0, -1};  // N, E, S, W
constexpr int DY[4] = {-1, 0, 1, 0};

uint8_t mask[MAXCELLS];   // live stub bitmask, bit d = open toward DX/DY[d]
uint8_t goal[MAXCELLS];   // the solved layout, kept for par only
bool lit[MAXCELLS];       // reachable from the source this frame
int cols, rows, root, cursor;
Board board;
Stepper step;
float t;

uint8_t rotCW(uint8_t m) { return (uint8_t)(((m << 1) | (m >> 3)) & 15); }
uint8_t rotCCW(uint8_t m) { return (uint8_t)(((m >> 1) | (m << 3)) & 15); }

int bits(uint8_t m) {
  int n = 0;
  for (int d = 0; d < 4; d++)
    if (m & (1 << d)) n++;
  return n;
}

// Randomised depth-first spanning tree over the whole grid.
void buildTree() {
  int cells = cols * rows;
  bool seen[MAXCELLS] = {};
  int stack[MAXCELLS], sp = 0;
  for (int i = 0; i < cells; i++) mask[i] = 0;
  root = randRange(0, cells - 1);
  seen[root] = true;
  stack[sp++] = root;
  while (sp > 0) {
    int cur = stack[sp - 1];
    int x = cur % cols, y = cur / cols;
    int cand[4], nc = 0;
    for (int d = 0; d < 4; d++) {
      int nx = x + DX[d], ny = y + DY[d];
      if (nx < 0 || nx >= cols || ny < 0 || ny >= rows) continue;
      if (seen[ny * cols + nx]) continue;
      cand[nc++] = d;
    }
    if (nc == 0) {
      sp--;
      continue;
    }
    int d = cand[randRange(0, nc - 1)];
    int nb = (y + DY[d]) * cols + (x + DX[d]);
    mask[cur] = (uint8_t)(mask[cur] | (1 << d));
    mask[nb] = (uint8_t)(mask[nb] | (1 << ((d + 2) % 4)));
    seen[nb] = true;
    stack[sp++] = nb;
  }
}

int turnsToGoal(int i) {
  uint8_t m = mask[i];
  for (int k = 0; k < 4; k++) {
    if (m == goal[i]) return mini(k, 4 - k);
    m = rotCW(m);
  }
  return 0;
}

// Flood the network from the source, counting only stubs that face each other.
void power() {
  int cells = cols * rows;
  for (int i = 0; i < cells; i++) lit[i] = false;
  int queue[MAXCELLS], head = 0, tail = 0;
  lit[root] = true;
  queue[tail++] = root;
  while (head < tail) {
    int cur = queue[head++];
    int x = cur % cols, y = cur / cols;
    for (int d = 0; d < 4; d++) {
      if (!(mask[cur] & (1 << d))) continue;
      int nx = x + DX[d], ny = y + DY[d];
      if (nx < 0 || nx >= cols || ny < 0 || ny >= rows) continue;
      int nb = ny * cols + nx;
      if (lit[nb] || !(mask[nb] & (1 << ((d + 2) % 4)))) continue;
      lit[nb] = true;
      queue[tail++] = nb;
    }
  }
}

void start(int stage) {
  cols = rows = stage == 0 ? 5 : 6;
  int cells = cols * rows;
  buildTree();
  for (int i = 0; i < cells; i++) goal[i] = mask[i];
  int turns = 0;
  for (int i = 0; i < cells; i++) {
    int spins = randRange(0, 3);
    for (int k = 0; k < spins; k++) mask[i] = rotCW(mask[i]);
    turns += turnsToGoal(i);
  }
  // A board that lands back on its solution would score itself instantly.
  // Every other tile is already home, so one nudged tile is the whole par.
  while (turns == 0) {
    int i = randRange(0, cells - 1);
    if (bits(mask[i]) == 4) continue;  // a cross looks the same every way up
    mask[i] = rotCW(mask[i]);
    turns = turnsToGoal(i);
  }
  cursor = root;
  t = 0;
  board = fitBoard(cols, rows, 10);
  stepperInit(step);
  beginBoard(turns);
}

void drawPipe(int i, int px, int py, Color c) {
  int half = board.cell / 2;
  int cx = px + half, cy = py + half;
  int rest = board.cell - half;
  if (mask[i] & 1) fillRect(cx - 1, py, 2, half, c);
  if (mask[i] & 2) fillRect(cx, cy - 1, rest, 2, c);
  if (mask[i] & 4) fillRect(cx - 1, cy, 2, rest, c);
  if (mask[i] & 8) fillRect(px, cy - 1, half, 2, c);
  if (i == root) fillRect(cx - 2, cy - 2, 4, 4, c);
  else if (bits(mask[i]) == 1) fillRect(cx - 1, cy - 1, 3, 3, c);
  else fillRect(cx - 1, cy - 1, 2, 2, c);
}

Status update(float dt) {
  t += dt;
  int dx = 0, dy = 0;
  if (stepTilt(step, dt, dx, dy)) {
    int cx = clampi(cursor % cols + dx, 0, cols - 1);
    int cy = clampi(cursor / cols + dy, 0, rows - 1);
    int next = cy * cols + cx;
    if (next != cursor) sfx(SFX_BLIP, 1.5f);
    cursor = next;
  }
  if (input.justDown(BTN_CLICK)) {
    mask[cursor] = rotCW(mask[cursor]);
    moves++;
    sfx(SFX_SELECT, 1.3f);
  }
  if (input.justDown(BTN_UP)) {
    mask[cursor] = rotCCW(mask[cursor]);
    moves++;
    sfx(SFX_SELECT, 0.9f);
  }

  power();
  clear();
  int cells = cols * rows;
  for (int i = 0; i < cells; i++) {
    int px = board.cx(i % cols), py = board.cy(i / cols);
    fillRect(px, py, board.cell, board.cell, rgb(6, 8, 16));
    drawPipe(i, px, py, lit[i] ? rgb(60, 230, 255) : rgb(30, 40, 120));
  }
  fillRect(board.cx(root % cols) + board.cell / 2 - 1,
           board.cy(root / cols) + board.cell / 2 - 1, 2, 2, WHITE);
  cursorCorners(board, cursor % cols, cursor / cols, pulse(t));

  for (int i = 0; i < cells; i++)
    if (!lit[i]) return PLAYING;
  return SOLVED;
}

}  // namespace pz_pipes
