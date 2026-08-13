#pragma once
#include <stdint.h>

// The engine owns the menu <-> game <-> pause state machine and per-frame
// input edge detection. Both platform hosts (firmware main loop, WASM shim)
// drive it the same way each frame:
//
//   pt::engineTick(tiltX, tiltY, spin, rawButtons, dt);
//   ...then present pt::framebuffer however the platform does.
//
// Holding CLICK for ~0.7s in a game opens the pause menu (resume / settings /
// main menu). The main menu also has SCORES and SETTINGS entries; settings and
// high scores persist via the storage.h save-blob hooks.

namespace pt {

void engineInit();

// tiltX/tiltY in [-1,1], spin in rad/s (twist about the screen normal,
// positive = clockwise; pass 0 if the platform can't measure it), rawButtons
// is a Button bitmask, dt in seconds.
void engineTick(float tiltX, float tiltY, float spin, uint8_t rawButtons, float dt);

// Optional: linear (gravity-removed) acceleration in g, raw platform axes —
// same frame the platform's tilt is measured in. Call before engineTick each
// frame; the engine applies the mounting/rotation corrections and publishes it
// as input.accelX/Y/Z. Hosts that can't measure it just never call this.
void setAccel(float ax, float ay, float az);

// Optional: the full in-plane force field in g — the RAW specific force
// (tilt AND shake in one vector, may exceed 1 g, keep filtering minimal so
// shakes arrive with no latency), raw platform axes. Same calling pattern as
// setAccel; published as input.gravityX/Y.
void setGravity(float gx, float gy);

// Index into GAME_LIST of the running game, or -1 when in the menu.
int  currentGame();
// Tooling/introspection: index displayed by the score browser, or -1 while
// that screen is not active. Games with SCORE_NONE are never returned.
int currentScoreGame();
// Programmatic launch/exit (used by the emulator UI's game list).
void launchGame(int index);
void exitToMenu();

}  // namespace pt
