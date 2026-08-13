import { BTN_CLICK, BTN_DOWN, BTN_UP } from "../emulator/wasm";

// On-screen stand-in for the Hub S3's 3-way thumb wheel. Press-and-hold works
// like the real thing (pointer capture keeps the bit set until release).
const SPEC = [
  { mask: BTN_UP, glyph: "▲", key: "A", name: "up" },
  { mask: BTN_CLICK, glyph: "●", key: "S", name: "click" },
  { mask: BTN_DOWN, glyph: "▼", key: "D", name: "down" },
];

export function WheelButtons(props: {
  buttons: number;
  setVirtualButton(mask: number, down: boolean): void;
}) {
  return (
    <div className="wheel">
      {SPEC.map((b) => (
        <button
          key={b.name}
          className={`wheel-btn ${props.buttons & b.mask ? "active" : ""}`}
          onPointerDown={(ev) => {
            (ev.target as HTMLElement).setPointerCapture(ev.pointerId);
            props.setVirtualButton(b.mask, true);
          }}
          onPointerUp={() => props.setVirtualButton(b.mask, false)}
          onPointerCancel={() => props.setVirtualButton(b.mask, false)}
          aria-label={`wheel ${b.name}`}
          title={`Wheel ${b.name} (${b.key})`}
        >
          {b.glyph}
        </button>
      ))}
    </div>
  );
}
