// Pong — tilt up/down to move your paddle. Rally past the AI to score;
// the run ends when the AI takes 3 points off you.
#include "pixeltilt/pixeltilt.h"

using namespace pt;

namespace {

constexpr int TOP = 8;            // score bar height
constexpr float PAD_H = 12.0f;
constexpr float PLAYER_X = 2.0f;  // left edge of the 2px-wide player paddle
constexpr float AI_X = 60.0f;

float playerY, aiY;      // paddle centers
float bx, by, bvx, bvy;  // ball
float serveTimer;
int   playerScore, aiScore;
int   scoreRank;
bool  gameOver;
float overTime;

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

void serve(int dir) {
  bx = SCREEN_W / 2.0f;
  by = (TOP + SCREEN_H) / 2.0f;
  bvx = dir * 42.0f;
  bvy = (randf() - 0.5f) * 50.0f;
  serveTimer = 0.8f;
}

void init() {
  setSfxStyle(STYLE_ARCADE);
  music(MUS_ACTION);
  playerY = aiY = (TOP + SCREEN_H) / 2.0f;
  playerScore = aiScore = 0;
  scoreRank = -1;
  gameOver = false;
  overTime = 0;
  serve(randf() < 0.5f ? -1 : 1);
}

void draw() {
  clear();
  fillRect(0, 0, SCREEN_W, TOP - 1, rgb(10, 12, 24));
  text(2, 1, "PONG", CYAN);
  drawNumber(40, 1, playerScore, GREEN);
  text(48, 1, "-", GRAY);
  drawNumber(54, 1, aiScore, RED);
  hline(0, TOP - 1, SCREEN_W, DARKGRAY);

  for (int y = TOP + 1; y < SCREEN_H; y += 4) vline(31, y, 2, DARKGRAY);

  fillRect((int)PLAYER_X, (int)(playerY - PAD_H / 2), 2, (int)PAD_H, GREEN);
  fillRect((int)AI_X, (int)(aiY - PAD_H / 2), 2, (int)PAD_H, ORANGE);
  fillRect((int)bx - 1, (int)by - 1, 2, 2, WHITE);

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

  playerY += tiltCurve(input.tiltY) * 90.0f * dt;
  playerY = clampf(playerY, TOP + PAD_H / 2, SCREEN_H - PAD_H / 2);

  // AI chases the ball only while it approaches; its top speed creeps up as
  // you score so long runs stay contested.
  float aiSpeed = fminf_(30.0f + playerScore * 2.5f, 55.0f);
  float target = bvx > 0 ? by : (TOP + SCREEN_H) / 2.0f;
  if (aiY < target) aiY = fminf_(aiY + aiSpeed * dt, target);
  else              aiY = fmaxf_(aiY - aiSpeed * dt, target);
  aiY = clampf(aiY, TOP + PAD_H / 2, SCREEN_H - PAD_H / 2);

  if (serveTimer > 0) {
    serveTimer -= dt;
    draw();
    return;
  }

  bx += bvx * dt;
  by += bvy * dt;
  if (by < TOP + 1) { by = TOP + 1; bvy = fabsf_(bvy); sfx(SFX_BOUNCE, 1.3f); }
  if (by > SCREEN_H - 2) { by = SCREEN_H - 2; bvy = -fabsf_(bvy); sfx(SFX_BOUNCE, 1.3f); }

  // Paddle bounces speed the ball up; hit offset steers it.
  if (bvx < 0 && bx <= PLAYER_X + 3 && bx >= PLAYER_X &&
      fabsf_(by - playerY) <= PAD_H / 2 + 1) {
    bx = PLAYER_X + 3;
    bvx = -bvx * 1.06f;
    bvy = clampf(bvy + (by - playerY) * 4.0f, -70.0f, 70.0f);
    sfx(SFX_BOUNCE);
  }
  if (bvx > 0 && bx >= AI_X - 1 && bx <= AI_X + 2 &&
      fabsf_(by - aiY) <= PAD_H / 2 + 1) {
    bx = AI_X - 1;
    bvx = -bvx * 1.06f;
    bvy = clampf(bvy + (by - aiY) * 4.0f, -70.0f, 70.0f);
    sfx(SFX_BOUNCE, 0.85f);
  }
  bvx = clampf(bvx, -110.0f, 110.0f);

  if (bx > SCREEN_W + 2) {
    playerScore++;
    sfx(SFX_COIN);
    serve(-1);
  } else if (bx < -2) {
    aiScore++;
    if (aiScore >= 3) {
      gameOver = true;
      overTime = 0;
      scoreRank = submitScore(playerScore);
      sfx(SFX_LOSE);
    } else {
      sfx(SFX_HURT);
      serve(1);
    }
  }

  draw();
}

}  // namespace

PT_GAME(pong, "PONG", init, update)
