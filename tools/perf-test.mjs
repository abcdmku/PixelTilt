// Informational WASM benchmark and Sand II flavor check. Timing has no hard
// budget because host load varies; deterministic visual checks do fail.
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const wasmPath = join(root, "frontend", "public", "pixeltilt.wasm");
const bytes = readFileSync(wasmPath);
const { instance } = await WebAssembly.instantiate(bytes, {});
const e = instance.exports;

const DT = 1 / 60;
const W = 64;
const H = 64;
const FRAME_BYTES = W * H * 3;
const WARMUP_SAMPLES = 2;
const MEASURED_SAMPLES = 9;
const EXPECTED_SAND_VARIANTS = [
  "sand2_lava",
  "sand2_snow",
  "sand2_star",
  "sand2_ferro",
  "sand2_neon",
];
const MIN_HISTOGRAM_DISTANCE = 0.01;

function cstr(ptr) {
  const mem = new Uint8Array(e.memory.buffer);
  let end = ptr;
  while (mem[end] !== 0) end++;
  return new TextDecoder().decode(mem.subarray(ptr, end));
}

function quantile(values, q) {
  const sorted = [...values].sort((a, b) => a - b);
  const h = (sorted.length - 1) * q;
  const lo = Math.floor(h);
  const hi = Math.ceil(h);
  return sorted[lo] + (sorted[hi] - sorted[lo]) * (h - lo);
}

function resetAndLaunch(index, seed) {
  e.pt_init(seed >>> 0);
  // These inputs are sticky and live outside the save blob. Clear them before
  // launch so one sample cannot leak into the next.
  e.pt_accel(0, 0, 0);
  e.pt_gravity(0, 0);
  e.pt_tick(0, 0, 0, 0, 0);
  e.pt_launch(index);
}

function applyInput(input) {
  e.pt_accel(input.ax, input.ay, input.az);
  e.pt_gravity(input.gx, input.gy);
  e.pt_tick(input.dt, input.tx, input.ty, input.spin, input.buttons ?? 0);
}

function timeBatch(index, scenario, sample) {
  resetAndLaunch(index, 0x51a2d000 + sample);
  for (let frame = 0; frame < scenario.prepare; frame++) {
    applyInput(scenario.input(frame, true));
  }
  const start = process.hrtime.bigint();
  for (let frame = 0; frame < scenario.frames; frame++) {
    applyInput(scenario.input(frame, false));
  }
  const elapsedMs = Number(process.hrtime.bigint() - start) / 1e6;
  return elapsedMs / scenario.frames;
}

function measure(index, scenario) {
  for (let i = 0; i < WARMUP_SAMPLES; i++) timeBatch(index, scenario, i);
  const samples = [];
  for (let i = 0; i < MEASURED_SAMPLES; i++) {
    samples.push(timeBatch(index, scenario, WARMUP_SAMPLES + i));
  }
  return {
    median: quantile(samples, 0.5),
    p95: quantile(samples, 0.95),
  };
}

function fixedInput({ dt = DT, tx = 0, ty = 0, gx = tx, gy = ty, spin = 0 } = {}) {
  return { dt, tx, ty, gx, gy, spin, ax: 0, ay: 0, az: 0, buttons: 0 };
}

const mixedScenario = {
  name: "mixed",
  prepare: 90,
  frames: 300,
  input(frame) {
    const tx = Math.sin(frame / 23) * 0.78;
    const ty = Math.cos(frame / 31) * 0.72;
    const burst = frame % 90 < 9;
    const ax = burst ? Math.sin(frame * 0.71) * 1.1 : 0;
    const ay = burst ? Math.cos(frame * 0.53) * 1.1 : 0;
    return {
      dt: DT,
      tx,
      ty,
      gx: tx + ax,
      gy: ty + ay,
      spin: Math.sin(frame / 37) * 2.5,
      ax,
      ay,
      az: burst ? Math.sin(frame * 0.91) * 0.8 : 0,
      buttons: 0,
    };
  },
};

const sandScenarios = [
  {
    name: "level",
    prepare: 300,
    frames: 600,
    input: () => fixedInput(),
  },
  {
    name: "tilt",
    prepare: 180,
    frames: 300,
    input: () => fixedInput({ tx: 0.72, ty: 0.55, gx: 0.72, gy: 0.55 }),
  },
  {
    name: "shake",
    prepare: 60,
    frames: 300,
    input(frame) {
      const ax = Math.sin(frame * 0.43) * 1.25;
      const ay = Math.cos(frame * 0.37) * 1.1;
      return {
        ...fixedInput({ tx: 0.15, ty: 0.65, gx: 0.15 + ax, gy: 0.65 + ay }),
        ax,
        ay,
        az: Math.sin(frame * 0.61) * 0.9,
      };
    },
  },
  {
    name: "spin",
    prepare: 180,
    frames: 240,
    input: () => fixedInput({ gx: 0.08, gy: 0.08, spin: 8 }),
  },
  {
    name: "stress",
    prepare: 120,
    frames: 240,
    input(frame) {
      const phase = frame % 48;
      const slam = phase < 5 ? 1.6 : phase < 10 ? -1.4 : 0;
      const ax = slam + Math.sin(frame * 0.57) * 0.8;
      const ay = -slam * 0.7 + Math.cos(frame * 0.41) * 0.7;
      return {
        dt: DT,
        tx: Math.sin(frame / 17) * 0.9,
        ty: Math.cos(frame / 19) * 0.9,
        gx: ax,
        gy: ay,
        spin: phase < 24 ? 10 : -10,
        ax,
        ay,
        az: slam,
        buttons: 0,
      };
    },
  },
];

function framebufferCopy() {
  return new Uint8Array(e.memory.buffer, e.pt_framebuffer(), FRAME_BYTES).slice();
}

function fnv1a64(frames) {
  let hash = 0xcbf29ce484222325n;
  for (const frame of frames) {
    for (const byte of frame) {
      hash ^= BigInt(byte);
      hash = BigInt.asUintN(64, hash * 0x100000001b3n);
    }
  }
  return hash.toString(16).padStart(16, "0");
}

function colorHistogram(frames) {
  const bins = new Uint32Array(64);
  for (const frame of frames) {
    for (let i = 0; i < frame.length; i += 3) {
      const bin = (frame[i] >> 6) * 16 + (frame[i + 1] >> 6) * 4 + (frame[i + 2] >> 6);
      bins[bin]++;
    }
  }
  const total = frames.length * W * H;
  return Float64Array.from(bins, (count) => count / total);
}

function histogramDistance(a, b) {
  let sum = 0;
  for (let i = 0; i < a.length; i++) sum += Math.abs(a[i] - b[i]);
  return sum / 2;
}

function visualSignature(index) {
  resetAndLaunch(index, 0x5a2d2026);
  const captures = [];
  for (let frame = 0; frame < 360; frame++) {
    let input;
    if (frame < 60) {
      input = fixedInput({ ty: 0.85, gy: 0.85 });
    } else if (frame < 120) {
      input = fixedInput({ tx: 0.85, ty: 0.1, gx: 0.85, gy: 0.1 });
    } else if (frame < 180) {
      input = fixedInput({ spin: 7 });
    } else if (frame < 240) {
      const ax = Math.sin(frame * 0.61) * 1.4;
      const ay = Math.cos(frame * 0.47) * 1.2;
      input = { ...fixedInput({ gx: ax, gy: ay }), ax, ay, az: Math.sin(frame * 0.83) };
    } else if (frame < 300) {
      input = fixedInput({ tx: -0.7, ty: -0.55, gx: -0.7, gy: -0.55, spin: -3 });
    } else {
      input = fixedInput({ ty: 0.85, gy: 0.85 });
    }
    applyInput(input);
    if (frame % 60 === 59) captures.push(framebufferCopy());
  }
  return {
    hash: fnv1a64(captures),
    histogram: colorHistogram(captures),
  };
}

function formatMs(value) {
  return value.toFixed(4);
}

function printTable(columns, rows) {
  const widths = columns.map((column, i) =>
    Math.max(column.length, ...rows.map((row) => String(row[i]).length)),
  );
  const line = (row) => row.map((cell, i) => String(cell).padEnd(widths[i])).join("  ");
  console.log(line(columns));
  console.log(widths.map((width) => "-".repeat(width)).join("  "));
  for (const row of rows) console.log(line(row));
}

const gameCount = e.pt_game_count();
const games = Array.from({ length: gameCount }, (_, index) => ({
  index,
  id: cstr(e.pt_game_id(index)),
  title: cstr(e.pt_game_title(index)),
}));
if (games.some((game) => !game.id || !game.title)) throw new Error("game registry contains empty metadata");
if (new Set(games.map((game) => game.id)).size !== games.length) throw new Error("game ids are not unique");

console.log(`PixelTilt WASM performance, Node ${process.version}, ${bytes.length} bytes`);
console.log(`Samples: ${WARMUP_SAMPLES} warmup + ${MEASURED_SAMPLES} measured`);
console.log("\nAll games, mixed input, ms/tick");
const allRows = games.map((game) => {
  const result = measure(game.index, mixedScenario);
  return [game.id, game.title, formatMs(result.median), formatMs(result.p95)];
});
printTable(["id", "title", "median", "p95"], allRows);

const sandFamily = games.filter((game) => game.id === "sand2" || game.id.startsWith("sand2_"));
if (sandFamily.length === 0) throw new Error("Sand II is not registered");
console.log("\nSand II family scenarios, ms/tick");
const sandRows = [];
for (const game of sandFamily) {
  for (const scenario of sandScenarios) {
    const result = measure(game.index, scenario);
    sandRows.push([game.id, scenario.name, formatMs(result.median), formatMs(result.p95)]);
  }
}
printTable(["id", "scenario", "median", "p95"], sandRows);

const presentVariants = EXPECTED_SAND_VARIANTS.filter((id) => games.some((game) => game.id === id));
if (presentVariants.length !== EXPECTED_SAND_VARIANTS.length) {
  const missing = EXPECTED_SAND_VARIANTS.filter((id) => !presentVariants.includes(id));
  console.log(`\nVisual flavor validation pending: ${presentVariants.length}/5 variants registered.`);
  console.log(`Missing: ${missing.join(", ")}`);
} else {
  const expectedFamily = ["sand2", ...EXPECTED_SAND_VARIANTS];
  const unexpected = sandFamily.filter((game) => !expectedFamily.includes(game.id));
  if (unexpected.length > 0) {
    throw new Error(`unexpected Sand II family ids: ${unexpected.map((game) => game.id).join(", ")}`);
  }
  if (sandFamily.length !== expectedFamily.length) {
    throw new Error(`expected ${expectedFamily.length} Sand II family entries, found ${sandFamily.length}`);
  }

  console.log("\nSand II visual signatures");
  const signatures = new Map();
  for (const id of expectedFamily) {
    const game = games.find((candidate) => candidate.id === id);
    const first = visualSignature(game.index);
    const second = visualSignature(game.index);
    if (first.hash !== second.hash || histogramDistance(first.histogram, second.histogram) !== 0) {
      throw new Error(`${id} visual trace is not deterministic`);
    }
    signatures.set(id, first);
    console.log(`${id.padEnd(14)} ${first.hash}`);
  }

  console.log("\nPairwise histogram distance");
  const distanceRows = [];
  for (let i = 0; i < expectedFamily.length; i++) {
    for (let j = i + 1; j < expectedFamily.length; j++) {
      const a = expectedFamily[i];
      const b = expectedFamily[j];
      const sa = signatures.get(a);
      const sb = signatures.get(b);
      const distance = histogramDistance(sa.histogram, sb.histogram);
      distanceRows.push([a, b, distance.toFixed(4)]);
      if (sa.hash === sb.hash) throw new Error(`${a} and ${b} have identical framebuffer traces`);
      if (distance < MIN_HISTOGRAM_DISTANCE) {
        throw new Error(`${a} and ${b} color histograms are too similar: ${distance.toFixed(4)}`);
      }
    }
  }
  printTable(["a", "b", "distance"], distanceRows);
  console.log(`Visual flavor validation passed, minimum histogram distance ${MIN_HISTOGRAM_DISTANCE}.`);
}

console.log("\nTiming is informational. This command enforces registry and visual checks only.");
