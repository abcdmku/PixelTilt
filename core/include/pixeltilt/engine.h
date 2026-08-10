#pragma once
#include <stdint.h>

// The engine owns the menu <-> game state machine and per-frame input edge
// detection. Both platform hosts (firmware main loop, WASM shim) drive it the
// same way each frame:
//
//   pt::engineTick(tiltX, tiltY, rawButtons, dt);
//   ...then present pt::framebuffer however the platform does.
//
// Holding UP + DOWN together for ~1s exits the running game back to the menu.

namespace pt {

void engineInit();

// tiltX/tiltY in [-1,1], rawButtons is a Button bitmask, dt in seconds.
void engineTick(float tiltX, float tiltY, uint8_t rawButtons, float dt);

// Index into GAME_LIST of the running game, or -1 when in the menu.
int  currentGame();
// Programmatic launch/exit (used by the emulator UI's game list).
void launchGame(int index);
void exitToMenu();

}  // namespace pt
