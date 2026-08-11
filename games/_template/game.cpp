// __TITLE__ — describe your game here.
//
// The PixelTilt game API in one minute:
//   * init()        runs once each time the game is launched from the menu.
//   * update(dt)    runs every frame (dt = seconds since last frame, already
//                   clamped). Simulate, then draw — the whole 64x64 screen is
//                   yours via the pt:: drawing calls below.
//   * pt::input     the current frame's input:
//         input.tiltX / input.tiltY      tilt in [-1, 1]  (arrows on PC)
//         input.spin                     twist rate, rad/s, + = clockwise (Q/E on PC)
//         input.held(BTN_UP/CLICK/DOWN)  button currently down
//         input.justDown(...)            pressed this frame
//   * Drawing:      clear, pixel, line, rect, fillRect, circle, fillCircle,
//                   text, textCentered  — colors via rgb(r,g,b) or hsv(h,s,v).
//   * Math/RNG:     sinf_, cosf_, sqrtf_, clampf, lerpf, randRange, randf...
//                   (from ptmath.h — no <math.h> in games; the core is
//                   freestanding so it builds for both WASM and ESP32).
//   * No heap, no statics with constructors — keep state in plain globals in
//     the anonymous namespace and reset everything in init().
//   * Audio:        sfx(SFX_COIN) fires a one-shot from the game's sound bank
//                   (SFX_BLIP/SELECT/COIN/BOUNCE/JUMP/LASER/HURT/EXPLODE/
//                   POWERUP/WIN/LOSE/ALARM); an optional second arg is a pitch
//                   multiplier, e.g. sfx(SFX_COIN, 1.5f). Pick the bank with
//                   setSfxStyle(STYLE_ARCADE/CHIP/SOFT/GRIT) in init(), and
//                   request background music with music(MUS_CHILL/ACTION/
//                   TENSE) — or leave MUS_NONE for silence. The host renders
//                   everything; games never touch an audio device.
//   * High scores: when a run ends, call pt::submitScore(value) — the engine
//     keeps a persistent top-3 per game (browsable from the menu's SCORES
//     screen). It returns the rank (0 = new best) or -1. Points are the
//     default; register with PT_GAME_SCORED(..., pt::SCORE_TIME) for
//     lower-is-better times in deciseconds, or pt::SCORE_LEVEL for levels.
//
// The engine owns pausing: holding CLICK ~0.7s opens the pause menu (resume /
// settings / main menu) — you don't need to handle that, but avoid gameplay
// that requires holding CLICK.
#include "pixeltilt/pixeltilt.h"

using namespace pt;

namespace {

float x, y, vx, vy;
float hue;

void init() {
  setSfxStyle(STYLE_CHIP);
  music(MUS_CHILL);
  x = SCREEN_W / 2.0f;
  y = SCREEN_H / 2.0f;
  vx = 18.0f;
  vy = 12.0f;
  hue = 0;
}

void update(float dt) {
  // Tilt accelerates the ball.
  vx += input.tiltX * 60.0f * dt;
  vy += input.tiltY * 60.0f * dt;

  x += vx * dt;
  y += vy * dt;
  if (x < 3)            { x = 3; vx = fabsf_(vx); }
  if (x > SCREEN_W - 4) { x = SCREEN_W - 4; vx = -fabsf_(vx); }
  if (y < 3)            { y = 3; vy = fabsf_(vy); }
  if (y > SCREEN_H - 4) { y = SCREEN_H - 4; vy = -fabsf_(vy); }

  if (input.justDown(BTN_CLICK)) {  // click = random kick
    vx = (randf() - 0.5f) * 120.0f;
    vy = (randf() - 0.5f) * 120.0f;
    sfx(SFX_JUMP);
  }

  hue += dt * 60.0f;

  clear();
  textCentered(2, "__TITLE__", GRAY);
  fillCircle((int)x, (int)y, 3, hsv(hue, 0.9f, 1.0f));

  // Debug-style tilt readout, delete when you don't need it.
  hline(2, SCREEN_H - 2, SCREEN_W - 4, DARKGRAY);
  pixel(SCREEN_W / 2 + (int)(input.tiltX * 28), SCREEN_H - 2, CYAN);
}

}  // namespace

PT_GAME(__ID__, "__TITLE__", init, update)
