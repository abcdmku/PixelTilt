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

export type PixelStyle = "dots" | "squares" | "printed";

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

function cellNoise(x: number, y: number, salt: number): number {
  let h = Math.imul(x + 1, 0x9e3779b1) ^ Math.imul(y + 1, 0x85ebca6b) ^ salt;
  h = Math.imul(h ^ (h >>> 16), 0x7feb352d);
  h = Math.imul(h ^ (h >>> 15), 0x846ca68b);
  return ((h ^ (h >>> 16)) >>> 0) / 0xffffffff;
}

/** Masks for clear-filament pixels: textured face, hot LED die, and wall spill. */
export function makePrintedPixelMasks(
  size: number,
  cells: number,
): {
  body: HTMLCanvasElement;
  core: HTMLCanvasElement;
  spill: HTMLCanvasElement;
  fringe: HTMLCanvasElement;
} {
  const body = document.createElement("canvas");
  const core = document.createElement("canvas");
  const spill = document.createElement("canvas");
  const fringe = document.createElement("canvas");
  body.width = body.height = size;
  core.width = core.height = size;
  spill.width = spill.height = size;
  fringe.width = fringe.height = size;

  const bodyCtx = body.getContext("2d")!;
  const coreCtx = core.getContext("2d")!;
  const spillCtx = spill.getContext("2d")!;
  const fringeCtx = fringe.getContext("2d")!;
  const cell = size / cells;

  for (let y = 0; y < cells; y++) {
    for (let x = 0; x < cells; x++) {
      const width = cell * (0.79 + (cellNoise(x, y, 11) - 0.5) * 0.11);
      const height = cell * (0.79 + (cellNoise(x, y, 23) - 0.5) * 0.11);
      const cx =
        (x + 0.5) * cell + (cellNoise(x, y, 37) - 0.5) * cell * 0.08;
      const cy =
        (y + 0.5) * cell + (cellNoise(x, y, 53) - 0.5) * cell * 0.08;
      const turn = (cellNoise(x, y, 71) - 0.5) * 0.055;
      const radius = cell * (0.055 + cellNoise(x, y, 89) * 0.07);
      const transmission = 0.76 + cellNoise(x, y, 97) * 0.24;

      bodyCtx.save();
      bodyCtx.translate(cx, cy);
      bodyCtx.rotate(turn);
      bodyCtx.beginPath();
      bodyCtx.roundRect(-width / 2, -height / 2, width, height, radius);
      bodyCtx.clip();

      const face = bodyCtx.createLinearGradient(
        -width * 0.42,
        -height * 0.38,
        width * 0.42,
        height * 0.38,
      );
      face.addColorStop(0, `rgba(255,255,255,${transmission * 0.88})`);
      face.addColorStop(0.52, `rgba(255,255,255,${transmission})`);
      face.addColorStop(1, `rgba(255,255,255,${transmission * 0.82})`);
      bodyCtx.fillStyle = face;
      bodyCtx.fillRect(-width / 2, -height / 2, width, height);

      // The close-up of the real print has nested, roughly square tool paths.
      // Cut dark grooves, then reveal a thinner highlight beside each groove.
      bodyCtx.globalCompositeOperation = "destination-out";
      bodyCtx.lineCap = "round";
      bodyCtx.lineJoin = "round";
      const ringCount = 2 + Math.floor(cellNoise(x, y, 103) * 3);
      const ringStep = 0.29 / Math.max(1, ringCount - 1);
      for (let ring = 0; ring < ringCount; ring++) {
        const inset = cell * (0.052 + ring * ringStep);
        const ringJitterX = (cellNoise(x, y, 109 + ring * 17) - 0.5) * cell * 0.075;
        const ringJitterY = (cellNoise(x, y, 127 + ring * 19) - 0.5) * cell * 0.075;
        const ringTurn = (cellNoise(x, y, 149 + ring * 23) - 0.5) * 0.11;
        const ringAlpha = 0.42 + cellNoise(x, y, 173 + ring * 29) * 0.35;
        const ringWidth = cell * (0.045 + cellNoise(x, y, 197 + ring * 31) * 0.035);
        const ringW = Math.max(cell * 0.11, width - inset * 2);
        const ringH = Math.max(cell * 0.11, height - inset * 2);

        bodyCtx.save();
        bodyCtx.translate(ringJitterX, ringJitterY);
        bodyCtx.rotate(ringTurn);
        bodyCtx.beginPath();
        bodyCtx.roundRect(-ringW / 2, -ringH / 2, ringW, ringH, radius * 0.72);
        bodyCtx.strokeStyle = `rgba(0,0,0,${ringAlpha})`;
        bodyCtx.lineWidth = ringWidth;
        bodyCtx.stroke();
        bodyCtx.restore();
      }

      bodyCtx.globalCompositeOperation = "source-over";
      const highlightCount = 1 + Math.floor(cellNoise(x, y, 211) * 3);
      const highlightStep = 0.265 / Math.max(1, highlightCount - 1);
      for (let ring = 0; ring < highlightCount; ring++) {
        const inset = cell * (0.073 + ring * highlightStep);
        const ringW = Math.max(cell * 0.13, width - inset * 2);
        const ringH = Math.max(cell * 0.13, height - inset * 2);
        bodyCtx.save();
        bodyCtx.translate(
          (cellNoise(x, y, 223 + ring * 37) - 0.5) * cell * 0.035,
          (cellNoise(x, y, 241 + ring * 41) - 0.5) * cell * 0.035,
        );
        bodyCtx.rotate((cellNoise(x, y, 263 + ring * 43) - 0.5) * 0.045);
        bodyCtx.beginPath();
        bodyCtx.roundRect(-ringW / 2, -ringH / 2, ringW, ringH, radius * 0.6);
        bodyCtx.strokeStyle = `rgba(255,255,255,${0.14 + cellNoise(x, y, 281 + ring * 47) * 0.12})`;
        bodyCtx.lineWidth = cell * 0.032;
        bodyCtx.stroke();
        bodyCtx.restore();
      }

      // Tiny voids and dragged strands keep neighboring caps from sharing the
      // same perfect contour pattern.
      bodyCtx.globalCompositeOperation = "destination-out";
      const inclusionCount = 1 + Math.floor(cellNoise(x, y, 293) * 4);
      for (let mark = 0; mark < inclusionCount; mark++) {
        const markX = (cellNoise(x, y, 307 + mark * 53) - 0.5) * width * 0.68;
        const markY = (cellNoise(x, y, 331 + mark * 59) - 0.5) * height * 0.68;
        const markLength = cell * (0.045 + cellNoise(x, y, 353 + mark * 61) * 0.1);
        const markTurn = cellNoise(x, y, 379 + mark * 67) * Math.PI;
        bodyCtx.beginPath();
        bodyCtx.moveTo(markX, markY);
        bodyCtx.lineTo(
          markX + Math.cos(markTurn) * markLength,
          markY + Math.sin(markTurn) * markLength,
        );
        bodyCtx.strokeStyle = `rgba(0,0,0,${0.13 + cellNoise(x, y, 401 + mark * 71) * 0.22})`;
        bodyCtx.lineWidth = cell * (0.025 + cellNoise(x, y, 419 + mark * 73) * 0.035);
        bodyCtx.stroke();
      }
      bodyCtx.restore();

      coreCtx.save();
      coreCtx.translate(cx, cy);
      coreCtx.rotate(turn);
      coreCtx.beginPath();
      coreCtx.roundRect(-width / 2, -height / 2, width, height, radius);
      coreCtx.clip();
      const centerX = (cellNoise(x, y, 443) - 0.5) * cell * 0.18;
      const centerY = (cellNoise(x, y, 467) - 0.5) * cell * 0.18;
      const dieStrength = 0.46 + cellNoise(x, y, 479) * 0.24;
      const glow = coreCtx.createRadialGradient(
        centerX,
        centerY,
        0,
        centerX,
        centerY,
        cell * 0.155,
      );
      glow.addColorStop(0, `rgba(255,255,255,${dieStrength})`);
      glow.addColorStop(0.18, `rgba(255,255,255,${dieStrength * 0.92})`);
      glow.addColorStop(0.5, `rgba(255,255,255,${dieStrength * 0.38})`);
      glow.addColorStop(1, "rgba(255,255,255,0)");
      coreCtx.fillStyle = glow;
      coreCtx.fillRect(-width / 2, -height / 2, width, height);
      coreCtx.restore();

      const spillRadius = cell * (0.59 + cellNoise(x, y, 487) * 0.14);
      const spillGlow = spillCtx.createRadialGradient(
        cx + centerX,
        cy + centerY,
        cell * 0.12,
        cx + centerX,
        cy + centerY,
        spillRadius,
      );
      spillGlow.addColorStop(0, "rgba(255,255,255,0.55)");
      spillGlow.addColorStop(0.5, "rgba(255,255,255,0.25)");
      spillGlow.addColorStop(1, "rgba(255,255,255,0)");
      spillCtx.fillStyle = spillGlow;
      spillCtx.fillRect(
        cx - spillRadius,
        cy - spillRadius,
        spillRadius * 2,
        spillRadius * 2,
      );

      // Mixed colors reveal the red die as a broad band along the cap's upper
      // edge. The line wanders slightly from pixel to pixel, like uneven clear
      // filament layers rather than a perfectly aligned optical element.
      fringeCtx.save();
      fringeCtx.translate(cx, cy);
      fringeCtx.rotate(turn);
      fringeCtx.beginPath();
      fringeCtx.roundRect(-width / 2, -height / 2, width, height, radius);
      fringeCtx.clip();
      const fringeTilt = (cellNoise(x, y, 503) - 0.5) * width * 0.18;
      const fringeShift = (cellNoise(x, y, 521) - 0.5) * height * 0.15;
      const fringeStrength = 0.72 + cellNoise(x, y, 541) * 0.28;
      const fringeGlow = fringeCtx.createLinearGradient(
        fringeTilt,
        -height * 0.58 + fringeShift,
        -fringeTilt,
        height * 0.48 + fringeShift,
      );
      fringeGlow.addColorStop(0, `rgba(255,255,255,${fringeStrength})`);
      fringeGlow.addColorStop(0.4, `rgba(255,255,255,${fringeStrength * 0.94})`);
      fringeGlow.addColorStop(0.53, `rgba(255,255,255,${fringeStrength * 0.72})`);
      fringeGlow.addColorStop(0.66, `rgba(255,255,255,${fringeStrength * 0.08})`);
      fringeGlow.addColorStop(1, "rgba(255,255,255,0)");
      fringeCtx.fillStyle = fringeGlow;
      fringeCtx.fillRect(-width / 2, -height / 2, width, height);
      fringeCtx.restore();
    }
  }

  // Keep the color split inside the same grooves as the diffuser body. Keep
  // the bloom mostly outside the cap so it lights the printed divider walls.
  fringeCtx.globalCompositeOperation = "destination-in";
  fringeCtx.drawImage(body, 0, 0);
  spillCtx.globalCompositeOperation = "destination-out";
  spillCtx.globalAlpha = 0.82;
  spillCtx.drawImage(body, 0, 0);

  return { body, core, spill, fringe };
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
