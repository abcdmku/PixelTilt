// Four looks for the same 3D panel. Pick one in the HUD (or `?ui=<id>`);
// the choice sticks in localStorage. Interaction is identical on all four.
export const VARIANTS = [
  { id: "bare", label: "bare", blurb: "just the LED array" },
  { id: "glass", label: "glass", blurb: "smoked plate, thin metal rim" },
  { id: "frame", label: "frame", blurb: "matte white plexi, square cells" },
  {
    id: "frame-dark",
    label: "frame dark",
    blurb: "black printed frame, clear diffusers, dark cell walls",
  },
] as const;

export type VariantId = (typeof VARIANTS)[number]["id"];

const KEY = "pixeltilt.ui";
const DEFAULT: VariantId = "bare";

function isVariant(v: string | null): v is VariantId {
  return !!v && VARIANTS.some((x) => x.id === v);
}

/** URL wins (shareable), then the last choice, then the default. */
export function initialVariant(): VariantId {
  const q = new URLSearchParams(location.search).get("ui");
  if (isVariant(q)) return q;
  try {
    const saved = localStorage.getItem(KEY);
    if (isVariant(saved)) return saved;
  } catch {
    // storage unavailable — fall through to the default
  }
  return DEFAULT;
}

export function rememberVariant(v: VariantId) {
  try {
    localStorage.setItem(KEY, v);
  } catch {
    // ignore
  }
}
