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

  // The full in-plane force field in g: the device's raw specific force
  // (what an accelerometer measures), unfiltered and NOT rescaled to the
  // control full-scale angle. Held still it is pure tilt — magnitude
  // sin(physical tilt angle), 0 flat through 1.0 at vertical — but it also
  // carries shakes, so it can exceed 1: jerk the device toward its low edge
  // harder than free fall and the vector flips, and simulated grains lift
  // off the floor exactly like real sand in a rattled box. Use this for
  // physics (a = 9.8 m/s^2 * gravity); use tiltX/Y for menus and steering.
  // In the emulator: arrows (full press = vertical) plus Space shake noise.
  // Zero on hosts that never report it.
  float gravityX = 0.0f;
  float gravityY = 0.0f;

  // Twist rate about the axis pointing out of the screen, in rad/s; positive
  // = the panel rotating clockwise as you look at it. On hardware this is the
  // yaw rate from the BNO08x UART-RVC stream (0 in I2C-gravity mode); in the
  // emulator, the Q/E keys. Integrate it for a twist angle; expect a little
  // noise around 0, so gate on a threshold before acting on it.
  float spin = 0.0f;

  // Shake: linear (gravity-removed) acceleration in g, in the SAME axis and
  // sign convention as tilt — x/y are the in-screen-plane pseudo-force a loose
  // object feels, z is along the screen normal. Because tilt comes from the
  // same specific-force reading, games simulating free objects can simply add
  // it to their tilt field: field = tilt * G + accel * K, and a sharp jerk
  // slams things the physically right way. Expect sensor noise around 0, so
  // deadzone before acting on it. On hardware this is the IMU's linear
  // acceleration; in the emulator, holding Space synthesizes a shake and
  // devicemotion feeds it on phones. Zero on hosts that never report it.
  float accelX = 0.0f;
  float accelY = 0.0f;
  float accelZ = 0.0f;

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
