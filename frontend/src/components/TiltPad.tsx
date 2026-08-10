import { useCallback, useRef } from "react";

// 2-axis tilt visualizer + mouse override. Dragging simulates tilting the
// physical device; releasing lets it level back out.
export function TiltPad(props: {
  tilt: { x: number; y: number };
  setPadTilt(t: { x: number; y: number } | null): void;
}) {
  const ref = useRef<HTMLDivElement>(null);
  const dragging = useRef(false);
  const { setPadTilt } = props;

  const tiltFromEvent = useCallback((ev: PointerEvent | React.PointerEvent) => {
    const el = ref.current!;
    const r = el.getBoundingClientRect();
    const x = ((ev.clientX - r.left) / r.width) * 2 - 1;
    const y = ((ev.clientY - r.top) / r.height) * 2 - 1;
    return {
      x: Math.max(-1, Math.min(1, x)),
      y: Math.max(-1, Math.min(1, y)),
    };
  }, []);

  const onPointerDown = (ev: React.PointerEvent) => {
    dragging.current = true;
    ref.current!.setPointerCapture(ev.pointerId);
    setPadTilt(tiltFromEvent(ev));
  };
  const onPointerMove = (ev: React.PointerEvent) => {
    if (dragging.current) setPadTilt(tiltFromEvent(ev));
  };
  const onPointerUp = () => {
    dragging.current = false;
    setPadTilt(null);
  };

  const bx = 50 + props.tilt.x * 42;
  const by = 50 + props.tilt.y * 42;

  return (
    <div
      ref={ref}
      className="tilt-pad"
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerUp}
      onPointerCancel={onPointerUp}
      title="Drag to tilt (or use arrow keys)"
    >
      <span className="tilt-axis h" />
      <span className="tilt-axis v" />
      <span className="tilt-ring" />
      <span className="tilt-bubble" style={{ left: `${bx}%`, top: `${by}%` }} />
      <span className="tilt-readout x">{props.tilt.x >= 0 ? "+" : ""}{props.tilt.x.toFixed(2)}</span>
      <span className="tilt-readout y">{props.tilt.y >= 0 ? "+" : ""}{props.tilt.y.toFixed(2)}</span>
    </div>
  );
}
