#pragma once
// FLOW — join each pair of dots and cover every square. CLICK grabs the dot
// under the cursor, then tilt drags the line out; crossing another line cuts
// it back, and CLICK drops the line again.
//
// Boards come from a random Hamiltonian path over the grid, sliced into one
// segment per colour. That guarantees a solution that fills the board exists,
// which is what makes "cover every square" a fair rule to enforce. The path
// is randomised by backbiting — repeatedly folding one end back onto itself —
// because a search for a fresh Hamiltonian path can stall, and this cannot.
#include "shared.h"

namespace pz_flow {

using namespace pt;
using namespace pz;

constexpr int MAXN = 6;
constexpr int MAXCELLS = MAXN * MAXN;
constexpr int MAXCOLORS = 5;
constexpr float HUE[MAXCOLORS] = {0.0f, 120.0f, 215.0f, 50.0f, 300.0f};

int8_t owner[MAXCELLS];   // colour whose line covers this square, or -1
uint8_t dot[MAXCELLS];    // colour + 1 for the fixed endpoints, else 0
int8_t path[MAXCOLORS][MAXCELLS];
uint8_t plen[MAXCOLORS];
int n, colors, cursor, active;
Board board;
Stepper step;
float t;

Color colorOf(int c) { return hsv(HUE[c], 0.9f, 1.0f); }
bool adjacent(int a, int b) {
  int ax = a % n, ay = a / n, bx = b % n, by = b / n;
  return absi(ax - bx) + absi(ay - by) == 1;
}

// --- board generation --------------------------------------------------------
int8_t hamiltonian[MAXCELLS];
uint8_t at[MAXCELLS];  // cell -> index along the path

void reversePath(int from, int to) {
  while (from < to) {
    int8_t tmp = hamiltonian[from];
    hamiltonian[from] = hamiltonian[to];
    hamiltonian[to] = tmp;
    at[hamiltonian[from]] = (uint8_t)from;
    at[hamiltonian[to]] = (uint8_t)to;
    from++;
    to--;
  }
}

void generatePath() {
  int cells = n * n;
  // Boustrophedon seed: a valid Hamiltonian path to start folding.
  int k = 0;
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++)
      hamiltonian[k++] = (int8_t)(y * n + (y & 1 ? n - 1 - x : x));
  for (int i = 0; i < cells; i++) at[hamiltonian[i]] = (uint8_t)i;

  for (int iter = 0; iter < 240; iter++) {
    if (iter % 3 == 0) reversePath(0, cells - 1);  // fold from the other end
    int tail = hamiltonian[cells - 1];
    int tx = tail % n, ty = tail / n;
    int cand[4], nc = 0;
    const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
    for (int d = 0; d < 4; d++) {
      int nx = tx + DX[d], ny = ty + DY[d];
      if (nx < 0 || nx >= n || ny < 0 || ny >= n) continue;
      int nb = ny * n + nx;
      if (at[nb] == cells - 2) continue;  // already the tail's neighbour
      cand[nc++] = nb;
    }
    if (nc == 0) continue;
    int pick = cand[randRange(0, nc - 1)];
    reversePath(at[pick] + 1, cells - 1);
  }
}

void start(int stage) {
  n = stage < 2 ? 5 : MAXN;
  colors = mini(3 + stage, MAXCOLORS);
  int cells = n * n;
  generatePath();

  for (int i = 0; i < cells; i++) {
    owner[i] = -1;
    dot[i] = 0;
  }
  // Slice the path into one segment per colour, never shorter than three
  // squares so no pair lands as a trivial neighbouring couple.
  int start_ = 0, left = cells;
  for (int c = 0; c < colors; c++) {
    int len;
    if (c == colors - 1) {
      len = left;
    } else {
      int room = left - 3 * (colors - 1 - c);
      len = clampi(cells / colors + randRange(-1, 1), 3, room);
    }
    int endCell = hamiltonian[start_ + len - 1];
    int startCell = hamiltonian[start_];
    dot[startCell] = (uint8_t)(c + 1);
    dot[endCell] = (uint8_t)(c + 1);
    path[c][0] = (int8_t)startCell;
    plen[c] = 1;
    owner[startCell] = (int8_t)c;
    start_ += len;
    left -= len;
  }

  cursor = 0;
  active = -1;
  t = 0;
  board = fitBoard(n, n, 10);
  stepperInit(step, 0.26f, 0.11f);
  beginBoard(cells);
}

// --- line editing ------------------------------------------------------------
void truncate(int c, int keep) {
  for (int i = keep; i < plen[c]; i++) owner[path[c][i]] = -1;
  plen[c] = (uint8_t)keep;
}

int indexIn(int c, int cellIdx) {
  for (int i = 0; i < plen[c]; i++)
    if (path[c][i] == cellIdx) return i;
  return -1;
}

void grab(int c, int cellIdx) {
  active = c;
  if (dot[cellIdx] == c + 1) {
    truncate(c, 0);
    path[c][0] = (int8_t)cellIdx;
    plen[c] = 1;
    owner[cellIdx] = (int8_t)c;
  } else {
    truncate(c, indexIn(c, cellIdx) + 1);
  }
  cursor = cellIdx;
}

void drawTo(int target) {
  int head = path[active][plen[active] - 1];
  if (!adjacent(head, target)) return;

  int mine = indexIn(active, target);
  if (mine >= 0) {  // stepping back along our own line retracts it
    truncate(active, mine + 1);
    cursor = target;
    moves++;
    sfx(SFX_BLIP, 0.8f);
    return;
  }
  if (dot[target] && dot[target] != active + 1) return;  // never cross a dot

  int other = owner[target];
  // Cutting another line back to the crossing point. Index 0 is that colour's
  // own dot, which the check above already refuses to step on.
  if (other >= 0 && other != active)
    truncate(other, maxi(1, indexIn(other, target)));

  owner[target] = (int8_t)active;
  path[active][plen[active]++] = (int8_t)target;
  cursor = target;
  moves++;

  if (dot[target] == active + 1) {  // reached the far dot: drop the line
    sfx(SFX_COIN, 1.1f);
    active = -1;
  } else {
    sfx(SFX_BLIP, 1.3f);
  }
}

// A colour is done when its line runs from one of its dots to the other.
bool joined(int c) {
  if (plen[c] < 2) return false;
  return dot[path[c][plen[c] - 1]] == c + 1;
}

Status update(float dt) {
  t += dt;
  int dx = 0, dy = 0;
  if (stepTilt(step, dt, dx, dy)) {
    int cx = clampi(cursor % n + dx, 0, n - 1);
    int cy = clampi(cursor / n + dy, 0, n - 1);
    int next = cy * n + cx;
    if (next != cursor) {
      if (active >= 0) drawTo(next);
      else {
        cursor = next;
        sfx(SFX_BLIP, 1.6f);
      }
    }
  }

  if (input.justDown(BTN_CLICK)) {
    if (active >= 0) {
      active = -1;
      sfx(SFX_SELECT, 0.9f);
    } else if (dot[cursor]) {
      grab(dot[cursor] - 1, cursor);
      sfx(SFX_SELECT, 1.3f);
    } else if (owner[cursor] >= 0) {
      grab(owner[cursor], cursor);
      sfx(SFX_SELECT, 1.1f);
    } else {
      sfx(SFX_HURT, 1.8f);
    }
  }
  if (input.justDown(BTN_UP)) {  // wipe the line you are on back to its dot
    int c = active >= 0 ? active : (dot[cursor] ? dot[cursor] - 1 : owner[cursor]);
    if (c >= 0) {
      truncate(c, 1);
      cursor = path[c][0];
      active = -1;
      sfx(SFX_HURT, 1.2f);
    }
  }

  int covered = 0;
  for (int i = 0; i < n * n; i++)
    if (owner[i] >= 0) covered++;
  bool full = covered == n * n;
  bool allJoined = true;
  for (int c = 0; c < colors; c++)
    if (!joined(c)) allJoined = false;

  clear();
  int s = board.cell;
  int thick = maxi(2, s / 2 - 1);
  int half = thick / 2;
  // Every pair joined with squares still bare is the one way this board can
  // look finished without being finished, so the gaps say so themselves.
  Color bare = rgb(9, 9, 16);
  if (allJoined && !full) {
    float k = 0.5f + 0.5f * sinf_(t * 6.0f);
    bare = rgb((uint8_t)(50 + 170 * k), (uint8_t)(30 + 70 * k), 0);
  }
  for (int i = 0; i < n * n; i++)
    fillRect(board.cx(i % n), board.cy(i / n), s - 1, s - 1,
             owner[i] < 0 ? bare : rgb(9, 9, 16));
  for (int c = 0; c < colors; c++) {
    Color col = colorOf(c);
    for (int i = 0; i < plen[c]; i++) {
      int cellIdx = path[c][i];
      int cx = board.cx(cellIdx % n) + s / 2, cy = board.cy(cellIdx / n) + s / 2;
      if (i + 1 < plen[c]) {
        // One rect from this centre to the next covers both squares and the
        // gap between them, so corners join without a seam.
        int nb = path[c][i + 1];
        int nx = board.cx(nb % n) + s / 2, ny = board.cy(nb / n) + s / 2;
        fillRect(mini(cx, nx) - half, mini(cy, ny) - half,
                 absi(cx - nx) + thick, absi(cy - ny) + thick, col);
      } else {
        fillRect(cx - half, cy - half, thick, thick, col);
      }
    }
  }
  for (int i = 0; i < n * n; i++) {
    if (!dot[i]) continue;
    int px = board.cx(i % n), py = board.cy(i / n);
    Color col = colorOf(dot[i] - 1);
    fillCircle(px + s / 2, py + s / 2, s / 2 - 1, col);
    fillCircle(px + s / 2, py + s / 2, s / 2 - 3, WHITE);
  }
  cursorCorners(board, cursor % n, cursor / n,
                active >= 0 ? colorOf(active) : pulse(t));

  // Squares covered out of squares on the board. Joining the pairs is only
  // half of it, and this is the half players cannot see from the lines alone.
  if (bottomRowFree) {
    char buf[10];
    int k = numToStr(covered, buf);
    buf[k++] = '/';
    numToStr(n * n, buf + k);
    labelText(SCREEN_H - 6, "FILL", buf, rgb(70, 70, 90),
              full ? rgb(60, 230, 110)
                   : allJoined ? rgb(255, 170, 40) : rgb(150, 150, 180));
  }

  return allJoined && full ? SOLVED : PLAYING;
}

}  // namespace pz_flow
