// Lander — set down gently on the flashing pad. Tilt steers sideways, UP
// fires the thruster (fuel is finite). Each landing shrinks the pad and
// strengthens gravity; your score is how many pads you stuck.
#include "pixeltilt/pixeltilt.h"

using namespace pt;

namespace {

constexpr int TOP = 8;
constexpr float SAFE_VY = 16.0f;
constexpr float SAFE_VX = 10.0f;

uint8_t ground[SCREEN_W];
int   padX, padW;
float sx, sy, vx, vy, fuel;
int   level;      // pads landed so far
int   scoreRank;
float t;          // wall clock for blinking
enum State { FLYING, LANDED, CRASHED };
State state;
float stateTime;
bool  thrusting;

void drawNumber(int x, int y, int n, Color c) {
  char buf[8];
  int i = 0;
  if (n == 0) buf[i++] = '0';
  char tmp[8];
  int t2 = 0;
  while (n > 0) { tmp[t2++] = '0' + n % 10; n /= 10; }
  while (t2 > 0) buf[i++] = tmp[--t2];
  buf[i] = 0;
  text(x, y, buf, c);
}

void genTerrain() {
  int h = randRange(42, 54);
  for (int x = 0; x < SCREEN_W; x++) {
    h = clampi(h + randRange(-2, 2), 34, 60);
    ground[x] = (uint8_t)h;
  }
  padW = maxi(14 - level * 2, 7);
  padX = randRange(3, SCREEN_W - padW - 3);
  for (int x = padX; x < padX + padW; x++) ground[x] = ground[padX];
}

void startApproach() {
  sx = 6;
  sy = 14;
  vx = 16;
  vy = -2;
  state = FLYING;
  stateTime = 0;
}

void init() {
  setSfxStyle(STYLE_SOFT);
  music(MUS_CHILL);
  level = 0;
  scoreRank = -1;
  fuel = 100;
  t = 0;
  genTerrain();
  startApproach();
}

void draw() {
  clear();
  for (int i = 0; i < 10; i++)
    pixel((i * 23 + 5) % SCREEN_W, TOP + (i * 13 + 4) % 22, rgb(45, 45, 60));

  for (int x = 0; x < SCREEN_W; x++) {
    vline(x, ground[x], SCREEN_H - ground[x], rgb(70, 60, 50));
    pixel(x, ground[x], rgb(110, 95, 80));
  }
  Color padC = fmodf_(t, 0.6f) < 0.3f ? GREEN : rgb(10, 70, 25);
  hline(padX, ground[padX], padW, padC);

  int ix = (int)sx, iy = (int)sy;
  if (state != CRASHED) {
    fillRect(ix - 1, iy - 1, 3, 2, WHITE);
    pixel(ix - 1, iy + 1, GRAY);
    pixel(ix + 1, iy + 1, GRAY);
    if (thrusting) pixel(ix, iy + 2, (rand_() & 1) ? ORANGE : YELLOW);
  } else {
    fillCircle(ix, iy, 2 + (int)(fminf_(stateTime, 0.4f) * 10), RED);
  }

  // HUD: fuel bar, level, and a descent-speed lamp (green = safe to land).
  fillRect(0, 0, SCREEN_W, TOP - 1, rgb(10, 12, 24));
  text(2, 1, "LV", GRAY);
  drawNumber(11, 1, level, WHITE);
  int fw = (int)(fuel * 0.26f);
  rect(22, 2, 28, 3, DARKGRAY);
  if (fw > 0) fillRect(23, 3, mini(fw, 26), 1, fuel > 30 ? GREEN : RED);
  Color lamp = (fabsf_(vy) < SAFE_VY && fabsf_(vx) < SAFE_VX) ? GREEN : RED;
  fillRect(56, 1, 5, 5, lamp);
  hline(0, TOP - 1, SCREEN_W, DARKGRAY);

  if (state == LANDED) {
    fillRect(6, 22, 52, 14, rgb(4, 20, 8));
    rect(6, 22, 52, 14, GREEN);
    textCentered(25, "TOUCHDOWN!", WHITE);
    textCentered(31, "GET READY", GRAY);
  } else if (state == CRASHED && stateTime > 0.5f) {
    fillRect(8, 20, 48, 24, rgb(20, 4, 4));
    rect(8, 20, 48, 24, RED);
    textCentered(23, "CRASHED", WHITE);
    textCentered(31, "CLICK-RETRY", GRAY);
    if (scoreRank == 0) textCentered(39, "NEW BEST!", YELLOW);
  }
}

void update(float dt) {
  t += dt;

  if (state == LANDED) {
    stateTime += dt;
    thrusting = false;
    draw();
    if (stateTime > 1.2f) {
      level++;
      sfx(SFX_COIN);
      fuel = 100;
      genTerrain();
      startApproach();
    }
    return;
  }
  if (state == CRASHED) {
    bool shown = stateTime > 0.5f;
    stateTime += dt;
    thrusting = false;
    draw();
    if (!shown && stateTime > 0.5f) sfx(SFX_LOSE);
    if (stateTime > 0.8f && input.justDown(BTN_CLICK)) init();
    return;
  }

  float gravity = 24.0f + level * 2.0f;
  thrusting = input.held(BTN_UP) && fuel > 0;
  if (thrusting) {
    vy -= 58.0f * dt;
    bool low = fuel <= 30;
    fuel = fmaxf_(0, fuel - 14.0f * dt);
    if (!low && fuel <= 30) sfx(SFX_ALARM);
  }
  vx += tiltCurve(input.tiltX, 0.06f) * 46.0f * dt;
  vy += gravity * dt;
  sx += vx * dt;
  sy += vy * dt;
  if (sx < 2) { sx = 2; vx = 0; }
  if (sx > SCREEN_W - 3) { sx = SCREEN_W - 3; vx = 0; }
  if (sy < TOP + 1) { sy = TOP + 1; vy = fmaxf_(vy, 0); }

  // Touchdown check across the ship's 3px footprint.
  bool touched = false;
  for (int dx = -1; dx <= 1; dx++) {
    int gx = clampi((int)sx + dx, 0, SCREEN_W - 1);
    if (sy + 2 >= ground[gx]) touched = true;
  }
  if (touched) {
    bool onPad = (int)sx - 1 >= padX && (int)sx + 1 <= padX + padW - 1;
    bool soft = fabsf_(vy) < SAFE_VY && fabsf_(vx) < SAFE_VX;
    stateTime = 0;
    if (onPad && soft) {
      state = LANDED;
      sy = (float)ground[padX] - 2;
      sfx(SFX_WIN);
    } else {
      state = CRASHED;
      sfx(SFX_EXPLODE);
      scoreRank = submitScore(level);
    }
  }

  draw();
}

}  // namespace

PT_GAME_SCORED(lander, "LANDER", init, update, pt::SCORE_LEVEL)
