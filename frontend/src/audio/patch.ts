// TS mirror of pt::SfxPatch / pt::SfxEvent in core/include/pixeltilt/audio.h.
// The structs are read straight out of WASM linear memory, so the field order
// and sizes here must match the C++ layout exactly.

export const WAVE_SQUARE = 0;
export const WAVE_SAW = 1;
export const WAVE_TRIANGLE = 2;
export const WAVE_SINE = 3;
export const WAVE_NOISE = 4;
export const WAVE_SAMPLE = 5;

export interface SfxPatch {
  wave: number;
  volume: number;
  attack: number;
  sustain: number;
  release: number;
  freqStart: number;
  freqEnd: number;
  duty: number;
  vibDepth: number;
  vibRate: number;
  arpMult: number;
  arpTime: number;
  pitch: number;
  /** WAVE_SAMPLE only: the embedded PTA bytes, copied out of WASM memory. */
  sample?: Uint8Array;
}

/** sizeof(pt::SfxPatch): 13 float/u32 fields + sample pointer + length. */
export const PATCH_SIZE = 60;
/** sizeof(pt::SfxEvent): u32 serial + patch. */
export const EVENT_SIZE = 4 + PATCH_SIZE;

export function readPatch(mem: ArrayBuffer, ptr: number): SfxPatch {
  const f = new Float32Array(mem, ptr, PATCH_SIZE / 4);
  const u = new Uint32Array(mem, ptr, PATCH_SIZE / 4);
  const patch: SfxPatch = {
    wave: u[0],
    volume: f[1],
    attack: f[2],
    sustain: f[3],
    release: f[4],
    freqStart: f[5],
    freqEnd: f[6],
    duty: f[7],
    vibDepth: f[8],
    vibRate: f[9],
    arpMult: f[10],
    arpTime: f[11],
    pitch: f[12],
  };
  // wasm32 pointer = byte offset into linear memory. Copy the bytes out so
  // the patch stays valid if the ring slot is reused or memory grows.
  const samplePtr = u[13];
  const sampleLen = u[14];
  if (patch.wave === WAVE_SAMPLE && samplePtr && sampleLen && samplePtr + sampleLen <= mem.byteLength) {
    patch.sample = new Uint8Array(mem, samplePtr, sampleLen).slice();
  }
  return patch;
}
