// Cycles — light-cycle duel against the computer. Tilt to turn; both bikes
// leave solid walls. Outlast the AI to take the round; crash and the run
// ends. Score = rounds won.
#include "pixeltilt/pixeltilt.h"

using namespace pt;

namespace {

constexpr int TOP = 8;
constexpr int GW = 64, GH = 56;  // arena covers y 8..63
constexpr float STEP = 1.0f / 30.0f;

uint8_t grid[GW * GH];  // 0 empty, 1 player trail, 2 AI trail
int   px, py, pdx, pdy, qdx, qdy;
int   ax, ay, adx, ady;
float stepTimer;
int   score, roundNum, scoreRank;
enum State { COUNTDOWN, RUNNING, ROUNDWON, OVER };
State state;
float stateT;

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

bool occ(int x, int y) {
  if (x < 0 || y < 0 || x >= GW || y >= GH) return true;
  return grid[y * GW + x] != 0;
}

void startRound() {
  for (int i = 0; i < GW * GH; i++) grid[i] = 0;
  px = 12; py = GH / 2; pdx = 1; pdy = 0; qdx = 1; qdy = 0;
  ax = GW - 13; ay = GH / 2; adx = -1; ady = 0;
  stepTimer = 0;
  state = COUNTDOWN;
  stateT = 0;
}

void init() {
  score = 0;
  roundNum = 1;
  scoreRank = -1;
  startRound();
}

int rayLen(int x, int y, int dx, int dy) {
  int n = 0;
  x += dx; y += dy;
  while (!occ(x, y) && n < 20) { n++; x += dx; y += dy; }
  return n;
}

// Steer toward the longest open ray when blocked (or on a rare whim).
void aiDecide() {
  int fwd = rayLen(ax, ay, adx, ady);
  int ldx = ady, ldy = -adx;
  int rdx = -ady, rdy = adx;
  if (fwd < 2 || randf() < 0.02f) {
    int lf = rayLen(ax, ay, ldx, ldy);
    int rf = rayLen(ax, ay, rdx, rdy);
    int best = fwd, bdx = adx, bdy = ady;
    if (lf > best) { best = lf; bdx = ldx; bdy = ldy; }
    if (rf > best) { best = rf; bdx = rdx; bdy = rdy; }
    adx = bdx; ady = bdy;
  }
}

void step() {
  pdx = qdx; pdy = qdy;
  aiDecide();

  grid[py * GW + px] = 1;
  grid[ay * GW + ax] = 2;
  px += pdx; py += pdy;
  ax += adx; ay += ady;

  bool pDead = occ(px, py);
  bool aDead = occ(ax, ay);
  if (px == ax && py == ay) pDead = aDead = true;

  if (pDead) {
    state = OVER;
    stateT = 0;
    scoreRank = submitScore(score);
  } else if (aDead) {
    score++;
    state = ROUNDWON;
    stateT = 0;
  }
}

void draw() {
  clear();
  fillRect(0, 0, SCREEN_W, TOP - 1, rgb(10, 12, 24));
  text(2, 1, "CYCLES", CYAN);
  drawNumber(40, 1, score, WHITE);
  hline(0, TOP - 1, SCREEN_W, DARKGRAY);

  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++) {
      uint8_t v = grid[y * GW + x];
      if (v == 1) pixel(x, y + TOP, rgb(0, 90, 130));
      else if (v == 2) pixel(x, y + TOP, rgb(160, 65, 10));
    }
  if (state != OVER) {
    pixel(px, py + TOP, WHITE);
    pixel(ax, ay + TOP, YELLOW);
  }

  if (state == COUNTDOWN) {
    textCentered(28, "ROUND", GRAY);
    drawNumber(30, 36, roundNum, WHITE);
  } else if (state == ROUNDWON) {
    textCentered(32, "YOU WIN!", GREEN);
  } else if (state == OVER) {
    fillCircle(px, py + TOP, 2 + (int)(fminf_(stateT, 0.4f) * 10), RED);
    if (stateT > 0.5f) {
      fillRect(8, 22, 48, 18, rgb(20, 4, 4));
      rect(8, 22, 48, 18, RED);
      textCentered(25, "DEREZZED", WHITE);
      textCentered(33, "CLICK-RETRY", GRAY);
      if (scoreRank == 0) textCentered(44, "NEW BEST!", YELLOW);
    }
  }
}

void update(float dt) {
  stateT += dt;

  if (state == COUNTDOWN) {
    if (stateT >= 0.8f) state = RUNNING;
    draw();
    return;
  }
  if (state == ROUNDWON) {
    draw();
    if (stateT >= 1.0f) {
      roundNum++;
      startRound();
    }
    return;
  }
  if (state == OVER) {
    draw();
    if (stateT > 0.6f && input.justDown(BTN_CLICK)) init();
    return;
  }

  // Queue a turn from the dominant tilt axis; reversing is ignored.
  float tx = input.tiltX, ty = input.tiltY;
  float mx = fabsf_(tx), my = fabsf_(ty);
  if (mx > 0.4f || my > 0.4f) {
    if (mx > my) {
      int d = tx > 0 ? 1 : -1;
      if (!(pdx == -d && pdy == 0)) { qdx = d; qdy = 0; }
    } else {
      int d = ty > 0 ? 1 : -1;
      if (!(pdy == -d && pdx == 0)) { qdx = 0; qdy = d; }
    }
  }

  stepTimer += dt;
  while (stepTimer >= STEP && state == RUNNING) {
    stepTimer -= STEP;
    step();
  }

  draw();
}

}  // namespace

PT_GAME(cycles, "CYCLES", init, update)
