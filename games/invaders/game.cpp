// Invaders — tilt to slide the cannon, CLICK to shoot. Clear the marching
// fleet before it reaches the ground; watch for their bombs.
#include "pixeltilt/pixeltilt.h"

using namespace pt;

namespace {

constexpr int TOP = 8;
constexpr int COLS = 6, ROWS = 3;
constexpr int NBOMBS = 4;
constexpr float FLEET_W = COLS * 8 - 3;  // sprite is 5px in an 8px cell

bool  alive[ROWS * COLS];
int   aliveCount;
float fleetX, fleetY, fleetDir, fleetSpeed;
float cannonX;
float bx, by;
bool  bulletLive;
struct Bomb { float x, y; bool live; };
Bomb  bombs[NBOMBS];
float bombTimer;
int   score, wave, lives, scoreRank;
bool  gameOver;
float overTime, hitFlash, anim;

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

void resetWave() {
  for (int i = 0; i < ROWS * COLS; i++) alive[i] = true;
  aliveCount = ROWS * COLS;
  fleetX = 8;
  fleetY = 10.0f + fminf_((float)(wave * 2), 10.0f);
  fleetDir = 1;
  fleetSpeed = 8.0f + wave * 3.0f;
  bulletLive = false;
  for (int i = 0; i < NBOMBS; i++) bombs[i].live = false;
  bombTimer = 1.2f;
}

void init() {
  setSfxStyle(STYLE_ARCADE);
  music(MUS_TENSE);
  score = 0;
  wave = 0;
  lives = 3;
  scoreRank = -1;
  cannonX = SCREEN_W / 2.0f;
  gameOver = false;
  overTime = hitFlash = anim = 0;
  resetWave();
}

void draw() {
  clear();
  fillRect(0, 0, SCREEN_W, TOP - 1, rgb(10, 12, 24));
  drawNumber(2, 1, score, WHITE);
  for (int i = 0; i < lives; i++) fillRect(56 - i * 4, 2, 3, 3, GREEN);
  hline(0, TOP - 1, SCREEN_W, DARKGRAY);

  const Color rowColor[ROWS] = {MAGENTA, CYAN, GREEN};
  bool phase = fmodf_(anim, 0.8f) < 0.4f;
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) {
      if (!alive[r * COLS + c]) continue;
      int ax = (int)fleetX + c * 8, ay = (int)fleetY + r * 8;
      fillRect(ax, ay, 5, 3, rowColor[r]);
      pixel(ax + 1, ay + 1, BLACK);
      pixel(ax + 3, ay + 1, BLACK);
      if (phase) {
        pixel(ax, ay + 3, rowColor[r]);
        pixel(ax + 4, ay + 3, rowColor[r]);
      } else {
        pixel(ax + 1, ay + 3, rowColor[r]);
        pixel(ax + 3, ay + 3, rowColor[r]);
      }
    }

  Color cc = hitFlash > 0 ? RED : GREEN;
  int cx = (int)cannonX;
  fillRect(cx - 2, 58, 5, 3, cc);
  pixel(cx, 57, cc);
  pixel(cx, 56, cc);

  if (bulletLive) vline((int)bx, (int)by, 2, WHITE);
  for (int i = 0; i < NBOMBS; i++)
    if (bombs[i].live) {
      pixel((int)bombs[i].x, (int)bombs[i].y, YELLOW);
      pixel((int)bombs[i].x, (int)bombs[i].y - 1, rgb(120, 90, 20));
    }

  if (gameOver) {
    fillRect(8, 22, 48, 18, rgb(20, 4, 4));
    rect(8, 22, 48, 18, RED);
    textCentered(25, "GAME OVER", WHITE);
    textCentered(33, "CLICK-RETRY", GRAY);
    if (scoreRank == 0) textCentered(44, "NEW BEST!", YELLOW);
  }
}

void update(float dt) {
  if (gameOver) {
    overTime += dt;
    draw();
    if (overTime > 0.5f && input.justDown(BTN_CLICK)) init();
    return;
  }

  anim += dt;
  if (hitFlash > 0) hitFlash -= dt;
  cannonX = clampf(cannonX + input.tiltX * 55.0f * dt, 3.0f, SCREEN_W - 4.0f);

  if (input.justDown(BTN_CLICK) && !bulletLive) {
    bulletLive = true;
    bx = cannonX;
    by = 55;
    sfx(SFX_LASER);
  }
  if (bulletLive) {
    by -= 90.0f * dt;
    if (by < TOP) bulletLive = false;
  }

  fleetX += fleetDir * fleetSpeed * dt;
  if (fleetX < 2 || fleetX + FLEET_W > SCREEN_W - 2) {
    fleetX = clampf(fleetX, 2.0f, SCREEN_W - 2 - FLEET_W);
    fleetDir = -fleetDir;
    fleetY += 3;
    fleetSpeed *= 1.1f;
    if (fleetY >= 30) sfx(SFX_ALARM);
  }

  if (bulletLive) {
    for (int r = ROWS - 1; r >= 0; r--)
      for (int c = 0; c < COLS; c++) {
        if (!alive[r * COLS + c]) continue;
        float ax = fleetX + c * 8, ay = fleetY + r * 8;
        if (bx >= ax && bx <= ax + 4 && by >= ay && by <= ay + 3) {
          alive[r * COLS + c] = false;
          aliveCount--;
          score += (ROWS - r) * 10;
          fleetSpeed += 1.5f;
          bulletLive = false;
          sfx(SFX_COIN, 1.0f + (ROWS - 1 - r) * 0.15f);
          r = -1;  // break both loops
          break;
        }
      }
  }
  if (aliveCount == 0) {
    wave++;
    score += 50;
    sfx(SFX_POWERUP);
    resetWave();
  }

  // Bottom-most living alien: invasion check + bomb source.
  int lowRow = -1;
  for (int r = ROWS - 1; r >= 0 && lowRow < 0; r--)
    for (int c = 0; c < COLS; c++)
      if (alive[r * COLS + c]) { lowRow = r; break; }
  if (lowRow >= 0 && fleetY + lowRow * 8 + 4 >= 55) {
    gameOver = true;
    overTime = 0;
    scoreRank = submitScore(score);
    sfx(SFX_LOSE);
    draw();
    return;
  }

  bombTimer -= dt;
  if (bombTimer <= 0) {
    bombTimer = fmaxf_(0.4f, 1.4f - wave * 0.15f) + randf() * 0.5f;
    for (int i = 0; i < NBOMBS; i++) {
      if (bombs[i].live) continue;
      // Drop from the lowest alien in a random occupied column.
      int c = randRange(0, COLS - 1);
      for (int tries = 0; tries < COLS; tries++, c = (c + 1) % COLS) {
        int low = -1;
        for (int r = ROWS - 1; r >= 0; r--)
          if (alive[r * COLS + c]) { low = r; break; }
        if (low < 0) continue;
        bombs[i].x = fleetX + c * 8 + 2;
        bombs[i].y = fleetY + low * 8 + 4;
        bombs[i].live = true;
        break;
      }
      break;
    }
  }
  for (int i = 0; i < NBOMBS; i++) {
    if (!bombs[i].live) continue;
    bombs[i].y += (35.0f + wave * 6.0f) * dt;
    if (bombs[i].y > SCREEN_H - 1) { bombs[i].live = false; continue; }
    if (bombs[i].y >= 56 && fabsf_(bombs[i].x - cannonX) <= 3.0f) {
      bombs[i].live = false;
      lives--;
      hitFlash = 0.5f;
      sfx(SFX_EXPLODE);
      if (lives <= 0) {
        gameOver = true;
        overTime = 0;
        scoreRank = submitScore(score);
        sfx(SFX_LOSE);
      }
    }
  }

  draw();
}

}  // namespace

PT_GAME(invaders, "INVADERS", init, update)
