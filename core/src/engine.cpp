#include "pixeltilt/engine.h"
#include "pixeltilt/gfx.h"
#include "pixeltilt/input.h"
#include "pixeltilt/game.h"
#include "pixeltilt/ptmath.h"

namespace pt {

InputState input;

namespace {

constexpr float EXIT_HOLD_SECONDS = 1.0f;  // UP+DOWN held this long -> menu
constexpr float MAX_DT = 0.05f;            // clamp hitches so physics stays sane

int   runningGame = -1;   // -1 = menu
int   menuCursor = 0;
int   menuScroll = 0;
float exitHold = 0.0f;
float menuTime = 0.0f;
uint8_t prevButtons = 0;

void drawMenu() {
  clear();

  // Animated rainbow title strip.
  const char* title = "PIXELTILT";
  int tw = textWidth(title, 1);
  int tx = (SCREEN_W - tw) / 2;
  for (int i = 0; title[i]; i++) {
    char one[2] = {title[i], 0};
    text(tx + i * 4, 3, one, hsv(menuTime * 60.0f + i * 24.0f, 0.9f, 1.0f));
  }
  hline(2, 10, SCREEN_W - 4, DARKGRAY);

  if (GAME_COUNT == 0) {
    textCentered(28, "NO GAMES", GRAY);
    return;
  }

  // Scrolling list, 7 visible rows of 7px.
  constexpr int VISIBLE = 7;
  constexpr int ROW_H = 7;
  constexpr int LIST_Y = 13;
  if (menuCursor < menuScroll) menuScroll = menuCursor;
  if (menuCursor >= menuScroll + VISIBLE) menuScroll = menuCursor - VISIBLE + 1;

  for (int row = 0; row < VISIBLE; row++) {
    int idx = menuScroll + row;
    if (idx >= GAME_COUNT) break;
    int y = LIST_Y + row * ROW_H;
    bool sel = idx == menuCursor;
    if (sel) {
      fillRect(1, y - 1, SCREEN_W - 2, ROW_H, rgb(24, 24, 48));
      text(3, y, ">", YELLOW);
    }
    text(9, y, GAME_LIST[idx]->title, sel ? WHITE : GRAY);
  }

  // Tilt bubble in the footer: live feedback that the IMU (or arrow keys) work.
  hline(2, SCREEN_H - 2, SCREEN_W - 4, DARKGRAY);
  int bx = SCREEN_W / 2 + (int)(input.tiltX * 28.0f);
  pixel(clampi(bx, 2, SCREEN_W - 3), SCREEN_H - 2, CYAN);
}

void tickMenu(float dt) {
  menuTime += dt;
  if (input.justDown(BTN_UP))   menuCursor = (menuCursor + GAME_COUNT - 1) % GAME_COUNT;
  if (input.justDown(BTN_DOWN)) menuCursor = (menuCursor + 1) % GAME_COUNT;
  if (input.justDown(BTN_CLICK) && GAME_COUNT > 0) launchGame(menuCursor);
  drawMenu();
}

}  // namespace

void engineInit() {
  runningGame = -1;
  menuCursor = 0;
  menuScroll = 0;
  exitHold = 0.0f;
  prevButtons = 0;
  srand_(0x50495854u);  // "PIXT" — hosts may reseed with entropy at startup
  clear();
}

void launchGame(int index) {
  if (index < 0 || index >= GAME_COUNT) return;
  runningGame = index;
  menuCursor = index;
  exitHold = 0.0f;
  GAME_LIST[index]->init();
}

void exitToMenu() {
  runningGame = -1;
  exitHold = 0.0f;
}

int currentGame() { return runningGame; }

void engineTick(float tiltX, float tiltY, uint8_t rawButtons, float dt) {
  dt = clampf(dt, 0.0f, MAX_DT);

  input.tiltX = clampf(tiltX, -1.0f, 1.0f);
  input.tiltY = clampf(tiltY, -1.0f, 1.0f);
  input.pressed  = rawButtons & ~prevButtons;
  input.released = prevButtons & ~rawButtons;
  input.buttons  = rawButtons;
  prevButtons = rawButtons;

  if (runningGame >= 0) {
    // Global escape hatch: hold UP+DOWN (both ends of the thumb wheel).
    if (input.held(BTN_UP) && input.held(BTN_DOWN)) {
      exitHold += dt;
      if (exitHold >= EXIT_HOLD_SECONDS) {
        exitToMenu();
        return;
      }
    } else {
      exitHold = 0.0f;
    }
    GAME_LIST[runningGame]->update(dt);

    // Progress bar overlay while the exit combo is held.
    if (exitHold > 0.15f) {
      int w = (int)((exitHold / EXIT_HOLD_SECONDS) * (SCREEN_W - 8));
      fillRect(4, SCREEN_H - 3, SCREEN_W - 8, 2, DARKGRAY);
      fillRect(4, SCREEN_H - 3, w, 2, RED);
    }
  } else {
    tickMenu(dt);
  }
}

}  // namespace pt
