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
function framebufferSnapshot() {
  return new Uint8Array(e.memory.buffer, e.pt_framebuffer(), 64 * 64 * 3).slice();
}
function bytesEqual(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}
function saveSnapshot() {
  return new Uint8Array(e.memory.buffer, e.pt_save_ptr(), e.pt_save_size()).slice();
}
function fnv1a(text) {
  let hash = 0x811c9dc5;
  for (const byte of new TextEncoder().encode(text)) {
    hash ^= byte;
    hash = Math.imul(hash, 0x01000193) >>> 0;
  }
  return hash || 1;
}
const BTN_UP = 1, BTN_CLICK = 2, BTN_DOWN = 4;
const tick = (tx = 0, ty = 0, buttons = 0, spin = 0) => e.pt_tick(1 / 60, tx, ty, spin, buttons);

e.pt_init(1234);
check("screen is 64x64", e.pt_screen_w() === 64 && e.pt_screen_h() === 64);

const count = e.pt_game_count();
check("has games registered", count >= 3);
const titles = Array.from({ length: count }, (_, i) => cstr(e.pt_game_title(i)));
const ids = Array.from({ length: count }, (_, i) => cstr(e.pt_game_id(i)));
console.log("     games:", ids.map((id, i) => `${id}=${titles[i]}`).join(", "));
check("titles are non-empty", titles.every((t) => t.length > 0));
check("titles are unique", new Set(titles).size === count);
check("game ids are valid slugs", ids.every((id) => /^[a-z][a-z0-9_]*$/.test(id)));
check("game ids are unique", new Set(ids).size === count);
check("game registry is sorted by id", ids.every((id, i) => i === 0 || ids[i - 1] < id));
check(
  "game metadata rejects invalid indices",
  cstr(e.pt_game_id(-1)) === "" && cstr(e.pt_game_id(count)) === "" &&
    cstr(e.pt_game_title(-1)) === "" && cstr(e.pt_game_title(count)) === "",
);

const gameHasScores = Array.from(
  { length: count }, (_, i) => e.pt_game_has_scores(i) !== 0,
);
const scoredGames = gameHasScores.filter(Boolean).length;
check("scored registry metadata matches games", e.pt_scored_game_count() === scoredGames);
check("scored games fit the save capacity", scoredGames <= e.pt_score_game_capacity());
const sand = titles.indexOf("SAND");
const sand2 = titles.indexOf("SAND II");
const expectedSand2Family = [
  "sand2", "sand2_ferro", "sand2_lava", "sand2_neon", "sand2_snow", "sand2_star",
];
const sand2Family = ids.filter((id) => id === "sand2" || id.startsWith("sand2_"));
check(
  "all six Sand II family entries are registered",
  JSON.stringify(sand2Family) === JSON.stringify(expectedSand2Family),
);
check(
  "sand toys are registered as unscored",
  sand >= 0 && sand2 >= 0 && !gameHasScores[sand] &&
    expectedSand2Family.every((id) => !gameHasScores[ids.indexOf(id)]),
);

// Unscored games must not reserve or alter persistent storage, even if game
// code accidentally tries to submit a result.
let unscoredSubmissionsSafe = true;
for (let game = 0; game < count; game++) {
  if (gameHasScores[game]) continue;
  e.pt_init(1234);
  e.pt_launch(game);
  e.pt_save_clear_dirty();
  const before = saveSnapshot();
  const rank = e.pt_submit_score(1234);
  const after = saveSnapshot();
  unscoredSubmissionsSafe &&=
    rank === -1 && e.pt_save_dirty() === 0 && bytesEqual(before, after);
}
check("unscored submissions leave the save untouched", unscoredSubmissionsSafe);

e.pt_init(1234);
e.pt_save_clear_dirty();
const beforeNoGameSubmit = saveSnapshot();
check(
  "score submission outside a game is rejected without mutation",
  e.pt_submit_score(10) === -1 && e.pt_save_dirty() === 0 &&
    bytesEqual(beforeNoGameSubmit, saveSnapshot()),
);
const scoredGame = gameHasScores.indexOf(true);
e.pt_launch(scoredGame);
e.pt_save_clear_dirty();
const beforeNegativeSubmit = saveSnapshot();
check(
  "negative score is rejected without mutation",
  e.pt_submit_score(-1) === -1 && e.pt_save_dirty() === 0 &&
    bytesEqual(beforeNegativeSubmit, saveSnapshot()),
);

// Before SCORE_NONE existed, merely browsing SAND's scores could leave its
// hash in a v5 slot. Loading such a blob must reclaim that slot and request a
// one-time rewrite. GameSlot is the persisted {u32 hash, i32 best[3]} tail.
e.pt_init(1234);
const staleSlot = e.pt_save_ptr() + e.pt_save_size() - 16;
const staleView = new DataView(e.memory.buffer, staleSlot, 16);
staleView.setUint32(0, fnv1a("sand"), true);
staleView.setInt32(4, 999, true);
staleView.setInt32(8, 500, true);
staleView.setInt32(12, 100, true);
e.pt_save_clear_dirty();
const staleLoaded = e.pt_save_loaded() === 1;
check(
  "loading v5 reclaims stale unscored slots",
  staleLoaded && e.pt_save_dirty() === 1 && staleView.getUint32(0, true) === 0 &&
    [4, 8, 12].every((offset) => staleView.getInt32(offset, true) === -1),
);

// Opening the score browser is a read. It must not allocate a table for the
// displayed game or otherwise dirty the persisted blob.
e.pt_init(1234);
e.pt_save_clear_dirty();
const beforeScoreBrowser = saveSnapshot();
tick(0, 0, BTN_UP); tick();                 // SETTINGS (wrap from first game)
tick(0, 0, BTN_UP); tick();                 // SCORES
tick(0, 0, BTN_CLICK); tick();              // open score browser
const scoredIndices = gameHasScores
  .map((hasScores, index) => hasScores ? index : -1)
  .filter((index) => index >= 0);
const scoreSequence = [e.pt_current_score_game()];
for (let i = 1; i < scoredIndices.length; i++) {
  tick(0, 0, BTN_DOWN); tick();
  scoreSequence.push(e.pt_current_score_game());
}
check(
  "score browser skips every unscored game",
  scoreSequence.length === scoredIndices.length &&
    scoreSequence.every((game, i) => game === scoredIndices[i]),
);
tick(0, 0, BTN_DOWN); tick();
check("score browser wraps scored games", e.pt_current_score_game() === scoredIndices[0]);
tick(0, 0, BTN_UP); tick();
check(
  "score browser skips unscored games in reverse",
  e.pt_current_score_game() === scoredIndices.at(-1),
);
check(
  "score browser reads without allocating a save slot",
  e.pt_save_dirty() === 0 && bytesEqual(beforeScoreBrowser, saveSnapshot()),
);

// pt_init's seed is part of the host contract. METEORS randomizes its stars
// and hazards in init(), which makes its rendered frame a useful end-to-end
// check that equal seeds reproduce and different seeds reach game code.
const rngGame = titles.indexOf("METEORS");
check("RNG regression game is registered", rngGame >= 0);
if (rngGame >= 0) {
  const seededFrame = (seed) => {
    e.pt_init(seed);
    e.pt_launch(rngGame);
    for (let f = 0; f < 30; f++) tick(0.35, 0.2);
    return framebufferSnapshot();
  };
  const seedA1 = seededFrame(0x12345678);
  const seedA2 = seededFrame(0x12345678);
  const seedB = seededFrame(0x87654321);
  check("same RNG seed reproduces the framebuffer", bytesEqual(seedA1, seedA2));
  check("different RNG seeds diverge", !bytesEqual(seedA1, seedB));
}

// BREAKOUT schedules each drop within 7 to 20 broken bricks. Exercise the
// first multiball drop so a bad countdown cannot hide pickups for a full board.
const breakout = ids.indexOf("breakout");
check("BREAKOUT power-up regression game is registered", breakout >= 0);
if (breakout >= 0) {
  e.pt_init(4);
  e.pt_launch(breakout);
  tick(0.35, 0, BTN_CLICK);
  tick(0.35);
  let sawMultiballDrop = false;
  for (let frame = 0; frame < 2400 && !sawMultiballDrop; frame++) {
    const fb = new Uint8Array(e.memory.buffer, e.pt_framebuffer(), 64 * 64 * 3);
    let magentaPixels = 0;
    for (let y = 28; y < 60; y++) {
      for (let x = 0; x < 64; x++) {
        const p = (y * 64 + x) * 3;
        if (fb[p] === 255 && fb[p + 1] === 60 && fb[p + 2] === 200) {
          magentaPixels++;
        }
      }
    }
    if (magentaPixels >= 10) sawMultiballDrop = true;

    let lowestBallX = 32;
    let lowestBallY = -1;
    for (let y = 8; y < 59; y++) {
      for (let x = 0; x < 63; x++) {
        const p = (y * 64 + x) * 3;
        const right = p + 3;
        const below = p + 64 * 3;
        const diagonal = below + 3;
        if ([p, right, below, diagonal].every(
          (q) => fb[q] === 255 && fb[q + 1] === 255 && fb[q + 2] === 255,
        )) {
          if (y > lowestBallY) {
            lowestBallX = x + 0.5;
            lowestBallY = y;
          }
        }
      }
    }
    const wantedTilt = Math.max(-1, Math.min(1, (lowestBallX - 32) / 30));
    const tiltSign = wantedTilt < 0 ? -1 : 1;
    let tiltLo = 0.08;
    let tiltHi = 1;
    for (let step = 0; step < 12; step++) {
      const raw = (tiltLo + tiltHi) / 2;
      const curved = (raw - 0.08) / 0.92;
      const shaped = curved * (0.45 + 0.55 * curved);
      if (shaped < Math.abs(wantedTilt)) tiltLo = raw;
      else tiltHi = raw;
    }
    const tiltX = Math.abs(wantedTilt) < 0.001 ? 0 : tiltSign * (tiltLo + tiltHi) / 2;
    tick(tiltX, 0, frame % 120 === 0 ? BTN_CLICK : 0);
  }
  check("BREAKOUT shows a power-up within 20 broken bricks", sawMultiballDrop);
}

// Restore the normal boot state for the remaining menu tests.
e.pt_init(1234);

tick();
check("menu draws pixels", fbLitPixels() > 50);
check("starts in menu", e.pt_current_game() === -1);
check("menu requests the MENU music track", e.pt_music_track() === 1);

// The SFX patch library is data the host reads straight out of linear memory.
check("sfx library has styles and events", e.pt_sfx_style_count() === 4 && e.pt_sfx_count() === 12);
check("sfx names are non-empty", cstr(e.pt_sfx_name(0)).length > 0 && cstr(e.pt_sfx_style_name(3)).length > 0);
{
  // SfxPatch layout: u32 wave + 12 floats (see core/include/pixeltilt/audio.h).
  const p = e.pt_sfx_patch(0, 0);
  const wave = new Uint32Array(e.memory.buffer, p, 1)[0];
  const volume = new Float32Array(e.memory.buffer, p + 4, 1)[0];
  check("sfx patches look sane", wave < 5 && volume > 0 && volume <= 1);
}

// DOWN moves the cursor, CLICK launches whatever is selected.
tick(0, 0, BTN_DOWN);
tick();
tick(0, 0, BTN_CLICK);
check("click launches game 1", e.pt_current_game() === 1);

// Every game must survive 300 frames of wiggly tilt + button mashing + a
// shake burst (the optional accel input) and draw.
for (let g = 0; g < count; g++) {
  e.pt_launch(g);
  for (let f = 0; f < 300; f++) {
    const btn = f % 40 === 0 ? BTN_CLICK : 0;
    if (f % 60 < 10) e.pt_accel(Math.sin(f) * 1.2, Math.cos(f) * 1.2, Math.sin(f * 2) * 0.8);
    else e.pt_accel(0, 0, 0);
    e.pt_gravity(Math.sin(f / 20) * 0.8, Math.cos(f / 31) * 0.8);
    tick(Math.sin(f / 20) * 0.8, Math.cos(f / 31) * 0.8, btn);
  }
  check(`"${titles[g]}" runs 300 frames and draws`, fbLitPixels() > 20);
}
e.pt_accel(0, 0, 0);
e.pt_gravity(0, 0);
check("games produced sfx events", e.pt_sfx_head() > 0 && e.pt_sfx_ring_cap() === 16);
check("WIZ3 is registered as a scored game", ids.includes("wiz3") && gameHasScores[ids.indexOf("wiz3")]);
const wiz3Source = readFileSync(join(root, "games", "wiz3", "game.cpp"), "utf8");
check(
  "WIZ3 keeps separate side and one-way landing masks",
  wiz3Source.includes("blockAt(tx, ty, 1)") && wiz3Source.includes("blockAt(tx, ty, 3)"),
);
check(
  "WIZ3 right-wall collision stops before the block",
  wiz3Source.includes("nextX = (float)(tr * TILE - 12);") &&
    !wiz3Source.includes("nextX = (float)(tr * TILE + 4);"),
);
check(
  "WIZ3 collected items retain their art and rise while shrinking",
  wiz3Source.includes("c.potionRed = nextPotionRed;") &&
    wiz3Source.includes("nextPotionRed = !nextPotionRed;") &&
    wiz3Source.includes("addPickupEffect(bonus, tx, ty, c.potionRed);") &&
    wiz3Source.includes("drawShrunkBitmap(wiz3_art::PICKUPS[e.pickupEffect]") &&
    wiz3Source.includes("e.y -= 32.0f * dt;") &&
    wiz3Source.includes("e.frame = clampi((int)(e.t / 0.0875f), 0, 7);"),
);
check(
  "WIZ3 enters overlapping doors with the UP key",
  wiz3Source.includes("findOverlappingBonus(7, tx, ty)") &&
    wiz3Source.includes("if (input.justDown(BTN_UP)) handleUpInteraction();") &&
    !wiz3Source.includes("if (input.justDown(BTN_CLICK)) handleUpInteraction();"),
);
check(
  "WIZ3 distinguishes collision-backed stone from decorative stone",
  wiz3Source.includes("c.fore == 0 && (c.block & 3) != 0 && c.back >= 32 && c.back <= 34") &&
    wiz3Source.includes("drawStoneCollision(sx, sy, exposedTop);") &&
    !wiz3Source.includes("surfaceCap"),
);
check(
  "WIZ3 marks only collision-backed tree ledges as standable",
  wiz3Source.includes("(c.block & 3) != 0 && c.fore >= 97 && c.fore <= 103") &&
    wiz3Source.includes("drawTreeLedge(sx, sy);") &&
    wiz3Source.includes("const Color mossTop = rgb(132, 181, 78);"),
);
check(
  "WIZ3 embeds original sample banks as PTA",
  wiz3Source.includes("sfxSample(wiz3_assets::SAMPLE_JUMP, sizeof(wiz3_assets::SAMPLE_JUMP))") &&
    readFileSync(join(root, "games", "wiz3", "assets.h"), "utf8").includes("static const uint8_t SAMPLE_JUMP[]"),
);

// PUZZLES is eight puzzle types behind one menu entry, so the sweep above only
// ever reaches the first slot. Walk into each type from its own picker slot
// and back out again: a puzzle that fails to start, or one that swallows the
// DOWN that leaves it, would otherwise be invisible here.
const puzzles = ids.indexOf("puzzles");
check("PUZZLES is registered as a scored game", puzzles >= 0 && gameHasScores[puzzles]);
{
  const puzzleSource = readFileSync(join(root, "games", "puzzles", "game.cpp"), "utf8");
  const wired = puzzleSource.match(/pz_[a-z0-9]+::start/g) ?? [];
  check("PUZZLES wires up eight puzzle types", wired.length === 8);

  // The picker's header rule is a full-width line at row 9; no board draws
  // one, so it is a cheap "are we back on the list" probe.
  const onPicker = () => {
    const fb = new Uint8Array(e.memory.buffer, e.pt_framebuffer(), 64 * 64 * 3);
    const row = 9 * 64 * 3;
    if (!(fb[row] | fb[row + 1] | fb[row + 2])) return false;
    for (let x = 1; x < 64; x++) {
      const i = row + x * 3;
      if (fb[i] !== fb[row] || fb[i + 1] !== fb[row + 1] || fb[i + 2] !== fb[row + 2]) return false;
    }
    return true;
  };
  let started = true, returned = true;
  for (let slot = 0; slot < wired.length; slot++) {
    e.pt_launch(puzzles);
    tick();
    for (let k = 0; k < slot; k++) { tick(0, 0, BTN_DOWN); tick(); }
    tick(0, 0, BTN_CLICK);
    tick();
    started &&= fbLitPixels() > 20 && !onPicker();
    for (let f = 0; f < 120; f++) {
      const btn = f % 30 === 10 ? BTN_CLICK : f % 30 === 20 ? BTN_UP : 0;
      tick(Math.sin(f / 7) * 0.9, Math.cos(f / 9) * 0.9, btn);
    }
    // Clear a solved/failed panel first — those own the click, not the board.
    for (let f = 0; f < 40; f++) tick();
    tick(0, 0, BTN_CLICK);
    for (let f = 0; f < 10; f++) tick();
    tick(0, 0, BTN_DOWN);
    tick();
    returned &&= onPicker();
  }
  check("every PUZZLES type starts from its own picker slot", started);
  check("DOWN leaves any PUZZLES type for the picker", returned);

  // CRATES board 1 is hand-built and its optimal solution is five moves, so it
  // is the one board the test can actually finish. That gets the run past
  // board 1, which is what makes resume mean anything.
  const names = [...puzzleSource.matchAll(/\{"([A-Z]+)",\s*\d/g)].map((m) => m[1]);
  const crates = names.indexOf("CRATES");
  check("CRATES is one of the wired puzzle types", crates >= 0);
  // A lean has to cross 0.45 to register and drop under 0.25 to re-arm.
  const lean = (tx, ty) => {
    for (let i = 0; i < 3; i++) tick(tx, ty);
    for (let i = 0; i < 4; i++) tick();
  };
  const bannerIsGreen = () => {
    const fb = new Uint8Array(e.memory.buffer, e.pt_framebuffer(), 64 * 64 * 3);
    const i = (17 * 64 + 6) * 3;  // top-left corner of the solved panel
    return fb[i + 1] > 150 && fb[i] < 120;
  };
  e.pt_launch(puzzles);
  tick();
  for (let k = 0; k < crates; k++) { tick(0, 0, BTN_DOWN); tick(); }
  tick(0, 0, BTN_CLICK); tick();
  for (const [tx, ty] of [[-0.9, 0], [-0.9, 0], [0, -0.9], [0.9, 0], [0.9, 0]]) lean(tx, ty);
  check("CRATES board 1 solves in its five optimal moves", bannerIsGreen());
  for (let f = 0; f < 130; f++) tick();   // panel rolls into board 2
  tick(0, 0, BTN_DOWN); tick();           // leave, banking the run
  tick(0, 0, BTN_CLICK); tick();          // re-enter the same type
  check("a played PUZZLES type offers resume or restart", onPicker());
  tick(0, 0, BTN_CLICK); tick();          // confirm the highlighted RESUME
  check("the resume choice starts a board", !onPicker() && fbLitPixels() > 20);
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

// The menu row above the first game (wrap via UP) is SETTINGS. The first
// six items cycle screen rotation, tilt rotation, tilt flip, brightness and
// the two audio volumes.
tick(0, 0, BTN_UP); tick();
tick(0, 0, BTN_CLICK); tick();          // enter settings
tick(0, 0, BTN_CLICK); tick();          // SCREEN -> 90
check("changing a setting marks the save dirty", e.pt_save_dirty() === 1);
check("menu still draws when rotated", fbLitPixels() > 50);
tick(0, 0, BTN_DOWN); tick();
tick(0, 0, BTN_CLICK); tick();          // TILT -> 90
tick(0, 0, BTN_DOWN); tick();
tick(0, 0, BTN_CLICK); tick();          // FLIP -> ON
tick(0, 0, BTN_DOWN); tick();
tick(0, 0, BTN_CLICK); tick();          // BRIGHT 100 -> 20
check("brightness cycles", e.pt_brightness() === 20);
tick(0, 0, BTN_DOWN); tick();           // REFRESH (hardware-only, skipped here)
tick(0, 0, BTN_DOWN); tick();
tick(0, 0, BTN_CLICK); tick();          // SFX 80 -> 100
check("sfx volume cycles", e.pt_sfx_volume() === 100);
tick(0, 0, BTN_DOWN); tick();
tick(0, 0, BTN_CLICK); tick();          // MUSIC 60 -> 80
check("music volume cycles", e.pt_music_volume() === 80);

// Save blob round-trip: snapshot, wipe via re-init, restore.
e.pt_save_clear_dirty();
const saveSize = e.pt_save_size();
const blob = new Uint8Array(saveSize);
blob.set(new Uint8Array(e.memory.buffer, e.pt_save_ptr(), saveSize));
e.pt_init(99);
check("re-init resets settings", e.pt_brightness() === 100 && e.pt_sfx_volume() === 80);
new Uint8Array(e.memory.buffer, e.pt_save_ptr(), saveSize).set(blob);
check(
  "save blob restores settings",
  e.pt_save_loaded() === 1 && e.pt_brightness() === 20 &&
    e.pt_sfx_volume() === 100 && e.pt_music_volume() === 80,
);

console.log(failures === 0 ? "\nall checks passed" : `\n${failures} check(s) FAILED`);
process.exit(failures === 0 ? 0 : 1);
