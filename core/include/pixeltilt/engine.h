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

// Index into GAME_LIST of the running game, or -1 when in the menu.
int  currentGame();
// Programmatic launch/exit (used by the emulator UI's game list).
void launchGame(int index);
void exitToMenu();

}  // namespace pt
