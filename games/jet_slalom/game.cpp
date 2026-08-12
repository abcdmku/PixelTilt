// Jet Slalom — port of the 1997 freearcade Java applet "Jet slalom" by
// "C (not language!)". Fly a jet down an endless course, weaving through
// triangular pylons that rush out of the horizon; the whole world banks as
// you steer. Tilt left/right to steer, hold UP or DOWN on the wheel for
// turbo (3x speed, original 'A' key). Survive longer to advance rounds:
// the sky changes, pylons come faster, and from round 8 they form swinging
// slalom gates.
//
// The simulation is step-exact with the original: 55 ms fixed steps, the
// same 120/(1+0.6z) projection (rescaled 320x200 -> 64x64), and the
// original spawn tables and round thresholds.
#include "pixeltilt/pixeltilt.h"

#include "bomb_pta.h"

using namespace pt;

namespace {

constexpr float STEP = 0.055f;      // original frame pace
constexpr float SCALE = 24.0f;      // 120 * 64/320
constexpr int CX = 32, CY = 32;     // projection center
constexpr float T = 0.6f;           // perspective constant
constexpr float MY_WIDTH = 0.7f;    // player collision half-width (world units)

constexpr int CLEAR_SCORE[10] = {8000, 8200, 8400, 12000, 12200,
                                 25000, 25200, 25400, 40000, 99999};
constexpr int MAX_COUNTS[10] = {4, 4, 4, 3, 3, 2, 2, 2, 1, 1};

constexpr Color OB_COLORS[4] = {{255, 255, 0}, {0, 0, 255}, {255, 0, 0}, {255, 0, 255}};
// Sky/ground pairs per round, straight from the applet.
constexpr Color BK_COLORS[20] = {
    {0, 160, 255},   {0, 200, 64},  {64, 160, 200}, {0, 180, 64},
    {200, 160, 160}, {64, 180, 64}, {240, 160, 160}, {64, 180, 64},
    {160, 64, 32},   {0, 180, 64},  {0, 0, 0},      {0, 128, 64},
    {0, 128, 128},   {0, 180, 64},  {128, 128, 180}, {0, 240, 64},
    {0, 255, 255},   {0, 240, 64},  {192, 192, 192}, {64, 180, 64},
};

struct Pylon {
  float x[3];  // triangle corners; y is always {2, -1.2, 2}
  float z;
  uint8_t color;
};

constexpr int MAX_OB = 32;
Pylon obs[MAX_OB];
int obTail, obCount;  // ring buffer, oldest (nearest) at obTail

float vx;                       // world drift = -steer direction
int counter, maxcount;          // spawn timer
int score, round_, scoreRank;
int mCounter;                   // animation clock (steps)
float ox1, ox2, ovx;            // round-8+ slalom gate walls
int swingCounter, direction;
int damaged;                    // 0 = fine, 1..20 = explosion frames
int crashTicks;
bool gameOver;
float overTime, acc;

void reset() {
  obTail = obCount = 0;
  vx = 0;
  counter = 0;
  maxcount = MAX_COUNTS[0];
  score = 0;
  round_ = 0;
  scoreRank = -1;
  mCounter = 0;
  ox1 = -17;
  ox2 = 17;
  ovx = 0;
  swingCounter = 0;
  direction = 1;
  damaged = 0;
  crashTicks = 0;
  gameOver = false;
  overTime = 0;
  acc = 0;
}

// No setSfxStyle/music: the applet's only audio is the bomb sample, and the
// engine already resets each launch to silence.
void init() { reset(); }

void spawnPylon() {
  float half = round_ >= 8 ? 1.1f : 0.6f;
  float center;

  if (round_ >= 8) {
    swingCounter--;
    ox1 += vx;
    ox2 += vx;

    if (round_ >= 9 && swingCounter % 13 < 7) {
      half = 0.7f;
      center = randf() * 32.0f - 16.0f;
      if (center < ox2 && center > ox1) {
        half = 1.2f;
        center = randf() > 0.5f ? ox1 : ox2;
      }
    } else {
      center = swingCounter % 2 == 0 ? ox1 : ox2;
    }

    if (ox2 - ox1 > 9.0f) {
      ox1 += 0.6f;
      ox2 -= 0.6f;
      if (ox2 - ox1 > 10.0f) half = 2.0f;
    } else if (ox1 > 18.0f) {
      ox2 -= 0.6f;
      ox1 -= 0.6f;
    } else if (ox2 < -18.0f) {
      ox2 += 0.6f;
      ox1 += 0.6f;
    } else {
      if (swingCounter < 0) {
        direction = -direction;
        swingCounter += 2 * (int)(randf() * 8.0f + 4.0f);
      }
      ovx = clampf(ovx + (direction > 0 ? 0.125f : -0.125f), -0.7f, 0.7f);
      ox1 += ovx;
      ox2 += ovx;
    }
  } else {
    center = randf() * 32.0f - 16.0f;
  }

  if (obCount >= MAX_OB) return;
  Pylon& p = obs[(obTail + obCount) % MAX_OB];
  p.x[0] = center - half;
  p.x[1] = center;
  p.x[2] = center + half;
  p.z = 25.0f;
  p.color = (uint8_t)(rand_() % 4);
  obCount++;
}

// Advance every pylon one step; true if the nearest one hit the player.
bool movePylons() {
  bool hit = false;
  for (int i = 0; i < obCount; i++) {
    Pylon& p = obs[(obTail + i) % MAX_OB];
    p.z -= 1.0f;
    for (int k = 0; k < 3; k++) p.x[k] += vx;
  }
  while (obCount > 0 && obs[obTail].z <= 1.1f) {
    Pylon& p = obs[obTail];
    if (p.x[0] < MY_WIDTH && p.x[2] > -MY_WIDTH) hit = true;
    obTail = (obTail + 1) % MAX_OB;
    obCount--;
  }
  if (++counter >= maxcount) {
    counter = 0;
    spawnPylon();
  }
  return hit;
}

void tickScore() {
  if (round_ < 9 && score > CLEAR_SCORE[round_]) {
    round_++;
    maxcount = MAX_COUNTS[round_];
  }
  score += 10;
}

void step() {
  mCounter++;

  if (damaged == 0) {
    // Tilt steers: the world drifts opposite the lean, like holding the
    // original's arrow keys (0.1/step ramp, +-0.6 cap).
    float target = -tiltCurve(input.tiltX) * 0.6f;
    if (vx < target) vx = fminf_(vx + 0.1f, target);
    else             vx = fmaxf_(vx - 0.1f, target);

    if (movePylons()) {
      damaged = 1;
      crashTicks = 0;
      vx = 0;
      sfxSample(BOMB_PTA, sizeof(BOMB_PTA));  // the applet's actual bomb.au
      return;
    }
    tickScore();
  } else {
    // Crash: world keeps rolling under the fireball for 30 steps.
    movePylons();
    tickScore();
    if (damaged <= 20) damaged++;
    if (++crashTicks >= 30) {
      gameOver = true;
      overTime = 0;
      scoreRank = submitScore(score);
    }
  }
}

// Banking rotation from the current drift, sin(pi*i/450) as in the applet.
void tiltSinCos(float& si, float& co) {
  float ang = fabsf_(vx) * 100.0f * (PI / 450.0f);
  si = sinf_(ang);
  co = cosf_(ang);
  if (vx > 0) si = -si;
}

void fillTri(int x0, int y0, int x1, int y1, int x2, int y2, Color c) {
  int yMin = maxi(0, mini(y0, mini(y1, y2)));
  int yMax = mini(SCREEN_H - 1, maxi(y0, maxi(y1, y2)));
  int ex[3][2] = {{x0, x1}, {x1, x2}, {x2, x0}};
  int ey[3][2] = {{y0, y1}, {y1, y2}, {y2, y0}};
  for (int y = yMin; y <= yMax; y++) {
    float lo = 1e9f, hi = -1e9f;
    for (int e = 0; e < 3; e++) {
      int ya = ey[e][0], yb = ey[e][1];
      if (ya == yb) {
        if (y == ya) {
          lo = fminf_(lo, (float)mini(ex[e][0], ex[e][1]));
          hi = fmaxf_(hi, (float)maxi(ex[e][0], ex[e][1]));
        }
        continue;
      }
      if ((y < ya && y < yb) || (y > ya && y > yb)) continue;
      float x = ex[e][0] + (ex[e][1] - ex[e][0]) * (float)(y - ya) / (float)(yb - ya);
      lo = fminf_(lo, x);
      hi = fmaxf_(hi, x);
    }
    if (lo <= hi) hline((int)lo, y, (int)hi - (int)lo + 1, c);
  }
}

void drawWorld() {
  clear(BK_COLORS[round_ * 2]);

  float si, co;
  tiltSinCos(si, co);

  // Ground: the horizon is the projected far edge (x = +-26 at z = 26),
  // banked by the steering angle; fill each column below it.
  float farScale = SCALE / (1.0f + T * 26.0f);
  float lx = CX + co * -26.0f * farScale;
  float ly = CY + (-si * -26.0f + 2.0f) * farScale;
  float rx = CX + co * 26.0f * farScale;
  float ry = CY + (-si * 26.0f + 2.0f) * farScale;
  Color ground = BK_COLORS[round_ * 2 + 1];
  for (int x = 0; x < SCREEN_W; x++) {
    int top = (int)(ly + (ry - ly) * (x - lx) / (rx - lx));
    if (top < 0) top = 0;
    vline(x, top, SCREEN_H - top, ground);
  }

  // Pylons, farthest (newest) first.
  for (int i = obCount - 1; i >= 0; i--) {
    const Pylon& p = obs[(obTail + i) % MAX_OB];
    float s = SCALE / (1.0f + T * p.z);
    int px[3], py[3];
    constexpr float Y[3] = {2.0f, -1.2f, 2.0f};
    for (int k = 0; k < 3; k++) {
      float yy = Y[k] - 2.0f;
      px[k] = CX + (int)((co * p.x[k] + si * yy) * s);
      py[k] = CY + (int)((-si * p.x[k] + co * yy + 2.0f) * s);
    }
    fillTri(px[0], py[0], px[1], py[1], px[2], py[2], OB_COLORS[p.color]);
  }
}

void drawJet() {
  if (damaged >= 20) return;
  // Hover bob, with a takeoff climb over the first 200 points.
  int lift = mCounter % 12 > 6 ? 7 : 8;
  if (score < 200) lift = (int)((12 + score / 20) * 0.32f);
  int ty = SCREEN_H - lift - 5;
  int lean = (int)(-vx * 3.5f);  // steering right -> right wing dips

  Color body = rgb(0, 52, 216);
  line(CX - 2, ty + 3, CX - 10, ty + 3 - lean, body);
  line(CX - 2, ty + 4, CX - 10, ty + 4 - lean, body);
  line(CX + 2, ty + 3, CX + 10, ty + 3 + lean, body);
  line(CX + 2, ty + 4, CX + 10, ty + 4 + lean, body);
  pixel(CX - 10, ty + 3 - lean, rgb(120, 160, 255));
  pixel(CX + 10, ty + 3 + lean, rgb(120, 160, 255));
  fillRect(CX - 2, ty, 5, 5, body);
  hline(CX - 1, ty + 1, 3, rgb(216, 232, 255));
  // Exhaust flicker (the applet's two-frame jiki/jiki2 swap).
  Color flame = mCounter % 4 > 1 ? ORANGE : YELLOW;
  pixel(CX, ty + 5, flame);
  if (mCounter % 4 > 1) pixel(CX, ty + 6, rgb(255, 60, 0));
}

void drawExplosion() {
  int d = mini(damaged, 20);
  float ex = d * 1.6f, ey = d * 1.28f;
  Color c = rgb(255, (uint8_t)maxi(0, 255 - d * 12), (uint8_t)maxi(0, 240 - d * 12));
  for (int dy = -(int)ey; dy <= (int)ey; dy++) {
    float f = 1.0f - (dy * dy) / (ey * ey);
    if (f <= 0) continue;
    int hw = (int)(ex * sqrtf_(f));
    hline(CX - hw, 61 + dy, hw * 2 + 1, c);
  }
}

void drawScore() {
  char buf[8];
  int n = score % 1000000;
  for (int i = 5; i >= 0; i--) {
    buf[i] = '0' + n % 10;
    n /= 10;
  }
  buf[6] = 0;
  text(3, 2, buf, BLACK);  // shadow keeps it readable on light skies
  text(2, 1, buf, WHITE);
}

void update(float dt) {
  if (gameOver) {
    overTime += dt;
    drawWorld();
    drawExplosion();
    fillRect(8, 22, 48, 20, rgb(20, 4, 4));
    rect(8, 22, 48, 20, RED);
    textCentered(25, "CRASH!", WHITE);
    textCentered(33, "CLICK-RETRY", GRAY);
    if (scoreRank == 0) textCentered(46, "NEW BEST!", YELLOW);
    drawScore();
    if (overTime > 0.5f && input.justDown(BTN_CLICK)) reset();
    return;
  }

  bool turbo = input.held(BTN_UP) || input.held(BTN_DOWN);
  acc += dt * (turbo ? 3.0f : 1.0f);
  int guard = 0;
  while (acc >= STEP && guard < 8) {
    step();
    acc -= STEP;
    guard++;
    if (gameOver) break;
  }

  drawWorld();
  drawJet();
  if (damaged > 0) drawExplosion();
  drawScore();
}

}  // namespace

PT_GAME(jet_slalom, "JET SLALOM", init, update)
