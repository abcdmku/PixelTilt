// Web Audio host: one lazily-created AudioContext with master -> sfx/music
// gain buses. Browsers gate audio behind a user gesture, so the context is
// created/resumed from a one-time pointer/key listener; sounds fired before
// that are simply dropped (they're fire-and-forget game blips).
import { SfxPatch, WAVE_SAMPLE } from "./patch";
import { ptaToAudioBuffer } from "./pta";
import { renderPatch, SFX_SAMPLE_RATE } from "./synth";

// The chiptune tables are authored ~-6 dB low (see music.ts); the music bus
// runs at 2x to compensate, which gives converted PTA songs headroom to sit
// above the chiptunes. A limiter after the bus catches the overshoot.
const MUSIC_SCALE = 2;

let ctx: AudioContext | null = null;
let sfxBus: GainNode | null = null;
let musicBus: GainNode | null = null;
let sfxLevel = 0.8;
let musicLevel = 0.6;

type UnlockListener = () => void;
const unlockListeners = new Set<UnlockListener>();

export function audioContext(): AudioContext | null {
  return ctx;
}

export function audioUnlocked(): boolean {
  return ctx !== null && ctx.state === "running";
}

/** Notifies when the context first reaches "running". */
export function onAudioUnlock(fn: UnlockListener): () => void {
  unlockListeners.add(fn);
  return () => unlockListeners.delete(fn);
}

function ensureContext(): AudioContext | null {
  if (!ctx) {
    const Ctor = window.AudioContext ?? (window as any).webkitAudioContext;
    if (!Ctor) return null;
    ctx = new Ctor();
    sfxBus = ctx.createGain();
    musicBus = ctx.createGain();
    sfxBus.gain.value = sfxLevel;
    musicBus.gain.value = musicLevel * MUSIC_SCALE;
    sfxBus.connect(ctx.destination);
    const limiter = ctx.createDynamicsCompressor();
    limiter.threshold.value = -6;
    limiter.knee.value = 4;
    limiter.ratio.value = 12;
    limiter.attack.value = 0.002;
    limiter.release.value = 0.25;
    musicBus.connect(limiter);
    limiter.connect(ctx.destination);
  }
  if (ctx.state === "suspended") {
    void ctx.resume().then(() => {
      if (audioUnlocked()) for (const fn of unlockListeners) fn();
    });
  } else {
    for (const fn of unlockListeners) fn();
  }
  return ctx;
}

/** Call from a user-gesture handler (keydown/pointerdown) to enable audio. */
export function unlockAudio() {
  ensureContext();
}

/** Install one-shot global gesture listeners that unlock audio. */
export function installAudioUnlock() {
  const once = () => {
    unlockAudio();
    if (audioUnlocked()) {
      window.removeEventListener("pointerdown", once);
      window.removeEventListener("keydown", once);
    }
  };
  window.addEventListener("pointerdown", once);
  window.addEventListener("keydown", once);
}

/** Volumes are percent 0..100, matching the device settings menu. */
export function setSfxVolume(percent: number) {
  sfxLevel = Math.max(0, Math.min(100, percent)) / 100;
  if (sfxBus && ctx) sfxBus.gain.setTargetAtTime(sfxLevel, ctx.currentTime, 0.02);
}

export function setMusicVolume(percent: number) {
  musicLevel = Math.max(0, Math.min(100, percent)) / 100;
  if (musicBus && ctx) {
    musicBus.gain.setTargetAtTime(musicLevel * MUSIC_SCALE, ctx.currentTime, 0.02);
  }
}

/** Fire-and-forget one-shot from a core SFX patch. */
export function playPatch(patch: SfxPatch) {
  if (!audioUnlocked() || !ctx || !sfxBus) return;
  if (patch.wave === WAVE_SAMPLE) {
    if (!patch.sample) return;
    let buf: AudioBuffer;
    try {
      buf = ptaToAudioBuffer(ctx, patch.sample);
    } catch {
      return; // corrupt sample data — drop the sound
    }
    const src = ctx.createBufferSource();
    src.buffer = buf;
    if (patch.pitch > 0 && patch.pitch !== 1) src.playbackRate.value = patch.pitch;
    const g = ctx.createGain();
    g.gain.value = patch.volume;
    src.connect(g);
    g.connect(sfxBus);
    src.start();
    return;
  }
  const samples = renderPatch(patch);
  const buf = ctx.createBuffer(1, samples.length, SFX_SAMPLE_RATE);
  buf.copyToChannel(samples, 0);
  const src = ctx.createBufferSource();
  src.buffer = buf;
  src.connect(sfxBus);
  src.start();
}

/** playPatch for UI buttons: unlocks the context first (call from a gesture). */
export function previewPatch(patch: SfxPatch) {
  const c = ensureContext();
  if (!c) return;
  if (c.state === "running") playPatch(patch);
  else void c.resume().then(() => playPatch(patch));
}

/** Get (and unlock) the context for decode/preview work; call from a gesture. */
export function ensureAudio(): AudioContext | null {
  return ensureContext();
}

/** Play an arbitrary buffer on a bus; returns a stop() handle.
 *  bus "master" bypasses the volume settings (Audio Lab previews). */
export function playBuffer(
  buffer: AudioBuffer,
  opts: { bus?: "sfx" | "music" | "master"; loop?: boolean; gain?: number } = {},
): (() => void) | null {
  if (!audioUnlocked() || !ctx) return null;
  const bus: AudioNode | null =
    opts.bus === "music" ? musicBus : opts.bus === "master" ? ctx.destination : sfxBus;
  if (!bus) return null;
  const src = ctx.createBufferSource();
  src.buffer = buffer;
  src.loop = opts.loop ?? false;
  if (opts.gain !== undefined && opts.gain !== 1) {
    const g = ctx.createGain();
    g.gain.value = opts.gain;
    src.connect(g);
    g.connect(bus);
  } else {
    src.connect(bus);
  }
  src.start();
  return () => {
    try {
      src.stop();
    } catch {
      // already ended
    }
  };
}

/** The music bus, for the sequencer to hang scheduled oscillators on. */
export function musicBusNode(): GainNode | null {
  return musicBus;
}
