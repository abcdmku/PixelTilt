// ES8311/I2S speaker output. A pinned FreeRTOS task drains the core's SFX
// event ring and music track requests and synthesizes them into the I2S DMA
// buffer. The SFX voice renderer is a 1:1 port of the emulator's
// frontend/src/audio/synth.ts (same waveforms, envelope, glide, vibrato and
// arp math), and the music sequencer plays the shared note tables from
// core/src/audio.cpp — the device and the browser sound the same.
#include "audio_out.h"

#include <Arduino.h>
#include <math.h>
#include <driver/i2s.h>

#include "board_config.h"
#if AUDIO_ENABLED

#include "es8311.h"
#include "generated_music.h"
#include "pixeltilt/audio.h"
#include "pixeltilt/storage.h"

namespace {

constexpr i2s_port_t I2S_PORT = I2S_NUM_1;  // HUB75 uses LCD_CAM, but stay clear of I2S0 anyway
constexpr int FRAMES = 128;                 // ~5.8 ms per mix block at 22050 Hz
constexpr int MAX_SFX_VOICES = 6;
constexpr int MAX_MUSIC_VOICES = 8;
constexpr int MAX_MUSIC_EVENTS = 96;
constexpr float RATE = (float)AUDIO_SAMPLE_RATE;

ES8311 codec;
bool ok = false;

// --- shared oscillator ------------------------------------------------------

inline float osc(uint32_t wave, float ph, float duty) {
  switch (wave) {
    case pt::WAVE_SQUARE:   return ph < duty ? 1.0f : -1.0f;
    case pt::WAVE_SAW:      return 2.0f * ph - 1.0f;
    case pt::WAVE_TRIANGLE: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
    case pt::WAVE_SINE:     return sinf(2.0f * (float)M_PI * ph);
    default:                return 0.0f;  // noise handled in the SFX voice
  }
}

// --- embedded PTA playback ---------------------------------------------------
// Streams a PTA file (frontend/src/audio/pta.ts format: 16-byte header, mono
// IMA ADPCM in 256-byte blocks or raw PCM8) from flash, decoding one sample
// at a time and linearly resampling from the file's rate to the mix rate.
// Loops forever; replaces the chiptune sequencer for tracks that have a file
// embedded (see assets/music/README.md).

// With the chiptunes authored -6 dB and the music path at AUDIO_MUSIC_SCALE,
// 0.8x puts RMS-normalized PTA songs ~5 dB above the chiptunes (mirrors the
// browser's gain in frontend/src/audio/music.ts).
constexpr float PTA_MUSIC_GAIN = 0.8f;

const int16_t IMA_STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41,
    45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190,
    209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724,
    796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
    2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132,
    7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767};
const int8_t IMA_INDEX[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

struct PtaStream {
  bool active = false;
  bool loop = true;  // music loops; sample SFX play once
  const uint8_t* data = nullptr;
  uint32_t len = 0;
  uint32_t sampleCount = 0;
  uint16_t blockSize = 0;
  uint8_t codec = 0;  // 0 = ADPCM, 1 = PCM8
  // decode position
  uint32_t decoded = 0, blockIdx = 0, bytePos = 0, blockLeft = 0;
  bool headerPending = false;
  uint8_t nibbleHalf = 0;
  int16_t predictor = 0;
  int8_t index = 0;
  // linear resampler
  float step = 1, frac = 1, s0 = 0, s1 = 0;
};

PtaStream pta;

bool ptaOpen(PtaStream& p, const uint8_t* data, uint32_t len, float pitch = 1.0f) {
  p.active = false;
  if (!data || len < 16) return false;
  if (data[0] != 'P' || data[1] != 'T' || data[2] != 'A' || data[3] != '1') return false;
  uint32_t rate = data[4] | (data[5] << 8) | (data[6] << 16) | ((uint32_t)data[7] << 24);
  p.sampleCount = data[8] | (data[9] << 8) | (data[10] << 16) | ((uint32_t)data[11] << 24);
  p.blockSize = data[12] | (data[13] << 8);
  p.codec = data[14];
  if (rate < 4000 || rate > 48000 || p.sampleCount == 0) return false;
  if (p.codec == 0 && p.blockSize < 8) return false;
  p.data = data;
  p.len = len;
  p.step = rate / RATE * (pitch > 0 ? pitch : 1.0f);
  p.decoded = 0;
  p.blockIdx = 0;
  p.blockLeft = 0;
  p.headerPending = false;
  p.frac = 1;  // pull two real samples before the first output
  p.s0 = p.s1 = 0;
  p.active = true;
  return true;
}

float ptaNextSample(PtaStream& p) {
  if (p.decoded >= p.sampleCount) {
    if (!p.loop) {  // one-shot sample: hold silence and let the voice retire
      p.active = false;
      return 0;
    }
    p.decoded = 0;
    p.blockIdx = 0;
    p.blockLeft = 0;
    p.headerPending = false;
  }
  if (p.codec == 1) {
    uint32_t at = 16 + p.decoded;
    if (at >= p.len) return 0;
    p.decoded++;
    return ((int)p.data[at] - 128) / 128.0f;
  }
  if (!p.headerPending && p.blockLeft == 0) {
    uint32_t base = 16 + p.blockIdx * p.blockSize;
    if (base + p.blockSize > p.len) {  // truncated file — restart
      p.decoded = p.sampleCount;
      return 0;
    }
    p.predictor = (int16_t)(p.data[base] | (p.data[base + 1] << 8));
    p.index = (int8_t)(p.data[base + 2] > 88 ? 88 : p.data[base + 2]);
    p.bytePos = base + 4;
    p.nibbleHalf = 0;
    p.blockLeft = (p.blockSize - 4) * 2;
    p.blockIdx++;
    p.headerPending = true;
  }
  if (p.headerPending) {
    p.headerPending = false;
    p.decoded++;
    return p.predictor / 32768.0f;
  }
  uint8_t nib = (p.data[p.bytePos] >> (p.nibbleHalf * 4)) & 0xF;
  if (p.nibbleHalf) p.bytePos++;
  p.nibbleHalf ^= 1;
  p.blockLeft--;

  int stepv = IMA_STEP[p.index];
  int delta = stepv >> 3;
  if (nib & 4) delta += stepv;
  if (nib & 2) delta += stepv >> 1;
  if (nib & 1) delta += stepv >> 2;
  int pred = p.predictor + ((nib & 8) ? -delta : delta);
  p.predictor = (int16_t)(pred < -32768 ? -32768 : (pred > 32767 ? 32767 : pred));
  int idx = p.index + IMA_INDEX[nib];
  p.index = (int8_t)(idx < 0 ? 0 : (idx > 88 ? 88 : idx));
  p.decoded++;
  return p.predictor / 32768.0f;
}

inline float ptaResampled(PtaStream& p) {
  p.frac += p.step;
  while (p.frac >= 1.0f) {
    p.s0 = p.s1;
    p.s1 = ptaNextSample(p);
    p.frac -= 1.0f;
  }
  return p.s0 + (p.s1 - p.s0) * p.frac;
}

// --- SFX voices (port of synth.ts renderPatch) ------------------------------

struct SfxVoice {
  bool active = false;
  pt::SfxPatch p;
  uint32_t n = 0, total = 0;
  float dur = 0, f0 = 1, glide = 0;
  float phase = 0, hold = 0, holdT = 0;
  uint32_t rng = 1;
  PtaStream pta;  // WAVE_SAMPLE voices stream their patch's embedded PTA
};

SfxVoice sfxVoices[MAX_SFX_VOICES];

inline float rngNext(uint32_t& s) {  // xorshift32, mirrors pt::rand_
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return (float)s / 4294967295.0f;
}

void spawnSfx(const pt::SfxPatch& p) {
  // Prefer a free slot; otherwise steal the voice closest to finishing.
  SfxVoice* v = nullptr;
  for (auto& s : sfxVoices) {
    if (!s.active) { v = &s; break; }
  }
  if (!v) {
    v = &sfxVoices[0];
    for (auto& s : sfxVoices) {
      if (s.total - s.n < v->total - v->n) v = &s;
    }
  }
  v->p = p;
  float pitch = p.pitch > 0 ? p.pitch : 1.0f;
  v->pta.active = false;
  if (p.wave == pt::WAVE_SAMPLE) {
    if (!ptaOpen(v->pta, p.sample, p.sampleLen, pitch)) return;
    v->pta.loop = false;
    v->n = 0;
    v->total = (uint32_t)fmaxf(1.0f, v->pta.sampleCount / v->pta.step);
    v->active = true;
    return;
  }
  v->dur = fminf(p.attack + p.sustain + p.release, 4.0f);
  v->total = (uint32_t)fmaxf(1.0f, v->dur * RATE);
  v->f0 = fmaxf(1.0f, p.freqStart * pitch);
  float f1 = fmaxf(1.0f, p.freqEnd * pitch);
  v->glide = logf(f1 / v->f0);
  v->n = 0;
  v->phase = 0;
  v->hold = 0;
  v->holdT = 0;
  v->rng = 0x50495854u;
  v->active = true;
}

float renderSfx(SfxVoice& v) {
  const pt::SfxPatch& p = v.p;

  if (p.wave == pt::WAVE_SAMPLE) {
    if (!v.pta.active) {
      v.active = false;
      return 0;
    }
    v.n++;
    float s = ptaResampled(v.pta) * p.volume;
    if (!v.pta.active) v.active = false;
    return s;
  }

  float t = v.n / RATE;
  float u = t / v.dur;

  float freq = v.f0 * expf(v.glide * u);
  if (p.arpMult > 0 && p.arpTime > 0 && t >= p.arpTime) freq *= p.arpMult;
  if (p.vibDepth > 0) freq += p.vibDepth * sinf(2.0f * (float)M_PI * p.vibRate * t);
  if (freq < 1) freq = 1;

  v.phase += freq / RATE;
  float ph = v.phase - floorf(v.phase);

  float s;
  if (p.wave == pt::WAVE_NOISE) {
    // White noise resampled at `freq` so the sweep reads as pitch.
    v.holdT += freq / RATE;
    if (v.holdT >= 0.5f) {
      v.holdT = 0;
      v.hold = rngNext(v.rng) * 2.0f - 1.0f;
    }
    s = v.hold;
  } else {
    s = osc(p.wave, ph, p.duty > 0 ? p.duty : 0.5f);
  }

  float env;
  if (t < p.attack) env = t / fmaxf(p.attack, 1e-5f);
  else if (t < p.attack + p.sustain) env = 1.0f;
  else env = fmaxf(0.0f, 1.0f - (t - p.attack - p.sustain) / fmaxf(p.release, 1e-5f));

  if (++v.n >= v.total) v.active = false;
  return s * env * p.volume;
}

// --- music sequencer (plays pt::musicDef note tables) ------------------------

struct MusicEvent {
  float t, dur, freq, vol;
  uint8_t wave;
};

struct MusicVoice {
  bool active = false;
  uint8_t wave = 0;
  float freq = 0, vol = 0, dur = 0;
  uint32_t n = 0;
  float phase = 0;
};

MusicEvent musicEvents[MAX_MUSIC_EVENTS];
int musicEventCount = 0;
float musicLoopDur = 0;
double musicPlayhead = 0;
int musicNextEvent = 0;
MusicVoice musicVoices[MAX_MUSIC_VOICES];

// Tiny three-band analyser for the panel visualizer. Two one-pole low-pass
// filters split the actual music sample into bass, mid, and high without an
// FFT or another audio-sized buffer. This also sees embedded PTA songs.
float analysisLow = 0;
float analysisLowMid = 0;
float analysisBass = 0;
float analysisMid = 0;
float analysisHigh = 0;
float analysisLevel = 0;
float analysisBassFloor = 0;
float analysisBeat = 0;

int noteMidi(const char* s, int len) {
  if (len < 2) return -1;
  static const int8_t OFFSET[7] = {9, 11, 0, 2, 4, 5, 7};  // A B C D E F G
  if (s[0] < 'A' || s[0] > 'G') return -1;
  int semi = OFFSET[s[0] - 'A'];
  int i = 1;
  if (s[i] == '#') { semi++; i++; }
  else if (s[i] == 'b') { semi--; i++; }
  if (i >= len) return -1;
  int octave = s[i] - '0';
  if (octave < 0 || octave > 9) return -1;
  return 12 * (octave + 1) + semi;
}

void loadTrack(pt::MusicTrack track) {
  musicEventCount = 0;
  musicLoopDur = 0;
  musicPlayhead = 0;
  musicNextEvent = 0;
  for (auto& v : musicVoices) v.active = false;
  pta.active = false;
  analysisLow = analysisLowMid = 0;
  analysisBass = analysisMid = analysisHigh = analysisLevel = 0;
  analysisBassFloor = analysisBeat = 0;
  pt::setMusicAnalysis(0, 0, 0, 0, 0);

  // An embedded PTA file (assets/music/) replaces the chiptune for the track.
  if (track > 0 && track < (int)(sizeof(MUSIC_PTA) / sizeof(MUSIC_PTA[0])) &&
      ptaOpen(pta, MUSIC_PTA[track], MUSIC_PTA_LEN[track])) {
    return;
  }

  const pt::MusicDef* def = pt::musicDef(track);
  if (!def) return;

  float stepDur = 60.0f / def->bpm / def->stepsPerBeat;
  for (int c = 0; c < def->channelCount; c++) {
    const pt::MusicChannel& ch = def->channels[c];
    const char* s = ch.steps;
    int step = 0;
    while (*s) {
      while (*s == ' ') s++;
      if (!*s) break;
      const char* tok = s;
      while (*s && *s != ' ') s++;
      int tokLen = (int)(s - tok);
      if (tokLen == 1 && tok[0] == '.') {
        step++;
        continue;
      }
      int nameLen = tokLen;
      int len = 1;
      for (int i = 0; i < tokLen; i++) {
        if (tok[i] == ':') {
          nameLen = i;
          len = atoi(tok + i + 1);
          if (len < 1) len = 1;
          break;
        }
      }
      int midi = noteMidi(tok, nameLen);
      if (midi >= 0 && musicEventCount < MAX_MUSIC_EVENTS) {
        MusicEvent& e = musicEvents[musicEventCount++];
        e.t = step * stepDur;
        e.dur = len * stepDur;
        e.freq = 440.0f * powf(2.0f, (midi - 69) / 12.0f);
        e.vol = ch.volume;
        e.wave = (uint8_t)ch.wave;
      }
      step += len;
    }
    musicLoopDur = fmaxf(musicLoopDur, step * stepDur);
  }

  // Sort by start time (insertion sort, the tables are tiny).
  for (int i = 1; i < musicEventCount; i++) {
    MusicEvent e = musicEvents[i];
    int j = i - 1;
    while (j >= 0 && musicEvents[j].t > e.t) {
      musicEvents[j + 1] = musicEvents[j];
      j--;
    }
    musicEvents[j + 1] = e;
  }
}

void spawnMusicNote(const MusicEvent& e) {
  MusicVoice* v = nullptr;
  for (auto& m : musicVoices) {
    if (!m.active) { v = &m; break; }
  }
  if (!v) return;  // drop the note rather than cut a ringing one
  v->wave = e.wave;
  v->freq = e.freq;
  v->vol = e.vol;
  v->dur = e.dur;
  v->n = 0;
  v->phase = 0;
  v->active = true;
}

float renderMusicVoice(MusicVoice& v) {
  float t = v.n / RATE;
  if (t >= v.dur) {
    v.active = false;
    return 0;
  }
  v.phase += v.freq / RATE;
  float ph = v.phase - floorf(v.phase);
  // Same envelope the web sequencer schedules: 8 ms attack, release capped
  // at 30% of the note.
  const float a = 0.008f;
  float rel = fminf(0.08f, v.dur * 0.3f);
  float env;
  if (t < a) env = t / a;
  else if (t < v.dur - rel) env = 1.0f;
  else env = fmaxf(0.0f, (v.dur - t) / rel);
  v.n++;
  return osc(v.wave, ph, 0.5f) * env * v.vol;
}

// Advance the loop clock by one mix block, spawning any notes that came due.
void sequencerAdvance(float blockDur) {
  if (musicEventCount == 0 || musicLoopDur <= 0) return;
  musicPlayhead += blockDur;
  for (;;) {
    if (musicNextEvent >= musicEventCount) {
      if (musicPlayhead < musicLoopDur) break;
      musicPlayhead -= musicLoopDur;
      musicNextEvent = 0;
      continue;
    }
    if (musicEvents[musicNextEvent].t > musicPlayhead) break;
    spawnMusicNote(musicEvents[musicNextEvent]);
    musicNextEvent++;
  }
}

// --- core state drain --------------------------------------------------------

uint32_t lastSfxSerial = 0;
uint32_t lastMusicSerial = 0;

void drainCore() {
  // New SFX events. The ring is written by the game loop on the other CPU;
  // the serial is stored after the patch, so read serial / copy / re-check.
  uint32_t head = pt::sfxHead();
  if (head > lastSfxSerial) {
    const pt::SfxEvent* ring = pt::sfxRing();
    uint32_t first = lastSfxSerial + 1;
    if (head > (uint32_t)pt::SFX_RING_CAP && first < head - pt::SFX_RING_CAP + 1) {
      first = head - pt::SFX_RING_CAP + 1;
    }
    for (uint32_t serial = first; serial <= head; serial++) {
      const pt::SfxEvent& slot = ring[(serial - 1) % pt::SFX_RING_CAP];
      if (slot.serial != serial) continue;
      pt::SfxPatch p = slot.patch;
      if (slot.serial != serial) continue;  // overwritten mid-copy, drop it
      spawnSfx(p);
    }
    lastSfxSerial = head;
  }

  if (pt::musicSerial() != lastMusicSerial) {
    lastMusicSerial = pt::musicSerial();
    loadTrack(pt::musicTrack());
  }
}

// --- mixer task --------------------------------------------------------------

int16_t mixBuf[FRAMES * 2];  // stereo interleaved, same sample both channels

void audioTask(void*) {
  const float blockDur = FRAMES / RATE;
  for (;;) {
    drainCore();
    sequencerAdvance(blockDur);

    float sfxGain = pt::settings().sfxVolume / 100.0f * AUDIO_MASTER_GAIN;
    float musicGain =
        pt::settings().musicVolume / 100.0f * AUDIO_MASTER_GAIN * AUDIO_MUSIC_SCALE;

    float bassSum = 0, midSum = 0, highSum = 0, levelSum = 0;

    for (int i = 0; i < FRAMES; i++) {
      float sfx = 0, music = 0;
      for (auto& v : sfxVoices) {
        if (v.active) sfx += renderSfx(v);
      }
      if (pta.active) {
        music = ptaResampled(pta) * PTA_MUSIC_GAIN;
      } else {
        for (auto& v : musicVoices) {
          if (v.active) music += renderMusicVoice(v);
        }
      }
      // About 180 Hz and 1.6 kHz at 22.05 kHz. Absolute band energy is
      // enough for light, motion, and onset detection and costs six adds per
      // sample on the otherwise idle audio core.
      analysisLow += (music - analysisLow) * 0.049f;
      analysisLowMid += (music - analysisLowMid) * 0.313f;
      bassSum += fabsf(analysisLow);
      midSum += fabsf(analysisLowMid - analysisLow);
      highSum += fabsf(music - analysisLowMid);
      levelSum += fabsf(music);
      float out = sfx * sfxGain + music * musicGain;
      out = fmaxf(-1.0f, fminf(1.0f, out));
      int16_t s = (int16_t)(out * 32000.0f);
      mixBuf[i * 2] = s;
      mixBuf[i * 2 + 1] = s;
    }

    const float invFrames = 1.0f / FRAMES;
    float bass = fminf(1.0f, bassSum * invFrames * 9.0f);
    float mid = fminf(1.0f, midSum * invFrames * 8.0f);
    float high = fminf(1.0f, highSum * invFrames * 6.5f);
    float level = fminf(1.0f, levelSum * invFrames * 6.0f);
    auto follow = [](float current, float target) {
      return current + (target - current) * (target > current ? 0.32f : 0.12f);
    };
    analysisBass = follow(analysisBass, bass);
    analysisMid = follow(analysisMid, mid);
    analysisHigh = follow(analysisHigh, high);
    analysisLevel = follow(analysisLevel, level);
    bool onset = analysisBass > 0.18f && analysisBass > analysisBassFloor + 0.075f;
    analysisBassFloor += (analysisBass - analysisBassFloor) *
                         (analysisBass > analysisBassFloor ? 0.10f : 0.018f);
    analysisBeat = onset ? 1.0f : analysisBeat * 0.72f;
    pt::setMusicAnalysis(analysisLevel, analysisBass, analysisMid,
                         analysisHigh, analysisBeat);

    size_t written = 0;
    i2s_write(I2S_PORT, mixBuf, sizeof(mixBuf), &written, portMAX_DELAY);
  }
}

}  // namespace

bool audioSetup() {
  pinMode(NS4150_EN_PIN, OUTPUT);
  digitalWrite(NS4150_EN_PIN, LOW);  // keep the amp off until the codec runs

  if (!codec.begin(ES8311_ADDR, AUDIO_SAMPLE_RATE)) {
    Serial.println("WARN: ES8311 codec not responding - audio disabled");
    return false;
  }
  codec.setVolumeDb(ES8311_DAC_DB);

  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = AUDIO_SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = 0;
  cfg.dma_buf_count = 6;
  cfg.dma_buf_len = FRAMES;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk = 0;
  cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;  // ES8311 wants MCLK = 256*fs
  cfg.bits_per_chan = I2S_BITS_PER_CHAN_16BIT;
  if (i2s_driver_install(I2S_PORT, &cfg, 0, nullptr) != ESP_OK) {
    Serial.println("WARN: i2s_driver_install failed - audio disabled");
    return false;
  }

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_MCLK_PIN;
  pins.bck_io_num = I2S_BCLK_PIN;
  pins.ws_io_num = I2S_LRCK_PIN;
  pins.data_out_num = I2S_DOUT_PIN;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(I2S_PORT, &pins) != ESP_OK) {
    Serial.println("WARN: i2s_set_pin failed - audio disabled");
    i2s_driver_uninstall(I2S_PORT);
    return false;
  }
  i2s_zero_dma_buffer(I2S_PORT);

  digitalWrite(NS4150_EN_PIN, HIGH);

  // The game loop owns core 1; mix on core 0 (radio unused, so it's idle).
  xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 5, nullptr, 0);
  ok = true;
  return true;
}

bool audioOk() { return ok; }

#else  // !AUDIO_ENABLED

bool audioSetup() { return false; }
bool audioOk() { return false; }

#endif
