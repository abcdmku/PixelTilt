#pragma once
#include <stdint.h>

// Small self-contained math kit. The core is compiled freestanding (no libc /
// libm) for the WASM emulator build, so everything a game needs lives here and
// behaves identically on device and in the browser.

// Arduino.h defines PI/TWO_PI as bare numeric macros, which would mangle the
// typed constants below when the firmware includes both headers.
#ifdef PI
#undef PI
#endif
#ifdef TWO_PI
#undef TWO_PI
#endif

namespace pt {

constexpr float PI = 3.14159265358979f;
constexpr float TWO_PI = 6.28318530717959f;

inline float fabsf_(float x) { return x < 0 ? -x : x; }
inline int   absi(int x)     { return x < 0 ? -x : x; }
inline float fminf_(float a, float b) { return a < b ? a : b; }
inline float fmaxf_(float a, float b) { return a > b ? a : b; }
inline int   mini(int a, int b) { return a < b ? a : b; }
inline int   maxi(int a, int b) { return a > b ? a : b; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int   clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

inline float sqrtf_(float x) { return __builtin_sqrtf(x); }

int   floori(float x);
float fmodf_(float x, float y);

// Polynomial approximations, plenty accurate for games (max err ~0.001).
float sinf_(float x);
float cosf_(float x);
float atan2f_(float y, float x);

// Deterministic xorshift RNG shared by all games.
void     srand_(uint32_t seed);
uint32_t rand_();                    // full 32-bit
int      randRange(int lo, int hi);  // inclusive lo..hi
float    randf();                    // [0,1)

}  // namespace pt
