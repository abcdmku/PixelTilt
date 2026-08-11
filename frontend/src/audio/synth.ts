// Sample-accurate renderer for pt::SfxPatch (see core/include/pixeltilt/audio.h).
// Patches are rendered into a plain Float32Array with an ordinary sample loop
// rather than an AudioNode graph, so the exact same algorithm can be ported to
// the ESP32 audio driver later and both platforms sound identical.
import {
  SfxPatch,
  WAVE_NOISE,
  WAVE_SAW,
  WAVE_SINE,
  WAVE_SQUARE,
  WAVE_TRIANGLE,
} from "./patch";

export const SFX_SAMPLE_RATE = 22050;

/** xorshift32 — deterministic noise, mirrors pt::rand_. */
function makeRng(seed: number) {
  let s = seed >>> 0 || 1;
  return () => {
    s ^= s << 13;
    s >>>= 0;
    s ^= s >>> 17;
    s ^= s << 5;
    s >>>= 0;
    return s / 0xffffffff;
  };
}

export function patchDuration(p: SfxPatch): number {
  return p.attack + p.sustain + p.release;
}

export function renderPatch(p: SfxPatch, sampleRate = SFX_SAMPLE_RATE): Float32Array {
  const dur = Math.min(patchDuration(p), 4);
  const n = Math.max(1, Math.floor(dur * sampleRate));
  const out = new Float32Array(n);
  const rng = makeRng(0x50495854);

  const pitch = p.pitch > 0 ? p.pitch : 1;
  const f0 = Math.max(1, p.freqStart * pitch);
  const f1 = Math.max(1, p.freqEnd * pitch);
  const glide = Math.log(f1 / f0); // exponential sweep exponent

  let phase = 0;
  let hold = 0; // sample & hold state for pitched noise
  let holdT = 0;

  for (let i = 0; i < n; i++) {
    const t = i / sampleRate;
    const u = t / dur;

    let freq = f0 * Math.exp(glide * u);
    if (p.arpMult > 0 && p.arpTime > 0 && t >= p.arpTime) freq *= p.arpMult;
    if (p.vibDepth > 0) freq += p.vibDepth * Math.sin(2 * Math.PI * p.vibRate * t);
    if (freq < 1) freq = 1;

    phase += freq / sampleRate;
    const ph = phase - Math.floor(phase);

    let s: number;
    switch (p.wave) {
      case WAVE_SQUARE:
        s = ph < (p.duty || 0.5) ? 1 : -1;
        break;
      case WAVE_SAW:
        s = 2 * ph - 1;
        break;
      case WAVE_TRIANGLE:
        s = ph < 0.5 ? 4 * ph - 1 : 3 - 4 * ph;
        break;
      case WAVE_SINE:
        s = Math.sin(2 * Math.PI * ph);
        break;
      case WAVE_NOISE:
      default: {
        // White noise resampled at `freq` so the sweep reads as pitch.
        holdT += freq / sampleRate;
        if (holdT >= 0.5) {
          holdT = 0;
          hold = rng() * 2 - 1;
        }
        s = hold;
        break;
      }
    }

    let env: number;
    if (t < p.attack) env = t / Math.max(p.attack, 1e-5);
    else if (t < p.attack + p.sustain) env = 1;
    else env = Math.max(0, 1 - (t - p.attack - p.sustain) / Math.max(p.release, 1e-5));

    out[i] = s * env * p.volume;
  }
  return out;
}
