// Thin typed wrapper around the freestanding pixeltilt.wasm module built by
// tools/build-wasm.mjs. No JS glue is generated — the exports are stable C
// symbols and the framebuffer is read directly out of linear memory.
import { EVENT_SIZE, readPatch, SfxPatch } from "../audio/patch";

export const SCREEN_W = 64;
export const SCREEN_H = 64;

export const BTN_UP = 1 << 0;
export const BTN_CLICK = 1 << 1;
export const BTN_DOWN = 1 << 2;

interface Exports {
  memory: WebAssembly.Memory;
  pt_init(seed: number): void;
  pt_tick(dt: number, tiltX: number, tiltY: number, spin: number, buttons: number): void;
  pt_framebuffer(): number;
  pt_screen_w(): number;
  pt_screen_h(): number;
  pt_game_count(): number;
  pt_game_title(i: number): number;
  pt_current_game(): number;
  pt_launch(i: number): void;
  pt_exit_to_menu(): void;
  pt_save_ptr(): number;
  pt_save_size(): number;
  pt_save_loaded(): number;
  pt_save_dirty(): number;
  pt_save_clear_dirty(): void;
  pt_brightness(): number;
  pt_sfx_ring_ptr(): number;
  pt_sfx_ring_cap(): number;
  pt_sfx_head(): number;
  pt_music_track(): number;
  pt_music_serial(): number;
  pt_sfx_volume(): number;
  pt_music_volume(): number;
  pt_sfx_count(): number;
  pt_sfx_style_count(): number;
  pt_sfx_patch(style: number, id: number): number;
  pt_sfx_name(id: number): number;
  pt_sfx_style_name(s: number): number;
}

export interface SfxLibrary {
  /** Style-bank names, index = pt::SfxStyle. */
  styles: string[];
  /** Event names, index = pt::SfxId. */
  names: string[];
  patch(style: number, id: number): SfxPatch;
}

export interface Emulator {
  titles: string[];
  init(seed: number): void;
  /** spin = twist rate about the screen normal, rad/s, + = clockwise. */
  tick(dt: number, tiltX: number, tiltY: number, spin: number, buttons: number): void;
  /** RGB888 view of the 64x64 framebuffer (valid until next memory growth — the module never grows). */
  framebuffer(): Uint8Array;
  currentGame(): number;
  launch(i: number): void;
  exitToMenu(): void;
  /** Copy of the settings + high-scores save blob (persist it somewhere). */
  saveBlob(): Uint8Array;
  /** Restore a previously persisted save blob; false if it was rejected. */
  loadSave(bytes: Uint8Array): boolean;
  saveDirty(): boolean;
  clearSaveDirty(): void;
  /** Display brightness setting in percent (host applies it). */
  brightness(): number;
  /** SFX events with serial > since, oldest first; play them and keep head. */
  drainSfx(since: number): { head: number; patches: SfxPatch[] };
  /** Requested background-music track id (pt::MusicTrack). */
  musicTrack(): number;
  /** Bumps whenever the requested track changes. */
  musicSerial(): number;
  /** User volume settings, percent 0..100 (host applies to its buses). */
  sfxVolume(): number;
  musicVolume(): number;
  /** The core's built-in parametric SFX patch banks. */
  sfxLibrary: SfxLibrary;
}

function cString(mem: WebAssembly.Memory, ptr: number): string {
  const bytes = new Uint8Array(mem.buffer);
  let end = ptr;
  while (bytes[end] !== 0) end++;
  return new TextDecoder().decode(bytes.subarray(ptr, end));
}

export async function loadEmulator(): Promise<Emulator> {
  const url = `${import.meta.env.BASE_URL}pixeltilt.wasm`;
  const res = await fetch(url);
  if (!res.ok) {
    throw new Error(
      `failed to fetch pixeltilt.wasm (HTTP ${res.status}) — run \`npm run wasm\` from the repo root first`,
    );
  }
  const { instance } = WebAssembly.instantiateStreaming
    ? await WebAssembly.instantiateStreaming(res, {})
    : await WebAssembly.instantiate(await res.arrayBuffer(), {});
  const e = instance.exports as unknown as Exports;

  if (e.pt_screen_w() !== SCREEN_W || e.pt_screen_h() !== SCREEN_H) {
    throw new Error("wasm module screen size mismatch");
  }

  const titles: string[] = [];
  for (let i = 0; i < e.pt_game_count(); i++) {
    titles.push(cString(e.memory, e.pt_game_title(i)));
  }

  const fbPtr = e.pt_framebuffer();
  const savePtr = e.pt_save_ptr();
  const saveSize = e.pt_save_size();

  const ringPtr = e.pt_sfx_ring_ptr();
  const ringCap = e.pt_sfx_ring_cap();
  const sfxLibrary: SfxLibrary = {
    styles: Array.from({ length: e.pt_sfx_style_count() }, (_, s) =>
      cString(e.memory, e.pt_sfx_style_name(s)),
    ),
    names: Array.from({ length: e.pt_sfx_count() }, (_, i) =>
      cString(e.memory, e.pt_sfx_name(i)),
    ),
    patch: (style, id) => readPatch(e.memory.buffer, e.pt_sfx_patch(style, id)),
  };

  return {
    titles,
    init: (seed) => e.pt_init(seed >>> 0),
    tick: (dt, tx, ty, spin, b) => e.pt_tick(dt, tx, ty, spin, b >>> 0),
    framebuffer: () => new Uint8Array(e.memory.buffer, fbPtr, SCREEN_W * SCREEN_H * 3),
    currentGame: () => e.pt_current_game(),
    launch: (i) => e.pt_launch(i),
    exitToMenu: () => e.pt_exit_to_menu(),
    saveBlob: () => new Uint8Array(e.memory.buffer, savePtr, saveSize).slice(),
    loadSave: (bytes) => {
      if (bytes.length !== saveSize) return false;
      new Uint8Array(e.memory.buffer, savePtr, saveSize).set(bytes);
      return e.pt_save_loaded() !== 0;
    },
    saveDirty: () => e.pt_save_dirty() !== 0,
    clearSaveDirty: () => e.pt_save_clear_dirty(),
    brightness: () => e.pt_brightness(),
    drainSfx: (since) => {
      const head = e.pt_sfx_head();
      const patches: SfxPatch[] = [];
      if (head > since) {
        // Ring slots hold the last ringCap events; anything older was lost
        // (only possible if the host stalls for many sounds — fine to drop).
        const first = Math.max(since + 1, head - ringCap + 1);
        for (let serial = first; serial <= head; serial++) {
          const base = ringPtr + ((serial - 1) % ringCap) * EVENT_SIZE;
          const slotSerial = new Uint32Array(e.memory.buffer, base, 1)[0];
          if (slotSerial === serial) patches.push(readPatch(e.memory.buffer, base + 4));
        }
      }
      return { head, patches };
    },
    musicTrack: () => e.pt_music_track(),
    musicSerial: () => e.pt_music_serial(),
    sfxVolume: () => e.pt_sfx_volume(),
    musicVolume: () => e.pt_music_volume(),
    sfxLibrary,
  };
}
