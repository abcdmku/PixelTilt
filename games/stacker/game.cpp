// Stacker — a block slides back and forth; CLICK drops it onto the tower.
// Any overhang past the row below is sliced off. Top out the tower to start
// a faster round; lose your last cell and it's over.
#include "pixeltilt/pixeltilt.h"

using namespace pt;

namespace {

constexpr int TOP = 8;
constexpr int NCOLS = 16, CELL = 4;
constexpr int NROWS = 13;  // row r sits at y = 59 - r*4, top row just under the bar

uint8_t rowL[NROWS], rowW[NROWS];
int   row;      // row currently being aimed
float posX;
int   dir;
float speed;
int   curW;
int   score, round_, scoreRank;
bool  gameOver;
float overTime, flashT, t;

void drawNumber(int x, int y, int n, Color c) {
  char buf[8];
  int i = 0;
  if (n == 0) buf[i++] = '0';
  char tmp[8];
  int tt = 0;
  while (n > 0) { tmp[tt++] = '0' + n % 10; n /= 10; }
  while (tt > 0) buf[i++] = tmp[--tt];
  buf[i] = 0;
  text(x, y, buf, c);
}

int rowY(int r) { return 59 - r * CELL; }

void startTower() {
  row = 0;
  for (int r = 0; r < NROWS; r++) rowW[r] = 0;
  posX = 0;
  dir = 1;
}

void init() {
  setSfxStyle(STYLE_CHIP);
  music(MUS_TENSE);
  curW = 5;
  speed = 22;
  score = 0;
  round_ = 0;
  scoreRank = -1;
  gameOver = false;
  overTime = flashT = t = 0;
  startTower();
}

void draw() {
  clear();
  fillRect(0, 0, SCREEN_W, TOP - 1, rgb(10, 12, 24));
  text(2, 1, "STACKER", CYAN);
  drawNumber(46, 1, score, WHITE);
  hline(0, TOP - 1, SCREEN_W, DARKGRAY);
  hline(0, 63, SCREEN_W, DARKGRAY);

  for (int r = 0; r < NROWS; r++) {
    if (rowW[r] == 0) continue;
    Color c = hsv(fmodf_(r * 18.0f + round_ * 60.0f, 360.0f), 0.8f, 0.85f);
    fillRect(rowL[r] * CELL, rowY(r), rowW[r] * CELL, CELL, c);
    rect(rowL[r] * CELL, rowY(r), rowW[r] * CELL, CELL, rgb(c.r / 2, c.g / 2, c.b / 2));
  }

  if (!gameOver) {
    // The moving block pulses so it reads against the stack.
    float pulse = 0.75f + 0.25f * sinf_(t * 10.0f);
    Color c = hsv(fmodf_(row * 18.0f + round_ * 60.0f, 360.0f), 0.5f, pulse);
    int l = floori(posX + 0.5f);
    fillRect(l * CELL, rowY(row), curW * CELL, CELL, c);
  }

  if (flashT > 0) textCentered(30, "ROUND UP!", YELLOW);

  if (gameOver) {
    fillRect(8, 24, 48, 18, rgb(20, 4, 4));
    rect(8, 24, 48, 18, RED);
    textCentered(27, "GAME OVER", WHITE);
    textCentered(35, "CLICK-RETRY", GRAY);
    if (scoreRank == 0) textCentered(46, "NEW BEST!", YELLOW);
  }
}

void update(float dt) {
  t += dt;
  if (flashT > 0) flashT -= dt;

  if (gameOver) {
    overTime += dt;
    draw();
    if (overTime > 0.5f && input.justDown(BTN_CLICK)) init();
    return;
  }

  posX += dir * speed * dt;
  if (posX < 0) { posX = 0; dir = 1; }
  if (posX > NCOLS - curW) { posX = (float)(NCOLS - curW); dir = -1; }

  if (input.justDown(BTN_CLICK)) {
    int l = floori(posX + 0.5f);
    bool trimmed = false;
    if (row > 0) {
      int pl = rowL[row - 1], pr = pl + rowW[row - 1];
      int nl = maxi(l, pl), nr = mini(l + curW, pr);
      if (nr <= nl) {
        gameOver = true;
        overTime = 0;
        scoreRank = submitScore(score);
        sfx(SFX_LOSE);
        draw();
        return;
      }
      trimmed = nr - nl < curW;
      l = nl;
      curW = nr - nl;
    }
    rowL[row] = (uint8_t)l;
    rowW[row] = (uint8_t)curW;
    score++;
    if (trimmed) sfx(SFX_HURT);
    else sfx(row > 0 ? SFX_POWERUP : SFX_BOUNCE, 1.0f + row * 0.04f);
    speed += 2.5f;
    row++;
    if (row >= NROWS) {
      round_++;
      score += 5;
      speed = 22.0f + round_ * 8.0f;
      flashT = 0.8f;
      sfx(SFX_WIN);
      startTower();
    }
    posX = dir > 0 ? 0 : (float)(NCOLS - curW);
  }

  draw();
}

}  // namespace

PT_GAME(stacker, "STACKER", init, update)
