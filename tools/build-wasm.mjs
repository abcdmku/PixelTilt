// Builds core + all games + the emulator shim into a single freestanding
// wasm module at frontend/public/pixeltilt.wasm. First run downloads a
// pinned wasi-sdk into .toolchain/ (nothing else to install).
import { readdirSync, existsSync, mkdirSync, statSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { execFileSync } from "node:child_process";
import { generate } from "./gen-games.mjs";
import { ensureClang } from "./toolchain.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");

generate();
const clang = await ensureClang();

const sources = [];
for (const f of readdirSync(join(root, "core", "src"))) {
  if (f.endsWith(".cpp")) sources.push(join("core", "src", f));
}
for (const dir of readdirSync(join(root, "games"), { withFileTypes: true })) {
  if (!dir.isDirectory() || dir.name.startsWith("_") || dir.name === "generated") continue;
  const src = join("games", dir.name, "game.cpp");
  if (existsSync(join(root, src))) sources.push(src);
}
sources.push(join("games", "generated", "game_list.cpp"));
sources.push(join("emulator", "wasm_main.cpp"));
// Directory enumeration order is filesystem-dependent. Stable link order
// keeps otherwise-identical wasm builds byte-for-byte reproducible.
sources.sort();

const outDir = join(root, "frontend", "public");
mkdirSync(outDir, { recursive: true });
const out = join(outDir, "pixeltilt.wasm");

const args = [
  "--target=wasm32",
  "-std=c++17",
  "-O2",
  "-ffreestanding",
  "-fno-exceptions",
  "-fno-rtti",
  "-nostdlib",
  "-Wall",
  "-Icore/include",
  "-Wl,--no-entry",
  "-Wl,--strip-all",
  "-Wl,-zstack-size=65536",
  ...sources,
  "-o", out,
];

console.log("compiling", sources.length, "files -> frontend/public/pixeltilt.wasm");
try {
  execFileSync(clang, args, { cwd: root, stdio: "inherit" });
} catch {
  process.exit(1);
}
console.log(`ok: ${(statSync(out).size / 1024).toFixed(1)} KiB`);
