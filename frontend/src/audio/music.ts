// Background music host. The core only requests an abstract mood track
// (pt::MusicTrack); this module supplies the audio — by default a programmatic
// chiptune loop scheduled on oscillators, or, if the user converted a song in
// the Audio Lab and assigned it to a track slot, that PTA file on loop.
import { audioUnlocked, audioContext, musicBusNode, onAudioUnlock, playBuffer } from "./engine";
import { ptaToAudioBuffer } from "./pta";

// Mirrors pt::MusicTrack in core/include/pixeltilt/audio.h.
export const MUS_NONE = 0;
export const MUS_WIZ3 = 5;
export const MUSIC_TRACK_NAMES = ["NONE", "MENU", "CHILL", "ACTION", "TENSE", "WIZ3"];

const BUNDLED_PTA_URLS: Partial<Record<number, string>> = {
  [MUS_WIZ3]: new URL("../../../assets/music/wiz3.pta", import.meta.url).href,
};

const OVERRIDE_KEY = (track: number) => `pixeltilt.music.${track}`;

interface Channel {
  wave: OscillatorType;
  volume: number;
  /** Space-separated steps: "C4" note, "C4:3" held 3 steps, "." rest. */
  steps: string;
}

interface TrackDef {
  bpm: number;
  stepsPerBeat: number;
  channels: Channel[];
}

// --- the built-in tracks ----------------------------------------------------
// Written as step strings so tweaking a tune is a one-line edit. The device
// firmware plays the same tunes from core/src/audio.cpp (MUSIC_DEFS) — keep
// both copies in sync when editing. Channel volumes sit ~-6 dB low on
// purpose; the music bus runs at 2x scale (engine.ts MUSIC_SCALE), leaving
// headroom for converted PTA songs to play above the chiptunes.

const TRACKS: Record<number, TrackDef> = {
  // MENU — easy Am/F/C/G arps, the "attract mode" loop.
  1: {
    bpm: 96,
    stepsPerBeat: 2,
    channels: [
      {
        wave: "triangle",
        volume: 0.15,
        steps: "A2:4 A2:4 F2:4 F2:4 C3:4 C3:4 G2:4 G2:4",
      },
      {
        wave: "square",
        volume: 0.055,
        steps:
          "A3 C4 E4 C4 A3 C4 E4 C4 F3 A3 C4 A3 F3 A3 C4 A3 " +
          "C4 E4 G4 E4 C4 E4 G4 E4 G3 B3 D4 B3 G3 B3 D4 B3",
      },
    ],
  },
  // CHILL — slow Cmaj7/Fmaj7 float for the zen games.
  2: {
    bpm: 72,
    stepsPerBeat: 2,
    channels: [
      {
        wave: "sine",
        volume: 0.15,
        steps: "C3:8 A2:8 F2:8 G2:8",
      },
      {
        wave: "triangle",
        volume: 0.08,
        steps:
          "E4:2 G4:2 B4:2 G4:2 E4:2 G4:2 C5:2 B4:2 " +
          "A4:2 C5:2 E5:2 C5:2 A4:2 B4:2 G4:2 D4:2",
      },
    ],
  },
  // ACTION — driving Em riff, sixteenth-note bass.
  3: {
    bpm: 140,
    stepsPerBeat: 4,
    channels: [
      {
        wave: "square",
        volume: 0.10,
        steps:
          "E2 E2 E3 E2 E2 E3 E2 E3 G2 G2 G3 G2 A2 A2 A3 A2 " +
          "E2 E2 E3 E2 E2 E3 E2 E3 C3 C3 C2 C3 B2 B2 B1 B2",
      },
      {
        wave: "square",
        volume: 0.05,
        steps:
          "E4:2 . G4:2 . B4:2 . E5:2 D5:2 B4:2 . G4:2 . " +
          "E4:2 . G4:2 A4:2 B4:4 . . G4:2 A4:2 B4:2 C5:2 B4:2 A4:2 G4:2 F#4:2",
      },
    ],
  },
  // TENSE — chromatic minor ostinato, countdown energy.
  4: {
    bpm: 120,
    stepsPerBeat: 4,
    channels: [
      {
        wave: "triangle",
        volume: 0.15,
        steps: "A2 A2 A2 A2 A2 A2 Bb2 B2 A2 A2 A2 A2 G2 G2 E2 E2",
      },
      {
        wave: "sawtooth",
        volume: 0.04,
        steps: "A4 . C5 . B4 . F5 E5 A4 . C5 . E5 . D5 Bb4",
      },
    ],
  },
};

// WIZ3 normally uses its bundled PTA song. Keep ACTION as its fallback if the
// asset cannot be loaded.
TRACKS[MUS_WIZ3] = TRACKS[3];

// --- note parsing ------------------------------------------------------------

const NOTE_OFFSETS: Record<string, number> = {
  C: 0, "C#": 1, Db: 1, D: 2, "D#": 3, Eb: 3, E: 4, F: 5, "F#": 6, Gb: 6,
  G: 7, "G#": 8, Ab: 8, A: 9, "A#": 10, Bb: 10, B: 11,
};

function noteFreq(name: string): number {
  const m = /^([A-G](?:#|b)?)(-?\d)$/.exec(name);
  if (!m) return 0;
  const midi = 12 * (parseInt(m[2], 10) + 1) + NOTE_OFFSETS[m[1]];
  return 440 * Math.pow(2, (midi - 69) / 12);
}

interface NoteEvent {
  t: number; // seconds from loop start
  dur: number;
  freq: number;
  wave: OscillatorType;
  volume: number;
}

function compile(def: TrackDef): { events: NoteEvent[]; loopDur: number } {
  const stepDur = 60 / def.bpm / def.stepsPerBeat;
  const events: NoteEvent[] = [];
  let loopDur = 0;
  for (const ch of def.channels) {
    let step = 0;
    for (const tok of ch.steps.trim().split(/\s+/)) {
      if (tok === ".") {
        step++;
        continue;
      }
      const [name, lenStr] = tok.split(":");
      const len = lenStr ? parseInt(lenStr, 10) : 1;
      const freq = noteFreq(name);
      if (freq > 0) {
        events.push({
          t: step * stepDur,
          dur: len * stepDur,
          freq,
          wave: ch.wave,
          volume: ch.volume,
        });
      }
      step += len;
    }
    loopDur = Math.max(loopDur, step * stepDur);
  }
  events.sort((a, b) => a.t - b.t);
  return { events, loopDur };
}

// --- PTA overrides -----------------------------------------------------------

export function musicOverride(track: number): Uint8Array | null {
  try {
    const b64 = localStorage.getItem(OVERRIDE_KEY(track));
    if (!b64) return null;
    return Uint8Array.from(atob(b64), (ch) => ch.charCodeAt(0));
  } catch {
    return null;
  }
}

/** Assign converted PTA bytes as a track's music (null clears the slot).
 *  Returns false if storage rejected it (usually the localStorage quota). */
export function setMusicOverride(track: number, bytes: Uint8Array | null): boolean {
  let ok = true;
  try {
    if (!bytes) localStorage.removeItem(OVERRIDE_KEY(track));
    else {
      let s = "";
      for (let i = 0; i < bytes.length; i += 0x8000) {
        s += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
      }
      localStorage.setItem(OVERRIDE_KEY(track), btoa(s));
    }
  } catch {
    ok = false; // storage full/unavailable — the override just doesn't stick
  }
  if (track === currentTrack) {
    const t = currentTrack;
    stopMusic();
    setMusicTrack(t);
  }
  return ok;
}

const bundledPtaLoads = new Map<number, Promise<Uint8Array>>();

function loadBundledPta(track: number): Promise<Uint8Array> | null {
  const url = BUNDLED_PTA_URLS[track];
  if (!url) return null;
  let pending = bundledPtaLoads.get(track);
  if (!pending) {
    pending = fetch(url).then(async (response) => {
      if (!response.ok) throw new Error(`music asset returned ${response.status}`);
      return new Uint8Array(await response.arrayBuffer());
    });
    bundledPtaLoads.set(track, pending);
  }
  return pending;
}

// --- sequencer ---------------------------------------------------------------

const LOOKAHEAD = 0.4; // seconds scheduled ahead
const TICK_MS = 120;

let currentTrack = MUS_NONE;
let timer: number | null = null;
let stopPta: (() => void) | null = null;
let live: { osc: OscillatorNode; gain: GainNode }[] = [];
let musicRevision = 0;

function scheduleNote(ev: NoteEvent, when: number) {
  const ctx = audioContext();
  const bus = musicBusNode();
  if (!ctx || !bus) return;
  const osc = ctx.createOscillator();
  const gain = ctx.createGain();
  osc.type = ev.wave;
  osc.frequency.value = ev.freq;
  const a = 0.008;
  const rel = Math.min(0.08, ev.dur * 0.3);
  gain.gain.setValueAtTime(0, when);
  gain.gain.linearRampToValueAtTime(ev.volume, when + a);
  gain.gain.setValueAtTime(ev.volume, when + Math.max(a, ev.dur - rel));
  gain.gain.linearRampToValueAtTime(0, when + ev.dur);
  osc.connect(gain);
  gain.connect(bus);
  osc.start(when);
  osc.stop(when + ev.dur + 0.02);
  const entry = { osc, gain };
  live.push(entry);
  osc.onended = () => {
    live = live.filter((l) => l !== entry);
    gain.disconnect();
  };
}

function startSequencer(def: TrackDef) {
  const ctx = audioContext();
  if (!ctx) return;
  const { events, loopDur } = compile(def);
  if (!events.length || loopDur <= 0) return;
  let loopStart = ctx.currentTime + 0.05;
  let nextIdx = 0;
  const tick = () => {
    const horizon = ctx.currentTime + LOOKAHEAD;
    for (;;) {
      if (nextIdx >= events.length) {
        if (loopStart + loopDur > horizon) break;
        loopStart += loopDur;
        nextIdx = 0;
      }
      const ev = events[nextIdx];
      const when = loopStart + ev.t;
      if (when > horizon) break;
      scheduleNote(ev, Math.max(when, ctx.currentTime + 0.01));
      nextIdx++;
    }
  };
  tick();
  timer = window.setInterval(tick, TICK_MS);
}

export function stopMusic() {
  musicRevision++;
  if (timer !== null) {
    clearInterval(timer);
    timer = null;
  }
  if (stopPta) {
    stopPta();
    stopPta = null;
  }
  for (const { osc } of live) {
    try {
      osc.stop();
    } catch {
      // already stopped
    }
  }
  live = [];
  currentTrack = MUS_NONE;
}

function startPta(bytes: Uint8Array): boolean {
  const ctx = audioContext();
  if (!ctx) return false;
  try {
    stopPta = playBuffer(ptaToAudioBuffer(ctx, bytes), {
      bus: "music",
      loop: true,
      gain: 0.8,
    });
    return true;
  } catch {
    return false;
  }
}

function startFallback(track: number) {
  const def = TRACKS[track];
  if (def) startSequencer(def);
}

/** Play a pt::MusicTrack id; restarts only when the track actually changes. */
export function setMusicTrack(track: number) {
  if (track === currentTrack) return;
  stopMusic();
  currentTrack = track;
  if (track === MUS_NONE || !audioUnlocked()) return;

  const override = musicOverride(track);
  const ctx = audioContext();
  if (override && ctx) {
    try {
      // With the chiptunes authored -6 dB, 0.8x puts converted songs ~5 dB
      // above them (mirrors PTA_MUSIC_GAIN in firmware/src/audio_out.cpp).
      stopPta = playBuffer(ptaToAudioBuffer(ctx, override), {
        bus: "music",
        loop: true,
        gain: 0.8,
      });
      return;
    } catch {
      // corrupt override — fall through to the built-in tune
    }
  }
  const bundled = loadBundledPta(track);
  if (bundled) {
    const revision = musicRevision;
    void bundled.then(
      (bytes) => {
        if (revision !== musicRevision || currentTrack !== track || !audioUnlocked()) return;
        if (!startPta(bytes)) startFallback(track);
      },
      () => {
        if (revision === musicRevision && currentTrack === track) startFallback(track);
      },
    );
    return;
  }
  startFallback(track);
}

export function currentMusicTrack(): number {
  return currentTrack;
}

// If the engine asked for music before the first user gesture, start it the
// moment audio unlocks.
onAudioUnlock(() => {
  const t = currentTrack;
  if (t !== MUS_NONE && timer === null && !stopPta) {
    currentTrack = MUS_NONE;
    setMusicTrack(t);
  }
});
