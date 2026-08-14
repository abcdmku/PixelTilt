// Dump 64x64 frames of a game to PNG for visual inspection.
// Usage: node tools/shot.mjs <gameTitleSubstring> <outPrefix> [scriptJson]
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { deflateSync } from "node:zlib";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const bytes = readFileSync(join(root, "frontend", "public", "pixeltilt.wasm"));
const { instance } = await WebAssembly.instantiate(bytes, {});
const e = instance.exports;

const BTN_UP = 1, BTN_CLICK = 2, BTN_DOWN = 4;
const tick = (tx = 0, ty = 0, buttons = 0, spin = 0) => e.pt_tick(1 / 60, tx, ty, spin, buttons);

function crc32(buf) {
  let c, table = [];
  for (let n = 0; n < 256; n++) {
    c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    table[n] = c >>> 0;
  }
  let crc = 0xffffffff;
  for (const b of buf) crc = table[(crc ^ b) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length);
  const body = Buffer.concat([Buffer.from(type, "ascii"), data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(body));
  return Buffer.concat([len, body, crc]);
}
function png(rgb, w, h, scale) {
  const W = w * scale, H = h * scale;
  const raw = Buffer.alloc((W * 3 + 1) * H);
  let o = 0;
  for (let y = 0; y < H; y++) {
    raw[o++] = 0;
    for (let x = 0; x < W; x++) {
      const s = ((y / scale | 0) * w + (x / scale | 0)) * 3;
      raw[o++] = rgb[s]; raw[o++] = rgb[s + 1]; raw[o++] = rgb[s + 2];
    }
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(W, 0); ihdr.writeUInt32BE(H, 4);
  ihdr[8] = 8; ihdr[9] = 2;
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk("IHDR", ihdr), chunk("IDAT", deflateSync(raw, { level: 9 })), chunk("IEND", Buffer.alloc(0)),
  ]);
}
function save(path, scale = 6, crop = null) {
  const fb = new Uint8Array(e.memory.buffer, e.pt_framebuffer(), 64 * 64 * 3);
  mkdirSync(dirname(path), { recursive: true });
  if (!crop) return writeFileSync(path, png(fb, 64, 64, scale));
  const [cx, cy, cw, ch] = crop;
  const sub = new Uint8Array(cw * ch * 3);
  for (let y = 0; y < ch; y++)
    for (let x = 0; x < cw; x++)
      for (let c = 0; c < 3; c++) sub[(y * cw + x) * 3 + c] = fb[((cy + y) * 64 + cx + x) * 3 + c];
  writeFileSync(path, png(sub, cw, ch, scale));
}

const [want = "WIZ", prefix = ".shots/wiz"] = process.argv.slice(2);
const script = process.argv[4] ? JSON.parse(process.argv[4]) : null;

e.pt_init(1234);
tick();
const count = e.pt_game_count();
const dec = new TextDecoder();
const cstr = (p) => { const m = new Uint8Array(e.memory.buffer); let q = p; while (m[q]) q++; return dec.decode(m.subarray(p, q)); };
let idx = -1;
for (let i = 0; i < count; i++) if (cstr(e.pt_game_title(i)).toUpperCase().includes(want.toUpperCase())) idx = i;
if (idx < 0) throw new Error(`no game matching ${want}; have ${Array.from({length: count}, (_, i) => cstr(e.pt_game_title(i))).join(", ")}`);
e.pt_launch(idx);

// script: [{name, frames, tx, buttons}] executed in order, PNG saved after each step
const steps = script ?? [
  { name: "start", frames: 2 },
  { name: "walk", frames: 90, tx: 0.9 },
  { name: "walk2", frames: 120, tx: 0.9 },
  { name: "jump", frames: 20, tx: 0.9, buttons: BTN_UP },
  { name: "far", frames: 240, tx: 0.9 },
];
for (const s of steps) {
  for (let i = 0; i < (s.frames ?? 1); i++) tick(s.tx ?? 0, s.ty ?? 0, s.buttons ?? 0, s.spin ?? 0);
  save(join(root, `${prefix}-${s.name}.png`));
  if (s.crop) save(join(root, `${prefix}-${s.name}-zoom.png`), 20, s.crop);
  console.log(`${prefix}-${s.name}.png`);
}
