#include "pixeltilt/audio.h"

namespace pt {

namespace {

SfxStyle currentStyle = STYLE_CHIP;
SfxEvent ring[SFX_RING_CAP];
uint32_t head = 0;  // last serial handed out; slot = (serial-1) % CAP

MusicTrack track = MUS_NONE;
uint32_t trackSerial = 0;

constexpr float NO_ARP = 0.0f;

// Patch field order:
//  {wave, volume, attack, sustain, release, freqStart, freqEnd,
//   duty, vibDepth, vibRate, arpMult, arpTime, pitch}
// One row per SfxId, in enum order:
//  BLIP SELECT COIN BOUNCE JUMP LASER HURT EXPLODE POWERUP WIN LOSE ALARM

const SfxPatch BANK_ARCADE[SFX_COUNT] = {
  {WAVE_SQUARE, 0.45f, 0.001f, 0.03f, 0.03f, 880.0f, 880.0f, 0.50f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_SQUARE, 0.50f, 0.001f, 0.05f, 0.06f, 660.0f, 990.0f, 0.50f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_SQUARE, 0.50f, 0.001f, 0.05f, 0.10f, 988.0f, 988.0f, 0.50f, 0, 0, 1.335f, 0.05f, 1.0f},
  {WAVE_SQUARE, 0.50f, 0.001f, 0.03f, 0.04f, 440.0f, 330.0f, 0.50f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_SQUARE, 0.45f, 0.001f, 0.10f, 0.10f, 330.0f, 880.0f, 0.50f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_SQUARE, 0.45f, 0.001f, 0.06f, 0.09f, 1400.0f, 320.0f, 0.30f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_SQUARE, 0.50f, 0.001f, 0.06f, 0.10f, 392.0f, 196.0f, 0.50f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_NOISE,  0.60f, 0.002f, 0.12f, 0.40f, 900.0f, 120.0f, 0.50f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_SQUARE, 0.50f, 0.001f, 0.15f, 0.15f, 523.0f, 1046.0f, 0.50f, 0, 0, 1.50f, 0.10f, 1.0f},
  {WAVE_SQUARE, 0.50f, 0.001f, 0.25f, 0.25f, 523.0f, 784.0f, 0.50f, 0, 0, 1.50f, 0.16f, 1.0f},
  {WAVE_SQUARE, 0.50f, 0.001f, 0.25f, 0.30f, 494.0f, 165.0f, 0.50f, 0, 0, 0.75f, 0.20f, 1.0f},
  {WAVE_SQUARE, 0.45f, 0.001f, 0.08f, 0.08f, 740.0f, 740.0f, 0.50f, 0, 0, 0.667f, 0.08f, 1.0f},
};

const SfxPatch BANK_CHIP[SFX_COUNT] = {
  {WAVE_SQUARE, 0.40f, 0.001f, 0.02f, 0.04f, 1175.0f, 1175.0f, 0.25f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_SQUARE, 0.45f, 0.001f, 0.04f, 0.08f, 784.0f, 1568.0f, 0.25f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_SQUARE, 0.45f, 0.001f, 0.04f, 0.12f, 1319.0f, 1319.0f, 0.25f, 0, 0, 1.50f, 0.045f, 1.0f},
  {WAVE_SQUARE, 0.45f, 0.001f, 0.02f, 0.05f, 587.0f, 440.0f, 0.25f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_SQUARE, 0.42f, 0.001f, 0.12f, 0.10f, 392.0f, 1046.0f, 0.25f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_SQUARE, 0.40f, 0.001f, 0.05f, 0.10f, 1760.0f, 440.0f, 0.15f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_SQUARE, 0.48f, 0.001f, 0.05f, 0.12f, 440.0f, 220.0f, 0.25f, 30.0f, 40.0f, NO_ARP, 0, 1.0f},
  {WAVE_NOISE,  0.55f, 0.002f, 0.10f, 0.45f, 1200.0f, 100.0f, 0.50f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_SQUARE, 0.45f, 0.001f, 0.12f, 0.20f, 587.0f, 1175.0f, 0.25f, 0, 0, 1.335f, 0.08f, 1.0f},
  {WAVE_SQUARE, 0.48f, 0.001f, 0.30f, 0.25f, 659.0f, 1319.0f, 0.25f, 12.0f, 7.0f, 1.50f, 0.15f, 1.0f},
  {WAVE_SQUARE, 0.48f, 0.001f, 0.30f, 0.30f, 587.0f, 147.0f, 0.25f, 10.0f, 6.0f, 0.667f, 0.22f, 1.0f},
  {WAVE_SQUARE, 0.42f, 0.001f, 0.06f, 0.08f, 988.0f, 988.0f, 0.25f, 0, 0, 0.75f, 0.07f, 1.0f},
};

const SfxPatch BANK_SOFT[SFX_COUNT] = {
  {WAVE_SINE,     0.40f, 0.005f, 0.03f, 0.08f, 1047.0f, 1047.0f, 0.50f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_SINE,     0.45f, 0.005f, 0.06f, 0.14f, 659.0f, 1047.0f, 0.50f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_SINE,     0.45f, 0.004f, 0.06f, 0.20f, 1319.0f, 1319.0f, 0.50f, 0, 0, 1.26f, 0.07f, 1.0f},
  {WAVE_TRIANGLE, 0.45f, 0.003f, 0.03f, 0.09f, 523.0f, 440.0f, 0.50f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_SINE,     0.42f, 0.005f, 0.12f, 0.15f, 440.0f, 880.0f, 0.50f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_TRIANGLE, 0.40f, 0.003f, 0.06f, 0.12f, 1568.0f, 523.0f, 0.50f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_SINE,     0.48f, 0.003f, 0.08f, 0.18f, 349.0f, 220.0f, 0.50f, 8.0f, 25.0f, NO_ARP, 0, 1.0f},
  {WAVE_NOISE,    0.45f, 0.010f, 0.15f, 0.55f, 500.0f, 80.0f, 0.50f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_SINE,     0.45f, 0.005f, 0.18f, 0.25f, 523.0f, 1047.0f, 0.50f, 0, 0, 1.26f, 0.12f, 1.0f},
  {WAVE_SINE,     0.48f, 0.005f, 0.35f, 0.35f, 523.0f, 784.0f, 0.50f, 5.0f, 5.0f, 1.335f, 0.20f, 1.0f},
  {WAVE_SINE,     0.48f, 0.005f, 0.35f, 0.40f, 440.0f, 175.0f, 0.50f, 4.0f, 4.0f, 0.80f, 0.25f, 1.0f},
  {WAVE_TRIANGLE, 0.40f, 0.003f, 0.10f, 0.12f, 880.0f, 880.0f, 0.50f, 0, 0, 0.841f, 0.10f, 1.0f},
};

const SfxPatch BANK_GRIT[SFX_COUNT] = {
  {WAVE_SAW,   0.35f, 0.001f, 0.02f, 0.04f, 740.0f, 740.0f, 0.50f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_SAW,   0.40f, 0.001f, 0.05f, 0.08f, 494.0f, 988.0f, 0.50f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_SAW,   0.40f, 0.001f, 0.05f, 0.10f, 880.0f, 880.0f, 0.50f, 0, 0, 1.50f, 0.05f, 1.0f},
  {WAVE_SAW,   0.40f, 0.001f, 0.02f, 0.05f, 330.0f, 247.0f, 0.50f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_SAW,   0.38f, 0.001f, 0.10f, 0.10f, 247.0f, 659.0f, 0.50f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_NOISE, 0.45f, 0.001f, 0.05f, 0.10f, 2400.0f, 400.0f, 0.50f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_SAW,   0.45f, 0.001f, 0.06f, 0.12f, 311.0f, 131.0f, 0.50f, 20.0f, 30.0f, NO_ARP, 0, 1.0f},
  {WAVE_NOISE, 0.65f, 0.002f, 0.18f, 0.60f, 700.0f, 60.0f, 0.50f, 0, 0, NO_ARP, 0, 1.0f},
  {WAVE_SAW,   0.42f, 0.001f, 0.14f, 0.18f, 415.0f, 831.0f, 0.50f, 0, 0, 1.50f, 0.09f, 1.0f},
  {WAVE_SAW,   0.45f, 0.001f, 0.28f, 0.28f, 440.0f, 880.0f, 0.50f, 15.0f, 9.0f, 1.335f, 0.16f, 1.0f},
  {WAVE_SAW,   0.45f, 0.002f, 0.30f, 0.35f, 349.0f, 87.0f, 0.50f, 12.0f, 7.0f, 0.707f, 0.22f, 1.0f},
  {WAVE_SAW,   0.40f, 0.001f, 0.07f, 0.07f, 622.0f, 622.0f, 0.50f, 0, 0, 0.667f, 0.07f, 1.0f},
};

const SfxPatch* const BANKS[STYLE_COUNT] = {BANK_ARCADE, BANK_CHIP, BANK_SOFT, BANK_GRIT};

const char* const SFX_NAMES[SFX_COUNT] = {
  "BLIP", "SELECT", "COIN", "BOUNCE", "JUMP", "LASER",
  "HURT", "EXPLODE", "POWERUP", "WIN", "LOSE", "ALARM",
};

const char* const STYLE_NAMES[STYLE_COUNT] = {"ARCADE", "CHIP", "SOFT", "GRIT"};

// --- music tracks (see MusicDef in audio.h for the steps grammar) -----------
// Kept in sync with the Web Audio sequencer's copies in
// frontend/src/audio/music.ts — edit both or the device and browser drift.
// Channel volumes sit ~-6 dB under full scale on purpose; hosts double their
// music bus scale to compensate, which leaves headroom for converted PTA
// songs to play louder than the chiptunes without clipping.

// MENU — easy Am/F/C/G arps, the "attract mode" loop.
const MusicChannel MENU_CH[] = {
    {WAVE_TRIANGLE, 0.15f, "A2:4 A2:4 F2:4 F2:4 C3:4 C3:4 G2:4 G2:4"},
    {WAVE_SQUARE, 0.055f,
     "A3 C4 E4 C4 A3 C4 E4 C4 F3 A3 C4 A3 F3 A3 C4 A3 "
     "C4 E4 G4 E4 C4 E4 G4 E4 G3 B3 D4 B3 G3 B3 D4 B3"},
};

// CHILL — slow Cmaj7/Fmaj7 float for the zen games.
const MusicChannel CHILL_CH[] = {
    {WAVE_SINE, 0.15f, "C3:8 A2:8 F2:8 G2:8"},
    {WAVE_TRIANGLE, 0.08f,
     "E4:2 G4:2 B4:2 G4:2 E4:2 G4:2 C5:2 B4:2 "
     "A4:2 C5:2 E5:2 C5:2 A4:2 B4:2 G4:2 D4:2"},
};

// ACTION — driving Em riff, sixteenth-note bass.
const MusicChannel ACTION_CH[] = {
    {WAVE_SQUARE, 0.10f,
     "E2 E2 E3 E2 E2 E3 E2 E3 G2 G2 G3 G2 A2 A2 A3 A2 "
     "E2 E2 E3 E2 E2 E3 E2 E3 C3 C3 C2 C3 B2 B2 B1 B2"},
    {WAVE_SQUARE, 0.05f,
     "E4:2 . G4:2 . B4:2 . E5:2 D5:2 B4:2 . G4:2 . "
     "E4:2 . G4:2 A4:2 B4:4 . . G4:2 A4:2 B4:2 C5:2 B4:2 A4:2 G4:2 F#4:2"},
};

// TENSE — chromatic minor ostinato, countdown energy.
const MusicChannel TENSE_CH[] = {
    {WAVE_TRIANGLE, 0.15f, "A2 A2 A2 A2 A2 A2 Bb2 B2 A2 A2 A2 A2 G2 G2 E2 E2"},
    {WAVE_SAW, 0.04f, "A4 . C5 . B4 . F5 E5 A4 . C5 . E5 . D5 Bb4"},
};

const MusicDef MUSIC_DEFS[MUS_COUNT] = {
    {0, 0, 0, nullptr},          // MUS_NONE
    {96.0f, 2, 2, MENU_CH},      // MUS_MENU
    {72.0f, 2, 2, CHILL_CH},     // MUS_CHILL
    {140.0f, 4, 2, ACTION_CH},   // MUS_ACTION
    {120.0f, 4, 2, TENSE_CH},    // MUS_TENSE
    {140.0f, 4, 2, ACTION_CH},   // MUS_WIZ3 (fallback when wiz3.pta is absent)
    {0, 0, 0, nullptr},          // MUS_RAVE (PTA-only)
    {0, 0, 0, nullptr},          // MUS_RAVE_ACID (PTA-only)
    {0, 0, 0, nullptr},          // MUS_RAVE_DODGEMS (PTA-only)
};

void push(const SfxPatch& p) {
  head++;
  SfxEvent& e = ring[(head - 1) % SFX_RING_CAP];
  e.patch = p;
  e.serial = head;
}

}  // namespace

volatile MusicAnalysis musicAnalysis = {0, 0, 0, 0, 0};

void setSfxStyle(SfxStyle s) {
  if (s >= 0 && s < STYLE_COUNT) currentStyle = s;
}

SfxStyle sfxStyle() { return currentStyle; }

void sfx(SfxId id, float pitch) { sfx(currentStyle, id, pitch); }

void sfx(SfxStyle style, SfxId id, float pitch) {
  if (id < 0 || id >= SFX_COUNT || style < 0 || style >= STYLE_COUNT) return;
  SfxPatch p = BANKS[style][id];
  p.pitch = pitch > 0.0f ? pitch : 1.0f;
  push(p);
}

void sfx(const SfxPatch& patch) { push(patch); }

void sfxSample(const uint8_t* pta, uint32_t len, float volume, float pitch) {
  if (!pta || len == 0) return;
  SfxPatch p = {};
  p.wave = WAVE_SAMPLE;
  p.volume = volume;
  p.pitch = pitch > 0.0f ? pitch : 1.0f;
  p.sample = pta;
  p.sampleLen = len;
  push(p);
}

void music(MusicTrack t) {
  if (t < 0 || t >= MUS_COUNT || t == track) return;
  track = t;
  trackSerial++;
}

MusicTrack musicTrack() { return track; }

void setMusicAnalysis(float level, float bass, float mid, float high, float beat) {
  musicAnalysis.level = level < 0 ? 0 : (level > 1 ? 1 : level);
  musicAnalysis.bass = bass < 0 ? 0 : (bass > 1 ? 1 : bass);
  musicAnalysis.mid = mid < 0 ? 0 : (mid > 1 ? 1 : mid);
  musicAnalysis.high = high < 0 ? 0 : (high > 1 ? 1 : high);
  musicAnalysis.beat = beat < 0 ? 0 : (beat > 1 ? 1 : beat);
}

const SfxPatch& sfxPatch(SfxStyle style, SfxId id) {
  int s = (style >= 0 && style < STYLE_COUNT) ? style : 0;
  int i = (id >= 0 && id < SFX_COUNT) ? id : 0;
  return BANKS[s][i];
}

const char* sfxName(SfxId id) {
  return (id >= 0 && id < SFX_COUNT) ? SFX_NAMES[id] : "";
}

const char* sfxStyleName(SfxStyle s) {
  return (s >= 0 && s < STYLE_COUNT) ? STYLE_NAMES[s] : "";
}

const MusicDef* musicDef(MusicTrack t) {
  if (t <= MUS_NONE || t >= MUS_COUNT) return nullptr;
  return &MUSIC_DEFS[t];
}

const SfxEvent* sfxRing() { return ring; }
uint32_t sfxHead() { return head; }
uint32_t musicSerial() { return trackSerial; }

void audioReset() {
  currentStyle = STYLE_CHIP;
  head = 0;
  track = MUS_NONE;
  trackSerial = 0;
  setMusicAnalysis(0, 0, 0, 0, 0);
  for (int i = 0; i < SFX_RING_CAP; i++) ring[i].serial = 0;
}

}  // namespace pt
