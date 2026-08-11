// Tunnel — fly through an endless scrolling cave. Tilt up/down to steer;
// the passage narrows and speeds up the deeper you get.
#include "pixeltilt/pixeltilt.h"

using namespace pt;

namespace {

constexpr int TOP = 8;
constexpr int SHIP_X = 10;

uint8_t center[SCREEN_W];
uint8_t half[SCREEN_W];
float shipY, shipVy;
float speed, scrollAcc;
int   gapHalf;
int   colCount;
int   score, scoreRank;
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

void newColumn() {
  for (int x = 0; x < SCREEN_W - 1; x++) {
    center[x] = center[x + 1];
    half[x] = half[x + 1];
  }
  int c = clampi(center[SCREEN_W - 2] + randRange(-2, 2),
                 TOP + 1 + gapHalf, SCREEN_H - 2 - gapHalf);
  center[SCREEN_W - 1] = (uint8_t)c;
  half[SCREEN_W - 1] = (uint8_t)gapHalf;
}

void init() {
  setSfxStyle(STYLE_GRIT);
  music(MUS_ACTION);
  gapHalf = 14;
  for (int x = 0; x < SCREEN_W; x++) {
    center[x] = (TOP + SCREEN_H) / 2;
    half[x] = (uint8_t)gapHalf;
  }
  shipY = (TOP + SCREEN_H) / 2.0f;
  shipVy = 0;
  speed = 26;
  scrollAcc = 0;
  colCount = 0;
  score = 0;
  scoreRank = -1;
  gameOver = false;
  overTime = 0;
}

void draw() {
  clear();
  for (int x = 0; x < SCREEN_W; x++) {
    int topWall = center[x] - half[x];
    int botWall = center[x] + half[x];
    Color rock = hsv(18.0f + sinf_((x + colCount) * 0.25f) * 8.0f, 0.6f, 0.35f);
    vline(x, TOP, topWall - TOP, rock);
    vline(x, botWall, SCREEN_H - botWall, rock);
    pixel(x, topWall, rgb(190, 120, 60));
    pixel(x, botWall, rgb(190, 120, 60));
  }

  int sy = (int)shipY;
  pixel(SHIP_X - 2, sy, rgb(0, 90, 130));
  hline(SHIP_X - 1, sy, 2, CYAN);
  pixel(SHIP_X + 1, sy, WHITE);
  pixel(SHIP_X - 1, sy - 1, rgb(0, 120, 160));
  pixel(SHIP_X - 1, sy + 1, rgb(0, 120, 160));

  fillRect(0, 0, SCREEN_W, TOP - 1, rgb(10, 12, 24));
  text(2, 1, "TUNNEL", ORANGE);
  drawNumber(40, 1, score, WHITE);
  hline(0, TOP - 1, SCREEN_W, DARKGRAY);

  if (gameOver) {
    fillCircle(SHIP_X, sy, 2 + (int)(fminf_(overTime, 0.4f) * 10), RED);
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

  speed = fminf_(26.0f + colCount * 0.02f, 60.0f);
  scrollAcc += speed * dt;
  while (scrollAcc >= 1.0f) {
    scrollAcc -= 1.0f;
    colCount++;
    if (colCount % 90 == 0 && gapHalf > 5) { gapHalf--; sfx(SFX_ALARM); }
    if (colCount % 250 == 0) sfx(SFX_COIN);
    newColumn();
  }
  score = colCount / 5;

  shipVy = lerpf(shipVy, input.tiltY * 50.0f, clampf(10.0f * dt, 0, 1));
  shipY = clampf(shipY + shipVy * dt, TOP + 1.0f, SCREEN_H - 2.0f);

  int topWall = center[SHIP_X] - half[SHIP_X];
  int botWall = center[SHIP_X] + half[SHIP_X];
  if (shipY - 1 <= topWall || shipY + 1 >= botWall) {
    gameOver = true;
    overTime = 0;
    scoreRank = submitScore(score);
    sfx(SFX_EXPLODE);
    sfx(SFX_LOSE);
  }

  draw();
}

}  // namespace

PT_GAME(tunnel, "TUNNEL", init, update)
