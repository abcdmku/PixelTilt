#include "pixeltilt/engine.h"
#include "pixeltilt/gfx.h"
#include "pixeltilt/input.h"
#include "pixeltilt/game.h"
#include "pixeltilt/ptmath.h"
#include "pixeltilt/storage.h"
#include "pixeltilt/audio.h"

namespace pt {

InputState input;

namespace {

constexpr float PAUSE_HOLD_SECONDS = 0.7f;  // hold CLICK this long in-game
constexpr float MAX_DT = 0.05f;             // clamp hitches so physics stays sane

enum State { ST_MENU, ST_GAME, ST_PAUSE, ST_SETTINGS, ST_SCORES };

State state = ST_MENU;
State settingsFrom = ST_MENU;  // where the settings BACK item returns to
int   runningGame = -1;
int   menuCursor = 0;
int   menuScroll = 0;
int   pauseCursor = 0;
int   settingsCursor = 0;
int   settingsScroll = 0;
bool  confirmReset = false;   // RESET SCORES armed, next click wipes
float resetFlash = 0.0f;      // "CLEARED" feedback timer
int   scoresCursor = 0;
float clickHold = 0.0f;
bool  pauseBackdrop = false;  // pause panel sits on a dimmed game frame
float menuTime = 0.0f;
uint8_t prevButtons = 0;

// The main menu appends two engine rows below the games: row GAME_COUNT is
// SCORES, row GAME_COUNT+1 is SETTINGS.
int menuRowCount() { return GAME_COUNT + 2; }

void intToStr(int32_t n, char* buf) {
  int i = 0;
  if (n < 0) { buf[i++] = '-'; n = -n; }
  char tmp[12];
  int t = 0;
  do { tmp[t++] = '0' + n % 10; n /= 10; } while (n > 0);
  while (t > 0) buf[i++] = tmp[--t];
  buf[i] = 0;
}

// Score -> display text by kind: "1250", "83.4S" (deciseconds), "LV 7".
void formatScore(ScoreKind kind, int32_t v, char* buf) {
  if (v == SCORE_EMPTY) { buf[0] = '-'; buf[1] = '-'; buf[2] = '-'; buf[3] = 0; return; }
  if (kind == SCORE_TIME) {
    intToStr(v / 10, buf);
    int n = 0;
    while (buf[n]) n++;
    buf[n++] = '.';
    buf[n++] = '0' + (v % 10);
    buf[n++] = 'S';
    buf[n] = 0;
  } else if (kind == SCORE_LEVEL) {
    buf[0] = 'L'; buf[1] = 'V'; buf[2] = ' ';
    intToStr(v, buf + 3);
  } else {
    intToStr(v, buf);
  }
}

// Multiply the current frame down once so the pause panel pops over it.
void dimFramebuffer() {
  for (int i = 0; i < SCREEN_W * SCREEN_H * 3; i++)
    framebuffer[i] = (uint8_t)(framebuffer[i] * 2 / 5);
}

void enterPause() {
  state = ST_PAUSE;
  pauseCursor = 0;
  clickHold = 0.0f;
  dimFramebuffer();
  pauseBackdrop = true;
  sfx(STYLE_CHIP, SFX_BLIP, 0.6f);
}

void enterSettings(State from) {
  state = ST_SETTINGS;
  settingsFrom = from;
  settingsCursor = 0;
  settingsScroll = 0;
  confirmReset = false;
  resetFlash = 0.0f;
}

// --- main menu -------------------------------------------------------------

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

  // Scrolling list, 7 visible rows of 7px: games, then SCORES and SETTINGS.
  constexpr int VISIBLE = 7;
  constexpr int ROW_H = 7;
  constexpr int LIST_Y = 13;
  if (menuCursor < menuScroll) menuScroll = menuCursor;
  if (menuCursor >= menuScroll + VISIBLE) menuScroll = menuCursor - VISIBLE + 1;

  for (int row = 0; row < VISIBLE; row++) {
    int idx = menuScroll + row;
    if (idx >= menuRowCount()) break;
    int y = LIST_Y + row * ROW_H;
    bool sel = idx == menuCursor;
    if (sel) {
      fillRect(1, y - 1, SCREEN_W - 2, ROW_H, rgb(24, 24, 48));
      text(3, y, ">", YELLOW);
    }
    if (idx < GAME_COUNT) {
      text(9, y, GAME_LIST[idx]->title, sel ? WHITE : GRAY);
    } else if (idx == GAME_COUNT) {
      text(9, y, "SCORES", sel ? CYAN : rgb(30, 110, 130));
    } else {
      text(9, y, "SETTINGS", sel ? ORANGE : rgb(130, 80, 30));
    }
  }

  // Tilt bubbles: live feedback that the IMU (or arrow keys) work — X along
  // the bottom strip, Y up the right edge, so both axes are visible.
  hline(2, SCREEN_H - 2, SCREEN_W - 4, DARKGRAY);
  int bx = SCREEN_W / 2 + (int)(input.tiltX * 28.0f);
  pixel(clampi(bx, 2, SCREEN_W - 3), SCREEN_H - 2, CYAN);
  vline(SCREEN_W - 2, 13, SCREEN_H - 16, DARKGRAY);
  int by = (13 + SCREEN_H - 3) / 2 + (int)(input.tiltY * 22.0f);
  pixel(SCREEN_W - 2, clampi(by, 13, SCREEN_H - 4), CYAN);
}

void tickMenu(float dt) {
  menuTime += dt;
  int rows = menuRowCount();
  if (input.justDown(BTN_UP) || input.justDown(BTN_DOWN)) {
    int d = input.justDown(BTN_UP) ? rows - 1 : 1;
    menuCursor = (menuCursor + d) % rows;
    sfx(STYLE_CHIP, SFX_BLIP);
  }
  if (input.justDown(BTN_CLICK)) {
    if (menuCursor < GAME_COUNT) {
      if (GAME_COUNT > 0) launchGame(menuCursor);
    } else if (menuCursor == GAME_COUNT) {
      state = ST_SCORES;
      scoresCursor = 0;
      sfx(STYLE_CHIP, SFX_SELECT);
    } else {
      enterSettings(ST_MENU);
      sfx(STYLE_CHIP, SFX_SELECT);
    }
  }
  drawMenu();
}

// --- running game + pause ---------------------------------------------------

void tickGame(float dt) {
  // Hold CLICK to pause. The game still sees the initial press (harmless for
  // click-to-launch/retry games); a hold this long isn't a game gesture.
  if (input.held(BTN_CLICK)) clickHold += dt;
  else clickHold = 0.0f;

  GAME_LIST[runningGame]->update(dt);

  if (clickHold >= PAUSE_HOLD_SECONDS) {
    enterPause();
    return;
  }
  // Progress bar overlay while the pause hold is charging.
  if (clickHold > 0.2f) {
    int w = (int)((clickHold / PAUSE_HOLD_SECONDS) * (SCREEN_W - 8));
    fillRect(4, SCREEN_H - 3, SCREEN_W - 8, 2, DARKGRAY);
    fillRect(4, SCREEN_H - 3, w, 2, YELLOW);
  }
}

void tickPause() {
  constexpr int ITEMS = 3;
  const char* labels[ITEMS] = {"RESUME", "SETTINGS", "MAIN MENU"};

  if (input.justDown(BTN_UP) || input.justDown(BTN_DOWN)) {
    int d = input.justDown(BTN_UP) ? ITEMS - 1 : 1;
    pauseCursor = (pauseCursor + d) % ITEMS;
    sfx(STYLE_CHIP, SFX_BLIP);
  }
  if (input.justDown(BTN_CLICK)) {
    sfx(STYLE_CHIP, SFX_SELECT);
    switch (pauseCursor) {
      case 0: state = ST_GAME; clickHold = 0.0f; return;
      case 1: enterSettings(ST_PAUSE); return;
      case 2: exitToMenu(); return;
    }
  }

  // Coming back from settings the dimmed game frame is gone; use black.
  if (!pauseBackdrop) clear();

  constexpr int BOX_W = 48, BOX_H = 34;
  constexpr int BOX_X = (SCREEN_W - BOX_W) / 2, BOX_Y = 14;
  fillRect(BOX_X, BOX_Y, BOX_W, BOX_H, rgb(8, 8, 20));
  rect(BOX_X, BOX_Y, BOX_W, BOX_H, CYAN);
  textCentered(BOX_Y + 3, "PAUSED", WHITE);
  hline(BOX_X + 3, BOX_Y + 10, BOX_W - 6, DARKGRAY);
  for (int i = 0; i < ITEMS; i++) {
    int y = BOX_Y + 13 + i * 7;
    bool sel = i == pauseCursor;
    if (sel) text(BOX_X + 3, y, ">", YELLOW);
    text(BOX_X + 9, y, labels[i], sel ? WHITE : GRAY);
  }
}

// --- settings ---------------------------------------------------------------

// Volume rows step 0..100 by 20, wrapping back to mute.
uint8_t cycleVolume(uint8_t v) {
  return v >= 100 ? 0 : (uint8_t)(v + 20);
}

void tickSettings(float dt) {
  // SCREEN, TILT, FLIP, BRIGHT, SFX, MUSIC, RESET SCORES, BACK
  constexpr int ITEMS = 8;
  constexpr int VISIBLE = 6;  // rows that fit between the title and footer
  if (resetFlash > 0) resetFlash -= dt;

  if (input.justDown(BTN_UP) || input.justDown(BTN_DOWN)) {
    confirmReset = false;
    int d = input.justDown(BTN_UP) ? ITEMS - 1 : 1;
    settingsCursor = (settingsCursor + d) % ITEMS;
    sfx(STYLE_CHIP, SFX_BLIP);
  }
  if (input.justDown(BTN_CLICK)) {
    switch (settingsCursor) {
      case 0:
        settings().rotation = (settings().rotation + 1) & 3;
        settingsChanged();
        sfx(STYLE_CHIP, SFX_SELECT);
        break;
      case 1:
        settings().tiltRotation = (settings().tiltRotation + 1) & 3;
        settingsChanged();
        sfx(STYLE_CHIP, SFX_SELECT);
        break;
      case 2:
        settings().tiltFlip ^= 1;
        settingsChanged();
        sfx(STYLE_CHIP, SFX_SELECT);
        break;
      case 3:
        settings().brightness = settings().brightness >= 100
                                    ? 20
                                    : (uint8_t)(settings().brightness + 20);
        settingsChanged();
        sfx(STYLE_CHIP, SFX_SELECT);
        break;
      case 4:
        settings().sfxVolume = cycleVolume(settings().sfxVolume);
        settingsChanged();
        sfx(STYLE_CHIP, SFX_COIN);  // audition the new level right away
        break;
      case 5:
        settings().musicVolume = cycleVolume(settings().musicVolume);
        settingsChanged();  // host adjusts the music bus live
        break;
      case 6:
        if (!confirmReset) {
          confirmReset = true;
          sfx(STYLE_CHIP, SFX_ALARM);
        } else {
          resetAllScores();
          confirmReset = false;
          resetFlash = 1.2f;
          sfx(STYLE_CHIP, SFX_POWERUP);
        }
        break;
      case 7:
        sfx(STYLE_CHIP, SFX_SELECT);
        if (settingsFrom == ST_PAUSE) {
          state = ST_PAUSE;
          pauseBackdrop = false;
        } else {
          state = ST_MENU;
        }
        return;
    }
  }

  if (settingsCursor < settingsScroll) settingsScroll = settingsCursor;
  if (settingsCursor >= settingsScroll + VISIBLE)
    settingsScroll = settingsCursor - VISIBLE + 1;

  clear();
  textCentered(3, "SETTINGS", ORANGE);
  hline(2, 10, SCREEN_W - 4, DARKGRAY);

  char buf[12];
  for (int row = 0; row < VISIBLE; row++) {
    int i = settingsScroll + row;
    if (i >= ITEMS) break;
    int y = 14 + row * 7;
    bool sel = i == settingsCursor;
    if (sel) {
      fillRect(1, y - 1, SCREEN_W - 2, 7, rgb(24, 24, 48));
      text(2, y, ">", YELLOW);
    }
    Color c = sel ? WHITE : GRAY;
    switch (i) {
      case 0:
        text(8, y, "SCREEN", c);
        intToStr(settings().rotation * 90, buf);
        text(SCREEN_W - textWidth(buf) - 3, y, buf, sel ? CYAN : GRAY);
        break;
      case 1:
        text(8, y, "TILT", c);
        intToStr(settings().tiltRotation * 90, buf);
        text(SCREEN_W - textWidth(buf) - 3, y, buf, sel ? CYAN : GRAY);
        break;
      case 2:
        text(8, y, "FLIP", c);
        text(SCREEN_W - textWidth(settings().tiltFlip ? "ON" : "OFF") - 3, y,
             settings().tiltFlip ? "ON" : "OFF", sel ? CYAN : GRAY);
        break;
      case 3:
        text(8, y, "BRIGHT", c);
        intToStr(settings().brightness, buf);
        text(SCREEN_W - textWidth(buf) - 3, y, buf, sel ? CYAN : GRAY);
        break;
      case 4:
        text(8, y, "SFX", c);
        intToStr(settings().sfxVolume, buf);
        text(SCREEN_W - textWidth(buf) - 3, y, buf, sel ? CYAN : GRAY);
        break;
      case 5:
        text(8, y, "MUSIC", c);
        intToStr(settings().musicVolume, buf);
        text(SCREEN_W - textWidth(buf) - 3, y, buf, sel ? CYAN : GRAY);
        break;
      case 6:
        if (resetFlash > 0)       text(8, y, "CLEARED!", GREEN);
        else if (confirmReset)    text(8, y, "SURE?", RED);
        else                      text(8, y, "RESET SCORES", c);
        break;
      case 7:
        text(8, y, "BACK", c);
        break;
    }
  }

  textCentered(SCREEN_H - 8, "CLICK-CHANGE", DARKGRAY);
}

// --- scores -----------------------------------------------------------------

void tickScores() {
  if (GAME_COUNT > 0 && (input.justDown(BTN_UP) || input.justDown(BTN_DOWN))) {
    int d = input.justDown(BTN_UP) ? GAME_COUNT - 1 : 1;
    scoresCursor = (scoresCursor + d) % GAME_COUNT;
    sfx(STYLE_CHIP, SFX_BLIP);
  }
  if (input.justDown(BTN_CLICK)) {
    state = ST_MENU;
    sfx(STYLE_CHIP, SFX_SELECT);
    return;
  }

  clear();
  textCentered(3, "SCORES", CYAN);
  hline(2, 10, SCREEN_W - 4, DARKGRAY);

  if (GAME_COUNT == 0) {
    textCentered(28, "NO GAMES", GRAY);
    return;
  }

  const Game* g = GAME_LIST[scoresCursor];
  text(3, 15, "<", DARKGRAY);
  text(SCREEN_W - 6, 15, ">", DARKGRAY);
  textCentered(15, g->title, WHITE);

  const int32_t* best = gameScores(scoresCursor);
  char buf[16];
  for (int i = 0; i < SCORES_PER_GAME; i++) {
    int y = 27 + i * 8;
    buf[0] = (char)('1' + i);
    buf[1] = 0;
    text(8, y, buf, i == 0 ? YELLOW : GRAY);
    formatScore(g->scoreKind, best[i], buf);
    text(16, y, buf, best[i] == SCORE_EMPTY ? DARKGRAY : WHITE);
  }

  textCentered(SCREEN_H - 8, "CLICK-BACK", DARKGRAY);
}

}  // namespace

void engineInit() {
  state = ST_MENU;
  runningGame = -1;
  menuCursor = 0;
  menuScroll = 0;
  clickHold = 0.0f;
  prevButtons = 0;
  srand_(0x50495854u);  // "PIXT" — hosts may reseed with entropy at startup
  storageInit();
  audioReset();
  music(MUS_MENU);
  clear();
}

void launchGame(int index) {
  if (index < 0 || index >= GAME_COUNT) return;
  state = ST_GAME;
  runningGame = index;
  menuCursor = index;
  clickHold = 0.0f;
  // Games opt into their own bank/track from init(); these are the defaults.
  setSfxStyle(STYLE_CHIP);
  music(MUS_NONE);
  sfx(STYLE_CHIP, SFX_SELECT);
  GAME_LIST[index]->init();
}

void exitToMenu() {
  state = ST_MENU;
  runningGame = -1;
  clickHold = 0.0f;
  setSfxStyle(STYLE_CHIP);
  music(MUS_MENU);
}

int currentGame() { return runningGame; }

void engineTick(float tiltX, float tiltY, float spin, uint8_t rawButtons, float dt) {
  dt = clampf(dt, 0.0f, MAX_DT);

  // TILT setting first: quarter-turn the raw tilt to correct for however the
  // IMU breakout is mounted, without needing a reflash.
  float tx = clampf(tiltX, -1.0f, 1.0f);
  float ty = clampf(tiltY, -1.0f, 1.0f);
  switch (settings().tiltRotation) {
    case 1: { float t = tx; tx = ty; ty = -t; } break;
    case 2: tx = -tx; ty = -ty; break;
    case 3: { float t = tx; tx = -ty; ty = t; } break;
  }
  if (settings().tiltFlip) tx = -tx;  // mirror fix for a flipped-over IMU
  // Then counter-rotate for the screen so "toward the bottom of the picture"
  // keeps meaning "tilted toward the player" whatever way the panel hangs.
  switch (rotation()) {
    case 1: { float t = tx; tx = ty; ty = -t; } break;
    case 2: tx = -tx; ty = -ty; break;
    case 3: { float t = tx; tx = -ty; ty = t; } break;
  }
  input.tiltX = tx;
  input.tiltY = ty;
  // Spin is about the screen normal, so the in-plane TILT/rotation corrections
  // don't touch it — only a flipped-over IMU (Z pointing backwards) reverses it.
  input.spin = settings().tiltFlip ? -spin : spin;
  input.pressed  = rawButtons & ~prevButtons;
  input.released = prevButtons & ~rawButtons;
  input.buttons  = rawButtons;
  prevButtons = rawButtons;

  switch (state) {
    case ST_MENU:     tickMenu(dt); break;
    case ST_GAME:     tickGame(dt); break;
    case ST_PAUSE:    tickPause(); break;
    case ST_SETTINGS: tickSettings(dt); break;
    case ST_SCORES:   tickScores(); break;
  }
}

}  // namespace pt
