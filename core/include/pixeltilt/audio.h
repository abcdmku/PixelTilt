#pragma once
#include <stdint.h>

// Shared audio framework. The core never synthesizes or touches an audio
// device — it is freestanding and identical on device and browser. Instead it
// keeps a small library of parametric SFX patches (sfxr-style, in several
// sound-bank "styles") and records play requests into a ring
// buffer of events. Each platform host drains the ring and renders the
// patches however it likes (Web Audio in the emulator, a DAC/codec driver on
// the ESP32). Background music works the same way: games request an abstract
// mood track and the host supplies the actual audio (a built-in chiptune
// pattern or a converted PTA file).
//
// Game-facing API:
//   setSfxStyle(STYLE_CHIP);        // in init(): pick the game's sound bank
//   sfx(SFX_COIN);                  // fire-and-forget one-shot
//   sfx(SFX_BOUNCE, 1.3f);          // same patch, pitched up 30%
//   sfxSample(BYTES, sizeof(BYTES)); // one-shot from an embedded .pta sample
//   music(MUS_ACTION);              // in init(): request background music
//
// Host-facing API (see wasm_main.cpp / firmware):
//   sfxRing()/sfxHead()             // drain events with serial > last seen
//   musicTrack()/musicSerial()      // current track request + change counter
//   settings().sfxVolume/.musicVolume  // 0..100, user-set in the settings menu

namespace pt {

enum SfxWave : uint32_t {
  WAVE_SQUARE = 0,  // duty-cycle square
  WAVE_SAW,
  WAVE_TRIANGLE,
  WAVE_SINE,
  WAVE_NOISE,       // freq-tinted white noise (sample & hold at freq)
  WAVE_SAMPLE,      // embedded PTA sample (see SfxPatch::sample / sfxSample)
};

// Abstract game events; each style bank tunes a patch for every one of these.
enum SfxId {
  SFX_BLIP,     // cursor move / tiny tick
  SFX_SELECT,   // confirm / launch
  SFX_COIN,     // score, pickup
  SFX_BOUNCE,   // paddle / wall contact
  SFX_JUMP,
  SFX_LASER,    // shoot
  SFX_HURT,     // small hit, life lost
  SFX_EXPLODE,  // crash, big fail
  SFX_POWERUP,  // level up, upgrade
  SFX_WIN,      // short victory jingle
  SFX_LOSE,     // game over
  SFX_ALARM,    // warning, countdown tick
  SFX_COUNT,
};

// Sound banks — same events, different character.
enum SfxStyle {
  STYLE_ARCADE,  // classic 50%-square cabinet bleeps (pong, breakout)
  STYLE_CHIP,    // bright NES-ish 25%-square with arps and vibrato
  STYLE_SOFT,    // mellow sine/triangle, gentle attacks (zen games)
  STYLE_GRIT,    // harsh saw/noise, raw retro (space debris games)
  STYLE_COUNT,
};

// Abstract background-music moods; the host maps these to actual audio.
enum MusicTrack {
  MUS_NONE = 0,
  MUS_MENU,
  MUS_CHILL,
  MUS_ACTION,
  MUS_TENSE,
  MUS_WIZ3,
  MUS_COUNT,
};

// One synth voice, fully described by data so every host renders the same
// sound. Field layout is part of the host ABI (read straight out of WASM
// linear memory by frontend/src/audio/patch.ts) — append, don't reorder.
struct SfxPatch {
  uint32_t wave;    // SfxWave
  float volume;     // 0..1 patch gain (user SFX volume is applied on top)
  float attack;     // seconds, linear fade in
  float sustain;    // seconds at full level
  float release;    // seconds, linear fade out
  float freqStart;  // Hz
  float freqEnd;    // Hz — exponential glide across the whole duration
  float duty;       // square duty 0.05..0.95 (WAVE_SQUARE only)
  float vibDepth;   // vibrato depth, Hz
  float vibRate;    // vibrato rate, Hz
  float arpMult;    // pitch multiplier applied after arpTime (0 = no arp)
  float arpTime;    // seconds into the sound the arp jump happens
  float pitch;      // runtime multiplier (sfx(id, pitch)); 1.0 in the tables
  // WAVE_SAMPLE only: an embedded PTA file (frontend/src/audio/pta.ts format)
  // the host plays instead of synthesizing; the synth fields above are unused
  // except volume and pitch (playback-rate multiplier). The pointer must stay
  // valid for the life of the sound — point at const data, not a stack buffer.
  const uint8_t* sample;  // nullptr for synth patches
  uint32_t sampleLen;
};

struct SfxEvent {
  uint32_t serial;  // monotonic, starts at 1; 0 = slot never used
  SfxPatch patch;
};

constexpr int SFX_RING_CAP = 16;

// --- game API ---------------------------------------------------------------
void     setSfxStyle(SfxStyle s);  // reset to STYLE_CHIP on every game launch
SfxStyle sfxStyle();
void sfx(SfxId id, float pitch = 1.0f);  // play from the current style bank
void sfx(SfxStyle style, SfxId id, float pitch = 1.0f);
void sfx(const SfxPatch& patch);         // play a custom one-off patch
// Play an embedded PTA sample (encode with the Audio Lab or the same IMA
// ADPCM encoder as frontend/src/audio/pta.ts) as a fire-and-forget one-shot.
void sfxSample(const uint8_t* pta, uint32_t len, float volume = 1.0f,
               float pitch = 1.0f);
void music(MusicTrack t);                // no-op if t is already playing
MusicTrack musicTrack();

// --- library access (hosts, tooling) ----------------------------------------
const SfxPatch& sfxPatch(SfxStyle style, SfxId id);
const char* sfxName(SfxId id);
const char* sfxStyleName(SfxStyle s);

// --- built-in music tracks ---------------------------------------------------
// The chiptunes are data, not code, so every host plays the same music: the
// browser's Web Audio sequencer and the ESP32 audio task both read these.
// Steps grammar: space-separated tokens; "C4" = one-step note, "C4:3" = note
// held 3 steps, "." = one-step rest. Channels loop at their own length.
struct MusicChannel {
  uint32_t wave;      // SfxWave (WAVE_NOISE unsupported for music)
  float volume;       // 0..1 channel gain
  const char* steps;
};

struct MusicDef {
  float bpm;
  int stepsPerBeat;
  int channelCount;
  const MusicChannel* channels;
};

// Track definition, or nullptr for MUS_NONE / out of range.
const MusicDef* musicDef(MusicTrack t);

// --- host drain -------------------------------------------------------------
const SfxEvent* sfxRing();  // SFX_RING_CAP slots, overwritten oldest-first
uint32_t sfxHead();         // last serial assigned (0 = nothing played yet);
                            // new events are lastSeen < serial <= sfxHead()
uint32_t musicSerial();     // bumps on every accepted music() change

void audioReset();  // engineInit: clear ring, style, music

}  // namespace pt
