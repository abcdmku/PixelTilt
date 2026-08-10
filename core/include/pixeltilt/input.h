#pragma once
#include <stdint.h>

namespace pt {

// Button bitmask. The Hub S3 board exposes three inputs: UP, CLICK (select)
// and DOWN. The PC emulator maps three keyboard keys onto the same bits, so
// game code never knows which platform it is on.
enum Button : uint8_t {
  BTN_UP    = 1 << 0,
  BTN_CLICK = 1 << 1,
  BTN_DOWN  = 1 << 2,
};

struct InputState {
  // Tilt, normalized to [-1, 1]. +x = tilted right, +y = tilted toward the
  // player (bottom edge down). On hardware this comes from the BNO08x gravity
  // vector; in the emulator, from the arrow keys.
  float tiltX = 0.0f;
  float tiltY = 0.0f;

  uint8_t buttons  = 0;  // currently held
  uint8_t pressed  = 0;  // went down this frame
  uint8_t released = 0;  // went up this frame

  bool held(Button b) const     { return buttons & b; }
  bool justDown(Button b) const { return pressed & b; }
  bool justUp(Button b) const   { return released & b; }
};

// Global input state for the current frame, maintained by the engine.
extern InputState input;

}  // namespace pt
