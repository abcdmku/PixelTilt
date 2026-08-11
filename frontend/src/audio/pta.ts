// PTA ("PixelTilt Audio") — the project's ultra-small music/SFX file format.
// Mono, low sample rate, IMA ADPCM (4 bits/sample) or raw 8-bit PCM. Chosen
// over MP3 because the decoder is ~40 lines of integer math that runs fine on
// the ESP32 (no codec library), and 4-bit ADPCM at 8-16 kHz undercuts even
// low-bitrate MP3.
//
// Layout (little-endian):
//   0  "PTA1"           magic
//   4  u32 sampleRate
//   8  u32 sampleCount
//  12  u16 blockSize     bytes per ADPCM block (0 for PCM8)
//  14  u8  codec         0 = IMA ADPCM 4-bit, 1 = PCM 8-bit
//  15  u8  reserved
//  16  payload

export type PtaCodec = "adpcm" | "pcm8";

export interface PtaEncodeOptions {
  sampleRate: number; // output rate, e.g. 8000 / 11025 / 16000 / 22050
  codec: PtaCodec;
  normalize?: boolean; // peak-normalize to -0.5 dB before encoding
  trimSilence?: boolean; // drop leading/trailing quiet
}

export interface PtaData {
  sampleRate: number;
  samples: Float32Array;
}

const MAGIC = 0x31415450; // "PTA1" LE
const HEADER_SIZE = 16;
const BLOCK_SIZE = 256; // 4-byte block header + 252 data bytes = 505 samples

// Standard IMA ADPCM tables.
const STEP_TABLE = [
  7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
  50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
  253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
  1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
  3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
  11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
  32767,
];
const INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8];

function clampi16(v: number) {
  return v < -32768 ? -32768 : v > 32767 ? 32767 : v;
}

/** Average all channels of an AudioBuffer down to one Float32Array. */
export function toMono(buf: AudioBuffer): Float32Array {
  const out = new Float32Array(buf.length);
  for (let c = 0; c < buf.numberOfChannels; c++) {
    const ch = buf.getChannelData(c);
    for (let i = 0; i < ch.length; i++) out[i] += ch[i];
  }
  const inv = 1 / buf.numberOfChannels;
  for (let i = 0; i < out.length; i++) out[i] *= inv;
  return out;
}

/** Linear-interpolation resampler with a box pre-average when downsampling. */
export function resample(input: Float32Array, inRate: number, outRate: number): Float32Array {
  if (inRate === outRate) return input.slice();
  const ratio = inRate / outRate;
  const n = Math.max(1, Math.round(input.length / ratio));
  const out = new Float32Array(n);
  if (ratio > 1.5) {
    // Downsampling: average the source window so highs alias less harshly.
    for (let i = 0; i < n; i++) {
      const start = Math.floor(i * ratio);
      const end = Math.min(input.length, Math.max(start + 1, Math.floor((i + 1) * ratio)));
      let sum = 0;
      for (let j = start; j < end; j++) sum += input[j];
      out[i] = sum / (end - start);
    }
  } else {
    for (let i = 0; i < n; i++) {
      const pos = i * ratio;
      const j = Math.floor(pos);
      const frac = pos - j;
      const a = input[Math.min(j, input.length - 1)];
      const b = input[Math.min(j + 1, input.length - 1)];
      out[i] = a + (b - a) * frac;
    }
  }
  return out;
}

function trimSilence(input: Float32Array): Float32Array {
  const threshold = 0.01;
  let start = 0;
  let end = input.length;
  while (start < end && Math.abs(input[start]) < threshold) start++;
  while (end > start && Math.abs(input[end - 1]) < threshold) end--;
  if (start >= end) return new Float32Array(0);
  return input.subarray(Math.max(0, start - 32), Math.min(input.length, end + 32)).slice();
}

// Loudness (RMS) normalization, not peak: songs have ~10 dB of crest factor,
// so peak-normalized music sounds far quieter than the zero-crest chiptune
// square waves it plays alongside. Target the chiptunes' average level and
// only back off if the peaks would clip.
function normalize(input: Float32Array): Float32Array {
  const TARGET_RMS = 0.22;
  const PEAK_CEIL = 0.98;
  let peak = 0;
  let sumSq = 0;
  for (let i = 0; i < input.length; i++) {
    const v = Math.abs(input[i]);
    if (v > peak) peak = v;
    sumSq += input[i] * input[i];
  }
  const rms = Math.sqrt(sumSq / Math.max(1, input.length));
  if (peak < 1e-4 || rms < 1e-5) return input;
  const g = Math.min(TARGET_RMS / rms, PEAK_CEIL / peak);
  const out = new Float32Array(input.length);
  for (let i = 0; i < input.length; i++) out[i] = input[i] * g;
  return out;
}

export function encodePta(
  input: Float32Array,
  inRate: number,
  opts: PtaEncodeOptions,
): Uint8Array {
  let samples = resample(input, inRate, opts.sampleRate);
  if (opts.trimSilence) samples = trimSilence(samples);
  if (opts.normalize) samples = normalize(samples);

  const pcm = new Int16Array(samples.length);
  for (let i = 0; i < samples.length; i++) pcm[i] = clampi16(Math.round(samples[i] * 32767));

  let payload: Uint8Array;
  let blockSize = 0;
  if (opts.codec === "pcm8") {
    payload = new Uint8Array(pcm.length);
    for (let i = 0; i < pcm.length; i++) payload[i] = ((pcm[i] >> 8) + 128) & 0xff;
  } else {
    blockSize = BLOCK_SIZE;
    const samplesPerBlock = (BLOCK_SIZE - 4) * 2 + 1;
    const blocks = Math.ceil(Math.max(1, pcm.length) / samplesPerBlock);
    payload = new Uint8Array(blocks * BLOCK_SIZE);
    let si = 0;
    for (let b = 0; b < blocks; b++) {
      const base = b * BLOCK_SIZE;
      let predictor = pcm[Math.min(si, pcm.length - 1)] | 0;
      let index = 0;
      // Seed the step index from the local signal so loud blocks start sane.
      {
        let target = 0;
        for (let j = si; j < Math.min(si + 64, pcm.length - 1); j++)
          target = Math.max(target, Math.abs(pcm[j + 1] - pcm[j]));
        while (index < 88 && STEP_TABLE[index] < target) index++;
      }
      payload[base] = predictor & 0xff;
      payload[base + 1] = (predictor >> 8) & 0xff;
      payload[base + 2] = index;
      payload[base + 3] = 0;
      si++; // predictor carries the block's first sample

      for (let i = 0; i < BLOCK_SIZE - 4; i++) {
        let byte = 0;
        for (let half = 0; half < 2; half++) {
          const sample = si < pcm.length ? pcm[si] : predictor;
          si++;
          const step = STEP_TABLE[index];
          let diff = sample - predictor;
          let nibble = 0;
          if (diff < 0) {
            nibble = 8;
            diff = -diff;
          }
          let delta = step >> 3;
          if (diff >= step) {
            nibble |= 4;
            diff -= step;
            delta += step;
          }
          if (diff >= step >> 1) {
            nibble |= 2;
            diff -= step >> 1;
            delta += step >> 1;
          }
          if (diff >= step >> 2) {
            nibble |= 1;
            delta += step >> 2;
          }
          predictor = clampi16(nibble & 8 ? predictor - delta : predictor + delta);
          index = Math.max(0, Math.min(88, index + INDEX_TABLE[nibble]));
          byte |= (nibble & 0xf) << (half * 4);
        }
        payload[base + 4 + i] = byte;
      }
    }
  }

  const out = new Uint8Array(HEADER_SIZE + payload.length);
  const dv = new DataView(out.buffer);
  dv.setUint32(0, MAGIC, true);
  dv.setUint32(4, opts.sampleRate, true);
  dv.setUint32(8, samples.length, true);
  dv.setUint16(12, blockSize, true);
  dv.setUint8(14, opts.codec === "pcm8" ? 1 : 0);
  out.set(payload, HEADER_SIZE);
  return out;
}

export function decodePta(bytes: Uint8Array): PtaData {
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (bytes.length < HEADER_SIZE || dv.getUint32(0, true) !== MAGIC) {
    throw new Error("not a PTA file");
  }
  const sampleRate = dv.getUint32(4, true);
  const sampleCount = dv.getUint32(8, true);
  const blockSize = dv.getUint16(12, true);
  const codec = dv.getUint8(14);
  const payload = bytes.subarray(HEADER_SIZE);
  const samples = new Float32Array(sampleCount);

  if (codec === 1) {
    for (let i = 0; i < sampleCount && i < payload.length; i++) {
      samples[i] = (payload[i] - 128) / 128;
    }
  } else {
    let si = 0;
    for (let base = 0; base + blockSize <= payload.length && si < sampleCount; base += blockSize) {
      let predictor = (payload[base] | (payload[base + 1] << 8)) << 16 >> 16;
      let index = Math.min(88, payload[base + 2]);
      samples[si++] = predictor / 32768;
      for (let i = 0; i < blockSize - 4 && si < sampleCount; i++) {
        const byte = payload[base + 4 + i];
        for (let half = 0; half < 2 && si < sampleCount; half++) {
          const nibble = (byte >> (half * 4)) & 0xf;
          const step = STEP_TABLE[index];
          let delta = step >> 3;
          if (nibble & 4) delta += step;
          if (nibble & 2) delta += step >> 1;
          if (nibble & 1) delta += step >> 2;
          predictor = clampi16(nibble & 8 ? predictor - delta : predictor + delta);
          index = Math.max(0, Math.min(88, index + INDEX_TABLE[nibble]));
          samples[si++] = predictor / 32768;
        }
      }
    }
  }
  return { sampleRate, samples };
}

export function ptaToAudioBuffer(ctx: AudioContext, bytes: Uint8Array): AudioBuffer {
  const { sampleRate, samples } = decodePta(bytes);
  const buf = ctx.createBuffer(1, Math.max(1, samples.length), sampleRate);
  buf.copyToChannel(samples, 0);
  return buf;
}
