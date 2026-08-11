// Instantiates the built wasm in Node and exercises the engine: menu render,
// navigation, launching every game, ticking each with input, the hold-CLICK
// pause menu, settings, and save persistence. Run `npm run wasm` first
// (npm test does both).
import { readFileSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const bytes = readFileSync(join(root, "frontend", "public", "pixeltilt.wasm"));
const { instance } = await WebAssembly.instantiate(bytes, {});
const e = instance.exports;

let failures = 0;
function check(name, cond) {
  console.log(`${cond ? "ok  " : "FAIL"} ${name}`);
  if (!cond) failures++;
}
function cstr(ptr) {
  const mem = new Uint8Array(e.memory.buffer);
  let end = ptr;
  while (mem[end] !== 0) end++;
  return new TextDecoder().decode(mem.subarray(ptr, end));
}
function fbLitPixels() {
  const fb = new Uint8Array(e.memory.buffer, e.pt_framebuffer(), 64 * 64 * 3);
  let lit = 0;
  for (let i = 0; i < fb.length; i += 3) if (fb[i] | fb[i + 1] | fb[i + 2]) lit++;
  return lit;
}
const BTN_UP = 1, BTN_CLICK = 2, BTN_DOWN = 4;
const tick = (tx = 0, ty = 0, buttons = 0) => e.pt_tick(1 / 60, tx, ty, buttons);

e.pt_init(1234);
check("screen is 64x64", e.pt_screen_w() === 64 && e.pt_screen_h() === 64);

const count = e.pt_game_count();
check("has games registered", count >= 3);
const titles = Array.from({ length: count }, (_, i) => cstr(e.pt_game_title(i)));
console.log("     games:", titles.join(", "));
check("titles are non-empty", titles.every((t) => t.length > 0));

tick();
check("menu draws pixels", fbLitPixels() > 50);
check("starts in menu", e.pt_current_game() === -1);

// DOWN moves the cursor, CLICK launches whatever is selected.
tick(0, 0, BTN_DOWN);
tick();
tick(0, 0, BTN_CLICK);
check("click launches game 1", e.pt_current_game() === 1);

// Every game must survive 300 frames of wiggly tilt + button mashing and draw.
for (let g = 0; g < count; g++) {
  e.pt_launch(g);
  for (let f = 0; f < 300; f++) {
    const btn = f % 40 === 0 ? BTN_CLICK : 0;
    tick(Math.sin(f / 20) * 0.8, Math.cos(f / 31) * 0.8, btn);
  }
  check(`"${titles[g]}" runs 300 frames and draws`, fbLitPixels() > 20);
}

// Holding CLICK ~0.7s pauses; the pause menu's third item is MAIN MENU.
e.pt_launch(0);
for (let f = 0; f < 50; f++) tick(0, 0, BTN_CLICK);
check("hold CLICK pauses without exiting", e.pt_current_game() === 0);
tick(); // release the held click
tick(0, 0, BTN_DOWN); tick();
tick(0, 0, BTN_DOWN); tick();
tick(0, 0, BTN_CLICK);
check("pause > MAIN MENU exits to menu", e.pt_current_game() === -1);
tick();

// The menu row above the first game (wrap via UP) is SETTINGS. Clicking the
// first two items cycles rotation and brightness.
tick(0, 0, BTN_UP); tick();
tick(0, 0, BTN_CLICK); tick();          // enter settings
tick(0, 0, BTN_CLICK); tick();          // ROTATE -> 90
check("changing a setting marks the save dirty", e.pt_save_dirty() === 1);
check("menu still draws when rotated", fbLitPixels() > 50);
tick(0, 0, BTN_DOWN); tick();
tick(0, 0, BTN_CLICK); tick();          // BRIGHT 100 -> 20
check("brightness cycles", e.pt_brightness() === 20);

// Save blob round-trip: snapshot, wipe via re-init, restore.
e.pt_save_clear_dirty();
const saveSize = e.pt_save_size();
const blob = new Uint8Array(saveSize);
blob.set(new Uint8Array(e.memory.buffer, e.pt_save_ptr(), saveSize));
e.pt_init(99);
check("re-init resets settings", e.pt_brightness() === 100);
new Uint8Array(e.memory.buffer, e.pt_save_ptr(), saveSize).set(blob);
check("save blob restores settings", e.pt_save_loaded() === 1 && e.pt_brightness() === 20);

console.log(failures === 0 ? "\nall checks passed" : `\n${failures} check(s) FAILED`);
process.exit(failures === 0 ? 0 : 1);
