import { BTN_CLICK, BTN_DOWN, BTN_UP } from "../emulator/wasm";

// On-screen stand-in for the Hub S3's 3-way thumb wheel. Press-and-hold works
// like the real thing (pointer capture keeps the bit set until release).
export function WheelButtons(props: {
  buttons: number;
  setVirtualButton(mask: number, down: boolean): void;
}) {
  const spec = [
    { mask: BTN_UP, label: "▲", key: "A", name: "UP" },
    { mask: BTN_CLICK, label: "●", key: "S", name: "CLICK" },
    { mask: BTN_DOWN, label: "▼", key: "D", name: "DOWN" },
  ];
  return (
    <div className="wheel">
      <div className="wheel-track">
        {spec.map((b) => (
          <button
            key={b.name}
            className={`wheel-btn ${props.buttons & b.mask ? "active" : ""}`}
            onPointerDown={(ev) => {
              (ev.target as HTMLElement).setPointerCapture(ev.pointerId);
              props.setVirtualButton(b.mask, true);
            }}
            onPointerUp={() => props.setVirtualButton(b.mask, false)}
            onPointerCancel={() => props.setVirtualButton(b.mask, false)}
            aria-label={`wheel ${b.name.toLowerCase()}`}
          >
            <span className="wheel-glyph">{b.label}</span>
            <kbd>{b.key}</kbd>
          </button>
        ))}
      </div>
      <span className="wheel-caption">SW1 · THUMB WHEEL</span>
    </div>
  );
}
