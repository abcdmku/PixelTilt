// Thin typed wrapper around the freestanding pixeltilt.wasm module built by
// tools/build-wasm.mjs. No JS glue is generated — the exports are stable C
// symbols and the framebuffer is read directly out of linear memory.

export const SCREEN_W = 64;
export const SCREEN_H = 64;

export const BTN_UP = 1 << 0;
export const BTN_CLICK = 1 << 1;
export const BTN_DOWN = 1 << 2;

interface Exports {
  memory: WebAssembly.Memory;
  pt_init(seed: number): void;
  pt_tick(dt: number, tiltX: number, tiltY: number, buttons: number): void;
  pt_framebuffer(): number;
  pt_screen_w(): number;
  pt_screen_h(): number;
  pt_game_count(): number;
  pt_game_title(i: number): number;
  pt_current_game(): number;
  pt_launch(i: number): void;
  pt_exit_to_menu(): void;
}

export interface Emulator {
  titles: string[];
  init(seed: number): void;
  tick(dt: number, tiltX: number, tiltY: number, buttons: number): void;
  /** RGB888 view of the 64x64 framebuffer (valid until next memory growth — the module never grows). */
  framebuffer(): Uint8Array;
  currentGame(): number;
  launch(i: number): void;
  exitToMenu(): void;
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
  return {
    titles,
    init: (seed) => e.pt_init(seed >>> 0),
    tick: (dt, tx, ty, b) => e.pt_tick(dt, tx, ty, b >>> 0),
    framebuffer: () => new Uint8Array(e.memory.buffer, fbPtr, SCREEN_W * SCREEN_H * 3),
    currentGame: () => e.pt_current_game(),
    launch: (i) => e.pt_launch(i),
    exitToMenu: () => e.pt_exit_to_menu(),
  };
}
