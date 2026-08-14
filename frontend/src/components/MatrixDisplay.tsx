import { useEffect, useRef } from "react";

// The star of the show: a HUB75-style panel — one canvas of discrete LED dots
// on a black mask, framed in a PCB-black bezel. No bloom or halo: what you see
// is the emitters themselves, drawn at 10 device pixels per LED so each dot
// gets a round edge instead of a square.
export function MatrixDisplay(props: {
  registerCanvas(main: HTMLCanvasElement | null): void;
}) {
  const mainRef = useRef<HTMLCanvasElement>(null);
  const { registerCanvas } = props;

  useEffect(() => {
    registerCanvas(mainRef.current);
    return () => registerCanvas(null);
  }, [registerCanvas]);

  return (
    <div className="panel-bezel">
      <span className="bezel-hole tl" />
      <span className="bezel-hole tr" />
      <span className="bezel-hole bl" />
      <span className="bezel-hole br" />
      <div className="panel-face">
        <canvas
          ref={mainRef}
          className="matrix-main"
          width={640}
          height={640}
          draggable={false}
        />
        <span className="panel-grid" />
      </div>
      <span className="panel-plexi" />
    </div>
  );
}
