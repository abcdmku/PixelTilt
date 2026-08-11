// Meteors — tilt left/right to dodge the falling rocks. Every meteor that
// slips past scores a point; the rain thickens the longer you last.
#include "pixeltilt/pixeltilt.h"

using namespace pt;

namespace {

constexpr int TOP = 8;
constexpr int NMET = 12;
constexpr int NSTARS = 18;
constexpr float SHIP_Y = 58.0f;

struct Met { float x, y, vy, r; };
struct Star { float x, y, v; };

Met  mets[NMET];
Star stars[NSTARS];
float shipX;
float difficulty;
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

void spawnMet(Met& m, float extraLift) {
  m.r = (float)randRange(1, 3);
  m.x = (float)randRange(2, SCREEN_W - 3);
  m.y = TOP - m.r - randf() * 12.0f - extraLift;
  m.vy = (18.0f + randf() * 22.0f) * (1.0f + difficulty * 0.6f);
}

void init() {
  setSfxStyle(STYLE_GRIT);
  music(MUS_TENSE);
  shipX = SCREEN_W / 2.0f;
  difficulty = 0;
  score = 0;
  scoreRank = -1;
  gameOver = false;
  overTime = 0;
  // Stagger the first wave so it trickles in.
  for (int i = 0; i < NMET; i++) spawnMet(mets[i], (float)(i * 14));
  for (int i = 0; i < NSTARS; i++) {
    stars[i].x = (float)randRange(0, SCREEN_W - 1);
    stars[i].y = (float)randRange(TOP, SCREEN_H - 1);
    stars[i].v = 6.0f + randf() * 14.0f;
  }
}

void draw() {
  clear();
  for (int i = 0; i < NSTARS; i++)
    pixel((int)stars[i].x, (int)stars[i].y, rgb(50, 50, 70));

  fillRect(0, 0, SCREEN_W, TOP - 1, rgb(10, 12, 24));
  text(2, 1, "METEORS", MAGENTA);
  drawNumber(46, 1, score, WHITE);
  hline(0, TOP - 1, SCREEN_W, DARKGRAY);

  for (int i = 0; i < NMET; i++) {
    const Met& m = mets[i];
    if (m.y + m.r < TOP) continue;
    fillCircle((int)m.x, (int)m.y, (int)m.r, rgb(140, 100, 70));
    pixel((int)m.x - 1, (int)m.y - 1, rgb(210, 170, 130));
  }

  int sx = (int)shipX;
  pixel(sx, (int)SHIP_Y - 2, WHITE);
  hline(sx - 1, (int)SHIP_Y - 1, 3, CYAN);
  hline(sx - 2, (int)SHIP_Y, 5, rgb(30, 150, 200));
  if (!gameOver && (rand_() & 1)) pixel(sx, (int)SHIP_Y + 1, ORANGE);

  if (gameOver) {
    fillCircle(sx, (int)SHIP_Y, 2 + (int)(fminf_(overTime, 0.4f) * 12), RED);
    fillRect(8, 22, 48, 18, rgb(20, 4, 4));
    rect(8, 22, 48, 18, RED);
    textCentered(25, "GAME OVER", WHITE);
    textCentered(33, "CLICK-RETRY", GRAY);
    if (scoreRank == 0) textCentered(44, "NEW BEST!", YELLOW);
  }
}

void update(float dt) {
  if (gameOver) {
    bool shown = overTime > 0.5f;
    overTime += dt;
    draw();
    if (!shown && overTime > 0.5f) sfx(SFX_LOSE);
    if (overTime > 0.5f && input.justDown(BTN_CLICK)) init();
    return;
  }

  difficulty += dt * 0.03f;
  shipX = clampf(shipX + tiltCurve(input.tiltX) * 75.0f * dt, 3.0f, SCREEN_W - 4.0f);

  for (int i = 0; i < NSTARS; i++) {
    stars[i].y += stars[i].v * dt;
    if (stars[i].y >= SCREEN_H) {
      stars[i].y = TOP;
      stars[i].x = (float)randRange(0, SCREEN_W - 1);
    }
  }

  for (int i = 0; i < NMET; i++) {
    Met& m = mets[i];
    m.y += m.vy * dt;
    if (m.y - m.r > SCREEN_H) {
      score++;
      sfx(SFX_COIN, 1.6f - m.r * 0.2f);
      if (score % 25 == 0) sfx(SFX_POWERUP);
      spawnMet(m, 0);
      continue;
    }
    if (fabsf_(m.x - shipX) < m.r + 2.5f && fabsf_(m.y - SHIP_Y) < m.r + 2.0f) {
      gameOver = true;
      overTime = 0;
      sfx(SFX_EXPLODE);
      scoreRank = submitScore(score);
    }
  }

  draw();
}

}  // namespace

PT_GAME(meteors, "METEORS", init, update)
