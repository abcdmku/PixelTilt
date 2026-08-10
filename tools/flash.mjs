// Smart flasher: checks the toolchain, finds the attached board, builds,
// uploads, and reports memory usage.
//
//   npm run flash                  auto-detect the S3 and flash it
//   npm run flash -- --port COM7   pin a specific serial port
//   npm run flash -- --monitor     open the serial monitor after flashing
//
// Interactive when something needs a decision (PlatformIO missing, no board,
// ambiguous ports); falls back to sensible defaults when stdin isn't a TTY
// (CI, scripts).
import { spawn, spawnSync } from "node:child_process";
import { createInterface } from "node:readline/promises";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const argv = process.argv.slice(2);
const argPort = argv.includes("--port") ? argv[argv.indexOf("--port") + 1] : null;
const wantMonitor = argv.includes("--monitor");
const isTTY = process.stdin.isTTY && process.stdout.isTTY;
const shell = process.platform === "win32";

// USB VID:PIDs ranked by how likely they are to be the game device. The
// Seengreat HUB75 S3 programs over the ESP32-S3's native USB-Serial/JTAG
// (Espressif VID 303A); the bridge chips cover other ESP32 dev boards.
function scorePort(hwid, description) {
  const h = (hwid || "").toUpperCase();
  const d = (description || "").toLowerCase();
  if (h.includes("VID:PID=303A:")) return 3; // Espressif native USB (the S3)
  if (h.includes("VID:PID=10C4:EA60") || h.includes("VID:PID=1A86:55D")) return 2; // CP210x / CH9102
  if (h.includes("VID:PID=1A86:7523")) return 1; // CH340 (could be anything)
  if (d.includes("bluetooth")) return -1;
  return 0;
}

function fail(msg) {
  console.error(`\nx ${msg}`);
  process.exit(1);
}

async function ask(question, fallback) {
  if (!isTTY) {
    console.log(`${question} -> ${fallback} (non-interactive)`);
    return fallback;
  }
  const rl = createInterface({ input: process.stdin, output: process.stdout });
  const answer = (await rl.question(question)).trim();
  rl.close();
  return answer || fallback;
}

function pioSync(args, opts = {}) {
  return spawnSync("pio", args, { cwd: root, encoding: "utf8", shell, ...opts });
}

// Run pio streaming output to the console while also capturing it (the
// memory report is parsed out of the build log afterwards).
function pioStream(args) {
  return new Promise((resolve) => {
    const child = spawn("pio", args, { cwd: root, shell, stdio: ["inherit", "pipe", "pipe"] });
    let captured = "";
    child.stdout.on("data", (d) => { captured += d; process.stdout.write(d); });
    child.stderr.on("data", (d) => { captured += d; process.stderr.write(d); });
    child.on("close", (code) => resolve({ code, output: captured }));
  });
}

// --- 1. toolchain ----------------------------------------------------------

async function ensurePio() {
  if (pioSync(["--version"]).status === 0) return;

  console.log("PlatformIO is not installed (it builds and flashes the ESP32-S3 firmware).");
  if (spawnSync("pip", ["--version"], { shell, encoding: "utf8" }).status !== 0) {
    fail("pip was not found either — install Python 3 (python.org), then run: pip install platformio");
  }
  const answer = await ask("Install it now via `pip install platformio`? [Y/n] ", "y");
  if (!/^y/i.test(answer)) fail("aborted — install PlatformIO and rerun `npm run flash`");

  const install = spawnSync("pip", ["install", "platformio"], { shell, stdio: "inherit" });
  if (install.status !== 0 || pioSync(["--version"]).status !== 0) {
    fail("PlatformIO install failed — try `pip install platformio` manually");
  }
  console.log("PlatformIO installed.\n");
}

// --- 2. find the board -----------------------------------------------------

function listPorts() {
  const res = pioSync(["device", "list", "--json-output"]);
  if (res.status !== 0) return [];
  try {
    return JSON.parse(res.stdout);
  } catch {
    return [];
  }
}

async function pickPort() {
  if (argPort) return argPort;

  for (;;) {
    const ports = listPorts()
      .map((p) => ({ ...p, score: scorePort(p.hwid, p.description) }))
      .filter((p) => p.score >= 0)
      .sort((a, b) => b.score - a.score);

    if (ports.length === 0) {
      console.log("\nNo serial device detected. Check that:");
      console.log("  - the board is plugged in via the MAIN USB-C port (not the matrix-power one)");
      console.log("  - the cable carries data (some USB-C cables are charge-only)");
      const answer = await ask("Retry scan? [Y/n] ", "n");
      if (!/^y/i.test(answer)) fail("no board found");
      continue;
    }

    const best = ports[0];
    if (best.score >= 3) {
      // Unambiguous: an Espressif-native USB device (ESP32-S3).
      console.log(`Found ESP32-S3 on ${best.port} (${best.description})`);
      return best.port;
    }

    if (ports.length === 1) {
      console.log(`Found one serial device: ${best.port} (${best.description})`);
      console.log("It doesn't identify as an ESP32-S3, but it's the only candidate.");
      const answer = await ask(`Flash to ${best.port}? [y/N] `, "n");
      if (/^y/i.test(answer)) return best.port;
      fail("aborted — pass --port <PORT> if you know the right one");
    }

    console.log("\nMultiple serial devices found, none clearly an ESP32-S3:");
    ports.forEach((p, i) => console.log(`  [${i + 1}] ${p.port}  ${p.description}`));
    const answer = await ask(`Which one? [1-${ports.length}, q to quit] `, "q");
    if (/^q/i.test(answer)) fail("aborted — pass --port <PORT> to skip this prompt");
    const idx = parseInt(answer, 10) - 1;
    if (idx >= 0 && idx < ports.length) return ports[idx].port;
    console.log("Invalid choice.");
  }
}

// --- 3. build + flash + memory report --------------------------------------

function parseMemory(output) {
  const ram = output.match(/RAM:.*?([\d.]+)%\s*\(used\s+(\d+)\s+bytes\s+from\s+(\d+)/);
  const flash = output.match(/Flash:.*?([\d.]+)%\s*\(used\s+(\d+)\s+bytes\s+from\s+(\d+)/);
  const fmt = (m) =>
    m && {
      pct: parseFloat(m[1]),
      used: parseInt(m[2], 10),
      total: parseInt(m[3], 10),
    };
  return { ram: fmt(ram), flash: fmt(flash) };
}

function memoryLine(label, m) {
  if (!m) return `  ${label}  (not reported)`;
  const bar = "#".repeat(Math.round(m.pct / 5)).padEnd(20, ".");
  return `  ${label}  [${bar}] ${m.pct.toFixed(1).padStart(5)}%   ${m.used.toLocaleString()} / ${m.total.toLocaleString()} bytes`;
}

await ensurePio();
const port = await pickPort();

console.log("\n> building firmware...\n");
const build = await pioStream(["run"]);
if (build.code !== 0) fail("build failed — fix the errors above and rerun");
const mem = parseMemory(build.output);

console.log(`\n> flashing ${port}...\n`);
let upload = await pioStream(["run", "-t", "upload", "--upload-port", port]);
if (upload.code !== 0) {
  console.log("\nUpload failed. Common fix: hold BOOT, tap RESET (EN), release BOOT,");
  console.log("then retry — that forces the S3 into download mode.");
  const answer = await ask("Retry upload now? [Y/n] ", "n");
  if (!/^y/i.test(answer)) fail("upload failed");
  upload = await pioStream(["run", "-t", "upload", "--upload-port", port]);
  if (upload.code !== 0) fail("upload failed again — check the cable/port and BOOT+RESET");
}

console.log("\n" + "-".repeat(72));
console.log(`  FLASHED OK -> ${port}`);
console.log(memoryLine("RAM  ", mem.ram));
console.log(memoryLine("Flash", mem.flash));
console.log("-".repeat(72));

if (wantMonitor) {
  console.log("\n> opening serial monitor (Ctrl+C to exit)...\n");
  spawnSync("pio", ["device", "monitor", "-b", "115200", "-p", port], {
    cwd: root,
    shell,
    stdio: "inherit",
  });
} else {
  console.log("  serial console: npm run monitor");
}
