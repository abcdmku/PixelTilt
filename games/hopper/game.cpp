// Hopper — a frogger. Tip the board past the threshold to hop one cell
// (re-center between hops). Cross six lanes of traffic to the far bank:
// each first-time forward hop scores, a full crossing scores big and speeds
// the cars up. Three lives.
#include "pixeltilt/pixeltilt.h"

using namespace pt;

namespace {

constexpr int TOP = 8;
constexpr int CELL = 7;
constexpr int NCOLS = 9;     // 9 * 7 = 63px
constexpr int NLANES = 6;    // rows 1..6 carry traffic
constexpr int START_ROW = 7;
constexpr float CAR_W = 10, CAR_SPACING = 26, WRAP = 78;

const float LANE_SPEED[NLANES] = {14, -18, 22, -14, 26, -20};
const Color LANE_COLOR[NLANES] = {RED, YELLOW, CYAN, MAGENTA, ORANGE, BLUE};

float laneOff[NLANES];
float speedMul;
int   frogCol, frogRow, bestRow;
int   lives, score, scoreRank;
bool  gameOver;
float overTime, deathFlash;
bool  tiltArmed;

void drawNumber(int x, int y, int n, Color c) {
  char buf[8];
  int i = 0;
  if (n == 0) buf[i++] = '0';
  char tmp[8];
  int t = 0;
  while (n > 0) { tmp[t++] = '0' + n % 10; n /= 10; }
  while (t > 0) buf[i++] = tmp[--t];
  buf[i] = 0;
  text(x, y, buf, c);
}

void resetFrog() {
  frogCol = 4;
  frogRow = START_ROW;
  bestRow = START_ROW;
  tiltArmed = false;
}

void init() {
  setSfxStyle(STYLE_CHIP);
  music(MUS_ACTION);
  for (int i = 0; i < NLANES; i++) laneOff[i] = randf() * WRAP;
  speedMul = 1.0f;
  lives = 3;
  score = 0;
  scoreRank = -1;
  gameOver = false;
  overTime = deathFlash = 0;
  resetFrog();
}

// One hop per tilt: returns 0=up 1=right 2=down 3=left, or -1. The board
// must come back near level before the next hop registers.
int readHop() {
  float ax = fabsf_(input.tiltX), ay = fabsf_(input.tiltY);
  if (ax < 0.45f && ay < 0.45f) {
    tiltArmed = true;
    return -1;
  }
  if (!tiltArmed) return -1;
  tiltArmed = false;
  if (ax > ay) return input.tiltX > 0 ? 1 : 3;
  return input.tiltY > 0 ? 2 : 0;
}

float carX(int lane, int k) {
  return fmodf_(laneOff[lane] + k * CAR_SPACING, WRAP) - (CAR_W + 4);
}

void draw() {
  clear();
  fillRect(0, 0, SCREEN_W, TOP - 1, rgb(10, 12, 24));
  text(2, 1, "HOPPER", GREEN);
  drawNumber(40, 1, score, WHITE);
  for (int i = 0; i < lives; i++) fillRect(60 - i * 4 + 1, 2, 2, 3, GREEN);
  hline(0, TOP - 1, SCREEN_W, DARKGRAY);

  // Goal bank with lily pads, traffic lanes, start sidewalk.
  fillRect(0, TOP, SCREEN_W, CELL, rgb(8, 45, 22));
  for (int c = 0; c < NCOLS; c += 2)
    fillCircle(c * CELL + 3, TOP + 3, 2, rgb(20, 110, 45));
  for (int l = 0; l < NLANES; l++) {
    int y = TOP + (l + 1) * CELL;
    fillRect(0, y, SCREEN_W, CELL, (l & 1) ? rgb(22, 22, 27) : rgb(16, 16, 20));
    for (int k = 0; k < 3; k++) {
      int cx = (int)carX(l, k);
      fillRect(cx, y + 1, (int)CAR_W, 5, LANE_COLOR[l]);
      int wx = LANE_SPEED[l] > 0 ? cx + 7 : cx + 1;
      fillRect(wx, y + 2, 2, 3, rgb(200, 230, 255));
    }
  }
  fillRect(0, TOP + START_ROW * CELL, SCREEN_W, CELL, rgb(35, 35, 40));

  int fx = frogCol * CELL + 1, fy = TOP + frogRow * CELL + 1;
  fillRect(fx, fy, 5, 5, GREEN);
  pixel(fx + 1, fy, WHITE);
  pixel(fx + 3, fy, WHITE);
  if (deathFlash > 0)
    circle(fx + 2, fy + 2, 3 + (int)(deathFlash * 10), RED);

  if (gameOver) {
    fillRect(8, 24, 48, 18, rgb(20, 4, 4));
    rect(8, 24, 48, 18, RED);
    textCentered(27, "GAME OVER", WHITE);
    textCentered(35, "CLICK-RETRY", GRAY);
    if (scoreRank == 0) textCentered(46, "NEW BEST!", YELLOW);
  }
}

void update(float dt) {
  if (gameOver) {
    overTime += dt;
    draw();
    if (overTime > 0.5f && input.justDown(BTN_CLICK)) init();
    return;
  }

  if (deathFlash > 0) deathFlash -= dt;
  for (int i = 0; i < NLANES; i++) {
    laneOff[i] += LANE_SPEED[i] * speedMul * dt;
    laneOff[i] = fmodf_(laneOff[i], WRAP);
  }

  int hop = readHop();
  if (hop >= 0) sfx(SFX_JUMP);
  if (hop == 0) frogRow = maxi(frogRow - 1, 0);
  else if (hop == 2) frogRow = mini(frogRow + 1, START_ROW);
  else if (hop == 1) frogCol = mini(frogCol + 1, NCOLS - 1);
  else if (hop == 3) frogCol = maxi(frogCol - 1, 0);

  if (frogRow < bestRow) {
    bestRow = frogRow;
    score++;
    sfx(SFX_COIN, 1.0f + (START_ROW - bestRow) * 0.05f);
  }
  if (frogRow == 0) {
    score += 10;
    speedMul *= 1.12f;
    sfx(SFX_POWERUP);
    resetFrog();
  }

  // Squash check against the frog's lane.
  int lane = frogRow - 1;
  if (lane >= 0 && lane < NLANES) {
    float fx0 = frogCol * CELL + 1, fx1 = fx0 + 5;
    for (int k = 0; k < 3; k++) {
      float cx = carX(lane, k);
      if (fx0 < cx + CAR_W && fx1 > cx) {
        lives--;
        deathFlash = 0.4f;
        if (lives <= 0) {
          gameOver = true;
          overTime = 0;
          scoreRank = submitScore(score);
          sfx(SFX_LOSE);
        } else {
          sfx(SFX_HURT);
          resetFrog();
        }
        break;
      }
    }
  }

  draw();
}

}  // namespace

PT_GAME(hopper, "HOPPER", init, update)
