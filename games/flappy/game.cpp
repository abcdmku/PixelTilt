// Flappy — tap CLICK (or UP) to flap through the pipe gaps. One point per
// pipe; the gaps tighten and the scroll speeds up as you go.
#include "pixeltilt/pixeltilt.h"

using namespace pt;

namespace {

constexpr int TOP = 8;
constexpr int BIRD_X = 13;   // left edge of the 3x3 bird
constexpr int PIPE_W = 6;
constexpr int NPIPES = 3;
constexpr float PIPE_SPACING = 30.0f;

struct Pipe { float x; int gapY; bool passed; };

Pipe  pipes[NPIPES];
float birdY, vy;
int   gapH;
int   score, scoreRank;
bool  started, gameOver;
float overTime, idleT;

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

int randGapY() { return randRange(TOP + 3 + gapH / 2, SCREEN_H - 4 - gapH / 2); }

void init() {
  birdY = 32;
  vy = 0;
  gapH = 22;
  score = 0;
  scoreRank = -1;
  started = false;
  gameOver = false;
  overTime = idleT = 0;
  for (int i = 0; i < NPIPES; i++) {
    pipes[i].x = 70.0f + i * PIPE_SPACING;
    pipes[i].gapY = randGapY();
    pipes[i].passed = false;
  }
}

bool flapPressed() {
  return input.justDown(BTN_CLICK) || input.justDown(BTN_UP);
}

void draw() {
  clear(rgb(4, 8, 18));
  for (int i = 0; i < 12; i++)
    pixel((i * 31 + 7) % SCREEN_W, TOP + (i * 17 + 3) % 54, rgb(35, 38, 55));

  for (int i = 0; i < NPIPES; i++) {
    int x = (int)pipes[i].x;
    if (x > SCREEN_W || x + PIPE_W < 0) continue;
    int gTop = pipes[i].gapY - gapH / 2;
    int gBot = pipes[i].gapY + gapH / 2;
    fillRect(x, TOP, PIPE_W, gTop - TOP, rgb(20, 130, 40));
    fillRect(x, gBot, PIPE_W, SCREEN_H - gBot, rgb(20, 130, 40));
    hline(x, gTop - 1, PIPE_W, rgb(60, 220, 90));
    hline(x, gBot, PIPE_W, rgb(60, 220, 90));
    vline(x, TOP, gTop - TOP, rgb(12, 80, 25));
    vline(x, gBot, SCREEN_H - gBot, rgb(12, 80, 25));
  }

  int by = (int)birdY;
  fillRect(BIRD_X, by - 1, 3, 3, YELLOW);
  pixel(BIRD_X + 2, by - 1, WHITE);
  pixel(BIRD_X + 3, by, ORANGE);
  pixel(BIRD_X, by + (vy < 0 ? -1 : 1), rgb(200, 150, 20));

  fillRect(0, 0, SCREEN_W, TOP - 1, rgb(10, 12, 24));
  text(2, 1, "FLAPPY", YELLOW);
  drawNumber(40, 1, score, WHITE);
  hline(0, TOP - 1, SCREEN_W, DARKGRAY);

  if (!started) {
    textCentered(44, "CLICK TO FLAP", fmodf_(idleT, 0.8f) < 0.5f ? WHITE : GRAY);
  }
  if (gameOver) {
    fillRect(8, 24, 48, 18, rgb(20, 4, 4));
    rect(8, 24, 48, 18, RED);
    textCentered(27, "GAME OVER", WHITE);
    textCentered(35, "CLICK-RETRY", GRAY);
    if (scoreRank == 0) textCentered(46, "NEW BEST!", YELLOW);
  }
}

void die() {
  gameOver = true;
  overTime = 0;
  scoreRank = submitScore(score);
}

void update(float dt) {
  if (gameOver) {
    overTime += dt;
    draw();
    if (overTime > 0.5f && input.justDown(BTN_CLICK)) init();
    return;
  }

  if (!started) {
    idleT += dt;
    birdY = 32.0f + sinf_(idleT * 3.0f) * 2.0f;
    draw();
    if (flapPressed()) {
      started = true;
      vy = -50;
    }
    return;
  }

  if (flapPressed()) vy = -50;
  vy += 140.0f * dt;
  birdY += vy * dt;

  float speed = fminf_(26.0f + score * 0.4f, 45.0f);
  gapH = maxi(15, 22 - score / 8);

  for (int i = 0; i < NPIPES; i++) {
    Pipe& p = pipes[i];
    p.x -= speed * dt;
    if (p.x < -PIPE_W - 1) {
      float maxX = p.x;
      for (int j = 0; j < NPIPES; j++) maxX = fmaxf_(maxX, pipes[j].x);
      p.x = maxX + PIPE_SPACING;
      p.gapY = randGapY();
      p.passed = false;
    }
    if (!p.passed && p.x + PIPE_W < BIRD_X) {
      p.passed = true;
      score++;
    }
    if (BIRD_X + 3 > p.x && BIRD_X < p.x + PIPE_W) {
      if (birdY - 1 < p.gapY - gapH / 2 || birdY + 1 > p.gapY + gapH / 2) {
        die();
        draw();
        return;
      }
    }
  }

  if (birdY < TOP + 1 || birdY > SCREEN_H - 2) {
    die();
    draw();
    return;
  }

  draw();
}

}  // namespace

PT_GAME(flappy, "FLAPPY", init, update)
