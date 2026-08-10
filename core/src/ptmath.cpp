#include "pixeltilt/ptmath.h"

namespace pt {

int floori(float x) {
  int i = (int)x;
  return (x < 0 && (float)i != x) ? i - 1 : i;
}

float fmodf_(float x, float y) {
  if (y == 0) return 0;
  return x - y * (float)floori(x / y);
}

// Sine via the Bhaskara-style parabola approximation, wrapped to [-PI, PI].
// Max error ~0.001 — indistinguishable at 64x64.
float sinf_(float x) {
  x = fmodf_(x + PI, TWO_PI) - PI;
  const float B = 4.0f / PI;
  const float C = -4.0f / (PI * PI);
  float y = B * x + C * x * fabsf_(x);
  const float P = 0.225f;
  return P * (y * fabsf_(y) - y) + y;
}

float cosf_(float x) { return sinf_(x + PI * 0.5f); }

float atan2f_(float y, float x) {
  if (x == 0 && y == 0) return 0;
  float ax = fabsf_(x), ay = fabsf_(y);
  float a = fminf_(ax, ay) / fmaxf_(ax, ay);
  float s = a * a;
  // Minimax polynomial for atan on [0,1].
  float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
  if (ay > ax) r = 1.57079637f - r;
  if (x < 0) r = PI - r;
  if (y < 0) r = -r;
  return r;
}

static uint32_t rngState = 0x12345678u;

void srand_(uint32_t seed) { rngState = seed ? seed : 0xdeadbeefu; }

uint32_t rand_() {
  uint32_t x = rngState;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rngState = x;
  return x;
}

int randRange(int lo, int hi) {
  if (hi <= lo) return lo;
  return lo + (int)(rand_() % (uint32_t)(hi - lo + 1));
}

float randf() { return (float)(rand_() >> 8) / 16777216.0f; }

}  // namespace pt
