// Snake — tilt to steer. The dominant tilt axis picks the direction.
#include "pixeltilt/pixeltilt.h"

using namespace pt;

namespace {

constexpr int GRID_W = 16;
constexpr int GRID_H = 14;
constexpr int CELL = 4;
constexpr int OFFSET_Y = 8;  // top 8px = score bar
constexpr int MAX_LEN = GRID_W * GRID_H;
constexpr float TILT_DEADZONE = 0.30f;

struct Cell { int8_t x, y; };

Cell body[MAX_LEN];
int  length;
int  dirX, dirY;      // current heading
int  nextX, nextY;    // queued heading (applied on step)
Cell food;
int  score;
int  scoreRank;  // rank in the high-score table this run earned, -1 if none
float stepTimer, stepInterval;
bool gameOver;
float overTime;

bool onSnake(int x, int y, int skipTail) {
  for (int i = 0; i < length - skipTail; i++)
    if (body[i].x == x && body[i].y == y) return true;
  return false;
}

void placeFood() {
  do {
    food.x = (int8_t)randRange(0, GRID_W - 1);
    food.y = (int8_t)randRange(0, GRID_H - 1);
  } while (onSnake(food.x, food.y, 0));
}

void init() {
  setSfxStyle(STYLE_CHIP);
  music(MUS_ACTION);
  length = 3;
  for (int i = 0; i < length; i++) body[i] = {(int8_t)(7 - i), 7};
  dirX = nextX = 1;
  dirY = nextY = 0;
  score = 0;
  scoreRank = -1;
  stepInterval = 0.20f;
  stepTimer = 0;
  gameOver = false;
  overTime = 0;
  placeFood();
}

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

void draw() {
  clear();
  // Score bar.
  fillRect(0, 0, SCREEN_W, OFFSET_Y - 1, rgb(10, 12, 24));
  text(2, 1, "SNAKE", GREEN);
  drawNumber(40, 1, score, WHITE);
  hline(0, OFFSET_Y - 1, SCREEN_W, DARKGRAY);

  // Food pulses.
  int fx = food.x * CELL, fy = food.y * CELL + OFFSET_Y;
  fillRect(fx + 1, fy + 1, 2, 2, RED);
  pixel(fx + 1, fy + 1, rgb(255, 160, 160));

  // Snake body, head brightest.
  for (int i = length - 1; i >= 0; i--) {
    float t = length > 1 ? (float)i / (length - 1) : 0.0f;
    Color c = i == 0 ? rgb(190, 255, 190)
                     : hsv(120.0f - t * 40.0f, 0.85f, 1.0f - t * 0.55f);
    fillRect(body[i].x * CELL, body[i].y * CELL + OFFSET_Y, CELL, CELL, c);
  }

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

  // Dominant tilt axis chooses direction; reversing into yourself is ignored.
  float ax = fabsf_(input.tiltX), ay = fabsf_(input.tiltY);
  if (ax > TILT_DEADZONE || ay > TILT_DEADZONE) {
    if (ax > ay) {
      int d = input.tiltX > 0 ? 1 : -1;
      if (d != -dirX) { nextX = d; nextY = 0; }
    } else {
      int d = input.tiltY > 0 ? 1 : -1;
      if (d != -dirY) { nextX = 0; nextY = d; }
    }
  }

  stepTimer += dt;
  if (stepTimer >= stepInterval) {
    stepTimer -= stepInterval;
    dirX = nextX;
    dirY = nextY;

    int hx = body[0].x + dirX;
    int hy = body[0].y + dirY;
    bool ate = hx == food.x && hy == food.y;

    if (hx < 0 || hy < 0 || hx >= GRID_W || hy >= GRID_H ||
        onSnake(hx, hy, ate ? 0 : 1)) {
      gameOver = true;
      overTime = 0;
      scoreRank = submitScore(score);
      sfx(SFX_EXPLODE);
      sfx(SFX_LOSE);
      draw();
      return;
    }

    if (ate && length < MAX_LEN) length++;
    for (int i = length - 1; i > 0; i--) body[i] = body[i - 1];
    body[0] = {(int8_t)hx, (int8_t)hy};

    if (ate) {
      score++;
      if (score % 10 == 0) sfx(SFX_POWERUP);
      else sfx(SFX_COIN, 1.0f + (length % 8) * 0.06f);
      stepInterval = fmaxf_(0.08f, stepInterval - 0.004f);
      placeFood();
    }
  }

  draw();
}

}  // namespace

PT_GAME(snake, "SNAKE", init, update)
