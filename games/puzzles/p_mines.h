// MINES — Minesweeper on an 8x8 field. CLICK digs, UP plants a flag, and the
// first dig is always safe because the mines are laid after it, around the
// square you opened. Digging a mine ends the run.
#pragma once
#include "shared.h"

namespace pz_mines {

using namespace pt;
using namespace pz;

constexpr int N = 8;
constexpr int CELLS = N * N;

// Classic-ish number colours, pushed to saturated hues the panel can show.
const Color NUM_COLOR[9] = {
    rgb(0, 0, 0),      rgb(70, 160, 255), rgb(60, 230, 90),
    rgb(255, 70, 70),  rgb(200, 110, 255), rgb(255, 170, 40),
    rgb(60, 230, 230), rgb(255, 255, 255), rgb(180, 180, 190),
};

bool mine[CELLS], open[CELLS], flag[CELLS];
uint8_t around[CELLS];
int mines, cursor;
bool placed, exploded;
Board board;
Stepper step;
float t;

void start(int stage) {
  mines = mini(8 + stage * 2, 16);
  for (int i = 0; i < CELLS; i++) {
    mine[i] = open[i] = flag[i] = false;
    around[i] = 0;
  }
  cursor = CELLS / 2 + N / 2;
  placed = false;
  exploded = false;
  t = 0;
  board = fitBoard(N, N, 7);
  stepperInit(step);
  beginBoard(mines * 2 + 12);
}

void layMines(int safe) {
  int sx = safe % N, sy = safe / N;
  for (int laid = 0; laid < mines;) {
    int i = randRange(0, CELLS - 1);
    if (mine[i]) continue;
    if (absi(i % N - sx) <= 1 && absi(i / N - sy) <= 1) continue;
    mine[i] = true;
    laid++;
  }
  for (int i = 0; i < CELLS; i++) {
    int n = 0;
    for (int dy = -1; dy <= 1; dy++) {
      for (int dx = -1; dx <= 1; dx++) {
        int x = i % N + dx, y = i / N + dy;
        if (x < 0 || x >= N || y < 0 || y >= N) continue;
        if (mine[y * N + x]) n++;
      }
    }
    around[i] = (uint8_t)n;
  }
  placed = true;
}

// Open a square, spilling outward through the blanks the way the mouse
// version does.
void dig(int from) {
  int queue[CELLS], head = 0, tail = 0;
  // Opening on the way in doubles as the visited mark, which is what keeps
  // the queue to one slot per square.
  open[from] = true;
  queue[tail++] = from;
  while (head < tail) {
    int i = queue[head++];
    if (around[i] != 0) continue;
    for (int dy = -1; dy <= 1; dy++) {
      for (int dx = -1; dx <= 1; dx++) {
        int x = i % N + dx, y = i / N + dy;
        if (x < 0 || x >= N || y < 0 || y >= N) continue;
        int nb = y * N + x;
        if (open[nb] || flag[nb]) continue;
        open[nb] = true;
        queue[tail++] = nb;
      }
    }
  }
}

bool cleared() {
  for (int i = 0; i < CELLS; i++)
    if (!mine[i] && !open[i]) return false;
  return true;
}

int flagsLeft() {
  int used = 0;
  for (int i = 0; i < CELLS; i++)
    if (flag[i] && !open[i]) used++;
  return mines - used;
}

Status update(float dt) {
  t += dt;
  int dx = 0, dy = 0;
  if (stepTilt(step, dt, dx, dy)) {
    int cx = clampi(cursor % N + dx, 0, N - 1);
    int cy = clampi(cursor / N + dy, 0, N - 1);
    int next = cy * N + cx;
    if (next != cursor) sfx(SFX_BLIP, 1.5f);
    cursor = next;
  }
  if (input.justDown(BTN_UP) && !open[cursor]) {
    flag[cursor] = !flag[cursor];
    moves++;
    sfx(SFX_SELECT, flag[cursor] ? 1.4f : 0.9f);
  }
  if (input.justDown(BTN_CLICK) && !open[cursor] && !flag[cursor]) {
    if (!placed) layMines(cursor);
    moves++;
    if (mine[cursor]) {
      exploded = true;
      sfx(SFX_EXPLODE);
    } else {
      dig(cursor);
      sfx(SFX_BLIP, 0.9f);
    }
  }

  clear();
  int s = board.cell;
  char buf[3];
  for (int i = 0; i < CELLS; i++) {
    int px = board.cx(i % N), py = board.cy(i / N);
    bool show = open[i] || (exploded && mine[i]);
    if (!show) {
      fillRect(px, py, s - 1, s - 1, flag[i] ? rgb(90, 20, 30) : rgb(26, 34, 78));
      if (flag[i]) fillRect(px + 1, py + 1, s - 3, s - 3, rgb(255, 50, 50));
      continue;
    }
    if (mine[i]) {
      fillRect(px, py, s - 1, s - 1, rgb(160, 20, 20));
      fillRect(px + 1, py + 1, s - 3, s - 3, rgb(255, 220, 60));
      continue;
    }
    fillRect(px, py, s - 1, s - 1, rgb(10, 10, 16));
    if (around[i]) {
      numToStr(around[i], buf);
      text(px + (s - 1 - textWidth(buf)) / 2, py + (s - 6) / 2, buf,
           NUM_COLOR[around[i]]);
    }
  }
  rect(board.cx(cursor % N) - 1, board.cy(cursor / N) - 1, s + 1, s + 1,
       pulse(t));
  // Mines still unaccounted for, so the flag count is worth something.
  numRight(SCREEN_W - 1, SCREEN_H - 6, flagsLeft(), rgb(150, 40, 50));

  if (exploded) return FAILED;
  return cleared() ? SOLVED : PLAYING;
}

}  // namespace pz_mines
