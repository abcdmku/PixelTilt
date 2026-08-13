import { useCallback, useRef } from "react";

// 2-axis tilt visualizer + pointer override. Dragging simulates tilting the
// physical device; releasing lets it level back out.
export function TiltPad(props: {
  tilt: { x: number; y: number };
  setPadTilt(t: { x: number; y: number } | null): void;
}) {
  const ref = useRef<HTMLDivElement>(null);
  const dragging = useRef(false);
  const { setPadTilt } = props;

  const tiltFromEvent = useCallback((ev: React.PointerEvent) => {
    const r = ref.current!.getBoundingClientRect();
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

  return (
    <div
      ref={ref}
      className="tilt-pad"
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerUp}
      onPointerCancel={onPointerUp}
      title="Drag to tilt (arrow keys work too)"
      aria-label="tilt pad"
    >
      <span className="tilt-axis h" />
      <span className="tilt-axis v" />
      <span className="tilt-ring" />
      <span
        className="tilt-bubble"
        style={{ left: `${50 + props.tilt.x * 42}%`, top: `${50 + props.tilt.y * 42}%` }}
      />
    </div>
  );
}
