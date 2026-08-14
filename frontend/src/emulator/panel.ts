// What the 64x64 HUB75 panel can actually show.
//
// Games write RGB888, but the panel is nowhere near an RGB888 display. On the
// device the framebuffer goes through ESP32-HUB75-MatrixPanel-I2S-DMA, which
// drives each channel as an 8-bit binary-coded-modulation duty cycle after
// running the value through a CIE 1931 lightness curve (`lumConvTab_8bit` in
// the library's cie_luts.h). That curve is what makes ramps look linear to the
// eye, but it also throws away most of the shadow end: the 256 input codes
// collapse onto 174 distinct duty levels, codes 0-4 are simply off, and the
// whole bottom quarter of the range (0-63) has just 12 levels left.
//
// This module reproduces that pipeline so the emulator shows the banding,
// the black-crush and the dimming a game will really get, instead of a clean
// 24-bit picture that only exists in the browser.

// CIE 1931 lightness LUT, 8-bit in -> 8-bit BCM duty out. Copied verbatim from
// the panel library (src/cie_luts.h, lumConvTab_8bit) so the emulator and the
// firmware agree code for code.
export const CIE_LUT = new Uint8Array([
  0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4,
  4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 7,
  7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 10, 10, 10, 10, 11, 11,
  11, 12, 12, 12, 13, 13, 13, 14, 14, 15, 15, 15, 16, 16, 17, 17,
  17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 23, 24, 24, 25,
  25, 26, 26, 27, 28, 28, 29, 29, 30, 31, 31, 32, 32, 33, 34, 34,
  35, 36, 37, 37, 38, 39, 39, 40, 41, 42, 43, 43, 44, 45, 46, 47,
  47, 48, 49, 50, 51, 52, 53, 54, 54, 55, 56, 57, 58, 59, 60, 61,
  62, 63, 64, 65, 66, 67, 68, 70, 71, 72, 73, 74, 75, 76, 77, 79,
  80, 81, 82, 83, 85, 86, 87, 88, 90, 91, 92, 94, 95, 96, 98, 99,
  100, 102, 103, 105, 106, 108, 109, 110, 112, 113, 115, 116, 118, 120, 121, 123,
  124, 126, 128, 129, 131, 132, 134, 136, 138, 139, 141, 143, 145, 146, 148, 150,
  152, 154, 155, 157, 159, 161, 163, 165, 167, 169, 171, 173, 175, 177, 179, 181,
  183, 185, 187, 189, 191, 193, 196, 198, 200, 202, 204, 207, 209, 211, 214, 216,
  218, 220, 223, 225, 228, 230, 232, 235, 237, 240, 242, 245, 247, 250, 252, 255,
]);

/** Distinct duty levels a channel can actually reach (of 256 input codes). */
export const PANEL_LEVELS = new Set(CIE_LUT).size; // 174

/** Highest input code that lands on duty 0 — anything darker is simply off. */
export const PANEL_BLACK_CEILING = CIE_LUT.lastIndexOf(0); // 4

// Screen gamma used to turn emitted light back into a pixel. Slightly above
// the monitor's ~2.2 on purpose: the panel's emitters are far brighter than
// anything a display can produce, and the eye is dark-adapted in front of
// them, so straight linear-light encoding renders a panel that reads much
// dimmer than the real thing.
const DISPLAY_GAMMA = 2.5;

// The brightness setting cuts emitted light linearly, but the eye adapts to
// it — the panel at 40 % is still dazzling in a dim room. Compress it so
// turning brightness down reads as "dimmer" without making the emulator
// unusably dark.
const BRIGHTNESS_ADAPT = 0.55;

/**
 * How far a hard-driven LED's die washes toward white (0-1). Kept small: the
 * die does read hotter than the rim, but the panel's colors stay startlingly
 * pure even at full drive, so most of the extra intensity has to arrive as
 * light rather than as white.
 */
export const LED_CORE_WHITE = 0.22;

/**
 * Chroma boost (0-1) applied by pulling the smallest channel down and
 * rescaling to keep the largest. The panel's primaries are narrow-band
 * emitters well outside sRGB — a mix that a monitor renders as a muddy
 * pastel comes off the panel as a clean, vivid color, and this is as close
 * as an sRGB canvas can get to showing that. Pure hues and greys pass
 * through untouched.
 */
export const LED_CHROMA = 0.55;

export interface PanelTables {
  /** Input code -> sRGB byte to paint, i.e. what the LED's light looks like. */
  emit: Uint8Array;
  /** Peak channel code -> how hard the die reads, 0-255 (core intensity). */
  hot: Uint8Array;
  brightness: number;
}

// A lit LED is a point source far brighter than a monitor pixel: its die
// blows out to near-white while the rim keeps the hue. `hot` drives that
// core — anything past a token drive level has one, at full by maximum.
function hotness(light: number): number {
  const t = (light - 0.15) / 0.85;
  if (t <= 0) return 0;
  return Math.min(1, t) ** 0.75;
}

let cached: PanelTables | null = null;

/**
 * Build (and cache) the code -> pixel tables for a given panel brightness
 * (the device setting, 20-100 %). Brightness on the device is OE blanking:
 * it scales emitted light after the CIE curve, so it dims without adding
 * levels back. The board's fixed PWM ceiling (PANEL_BRIGHTNESS in
 * board_config.h) is folded into "100 %" here — it isn't runtime-adjustable,
 * so it would only shift the whole picture.
 */
export function panelTables(brightnessPercent: number): PanelTables {
  if (cached && cached.brightness === brightnessPercent) return cached;
  const br = Math.max(0, Math.min(100, brightnessPercent)) / 100;
  const dim = br ** BRIGHTNESS_ADAPT;
  const emit = new Uint8Array(256);
  const hot = new Uint8Array(256);
  for (let v = 0; v < 256; v++) {
    const light = (CIE_LUT[v] / 255) * dim; // emitted light, as the eye takes it
    emit[v] = Math.round(255 * light ** (1 / DISPLAY_GAMMA));
    hot[v] = Math.round(255 * hotness(light));
  }
  cached = { emit, hot, brightness: brightnessPercent };
  return cached;
}

/**
 * A mask of one soft round dot per LED, used to carve the upscaled 64x64
 * image into discrete emitters with dark gaps between them (the panel is
 * mostly black plastic — the lit area is roughly a third of each cell).
 * `inner`/`outer` are fractions of the cell pitch.
 */
/** Hard square cells with a black gutter — the louvered look under a diffuser. */
export function makeSquareMask(
  size: number,
  cells: number,
  fill = 0.84,
): HTMLCanvasElement {
  const mask = document.createElement("canvas");
  mask.width = mask.height = size;
  const ctx = mask.getContext("2d")!;
  ctx.fillStyle = "#fff";
  const cell = size / cells;
  const pad = (cell * (1 - fill)) / 2;
  const inner = cell - pad * 2;
  for (let y = 0; y < cells; y++) {
    for (let x = 0; x < cells; x++) {
      ctx.fillRect(x * cell + pad, y * cell + pad, inner, inner);
    }
  }
  return mask;
}

export function makeLedMask(
  size: number,
  cells: number,
  inner: number,
  outer: number,
): HTMLCanvasElement {
  const mask = document.createElement("canvas");
  mask.width = mask.height = size;
  const ctx = mask.getContext("2d")!;
  const cell = size / cells;
  const r = cell * outer;
  for (let y = 0; y < cells; y++) {
    for (let x = 0; x < cells; x++) {
      const cx = (x + 0.5) * cell;
      const cy = (y + 0.5) * cell;
      const g = ctx.createRadialGradient(cx, cy, 0, cx, cy, r);
      g.addColorStop(0, "#fff");
      g.addColorStop(inner / outer, "#fff");
      g.addColorStop(0.82, "rgba(255,255,255,0.6)");
      g.addColorStop(1, "rgba(255,255,255,0)");
      ctx.fillStyle = g;
      ctx.fillRect(cx - r, cy - r, r * 2, r * 2);
    }
  }
  return mask;
}
