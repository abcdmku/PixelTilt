// Cycles — light-cycle duel against the computer. Steering is relative to
// the bike: flick the board toward the bike's right to turn right, its left
// to turn left (re-center between flicks), or twist the whole panel — every
// ~40 degrees of yaw is one 90-degree turn. Both bikes leave solid walls.
// Outlast the AI to take the round; crash and the run ends. Score = rounds
// won.
#include "pixeltilt/pixeltilt.h"

using namespace pt;

namespace {

constexpr int TOP = 8;
constexpr int GW = 64, GH = 56;  // arena covers y 8..63
constexpr float STEP = 1.0f / 22.0f;

uint8_t grid[GW * GH];  // 0 empty, 1 player trail, 2 AI trail
int   px, py, pdx, pdy;
int   ax, ay, adx, ady;
int   qTurn;        // queued relative turn: -1 left, +1 right, 0 none
bool  steerArmed;   // tilt must re-center before the next flick counts
float twistAcc;     // integrated panel twist (rad) toward the next turn
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
  px = 12; py = GH / 2; pdx = 1; pdy = 0;
  ax = GW - 13; ay = GH / 2; adx = -1; ady = 0;
  qTurn = 0;
  steerArmed = true;
  twistAcc = 0;
  stepTimer = 0;
  state = COUNTDOWN;
  stateT = 0;
}

void init() {
  setSfxStyle(STYLE_GRIT);
  music(MUS_TENSE);
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
  if (qTurn) {
    // Rotate the heading a quarter turn: +1 = clockwise on screen (the
    // bike's right), -1 = counterclockwise.
    int ndx = qTurn > 0 ? -pdy : pdy;
    int ndy = qTurn > 0 ? pdx : -pdx;
    pdx = ndx; pdy = ndy;
    qTurn = 0;
    sfx(SFX_BLIP);
  }
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
    sfx(SFX_EXPLODE);
    sfx(SFX_LOSE);
  } else if (aDead) {
    score++;
    state = ROUNDWON;
    stateT = 0;
    sfx(SFX_COIN);
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
    if (stateT >= 0.8f) { state = RUNNING; sfx(SFX_SELECT); }
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

  // Relative steering: the tilt component along the bike's right-hand side
  // queues a right turn, its left a left turn — so "lean right" always means
  // the same thing no matter which way the bike is heading. Linear accel is
  // blended in (input.h's tilt + accel*K pattern) so a sharp flick steers
  // immediately, before the board has tilted far. Hysteresis: fire past
  // 0.45, re-arm once the board re-centers under 0.25.
  float sx = input.tiltX + input.accelX * 0.6f;
  float sy = input.tiltY + input.accelY * 0.6f;
  float steer = sx * -pdy + sy * pdx;
  if (fabsf_(steer) < 0.25f) steerArmed = true;
  if (steerArmed && fabsf_(steer) > 0.45f) {
    steerArmed = false;
    qTurn = steer > 0 ? 1 : -1;
  }

  // Twist steering: integrate panel yaw; each ~40 degrees of twist is one
  // turn. The slow decay bleeds off sensor drift without eating real twists.
  twistAcc += input.spin * dt;
  twistAcc -= twistAcc * 1.5f * dt;
  if (fabsf_(twistAcc) > 0.7f) {
    qTurn = twistAcc > 0 ? 1 : -1;
    twistAcc = 0;
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
