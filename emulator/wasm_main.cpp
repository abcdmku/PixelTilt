// WASM host shim. Compiled freestanding (no libc); the browser drives the
// engine through these exports and reads the framebuffer straight out of
// linear memory. See frontend/src/emulator/wasm.ts for the JS side.
#include "pixeltilt/engine.h"
#include "pixeltilt/gfx.h"
#include "pixeltilt/game.h"
#include "pixeltilt/ptmath.h"
#include "pixeltilt/storage.h"
#include "pixeltilt/audio.h"

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

// Save-blob persistence (see storage.h): JS copies localStorage bytes over
// pt_save_ptr then calls pt_save_loaded; when pt_save_dirty it reads the blob
// back out and persists it.
WASM_EXPORT(pt_save_ptr) unsigned char* pt_save_ptr() { return pt::saveBlob(); }
WASM_EXPORT(pt_save_size) int pt_save_size() { return pt::saveBlobSize(); }
WASM_EXPORT(pt_save_loaded) int pt_save_loaded() { return pt::saveBlobLoad() ? 1 : 0; }
WASM_EXPORT(pt_save_dirty) int pt_save_dirty() { return pt::saveDirty() ? 1 : 0; }
WASM_EXPORT(pt_save_clear_dirty) void pt_save_clear_dirty() { pt::clearSaveDirty(); }

// Display brightness in percent; the host applies it (panel PWM / canvas).
WASM_EXPORT(pt_brightness) int pt_brightness() { return pt::settings().brightness; }

// --- audio (see audio.h) ----------------------------------------------------
// SFX are drained from a ring of SfxEvent{u32 serial, SfxPatch} — the JS side
// mirrors the struct layout in frontend/src/audio/patch.ts. Music is a track
// id the host maps to actual audio; volumes are user settings the host applies
// to its output buses.
WASM_EXPORT(pt_sfx_ring_ptr) const pt::SfxEvent* pt_sfx_ring_ptr() { return pt::sfxRing(); }
WASM_EXPORT(pt_sfx_ring_cap) int pt_sfx_ring_cap() { return pt::SFX_RING_CAP; }
WASM_EXPORT(pt_sfx_head) unsigned pt_sfx_head() { return pt::sfxHead(); }
WASM_EXPORT(pt_music_track) int pt_music_track() { return pt::musicTrack(); }
WASM_EXPORT(pt_music_serial) unsigned pt_music_serial() { return pt::musicSerial(); }
WASM_EXPORT(pt_sfx_volume) int pt_sfx_volume() { return pt::settings().sfxVolume; }
WASM_EXPORT(pt_music_volume) int pt_music_volume() { return pt::settings().musicVolume; }

// SFX library access for tooling (the Audio Lab patch browser).
WASM_EXPORT(pt_sfx_count) int pt_sfx_count() { return pt::SFX_COUNT; }
WASM_EXPORT(pt_sfx_style_count) int pt_sfx_style_count() { return pt::STYLE_COUNT; }
WASM_EXPORT(pt_sfx_patch) const pt::SfxPatch* pt_sfx_patch(int style, int id) {
  return &pt::sfxPatch((pt::SfxStyle)style, (pt::SfxId)id);
}
WASM_EXPORT(pt_sfx_name) const char* pt_sfx_name(int id) { return pt::sfxName((pt::SfxId)id); }
WASM_EXPORT(pt_sfx_style_name) const char* pt_sfx_style_name(int s) {
  return pt::sfxStyleName((pt::SfxStyle)s);
}

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
