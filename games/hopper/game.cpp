// Hopper — a compact Frogger. Cross three roomy road lanes, rest on the
// median, ride logs and turtles over three river lanes, and fill five homes.
// Steering stays analog: shallow tilts creep while steep tilts move quickly.
#include "pixeltilt/pixeltilt.h"

using namespace pt;

namespace {

constexpr int TOP = 8;
constexpr int CELL = 6;
constexpr int WATER_FIRST = 1;
constexpr int WATER_LANES = 3;
constexpr int MEDIAN_ROW = 4;
constexpr int ROAD_FIRST = 5;
constexpr int ROAD_LANES = 3;
constexpr int START_ROW = 8;
constexpr int HOME_COUNT = 5;
constexpr int HOME_W = 10;
constexpr int OBJECTS_PER_LANE = 3;

constexpr float FROG_SIZE = 5.0f;
constexpr float FROG_SPEED = 24.0f;
constexpr float STEER_RESPONSE = 9.0f;
constexpr float START_X = 29.0f;
constexpr float START_Y = 59.0f;
constexpr float GOAL_Y = TOP + 1.0f;

// The opening round has slow hazards, large road gaps, and support across
// roughly 80% of the river. Difficulty rises only after all homes are filled.
const float ROAD_SPEED[ROAD_LANES] = {4.5f, -6.0f, 5.0f};
const float ROAD_WIDTH[ROAD_LANES] = {8, 11, 9};
const float ROAD_SPACING[ROAD_LANES] = {27, 32, 29};
const Color ROAD_COLOR[ROAD_LANES] = {YELLOW, MAGENTA, CYAN};

const float WATER_SPEED[WATER_LANES] = {-3.5f, 4.0f, -4.5f};
const float WATER_WIDTH[WATER_LANES] = {22, 25, 20};
const float WATER_SPACING[WATER_LANES] = {27, 30, 25};
const bool WATER_TURTLES[WATER_LANES] = {true, false, true};

const int HOME_X[HOME_COUNT] = {1, 14, 27, 40, 53};

float roadOff[ROAD_LANES], waterOff[WATER_LANES];
float speedMul;
float frogX, frogY, frogVx, frogVy;
float deathX, deathY;
float overTime, deathFlash, levelFlash;
int bestRow;
int lives, score, scoreRank, level, homesFilled;
bool homes[HOME_COUNT];
bool gameOver;

void drawNumber(int x, int y, int n, Color c) {
  char buf[8];
  int i = 0;
  if (n == 0) buf[i++] = '0';
  char tmp[8];
  int t = 0;
  while (n > 0 && t < 7) { tmp[t++] = '0' + n % 10; n /= 10; }
  while (t > 0) buf[i++] = tmp[--t];
  buf[i] = 0;
  text(x, y, buf, c);
}

float laneWrap(float spacing) { return spacing * OBJECTS_PER_LANE; }

float objectX(float offset, int k, float spacing, float width) {
  return fmodf_(offset + k * spacing, laneWrap(spacing)) - width;
}

void clearHomes() {
  homesFilled = 0;
  for (int i = 0; i < HOME_COUNT; i++) homes[i] = false;
}

void resetFrog() {
  frogX = START_X;
  frogY = START_Y;
  frogVx = frogVy = 0;
  bestRow = START_ROW;
}

void init() {
  setSfxStyle(STYLE_CHIP);
  music(MUS_ACTION);
  for (int i = 0; i < ROAD_LANES; i++)
    roadOff[i] = randf() * laneWrap(ROAD_SPACING[i]);
  for (int i = 0; i < WATER_LANES; i++)
    waterOff[i] = randf() * laneWrap(WATER_SPACING[i]);
  speedMul = 1.0f;
  lives = 3;
  level = 1;
  score = 0;
  scoreRank = -1;
  gameOver = false;
  overTime = deathFlash = levelFlash = 0;
  clearHomes();
  resetFrog();
}

int rowAt(float y) {
  return clampi(floori((y - TOP) / CELL), 0, START_ROW);
}

int homeAt(float x) {
  for (int i = 0; i < HOME_COUNT; i++)
    if (x >= HOME_X[i] && x < HOME_X[i] + HOME_W) return i;
  return -1;
}

void loseFrog() {
  deathX = frogX + FROG_SIZE * 0.5f;
  deathY = frogY + FROG_SIZE * 0.5f;
  deathFlash = 0.45f;
  lives--;
  if (lives <= 0) {
    gameOver = true;
    overTime = 0;
    scoreRank = submitScore(score);
    sfx(SFX_LOSE);
  } else {
    sfx(SFX_HURT);
    resetFrog();
  }
}

void drawHomes() {
  fillRect(0, TOP, SCREEN_W, CELL, rgb(7, 48, 23));
  for (int i = 0; i < HOME_COUNT; i++) {
    Color pad = homes[i] ? rgb(20, 120, 42) : rgb(10, 78, 32);
    fillRect(HOME_X[i], TOP, HOME_W, CELL, pad);
    if (homes[i]) {
      fillRect(HOME_X[i] + 3, TOP + 1, 5, 5, GREEN);
      pixel(HOME_X[i] + 4, TOP + 1, WHITE);
      pixel(HOME_X[i] + 6, TOP + 1, WHITE);
    } else {
      hline(HOME_X[i] + 2, TOP + 3, HOME_W - 4, rgb(35, 170, 60));
    }
  }
}

void drawWater() {
  for (int lane = 0; lane < WATER_LANES; lane++) {
    int y = TOP + (WATER_FIRST + lane) * CELL;
    fillRect(0, y, SCREEN_W, CELL,
             (lane & 1) ? rgb(8, 28, 72) : rgb(7, 36, 88));
    for (int k = 0; k < OBJECTS_PER_LANE; k++) {
      int x = floori(objectX(waterOff[lane], k, WATER_SPACING[lane],
                             WATER_WIDTH[lane]) + 0.5f);
      int w = (int)WATER_WIDTH[lane];
      if (WATER_TURTLES[lane]) {
        for (int p = 0; p < w; p += 4)
          fillRect(x + p, y + 1, mini(3, w - p), 4, rgb(45, 190, 150));
      } else {
        fillRect(x, y + 1, w, 5, rgb(145, 78, 28));
        hline(x + 1, y + 1, maxi(0, w - 2), ORANGE);
      }
    }
  }
}

void drawRoad() {
  for (int lane = 0; lane < ROAD_LANES; lane++) {
    int y = TOP + (ROAD_FIRST + lane) * CELL;
    fillRect(0, y, SCREEN_W, CELL,
             (lane & 1) ? rgb(22, 22, 27) : rgb(16, 16, 20));
    for (int k = 0; k < OBJECTS_PER_LANE; k++) {
      int x = floori(objectX(roadOff[lane], k, ROAD_SPACING[lane],
                             ROAD_WIDTH[lane]) + 0.5f);
      int w = (int)ROAD_WIDTH[lane];
      fillRect(x, y + 1, w, 5, ROAD_COLOR[lane]);
      int windshield = ROAD_SPEED[lane] > 0 ? x + w - 2 : x + 1;
      fillRect(windshield, y + 1, 1, 3, WHITE);
    }
  }
}

void draw() {
  clear();
  fillRect(0, 0, SCREEN_W, TOP - 1, rgb(10, 12, 24));
  text(1, 1, "HOP", GREEN);
  text(15, 1, "L", GRAY);
  drawNumber(20, 1, level, WHITE);
  drawNumber(31, 1, score, WHITE);
  for (int i = 0; i < lives; i++) fillRect(62 - i * 4, 2, 2, 3, GREEN);
  hline(0, TOP - 1, SCREEN_W, DARKGRAY);

  drawHomes();
  drawWater();
  fillRect(0, TOP + MEDIAN_ROW * CELL, SCREEN_W, CELL, rgb(18, 82, 35));
  hline(0, TOP + MEDIAN_ROW * CELL + 2, SCREEN_W, rgb(40, 145, 55));
  drawRoad();
  fillRect(0, TOP + START_ROW * CELL, SCREEN_W,
           SCREEN_H - (TOP + START_ROW * CELL), rgb(38, 38, 43));
  hline(0, TOP + START_ROW * CELL, SCREEN_W, GRAY);

  int fx = floori(frogX + 0.5f), fy = floori(frogY + 0.5f);
  fillRect(fx, fy, (int)FROG_SIZE, (int)FROG_SIZE, GREEN);
  pixel(fx + 1, fy, WHITE);
  pixel(fx + 3, fy, WHITE);

  if (deathFlash > 0)
    circle((int)deathX, (int)deathY, 2 + (int)(deathFlash * 10), RED);

  if (levelFlash > 0) {
    fillRect(12, 22, 40, 20, rgb(4, 18, 8));
    rect(12, 22, 40, 20, GREEN);
    textCentered(25, "LEVEL", WHITE);
    drawNumber(30, 33, level, YELLOW);
  }

  if (gameOver) {
    fillRect(8, 22, 48, 22, rgb(20, 4, 4));
    rect(8, 22, 48, 22, RED);
    textCentered(25, "GAME OVER", WHITE);
    textCentered(33, "CLICK-RETRY", GRAY);
    if (scoreRank == 0) textCentered(40, "NEW BEST!", YELLOW);
  }
}

bool carHit() {
  float fx1 = frogX + FROG_SIZE, fy1 = frogY + FROG_SIZE;
  for (int lane = 0; lane < ROAD_LANES; lane++) {
    float cy = TOP + (ROAD_FIRST + lane) * CELL + 1;
    if (frogY >= cy + 5 || fy1 <= cy) continue;
    for (int k = 0; k < OBJECTS_PER_LANE; k++) {
      float cx = objectX(roadOff[lane], k, ROAD_SPACING[lane],
                         ROAD_WIDTH[lane]);
      if (frogX < cx + ROAD_WIDTH[lane] && fx1 > cx) return true;
    }
  }
  return false;
}

bool rideRiver(int row, float dt) {
  int lane = row - WATER_FIRST;
  float centerX = frogX + FROG_SIZE * 0.5f;
  bool supported = false;
  for (int k = 0; k < OBJECTS_PER_LANE; k++) {
    float x = objectX(waterOff[lane], k, WATER_SPACING[lane],
                      WATER_WIDTH[lane]);
    if (centerX >= x && centerX < x + WATER_WIDTH[lane]) {
      supported = true;
      break;
    }
  }
  if (!supported) return false;

  frogX += WATER_SPEED[lane] * speedMul * dt;
  return frogX >= 0 && frogX <= SCREEN_W - FROG_SIZE;
}

void reachHome() {
  int home = homeAt(frogX + FROG_SIZE * 0.5f);
  if (home < 0 || homes[home]) {
    loseFrog();
    return;
  }

  homes[home] = true;
  homesFilled++;
  score += 10;
  if (homesFilled == HOME_COUNT) {
    score += 25;
    level++;
    speedMul = fminf_(1.35f, speedMul + 0.05f);
    clearHomes();
    levelFlash = 1.0f;
    sfx(SFX_WIN);
  } else {
    sfx(SFX_POWERUP);
  }
  resetFrog();
}

void update(float dt) {
  if (gameOver) {
    overTime += dt;
    draw();
    if (overTime > 0.5f && input.justDown(BTN_CLICK)) init();
    return;
  }
  if (levelFlash > 0) {
    levelFlash -= dt;
    draw();
    return;
  }

  if (deathFlash > 0) deathFlash -= dt;
  for (int i = 0; i < ROAD_LANES; i++)
    roadOff[i] = fmodf_(roadOff[i] + ROAD_SPEED[i] * speedMul * dt,
                        laneWrap(ROAD_SPACING[i]));
  for (int i = 0; i < WATER_LANES; i++)
    waterOff[i] = fmodf_(waterOff[i] + WATER_SPEED[i] * speedMul * dt,
                         laneWrap(WATER_SPACING[i]));

  // Smooth analog steering on both axes; the tilt curve supplies fine control
  // near level while still allowing a quick dash at a steep angle.
  float targetVx = tiltCurve(input.tiltX, 0.06f) * FROG_SPEED;
  float targetVy = tiltCurve(input.tiltY, 0.06f) * FROG_SPEED;
  float response = clampf(STEER_RESPONSE * dt, 0.0f, 1.0f);
  frogVx = lerpf(frogVx, targetVx, response);
  frogVy = lerpf(frogVy, targetVy, response);
  frogX = clampf(frogX + frogVx * dt, 0.0f, SCREEN_W - FROG_SIZE);
  frogY = clampf(frogY + frogVy * dt, GOAL_Y, SCREEN_H - FROG_SIZE);

  int row = rowAt(frogY + FROG_SIZE * 0.5f);
  if (row < bestRow) {
    score += bestRow - row;
    bestRow = row;
    sfx(SFX_COIN, 1.0f + (START_ROW - bestRow) * 0.03f);
  }

  if (frogY <= GOAL_Y) {
    reachHome();
    draw();
    return;
  }
  if (carHit()) {
    loseFrog();
    draw();
    return;
  }
  if (row >= WATER_FIRST && row < WATER_FIRST + WATER_LANES &&
      !rideRiver(row, dt)) {
    loseFrog();
    draw();
    return;
  }

  draw();
}

}  // namespace

PT_GAME(hopper, "HOPPER", init, update)
