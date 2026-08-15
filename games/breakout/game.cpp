// Breakout: tilt left/right moves the paddle, CLICK launches the first ball.
#include "pixeltilt/pixeltilt.h"

using namespace pt;

namespace {

constexpr int COLS = 8, ROWS = 5;
constexpr int BRICK_W = 8, BRICK_H = 3;
constexpr int BRICK_TOP = 8;
constexpr int PADDLE_Y = 60;
constexpr int PADDLE_W = 13;
constexpr int WIDE_PADDLE_W = 21;
constexpr int MAX_LIVES = 5;
constexpr int MAX_POWERUPS = 4;
constexpr int MAX_BALLS = 5;
constexpr int MAX_SHOTS = 8;
constexpr float POWERUP_FALL_SPEED = 14.0f;
constexpr float LASER_SPEED = 52.0f;
constexpr float LASER_DELAY = 0.22f;
constexpr float SLOW_SCALE = 0.72f;

enum PowerupType : uint8_t {
  POWER_MULTI,
  POWER_WIDE,
  POWER_LASER,
  POWER_SLOW,
  POWER_STICKY,
  POWER_LIFE,
  POWER_COUNT,
};

constexpr PowerupType DROP_ORDER[POWER_COUNT] = {
  POWER_MULTI, POWER_WIDE, POWER_LASER,
  POWER_SLOW, POWER_STICKY, POWER_LIFE,
};

struct Powerup {
  float x, y;
  PowerupType type;
  bool active;
};

struct Ball {
  float x, y;
  float vx, vy;
  float stuckOffset;
  bool active;
  bool stuck;
};

struct Shot {
  float x, y;
  bool active;
};

bool bricks[ROWS][COLS];
int bricksLeft;
int bricksUntilDrop;
int dropSerial;
float paddleX;
Ball balls[MAX_BALLS];
Shot shots[MAX_SHOTS];
Powerup powerups[MAX_POWERUPS];
int lives, score, wave;
int scoreRank;
bool gameOver;
bool wideActive, slowActive, laserActive, stickyActive;
float flashTime;
float laserCooldown;

int paddleWidth() { return wideActive ? WIDE_PADDLE_W : PADDLE_W; }

int activeBallCount() {
  int count = 0;
  for (int i = 0; i < MAX_BALLS; i++)
    if (balls[i].active) count++;
  return count;
}

int stuckBallCount() {
  int count = 0;
  for (int i = 0; i < MAX_BALLS; i++)
    if (balls[i].active && balls[i].stuck) count++;
  return count;
}

bool allBallsStuck() {
  int active = activeBallCount();
  return active > 0 && stuckBallCount() == active;
}

void clearRoundPowerups() {
  for (int i = 0; i < MAX_POWERUPS; i++) powerups[i].active = false;
  for (int i = 0; i < MAX_SHOTS; i++) shots[i].active = false;
  wideActive = false;
  slowActive = false;
  laserActive = false;
  stickyActive = false;
  laserCooldown = 0.0f;
}

void resetBricks() {
  bricksLeft = ROWS * COLS;
  bricksUntilDrop = randRange(7, 20);
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) bricks[r][c] = true;
}

void resetBalls() {
  clearRoundPowerups();
  for (int i = 0; i < MAX_BALLS; i++) balls[i].active = false;
  balls[0] = {paddleX, PADDLE_Y - 2.0f, 0.0f, 0.0f, 0.0f, true, true};
}

void init() {
  setSfxStyle(STYLE_ARCADE);
  music(MUS_ACTION);
  paddleX = SCREEN_W / 2.0f;
  lives = 3;
  score = 0;
  scoreRank = -1;
  wave = 0;
  dropSerial = 0;
  gameOver = false;
  flashTime = 0.0f;
  resetBricks();
  resetBalls();
}

float ballSpeed() {
  float speed = 42.0f + wave * 6.0f + score * 0.15f;
  return slowActive ? speed * SLOW_SCALE : speed;
}

void releaseStuckBalls(float fallbackSteer) {
  int pw = paddleWidth();
  for (int i = 0; i < MAX_BALLS; i++) {
    Ball& b = balls[i];
    if (!b.active || !b.stuck) continue;
    float steer = clampf(b.stuckOffset / (pw / 2.0f), -1.0f, 1.0f);
    if (fabsf_(b.stuckOffset) < 0.1f) steer = fallbackSteer;
    float angle = -PI / 2.0f + steer * 1.1f;
    float speed = ballSpeed();
    b.vx = cosf_(angle) * speed;
    b.vy = sinf_(angle) * speed;
    b.stuck = false;
  }
}

bool addMultiball() {
  Ball* source = nullptr;
  for (int i = 0; i < MAX_BALLS; i++) {
    if (balls[i].active && !balls[i].stuck) {
      source = &balls[i];
      break;
    }
  }
  if (!source) {
    for (int i = 0; i < MAX_BALLS; i++) {
      if (balls[i].active) {
        source = &balls[i];
        break;
      }
    }
  }
  if (!source) return false;

  float sourceAngle = atan2f_(source->vy, source->vx);
  float sourceSpeed = sqrtf_(source->vx * source->vx + source->vy * source->vy);
  if (sourceSpeed < 1.0f) sourceSpeed = ballSpeed();
  bool added = false;

  for (int direction = -1; direction <= 1; direction += 2) {
    for (int i = 0; i < MAX_BALLS; i++) {
      if (balls[i].active) continue;
      Ball& b = balls[i];
      b.x = source->x;
      b.y = source->y;
      b.stuckOffset = clampf(source->stuckOffset + direction * 3.0f,
                             -paddleWidth() / 2.0f + 1.0f,
                             paddleWidth() / 2.0f - 1.0f);
      b.active = true;
      b.stuck = source->stuck;
      if (b.stuck) {
        b.x = paddleX + b.stuckOffset;
        b.vx = b.vy = 0.0f;
      } else {
        float angle = sourceAngle + direction * 0.38f;
        b.vx = cosf_(angle) * sourceSpeed;
        b.vy = sinf_(angle) * sourceSpeed;
      }
      added = true;
      break;
    }
  }
  return added;
}

void spawnPowerup(float x, float y) {
  if (--bricksUntilDrop > 0) return;

  for (int i = 0; i < MAX_POWERUPS; i++) {
    if (powerups[i].active) continue;
    PowerupType type = DROP_ORDER[dropSerial % POWER_COUNT];
    dropSerial++;
    if (type == POWER_LIFE && lives >= MAX_LIVES) type = POWER_MULTI;
    powerups[i] = {clampf(x, 3.0f, SCREEN_W - 4.0f), y, type, true};
    bricksUntilDrop = randRange(7, 20);
    return;
  }

  // All slots are busy. Try again on the next broken brick.
  bricksUntilDrop = 1;
}

void fireLasers() {
  int pw = paddleWidth();
  float muzzles[2] = {
    paddleX - pw / 2.0f + 2.0f,
    paddleX + pw / 2.0f - 2.0f,
  };
  bool fired = false;

  for (int side = 0; side < 2; side++) {
    for (int i = 0; i < MAX_SHOTS; i++) {
      if (shots[i].active) continue;
      shots[i] = {clampf(muzzles[side], 1.0f, SCREEN_W - 2.0f),
                  PADDLE_Y - 2.0f, true};
      fired = true;
      break;
    }
  }

  if (fired) {
    laserCooldown = LASER_DELAY;
    sfx(SFX_LASER);
  }
}

void collectPowerup(PowerupType type) {
  score += 20;
  if (type == POWER_MULTI) {
    addMultiball();
  } else if (type == POWER_WIDE) {
    wideActive = true;
    paddleX = clampf(paddleX, WIDE_PADDLE_W / 2.0f,
                     SCREEN_W - WIDE_PADDLE_W / 2.0f);
  } else if (type == POWER_LASER) {
    laserActive = true;
  } else if (type == POWER_SLOW) {
    if (!slowActive) {
      for (int i = 0; i < MAX_BALLS; i++) {
        if (!balls[i].active || balls[i].stuck) continue;
        balls[i].vx *= SLOW_SCALE;
        balls[i].vy *= SLOW_SCALE;
      }
    }
    slowActive = true;
  } else if (type == POWER_STICKY) {
    stickyActive = true;
  } else {
    lives = mini(lives + 1, MAX_LIVES);
  }
  sfx(SFX_POWERUP, 0.9f + (int)type * 0.1f);
}

void updatePowerups(float dt) {
  for (int i = 0; i < MAX_POWERUPS; i++) {
    Powerup& p = powerups[i];
    if (!p.active) continue;
    p.y += POWERUP_FALL_SPEED * dt;
    int pw = paddleWidth();
    if (p.y + 2 >= PADDLE_Y && p.y - 2 <= PADDLE_Y + 2 &&
        fabsf_(p.x - paddleX) <= pw / 2.0f + 2.0f) {
      p.active = false;
      collectPowerup(p.type);
    } else if (p.y - 2 > SCREEN_H) {
      p.active = false;
    }
  }
}

void drawPowerup(const Powerup& p) {
  int x = (int)p.x - 2;
  int y = (int)p.y - 2;
  Color color = MAGENTA;
  const char* label = "M";

  if (p.type == POWER_WIDE) {
    color = YELLOW;
    label = "W";
  } else if (p.type == POWER_LASER) {
    color = ORANGE;
    label = "L";
  } else if (p.type == POWER_SLOW) {
    color = BLUE;
    label = "S";
  } else if (p.type == POWER_STICKY) {
    color = GREEN;
    label = "G";
  } else if (p.type == POWER_LIFE) {
    fillRect(x, y, 5, 5, RED);
    hline(x + 1, y + 2, 3, WHITE);
    vline(x + 2, y + 1, 3, WHITE);
    return;
  }

  fillRect(x, y, 5, 5, color);
  text(x + 1, y, label, p.type == POWER_SLOW ? WHITE : BLACK);
}

void draw() {
  clear();
  fillRect(0, 0, SCREEN_W, 7, rgb(10, 12, 24));
  char buf[8];
  int s = score, i = 0;
  char tmp[8];
  int t = 0;
  if (s == 0) buf[i++] = '0';
  while (s > 0) { tmp[t++] = '0' + s % 10; s /= 10; }
  while (t > 0) buf[i++] = tmp[--t];
  buf[i] = 0;
  text(2, 1, buf, WHITE);

  int hudX = 26;
  if (wideActive) { text(hudX, 1, "W", YELLOW); hudX += 4; }
  if (slowActive) { text(hudX, 1, "S", BLUE); hudX += 4; }
  if (laserActive) { text(hudX, 1, "L", ORANGE); hudX += 4; }
  if (stickyActive) text(hudX, 1, "G", GREEN);
  for (int life = 0; life < lives; life++)
    fillRect(SCREEN_W - 4 - life * 4, 2, 3, 3, RED);
  hline(0, 7, SCREEN_W, DARKGRAY);

  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      if (!bricks[r][c]) continue;
      Color color = hsv(r * 32.0f + wave * 60.0f, 0.9f, 1.0f);
      fillRect(c * BRICK_W, BRICK_TOP + r * (BRICK_H + 1),
               BRICK_W - 1, BRICK_H, color);
    }
  }

  int pw = paddleWidth();
  int px0 = (int)(paddleX - pw / 2.0f);
  Color paddleColor = stickyActive ? GREEN : CYAN;
  fillRect(px0, PADDLE_Y, pw, 2, paddleColor);
  pixel(px0, PADDLE_Y, WHITE);
  pixel(px0 + pw - 1, PADDLE_Y, WHITE);
  if (laserActive) {
    vline(px0 + 2, PADDLE_Y - 2, 2, ORANGE);
    vline(px0 + pw - 3, PADDLE_Y - 2, 2, ORANGE);
  }

  for (int n = 0; n < MAX_BALLS; n++) {
    if (!balls[n].active) continue;
    fillRect((int)balls[n].x - 1, (int)balls[n].y - 1, 2, 2, WHITE);
  }
  for (int n = 0; n < MAX_SHOTS; n++) {
    if (shots[n].active) vline((int)shots[n].x, (int)shots[n].y, 3, ORANGE);
  }
  for (int n = 0; n < MAX_POWERUPS; n++) {
    if (powerups[n].active) drawPowerup(powerups[n]);
  }

  if (gameOver) {
    fillRect(8, 24, 48, 18, rgb(20, 4, 4));
    rect(8, 24, 48, 18, RED);
    textCentered(27, "GAME OVER", WHITE);
    textCentered(35, "CLICK-RETRY", GRAY);
    if (scoreRank == 0) textCentered(46, "NEW BEST!", YELLOW);
  } else if (allBallsStuck()) {
    if (stickyActive) {
      textCentered(44, "UP-RELEASE", GREEN);
    } else {
      textCentered(44, "CLICK!", hsv(flashTime * 200.0f, 0.6f, 1.0f));
    }
  }
}

bool breakBrick(int r, int c) {
  if (r < 0 || r >= ROWS || c < 0 || c >= COLS || !bricks[r][c]) return false;
  bricks[r][c] = false;
  bricksLeft--;
  score += 5 + (ROWS - 1 - r);
  sfx(SFX_COIN, 1.0f + (ROWS - 1 - r) * 0.1f);
  if (bricksLeft > 0) {
    float cx = c * BRICK_W + BRICK_W / 2.0f;
    float cy = BRICK_TOP + r * (BRICK_H + 1) + BRICK_H / 2.0f;
    spawnPowerup(cx, cy);
  }
  return bricksLeft == 0;
}

bool updateShots(float dt) {
  for (int i = 0; i < MAX_SHOTS; i++) {
    Shot& shot = shots[i];
    if (!shot.active) continue;
    shot.y -= LASER_SPEED * dt;
    int c = floori(shot.x / BRICK_W);
    int r = floori((shot.y - BRICK_TOP) / (BRICK_H + 1));
    if (r >= 0 && r < ROWS && c >= 0 && c < COLS && bricks[r][c]) {
      float localY = shot.y - BRICK_TOP - r * (BRICK_H + 1);
      if (localY <= BRICK_H) {
        shot.active = false;
        if (breakBrick(r, c)) return true;
      }
    }
    if (shot.y < 7.0f) shot.active = false;
  }
  return false;
}

void finishWave() {
  wave++;
  score += 50;
  sfx(SFX_POWERUP);
  resetBricks();
  resetBalls();
}

void update(float dt) {
  flashTime += dt;

  if (gameOver) {
    draw();
    if (input.justDown(BTN_CLICK)) init();
    return;
  }

  float target = SCREEN_W / 2.0f +
                 tiltCurve(input.tiltX) * (SCREEN_W / 2.0f - 2.0f);
  paddleX = lerpf(paddleX, target, clampf(dt * 14.0f, 0.0f, 1.0f));
  int pw = paddleWidth();
  paddleX = clampf(paddleX, pw / 2.0f, SCREEN_W - pw / 2.0f);

  for (int i = 0; i < MAX_BALLS; i++) {
    if (!balls[i].active || !balls[i].stuck) continue;
    balls[i].x = paddleX + balls[i].stuckOffset;
    balls[i].y = PADDLE_Y - 2.0f;
  }

  if (input.justDown(BTN_CLICK) && allBallsStuck() && !stickyActive) {
    releaseStuckBalls(clampf(input.tiltX * 0.64f, -0.64f, 0.64f));
    sfx(SFX_SELECT);
  }

  laserCooldown = fmaxf_(0.0f, laserCooldown - dt);
  bool releasedBalls = false;
  if (input.justDown(BTN_UP) && stickyActive && stuckBallCount() > 0) {
    releaseStuckBalls(0.0f);
    releasedBalls = true;
    sfx(SFX_BOUNCE, 1.25f);
  }
  if (laserActive && input.held(BTN_UP) && !releasedBalls &&
      stuckBallCount() == 0 && laserCooldown <= 0.0f) {
    fireLasers();
  }

  if (updateShots(dt)) {
    finishWave();
    draw();
    return;
  }

  bool clearedWave = false;
  for (int i = 0; i < MAX_BALLS; i++) {
    Ball& b = balls[i];
    if (!b.active || b.stuck) continue;

    b.x += b.vx * dt;
    b.y += b.vy * dt;

    if (b.x < 1.0f) {
      b.x = 1.0f;
      b.vx = fabsf_(b.vx);
      sfx(SFX_BOUNCE, 1.2f);
    }
    if (b.x > SCREEN_W - 1.0f) {
      b.x = SCREEN_W - 1.0f;
      b.vx = -fabsf_(b.vx);
      sfx(SFX_BOUNCE, 1.2f);
    }
    if (b.y < 8.0f) {
      b.y = 8.0f;
      b.vy = fabsf_(b.vy);
      sfx(SFX_BOUNCE, 1.2f);
    }

    if (b.vy > 0.0f && b.y >= PADDLE_Y - 1.0f && b.y <= PADDLE_Y + 2.0f &&
        fabsf_(b.x - paddleX) <= pw / 2.0f + 1.0f) {
      float hit = clampf((b.x - paddleX) / (pw / 2.0f), -1.0f, 1.0f);
      b.y = PADDLE_Y - 1.0f;
      if (stickyActive) {
        b.stuck = true;
        b.stuckOffset = clampf(b.x - paddleX,
                               -pw / 2.0f + 1.0f, pw / 2.0f - 1.0f);
        b.x = paddleX + b.stuckOffset;
        b.vx = b.vy = 0.0f;
      } else {
        float angle = -PI / 2.0f + hit * 1.1f;
        float speed = ballSpeed();
        b.vx = cosf_(angle) * speed;
        b.vy = sinf_(angle) * speed;
      }
      sfx(SFX_BOUNCE);
    }

    int c = floori(b.x / BRICK_W);
    int r = floori((b.y - BRICK_TOP) / (BRICK_H + 1));
    if (r >= 0 && r < ROWS && c >= 0 && c < COLS && bricks[r][c]) {
      float localY = b.y - BRICK_TOP - r * (BRICK_H + 1);
      if (localY <= BRICK_H) {
        float cx = c * BRICK_W + BRICK_W / 2.0f;
        float cy = BRICK_TOP + r * (BRICK_H + 1) + BRICK_H / 2.0f;
        float dx = (b.x - cx) / BRICK_W;
        float dy = (b.y - cy) / BRICK_H;
        if (fabsf_(dx) > fabsf_(dy)) b.vx = -b.vx;
        else b.vy = -b.vy;
        if (breakBrick(r, c)) {
          clearedWave = true;
          break;
        }
      }
    }

    if (b.y > SCREEN_H + 2.0f) b.active = false;
  }

  if (clearedWave) {
    finishWave();
    draw();
    return;
  }

  updatePowerups(dt);

  if (activeBallCount() == 0) {
    lives--;
    if (lives <= 0) {
      clearRoundPowerups();
      gameOver = true;
      scoreRank = submitScore(score);
      sfx(SFX_LOSE);
    } else {
      sfx(SFX_HURT);
      resetBalls();
    }
  }

  draw();
}

}  // namespace

PT_GAME(breakout, "BREAKOUT", init, update)
