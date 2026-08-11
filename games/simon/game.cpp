// Simon — tilt memory. Watch the four pads flash a sequence, then repeat it
// by tipping the board toward each pad (re-center between entries). The
// sequence grows by one every round.
#include "pixeltilt/pixeltilt.h"

using namespace pt;

namespace {

constexpr int MAXSEQ = 64;
// Pad order: 0=up 1=right 2=down 3=left.
const int PAD_X[4] = {32, 50, 32, 14};
const int PAD_Y[4] = {18, 36, 54, 36};
const Color PAD_COLOR[4] = {RED, BLUE, YELLOW, GREEN};

uint8_t seq[MAXSEQ];
int   seqLen, showIdx, inputIdx;
enum State { SHOW, INPUT, FLASH, ADVANCE, OVER };
State state;
float timer;
int   flashDir;
bool  tiltArmed;
int   completed;  // rounds fully repeated
int   scoreRank;

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

void init() {
  seqLen = 1;
  seq[0] = (uint8_t)randRange(0, 3);
  completed = 0;
  scoreRank = -1;
  state = SHOW;
  showIdx = 0;
  timer = 0;
  tiltArmed = false;
}

float stepTime() { return fmaxf_(0.22f, 0.5f - seqLen * 0.02f); }

int readDir() {
  float ax = fabsf_(input.tiltX), ay = fabsf_(input.tiltY);
  if (ax < 0.45f && ay < 0.45f) {
    tiltArmed = true;
    return -1;
  }
  if (!tiltArmed) return -1;
  tiltArmed = false;
  if (ax > ay) return input.tiltX > 0 ? 1 : 3;
  return input.tiltY > 0 ? 2 : 0;
}

void drawPad(int d, bool lit) {
  Color c = PAD_COLOR[d];
  if (!lit) c = rgb(c.r >> 2, c.g >> 2, c.b >> 2);
  fillCircle(PAD_X[d], PAD_Y[d], 7, c);
  if (lit) circle(PAD_X[d], PAD_Y[d], 7, WHITE);
}

void draw(int litDir) {
  clear();
  fillRect(0, 0, SCREEN_W, 7, rgb(10, 12, 24));
  text(2, 1, "SIMON", MAGENTA);
  text(38, 1, "LV", GRAY);
  drawNumber(47, 1, completed, WHITE);
  hline(0, 7, SCREEN_W, DARKGRAY);

  for (int d = 0; d < 4; d++) drawPad(d, d == litDir);

  // Progress dots while repeating the sequence.
  if (state == INPUT || state == FLASH)
    for (int i = 0; i < seqLen; i++)
      pixel(32 - seqLen + i * 2, 36, i < inputIdx ? WHITE : DARKGRAY);

  if (state == OVER) {
    fillRect(8, 22, 48, 20, rgb(20, 4, 4));
    rect(8, 22, 48, 20, RED);
    textCentered(25, "WRONG!", WHITE);
    textCentered(33, "CLICK-RETRY", GRAY);
    if (scoreRank == 0) textCentered(45, "NEW BEST!", YELLOW);
  } else if (state == ADVANCE) {
    textCentered(33, "GOOD!", WHITE);
  }
}

void update(float dt) {
  timer += dt;

  switch (state) {
    case SHOW: {
      float T = stepTime();
      bool lit = timer < T * 0.65f;
      draw(lit ? seq[showIdx] : -1);
      if (timer >= T) {
        timer = 0;
        showIdx++;
        if (showIdx >= seqLen) {
          state = INPUT;
          inputIdx = 0;
          tiltArmed = false;
        }
      }
      break;
    }
    case INPUT: {
      int d = readDir();
      if (d >= 0) {
        if (d == (int)seq[inputIdx]) {
          flashDir = d;
          state = FLASH;
          timer = 0;
        } else {
          state = OVER;
          timer = 0;
          scoreRank = submitScore(completed);
        }
      }
      draw(-1);
      break;
    }
    case FLASH:
      draw(flashDir);
      if (timer >= 0.18f) {
        timer = 0;
        inputIdx++;
        if (inputIdx >= seqLen) {
          completed = seqLen;
          if (seqLen < MAXSEQ) {
            seq[seqLen] = (uint8_t)randRange(0, 3);
            seqLen++;
          }
          state = ADVANCE;
        } else {
          state = INPUT;
        }
      }
      break;
    case ADVANCE:
      draw(-1);
      if (timer >= 0.7f) {
        timer = 0;
        showIdx = 0;
        state = SHOW;
      }
      break;
    case OVER:
      draw(-1);
      if (timer > 0.5f && input.justDown(BTN_CLICK)) init();
      break;
  }
}

}  // namespace

PT_GAME_SCORED(simon, "SIMON", init, update, pt::SCORE_LEVEL)
