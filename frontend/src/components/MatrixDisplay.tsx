import { useEffect, useRef } from "react";

// The star of the show: a HUB75-style panel. Two stacked canvases — a blurred
// one underneath for phosphor glow, a dot-masked one on top for the LED grid —
// framed in a PCB-black bezel with silkscreen markings.
export function MatrixDisplay(props: {
  registerCanvases(main: HTMLCanvasElement | null, glow: HTMLCanvasElement | null): void;
}) {
  const mainRef = useRef<HTMLCanvasElement>(null);
  const glowRef = useRef<HTMLCanvasElement>(null);
  const { registerCanvases } = props;

  useEffect(() => {
    registerCanvases(mainRef.current, glowRef.current);
    return () => registerCanvases(null, null);
  }, [registerCanvases]);

  return (
    <div className="panel-bezel">
      <span className="bezel-hole tl" />
      <span className="bezel-hole tr" />
      <span className="bezel-hole bl" />
      <span className="bezel-hole br" />
      <div className="panel-face">
        <canvas ref={glowRef} className="matrix-glow" width={512} height={512} />
        <canvas ref={mainRef} className="matrix-main" width={512} height={512} />
      </div>
      <div className="bezel-label">
        <span>P3-6464-HUB75E</span>
        <span className="bezel-brand">PIXELTILT</span>
        <span>1/32 SCAN</span>
      </div>
    </div>
  );
}
