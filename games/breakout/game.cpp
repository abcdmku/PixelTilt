// Breakout — tilt left/right moves the paddle, CLICK launches the ball.
#include "pixeltilt/pixeltilt.h"

using namespace pt;

namespace {

constexpr int COLS = 8, ROWS = 5;
constexpr int BRICK_W = 8, BRICK_H = 3;
constexpr int BRICK_TOP = 8;
constexpr int PADDLE_Y = 60;
constexpr int PADDLE_W = 13;

bool  bricks[ROWS][COLS];
int   bricksLeft;
float paddleX;              // center
float bx, by, bvx, bvy;     // ball
bool  stuck;                // ball riding the paddle, waiting for launch
int   lives, score, wave;
int   scoreRank;  // high-score table rank for this run, -1 if none
bool  gameOver, winFlash;
float flashTime;

void resetBricks() {
  bricksLeft = ROWS * COLS;
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) bricks[r][c] = true;
}

void resetBall() {
  stuck = true;
  bx = paddleX;
  by = PADDLE_Y - 2;
  bvx = bvy = 0;
}

void init() {
  paddleX = SCREEN_W / 2.0f;
  lives = 3;
  score = 0;
  scoreRank = -1;
  wave = 0;
  gameOver = false;
  winFlash = false;
  flashTime = 0;
  resetBricks();
  resetBall();
}

float ballSpeed() { return 42.0f + wave * 6.0f + score * 0.15f; }

void launch() {
  stuck = false;
  // Launch angle biased by current tilt so the player has some control.
  float a = -PI / 2.0f + input.tiltX * 0.7f;
  bvx = cosf_(a) * ballSpeed();
  bvy = sinf_(a) * ballSpeed();
}

void draw() {
  clear();
  fillRect(0, 0, SCREEN_W, 7, rgb(10, 12, 24));
  char buf[8];
  int s = score, i = 0;
  char tmp[8]; int t = 0;
  if (s == 0) buf[i++] = '0';
  while (s > 0) { tmp[t++] = '0' + s % 10; s /= 10; }
  while (t > 0) buf[i++] = tmp[--t];
  buf[i] = 0;
  text(2, 1, buf, WHITE);
  for (int l = 0; l < lives; l++) fillRect(SCREEN_W - 4 - l * 4, 2, 3, 3, RED);
  hline(0, 7, SCREEN_W, DARKGRAY);

  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      if (bricks[r][c]) {
        Color col = hsv(r * 32.0f + wave * 60.0f, 0.9f, 1.0f);
        fillRect(c * BRICK_W, BRICK_TOP + r * (BRICK_H + 1), BRICK_W - 1, BRICK_H, col);
      }

  int px0 = (int)(paddleX - PADDLE_W / 2.0f);
  fillRect(px0, PADDLE_Y, PADDLE_W, 2, CYAN);
  pixel(px0, PADDLE_Y, WHITE);
  pixel(px0 + PADDLE_W - 1, PADDLE_Y, WHITE);

  fillRect((int)bx - 1, (int)by - 1, 2, 2, WHITE);

  if (gameOver) {
    fillRect(8, 24, 48, 18, rgb(20, 4, 4));
    rect(8, 24, 48, 18, RED);
    textCentered(27, "GAME OVER", WHITE);
    textCentered(35, "CLICK-RETRY", GRAY);
    if (scoreRank == 0) textCentered(46, "NEW BEST!", YELLOW);
  } else if (stuck) {
    textCentered(44, "CLICK!", hsv(flashTime * 200.0f, 0.6f, 1.0f));
  }
}

void update(float dt) {
  flashTime += dt;

  if (gameOver) {
    draw();
    if (input.justDown(BTN_CLICK)) init();
    return;
  }

  // Absolute tilt->paddle mapping: full left tilt = far left, etc.
  float target = SCREEN_W / 2.0f + input.tiltX * (SCREEN_W / 2.0f - 2.0f);
  paddleX = lerpf(paddleX, target, clampf(dt * 14.0f, 0.0f, 1.0f));
  paddleX = clampf(paddleX, PADDLE_W / 2.0f, SCREEN_W - PADDLE_W / 2.0f);

  if (stuck) {
    bx = paddleX;
    by = PADDLE_Y - 2;
    if (input.justDown(BTN_CLICK)) launch();
    draw();
    return;
  }

  bx += bvx * dt;
  by += bvy * dt;

  // Walls.
  if (bx < 1)             { bx = 1; bvx = fabsf_(bvx); }
  if (bx > SCREEN_W - 1)  { bx = SCREEN_W - 1; bvx = -fabsf_(bvx); }
  if (by < 8)             { by = 8; bvy = fabsf_(bvy); }

  // Paddle: bounce angle depends on where the ball hits.
  if (bvy > 0 && by >= PADDLE_Y - 1 && by <= PADDLE_Y + 2 &&
      fabsf_(bx - paddleX) <= PADDLE_W / 2.0f + 1) {
    float hit = clampf((bx - paddleX) / (PADDLE_W / 2.0f), -1.0f, 1.0f);
    float a = -PI / 2.0f + hit * 1.1f;
    float sp = ballSpeed();
    bvx = cosf_(a) * sp;
    bvy = sinf_(a) * sp;
    by = PADDLE_Y - 1;
  }

  // Bricks.
  int c = floori(bx / BRICK_W);
  int r = floori((by - BRICK_TOP) / (BRICK_H + 1));
  if (r >= 0 && r < ROWS && c >= 0 && c < COLS && bricks[r][c]) {
    // Only solid if inside the brick's visible band (not the 1px gap).
    float localY = by - BRICK_TOP - r * (BRICK_H + 1);
    if (localY <= BRICK_H) {
      bricks[r][c] = false;
      bricksLeft--;
      score += 5 + (ROWS - 1 - r);
      // Reflect on the axis of shallower penetration.
      float cx = c * BRICK_W + BRICK_W / 2.0f;
      float cy = BRICK_TOP + r * (BRICK_H + 1) + BRICK_H / 2.0f;
      float dx = (bx - cx) / BRICK_W, dy = (by - cy) / BRICK_H;
      if (fabsf_(dx) > fabsf_(dy)) bvx = -bvx; else bvy = -bvy;

      if (bricksLeft == 0) {
        wave++;
        score += 50;
        resetBricks();
        resetBall();
      }
    }
  }

  // Bottom: lose a life.
  if (by > SCREEN_H + 2) {
    lives--;
    if (lives <= 0) {
      gameOver = true;
      scoreRank = submitScore(score);
    } else resetBall();
  }

  draw();
}

}  // namespace

PT_GAME(breakout, "BREAKOUT", init, update)
