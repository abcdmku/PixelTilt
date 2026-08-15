#pragma once
// NONO — nonogram. The numbers down each row and column give the runs of
// filled squares in order; fill them in and the picture is solved. CLICK
// fills, UP pencils an X on a square you have ruled out. Any grid that
// satisfies every clue counts, which is the honest rule when a small board
// happens to have two readings.
#include "shared.h"

namespace pz_nono {

using namespace pt;
using namespace pz;

// The grid stays 5x5 on every board. A 6x6 needs three-deep clue stacks on
// both edges, and what is left of a 64px screen after that is a 5px cell,
// which puts neighbouring clue rows a pixel apart. Difficulty comes from the
// picture instead: more runs, shorter runs, more to deduce.
constexpr int N = 5;
constexpr int MAXRUNS = 3;  // 4 runs need 7 cells, so 3 is the ceiling here
enum Mark : uint8_t { EMPTY, FILLED, CROSSED };

uint8_t mark[N * N];
uint8_t rowClue[N][MAXRUNS], colClue[N][MAXRUNS];
uint8_t rowRuns[N], colRuns[N];
int cursor, gridX, gridY, cell;
float t;
Stepper step;

void runsOf(const uint8_t* first, int stride, uint8_t* out, uint8_t& count) {
  count = 0;
  int run = 0;
  for (int i = 0; i < N; i++) {
    if (first[i * stride]) {
      run++;
    } else if (run) {
      out[count++] = (uint8_t)run;
      run = 0;
    }
  }
  if (run) out[count++] = (uint8_t)run;
}

// Runs the player has actually drawn on a line, for the win check and for
// greying out clues that are already satisfied.
void playerRuns(int line, bool isRow, uint8_t* out, uint8_t& count) {
  uint8_t tmp[N];
  for (int i = 0; i < N; i++)
    tmp[i] = mark[isRow ? line * N + i : i * N + line] == FILLED ? 1 : 0;
  count = 0;
  int run = 0;
  for (int i = 0; i < N; i++) {
    if (tmp[i]) {
      run++;
    } else if (run) {
      out[count++] = (uint8_t)run;
      run = 0;
    }
  }
  if (run) out[count++] = (uint8_t)run;
}

bool lineDone(int line, bool isRow) {
  uint8_t got[MAXRUNS + 1], k = 0;
  playerRuns(line, isRow, got, k);
  const uint8_t* want = isRow ? rowClue[line] : colClue[line];
  uint8_t wn = isRow ? rowRuns[line] : colRuns[line];
  if (k != wn) return false;
  for (int i = 0; i < k; i++)
    if (got[i] != want[i]) return false;
  return true;
}

// Rows and columns broken into how many separate runs. A picture of many
// short runs takes more deducing than one of a few long ones, which is the
// knob the board number turns.
int totalRuns(const uint8_t* pattern) {
  uint8_t buf[MAXRUNS], count = 0;
  int runs = 0;
  for (int r = 0; r < N; r++) {
    runsOf(pattern + r * N, 1, buf, count);
    runs += count;
  }
  for (int c = 0; c < N; c++) {
    runsOf(pattern + c, N, buf, count);
    runs += count;
  }
  return runs;
}

void start(int stage) {
  uint8_t pattern[N * N], best[N * N];
  int wantRuns = mini(6 + stage, 12);
  int bestScore = -1000, bestFilled = 0;
  for (int attempt = 0; attempt < 40; attempt++) {
    int filled = 0;
    for (int i = 0; i < N * N; i++) {
      pattern[i] = (uint8_t)(randRange(0, 99) < 50 ? 1 : 0);
      filled += pattern[i];
    }
    // Too sparse or too solid and there is nothing to work out; those score
    // below any sane picture but still seed `best` on a hopeless run of luck.
    int score = totalRuns(pattern);
    if (filled < N * N / 3 || filled > N * N * 3 / 4) score -= 100;
    if (score > bestScore) {
      bestScore = score;
      bestFilled = filled;
      for (int i = 0; i < N * N; i++) best[i] = pattern[i];
    }
    if (score >= wantRuns) break;
  }
  if (bestFilled == 0) {  // an all-blank picture would solve itself
    best[randRange(0, N * N - 1)] = 1;
    bestFilled = 1;
  }

  for (int r = 0; r < N; r++) runsOf(best + r * N, 1, rowClue[r], rowRuns[r]);
  for (int c = 0; c < N; c++) runsOf(best + c, N, colClue[c], colRuns[c]);
  for (int i = 0; i < N * N; i++) mark[i] = EMPTY;

  // Clue gutters size themselves to the widest stack this board needs, so an
  // easy grid gets fatter cells.
  int maxRow = 1, maxCol = 1;
  for (int i = 0; i < N; i++) {
    maxRow = maxi(maxRow, rowRuns[i]);
    maxCol = maxi(maxCol, colRuns[i]);
  }
  // 3px digit plus a 2px gap, so "2 1" cannot be misread as twenty-one.
  int gutterX = maxRow * 5 + 1;
  int gutterY = maxCol * 6;
  cell = mini((PLAY_BOT - PLAY_TOP - gutterY) / N, (SCREEN_W - gutterX) / N);
  cell = clampi(cell, 4, 9);
  gridX = gutterX + (SCREEN_W - gutterX - N * cell) / 2;
  gridY = PLAY_TOP + gutterY;

  cursor = 0;
  t = 0;
  stepperInit(step);
  beginBoard(bestFilled);
}

void drawClues() {
  char buf[4];
  for (int r = 0; r < N; r++) {
    bool done = lineDone(r, true);
    Color c = done ? rgb(40, 190, 80)
                   : (r == cursor / N ? WHITE : rgb(120, 120, 140));
    int x = gridX - 3;
    for (int k = rowRuns[r] - 1; k >= 0; k--) {
      numToStr(rowClue[r][k], buf);
      x -= textWidth(buf);
      text(x, gridY + r * cell + (cell - 5) / 2, buf, c);
      x -= 2;
    }
    if (rowRuns[r] == 0) text(gridX - 6, gridY + r * cell + (cell - 5) / 2, "0", c);
  }
  for (int col = 0; col < N; col++) {
    bool done = lineDone(col, false);
    Color c = done ? rgb(40, 190, 80)
                   : (col == cursor % N ? WHITE : rgb(120, 120, 140));
    int y = gridY - 6;
    for (int k = colRuns[col] - 1; k >= 0; k--) {
      numToStr(colClue[col][k], buf);
      text(gridX + col * cell + (cell - 3) / 2, y, buf, c);
      y -= 6;
    }
    if (colRuns[col] == 0)
      text(gridX + col * cell + (cell - 3) / 2, gridY - 6, "0", c);
  }
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
  if (input.justDown(BTN_CLICK)) {
    mark[cursor] = mark[cursor] == FILLED ? EMPTY : FILLED;
    moves++;
    sfx(SFX_SELECT, mark[cursor] == FILLED ? 1.2f : 0.8f);
  }
  if (input.justDown(BTN_UP)) {
    mark[cursor] = mark[cursor] == CROSSED ? EMPTY : CROSSED;
    moves++;
    sfx(SFX_BLIP, mark[cursor] == CROSSED ? 0.9f : 1.3f);
  }

  clear();
  for (int i = 0; i < N * N; i++) {
    int px = gridX + (i % N) * cell, py = gridY + (i / N) * cell;
    int s = cell - 1;
    if (mark[i] == FILLED) {
      fillRect(px, py, s, s, rgb(90, 200, 255));
      fillRect(px + 1, py + 1, s - 2, s - 2, rgb(220, 245, 255));
    } else {
      fillRect(px, py, s, s, rgb(28, 28, 54));
      if (mark[i] == CROSSED) {
        line(px + 1, py + 1, px + s - 2, py + s - 2, rgb(210, 60, 80));
        line(px + s - 2, py + 1, px + 1, py + s - 2, rgb(210, 60, 80));
      }
    }
  }
  drawClues();
  rect(gridX + (cursor % N) * cell - 1, gridY + (cursor / N) * cell - 1, cell + 1,
       cell + 1, pulse(t));

  for (int i = 0; i < N; i++)
    if (!lineDone(i, true) || !lineDone(i, false)) return PLAYING;
  return SOLVED;
}

}  // namespace pz_nono
