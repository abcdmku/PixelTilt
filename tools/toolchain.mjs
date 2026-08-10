// Locates a clang able to target wasm32, downloading a pinned wasi-sdk into
// .toolchain/ on first use if nothing suitable is installed. The core is
// compiled freestanding (-nostdlib), so all we need from wasi-sdk is
// clang + wasm-ld — no sysroot, no emscripten.
import { existsSync, mkdirSync, readdirSync, createWriteStream, rmSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { execFileSync } from "node:child_process";
import { Readable } from "node:stream";
import { pipeline } from "node:stream/promises";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const toolchainDir = join(root, ".toolchain");

const WASI_SDK_VERSION = "25";
const PLATFORMS = {
  "win32-x64": `x86_64-windows`,
  "linux-x64": `x86_64-linux`,
  "linux-arm64": `arm64-linux`,
  "darwin-x64": `x86_64-macos`,
  "darwin-arm64": `arm64-macos`,
};

function exeName(name) {
  return process.platform === "win32" ? `${name}.exe` : name;
}

function findInstalled() {
  if (!existsSync(toolchainDir)) return null;
  for (const entry of readdirSync(toolchainDir)) {
    const candidate = join(toolchainDir, entry, "bin", exeName("clang++"));
    if (entry.startsWith("wasi-sdk-") && existsSync(candidate)) return candidate;
  }
  return null;
}

function worksForWasm(clang) {
  try {
    execFileSync(clang, ["--target=wasm32", "-x", "c", "-c", "-o", process.platform === "win32" ? "NUL" : "/dev/null", "-"], {
      input: "int main(void){return 0;}",
      stdio: ["pipe", "ignore", "ignore"],
    });
    return true;
  } catch {
    return false;
  }
}

export async function ensureClang() {
  // 1. explicit override
  if (process.env.PT_CLANG) return process.env.PT_CLANG;

  // 2. previously downloaded wasi-sdk
  const installed = findInstalled();
  if (installed) return installed;

  // 3. a system clang that can target wasm32
  try {
    const which = process.platform === "win32" ? "where" : "which";
    const sys = execFileSync(which, ["clang++"], { encoding: "utf8" }).split(/\r?\n/)[0].trim();
    if (sys && worksForWasm(sys)) return sys;
  } catch { /* not found */ }

  // 4. download wasi-sdk (~100 MB, one time)
  const key = `${process.platform}-${process.arch}`;
  const plat = PLATFORMS[key];
  if (!plat) throw new Error(`no wasi-sdk build for ${key}; install clang+lld and set PT_CLANG`);

  const dirName = `wasi-sdk-${WASI_SDK_VERSION}.0-${plat}`;
  const url = `https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-${WASI_SDK_VERSION}/${dirName}.tar.gz`;
  const tarPath = join(toolchainDir, `${dirName}.tar.gz`);

  mkdirSync(toolchainDir, { recursive: true });
  console.log(`downloading wasi-sdk ${WASI_SDK_VERSION} (one-time, ~100 MB)...`);
  console.log(`  ${url}`);
  const res = await fetch(url, { redirect: "follow" });
  if (!res.ok) throw new Error(`download failed: HTTP ${res.status}`);
  await pipeline(Readable.fromWeb(res.body), createWriteStream(tarPath));

  console.log("extracting...");
  execFileSync("tar", ["-xzf", tarPath, "-C", toolchainDir], { stdio: "inherit" });
  rmSync(tarPath);

  const clang = join(toolchainDir, dirName, "bin", exeName("clang++"));
  if (!existsSync(clang)) throw new Error(`extraction finished but ${clang} not found`);
  console.log(`wasi-sdk ready at .toolchain/${dirName}`);
  return clang;
}
