// WASM host shim. Compiled freestanding (no libc); the browser drives the
// engine through these exports and reads the framebuffer straight out of
// linear memory. See frontend/src/emulator/wasm.ts for the JS side.
#include "pixeltilt/engine.h"
#include "pixeltilt/gfx.h"
#include "pixeltilt/game.h"
#include "pixeltilt/ptmath.h"

#define WASM_EXPORT(name) __attribute__((export_name(#name))) extern "C"

WASM_EXPORT(pt_init) void pt_init(unsigned seed) {
  pt::srand_(seed ? seed : 1u);
  pt::engineInit();
}

WASM_EXPORT(pt_tick) void pt_tick(float dt, float tiltX, float tiltY, unsigned buttons) {
  pt::engineTick(tiltX, tiltY, (unsigned char)buttons, dt);
}

WASM_EXPORT(pt_framebuffer) const unsigned char* pt_framebuffer() { return pt::framebuffer; }
WASM_EXPORT(pt_screen_w) int pt_screen_w() { return pt::SCREEN_W; }
WASM_EXPORT(pt_screen_h) int pt_screen_h() { return pt::SCREEN_H; }

WASM_EXPORT(pt_game_count) int pt_game_count() { return pt::GAME_COUNT; }
WASM_EXPORT(pt_game_title) const char* pt_game_title(int i) {
  return (i >= 0 && i < pt::GAME_COUNT) ? pt::GAME_LIST[i]->title : "";
}
WASM_EXPORT(pt_current_game) int pt_current_game() { return pt::currentGame(); }
WASM_EXPORT(pt_launch) void pt_launch(int i) { pt::launchGame(i); }
WASM_EXPORT(pt_exit_to_menu) void pt_exit_to_menu() { pt::exitToMenu(); }

// ---------------------------------------------------------------------------
// Freestanding runtime: clang may lower loops/aggregate copies to these.
// ---------------------------------------------------------------------------
typedef __SIZE_TYPE__ size_t;

extern "C" void* memset(void* dst, int v, size_t n) {
  unsigned char* d = (unsigned char*)dst;
  while (n--) *d++ = (unsigned char)v;
  return dst;
}

extern "C" void* memcpy(void* dst, const void* src, size_t n) {
  unsigned char* d = (unsigned char*)dst;
  const unsigned char* s = (const unsigned char*)src;
  while (n--) *d++ = *s++;
  return dst;
}

extern "C" void* memmove(void* dst, const void* src, size_t n) {
  unsigned char* d = (unsigned char*)dst;
  const unsigned char* s = (const unsigned char*)src;
  if (d < s) { while (n--) *d++ = *s++; }
  else { d += n; s += n; while (n--) *--d = *--s; }
  return dst;
}
