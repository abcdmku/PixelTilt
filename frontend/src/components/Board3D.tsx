import { useEffect, useRef } from "react";
import { MatrixDisplay } from "./MatrixDisplay";
import type { EmulatorPose } from "../emulator/useEmulator";

const MAX_TILT_DEG = 24;
const REST_X = 10; // desk angle so the slab's thickness reads at rest
const CORNER = 0.52;
const TILT_SCALE = 0.42;
const SPIN_FULL = 3; // rad/s at virtualSpin = 1, matches the emulator hook
const HOLD_TO_SHIFT_MS = 420;
const HOLD_SLOP_PX = 8;
const YAW_VISUAL = 8; // degrees of visual twist per rad/s of keyboard spin
const G_VEL = 2300; // px/s of slide that equals 1 g of lean
const G_DECEL = 34000; // px/s² of stop that equals 1 g
const G_MAX = 0.55;
const G_MAX_DECEL = 0.34;

type Mode = "none" | "tilt" | "yaw" | "shift";
type Pt = { x: number; y: number };

function clamp(v: number, lo = -1, hi = 1) {
  return Math.max(lo, Math.min(hi, v));
}

function unwrap(d: number) {
  if (d > Math.PI) return d - Math.PI * 2;
  if (d < -Math.PI) return d + Math.PI * 2;
  return d;
}

function centerAngle(p: Pt, r: DOMRect) {
  return Math.atan2(p.y - (r.top + r.height / 2), p.x - (r.left + r.width / 2));
}

function isCorner(p: Pt, r: DOMRect) {
  const x = ((p.x - r.left) / r.width) * 2 - 1;
  const y = ((p.y - r.top) / r.height) * 2 - 1;
  return Math.abs(x) > CORNER && Math.abs(y) > CORNER;
}

/** The panel as a physical object: drag to tilt, drag a corner or right-drag
 *  to spin, hold then drag or left+right drag to slide. */
export function Board3D(props: {
  registerCanvas(main: HTMLCanvasElement | null): void;
  setPadTilt(t: { x: number; y: number } | null): void;
  setVirtualSpin(dir: number): void;
  setPadAccel(a: { x: number; y: number; z: number } | null): void;
  getPose(): EmulatorPose;
}) {
  const stageRef = useRef<HTMLDivElement>(null);
  const slotRef = useRef<HTMLDivElement>(null);
  const rigRef = useRef<HTMLDivElement>(null);
  const shadowRef = useRef<HTMLDivElement>(null);
  const { setPadTilt, setVirtualSpin, setPadAccel, getPose } = props;

  useEffect(() => {
    const stage = stageRef.current;
    const slot = slotRef.current;
    const rig = rigRef.current;
    const shadow = shadowRef.current;
    if (!stage || !slot || !rig || !shadow) return;

    const pointers = new Map<number, Pt>();
    const visual = { tiltX: 0, tiltY: 0, yaw: 0, shaking: false, shiftX: 0, shiftY: 0 };
    let mode: Mode = "none";
    let last: Pt | null = null;
    let lastAngle = 0;
    let touchStart: Pt | null = null;
    let holdTimer: number | null = null;
    let lastT = 0;
    let hoverCorner = false;
    let mouseLeft = false;
    let mouseRight = false;
    let mouseId: number | null = null;
    let velX = 0;
    let velY = 0;
    let gX = 0;
    let gY = 0;
    let gPulseUntil = 0;
    let raf = 0;

    const setSize = () => {
      slot.style.setProperty("--board-size", `${slot.clientWidth}px`);
    };
    const ro = new ResizeObserver(setSize);
    ro.observe(slot);
    setSize();

    const shiftLimit = () => {
      const r = stage.getBoundingClientRect();
      return { x: r.width * 0.38, y: r.height * 0.32 };
    };

    const apply = () => {
      const jx = visual.shaking ? (Math.random() - 0.5) * 8 : 0;
      const jy = visual.shaking ? (Math.random() - 0.5) * 8 : 0;
      const jz = visual.shaking ? (Math.random() - 0.5) * 3 : 0;
      rig.style.transform =
        `translate3d(${visual.shiftX + jx}px,${visual.shiftY + jy}px,0)` +
        ` rotateX(${REST_X + visual.tiltY * MAX_TILT_DEG + jz}deg)` +
        ` rotateY(${visual.tiltX * MAX_TILT_DEG}deg)` +
        ` rotateZ(${visual.yaw}deg)`;
      const lean = Math.hypot(visual.tiltX, visual.tiltY);
      shadow.style.transform =
        `translate3d(${visual.shiftX + visual.tiltX * 34}px, ${visual.shiftY + 12 + visual.tiltY * 20}px, 0)` +
        ` scale(${0.82 + lean * 0.08}, 0.55)`;
      shadow.style.opacity = String(0.5 - lean * 0.18);
      stage.dataset.tiltX = visual.tiltX.toFixed(3);
      stage.dataset.tiltY = visual.tiltY.toFixed(3);
      stage.dataset.yaw = visual.yaw.toFixed(1);
      stage.dataset.shiftX = visual.shiftX.toFixed(0);
      stage.dataset.shiftY = visual.shiftY.toFixed(0);
    };

    const followPose = () => {
      const pose = getPose();
      visual.tiltX = pose.tilt.x;
      visual.tiltY = -pose.tilt.y;
      visual.yaw += (pose.spin * YAW_VISUAL - visual.yaw) * 0.18;
      visual.shaking = pose.shaking;
    };

    const gameTilt = () => ({ x: visual.tiltX, y: -visual.tiltY });

    const tick = () => {
      const now = performance.now();
      if (mode === "none" || mode === "shift") followPose();
      if (mode === "shift") {
        visual.shaking = false;
        if (lastT && now - lastT > 70 && (velX !== 0 || velY !== 0 || gX !== 0 || gY !== 0)) {
          coastG(now);
        }
      }
      if (gPulseUntil && now > gPulseUntil) {
        setPadAccel(null);
        gPulseUntil = 0;
      }
      apply();
      raf = requestAnimationFrame(tick);
    };
    apply();
    raf = requestAnimationFrame(tick);

    const boardRect = () => slot.getBoundingClientRect();

    const paintCursor = () => {
      stage.dataset.mode = mode === "none" ? (hoverCorner ? "yaw" : "tilt") : mode;
      stage.classList.toggle("is-down", mode !== "none");
    };

    const cancelHold = () => {
      if (holdTimer === null) return;
      window.clearTimeout(holdTimer);
      holdTimer = null;
    };

    const armHoldToShift = (pointerId: number) => {
      cancelHold();
      holdTimer = window.setTimeout(() => {
        holdTimer = null;
        if (!pointers.has(pointerId) || !last) return;
        mode = "shift";
        velX = 0;
        velY = 0;
        gX = 0;
        gY = 0;
        setPadTilt(null);
        setVirtualSpin(0);
        paintCursor();
      }, HOLD_TO_SHIFT_MS);
    };

    const enterMouseMode = (p: Pt) => {
      if (mouseLeft && mouseRight) {
        if (mode !== "shift") {
          mode = "shift";
          last = p;
          velX = 0;
          velY = 0;
          gX = 0;
          gY = 0;
          setVirtualSpin(0);
        }
        return;
      }
      if (mode === "shift") coastG();
      visual.shaking = false;
      if (mouseRight) {
        if (mode !== "yaw") {
          mode = "yaw";
          last = p;
          lastAngle = centerAngle(p, boardRect());
        }
        return;
      }
      if (mouseLeft && mode !== "tilt" && mode !== "yaw") {
        const pose = getPose();
        visual.tiltX = pose.tilt.x;
        visual.tiltY = -pose.tilt.y;
        mode = isCorner(p, boardRect()) ? "yaw" : "tilt";
        last = p;
        lastAngle = centerAngle(p, boardRect());
        if (mode === "tilt") setPadTilt(gameTilt());
      }
    };

    const noteMouseDown = (button: number, buttons: number, p: Pt) => {
      if (button === 0 || (buttons & 1)) mouseLeft = true;
      if (button === 2 || (buttons & 2)) mouseRight = true;
      enterMouseMode(p);
      paintCursor();
    };

    const noteMouseUp = (button: number, buttons: number, p: Pt) => {
      if (button === 0) mouseLeft = false;
      if (button === 2) mouseRight = false;
      if (!(buttons & 1)) mouseLeft = false;
      if (!(buttons & 2)) mouseRight = false;
      if (mouseLeft || mouseRight) enterMouseMode(p);
      paintCursor();
    };

    const applyG = (gx: number, gy: number, max = G_MAX) => {
      gX = clamp(gx, -max, max);
      gY = clamp(gy, -max, max);
      setPadAccel({ x: gX, y: gY, z: 0 });
      gPulseUntil = performance.now() + 80;
    };

    const shoveFromVel = (vx: number, vy: number) => {
      velX = velX * 0.45 + vx * 0.55;
      velY = velY * 0.45 + vy * 0.55;
      applyG(-velX / G_VEL, velY / G_VEL);
    };

    const coastG = (now = performance.now()) => {
      if (velX === 0 && velY === 0 && gX === 0 && gY === 0) return;
      const oldX = velX;
      const oldY = velY;
      velX *= 0.82;
      velY *= 0.82;
      const step = 1 / 60;
      const aSx = (velX - oldX) / step;
      const aSy = (velY - oldY) / step;
      // Small opposite kick as the board stops, plus leftover lean fading out.
      applyG(
        -velX / G_VEL * 0.15 - aSx / G_DECEL,
        velY / G_VEL * 0.15 + aSy / G_DECEL,
        G_MAX_DECEL,
      );
      if (Math.hypot(velX, velY) < 25 && Math.hypot(gX, gY) < 0.02) {
        velX = 0;
        velY = 0;
        gX = 0;
        gY = 0;
        setPadAccel(null);
        gPulseUntil = 0;
      }
      lastT = now;
    };

    const onDown = (ev: PointerEvent) => {
      if (ev.pointerType === "mouse" && ev.button === 1) return;

      if (ev.pointerType === "mouse" && (ev.button === 0 || ev.button === 2)) {
        ev.preventDefault();
        const p = { x: ev.clientX, y: ev.clientY };
        mouseId = ev.pointerId;
        last = p;
        lastT = performance.now();
        try {
          stage.setPointerCapture(ev.pointerId);
        } catch {
          // capture is a nicety
        }
        noteMouseDown(ev.button, ev.buttons, p);
        apply();
        return;
      }
      if (ev.pointerType === "mouse" && ev.button > 0) return;

      const now = performance.now();
      // Touch and pen use one pointer. Extra fingers have no simulator action.
      if (!ev.isPrimary || pointers.size > 0) return;
      pointers.clear();
      mode = "none";
      const p = { x: ev.clientX, y: ev.clientY };
      pointers.set(ev.pointerId, p);
      try {
        stage.setPointerCapture(ev.pointerId);
      } catch {
        // capture is a nicety
      }
      const r = boardRect();
      const pose = getPose();
      visual.tiltX = pose.tilt.x;
      visual.tiltY = -pose.tilt.y;
      mode = isCorner(p, r) ? "yaw" : "tilt";
      last = p;
      touchStart = p;
      lastAngle = centerAngle(p, r);
      lastT = now;
      if (mode === "tilt") setPadTilt(gameTilt());
      armHoldToShift(ev.pointerId);
      apply();
      paintCursor();
    };

    const onMove = (ev: PointerEvent) => {
      const p = { x: ev.clientX, y: ev.clientY };

      if (ev.pointerType === "mouse" && (mouseId !== null || mouseLeft || mouseRight)) {
        if (ev.buttons & 1) mouseLeft = true;
        if (ev.buttons & 2) mouseRight = true;
        enterMouseMode(p);
        if (mouseId === null && (mouseLeft || mouseRight)) mouseId = ev.pointerId;
        if (!mouseLeft && !mouseRight) {
          apply();
          return;
        }
        const now = performance.now();
        const dt = Math.max(0.008, (now - lastT) / 1000);
        const r = boardRect();
        if (mode === "shift" && last) {
          const dx = p.x - last.x;
          const dy = p.y - last.y;
          const lim = shiftLimit();
          visual.shiftX = clamp(visual.shiftX + dx, -lim.x, lim.x);
          visual.shiftY = clamp(visual.shiftY + dy, -lim.y, lim.y);
          if (Math.hypot(dx, dy) >= 0.75) shoveFromVel(dx / dt, dy / dt);
          last = p;
        } else if (mode === "yaw" && last) {
          visual.yaw += (p.x - last.x) * 0.12;
          setVirtualSpin(clamp((p.x - last.x) / dt / (r.width * 4.5)));
          last = p;
        } else if (mode === "tilt" && last) {
          visual.tiltX = clamp(visual.tiltX + (p.x - last.x) / (r.width * TILT_SCALE));
          visual.tiltY = clamp(visual.tiltY - (p.y - last.y) / (r.height * TILT_SCALE));
          last = p;
          setPadTilt(gameTilt());
          setVirtualSpin(0);
        }
        lastT = now;
        apply();
        return;
      }

      if (!pointers.has(ev.pointerId)) {
        hoverCorner = isCorner(p, boardRect());
        paintCursor();
        return;
      }
      pointers.set(ev.pointerId, p);
      const now = performance.now();
      const dt = Math.max(0.008, (now - lastT) / 1000);
      const r = boardRect();

      if (touchStart && Math.hypot(p.x - touchStart.x, p.y - touchStart.y) > HOLD_SLOP_PX) {
        cancelHold();
      }

      if (mode === "shift" && last) {
        const dx = p.x - last.x;
        const dy = p.y - last.y;
        const lim = shiftLimit();
        visual.shiftX = clamp(visual.shiftX + dx, -lim.x, lim.x);
        visual.shiftY = clamp(visual.shiftY + dy, -lim.y, lim.y);
        if (Math.hypot(dx, dy) >= 0.75) shoveFromVel(dx / dt, dy / dt);
        last = p;
      } else if (mode === "tilt" && last) {
        visual.tiltX = clamp(visual.tiltX + (p.x - last.x) / (r.width * TILT_SCALE));
        visual.tiltY = clamp(visual.tiltY - (p.y - last.y) / (r.height * TILT_SCALE));
        last = p;
        setPadTilt(gameTilt());
        setVirtualSpin(0);
      } else if (mode === "yaw") {
        const ang = centerAngle(p, r);
        const d = unwrap(ang - lastAngle);
        visual.yaw += (d * 180) / Math.PI;
        lastAngle = ang;
        setVirtualSpin(clamp(d / dt / SPIN_FULL));
        setPadTilt(gameTilt());
      }
      lastT = now;
      apply();
    };

    const endMouseIfIdle = (p: Pt) => {
      if (mouseLeft || mouseRight) {
        last = p;
        lastT = performance.now();
        enterMouseMode(p);
        paintCursor();
        return;
      }
      mouseId = null;
      mode = "none";
      last = null;
      setPadTilt(null);
      setVirtualSpin(0);
      coastG();
      visual.shaking = false;
      paintCursor();
    };

    const onMouseDown = (ev: MouseEvent) => {
      if (ev.button === 1) return;
      if (ev.button !== 0 && ev.button !== 2) return;
      ev.preventDefault();
      const p = { x: ev.clientX, y: ev.clientY };
      last = p;
      lastT = performance.now();
      noteMouseDown(ev.button, ev.buttons, p);
      apply();
    };

    const onMouseUp = (ev: MouseEvent) => {
      if (ev.button !== 0 && ev.button !== 2) return;
      const p = { x: ev.clientX, y: ev.clientY };
      noteMouseUp(ev.button, ev.buttons, p);
      endMouseIfIdle(p);
    };

    const onWinMouseMove = (ev: MouseEvent) => {
      if (!mouseLeft && !mouseRight && (ev.buttons & 3) === 0) return;
      const p = { x: ev.clientX, y: ev.clientY };
      if (ev.buttons & 1) mouseLeft = true;
      if (ev.buttons & 2) mouseRight = true;
      enterMouseMode(p);
      if (mode === "shift" && last) {
        const now = performance.now();
        const dt = Math.max(0.008, (now - lastT) / 1000);
        const dx = p.x - last.x;
        const dy = p.y - last.y;
        const lim = shiftLimit();
        visual.shiftX = clamp(visual.shiftX + dx, -lim.x, lim.x);
        visual.shiftY = clamp(visual.shiftY + dy, -lim.y, lim.y);
        if (Math.hypot(dx, dy) >= 0.75) shoveFromVel(dx / dt, dy / dt);
        last = p;
        lastT = now;
        apply();
      } else if (mode === "yaw" && last) {
        const now = performance.now();
        const dt = Math.max(0.008, (now - lastT) / 1000);
        const r = boardRect();
        visual.yaw += (p.x - last.x) * 0.12;
        setVirtualSpin(clamp((p.x - last.x) / dt / (r.width * 4.5)));
        last = p;
        lastT = now;
        apply();
      }
    };

    const release = (ev: PointerEvent) => {
      if (ev.pointerType === "mouse") {
        const p = { x: ev.clientX, y: ev.clientY };
        noteMouseUp(ev.button, ev.buttons, p);
        if (!mouseLeft && !mouseRight) {
          try {
            stage.releasePointerCapture(ev.pointerId);
          } catch {
            // already released
          }
        }
        endMouseIfIdle(p);
        return;
      }

      if (!pointers.has(ev.pointerId)) return;
      cancelHold();
      pointers.delete(ev.pointerId);
      try {
        stage.releasePointerCapture(ev.pointerId);
      } catch {
        // already released
      }
      if (pointers.size === 0) {
        mode = "none";
        last = null;
        touchStart = null;
        setPadTilt(null);
        setVirtualSpin(0);
        visual.shaking = false;
        coastG();
      }
      paintCursor();
    };

    const onLost = () => {
      cancelHold();
      pointers.clear();
      mouseLeft = false;
      mouseRight = false;
      mouseId = null;
      mode = "none";
      last = null;
      touchStart = null;
      setPadTilt(null);
      setVirtualSpin(0);
      setPadAccel(null);
      visual.shaking = false;
      paintCursor();
    };

    const stopGesture = (ev: Event) => ev.preventDefault();
    const onContext = (ev: Event) => ev.preventDefault();

    stage.addEventListener("pointerdown", onDown);
    stage.addEventListener("pointermove", onMove);
    stage.addEventListener("pointerup", release);
    stage.addEventListener("pointercancel", release);
    stage.addEventListener("mousedown", onMouseDown);
    stage.addEventListener("mouseup", onMouseUp);
    window.addEventListener("mousemove", onWinMouseMove);
    window.addEventListener("mouseup", onMouseUp);
    stage.addEventListener("contextmenu", onContext);
    stage.addEventListener("gesturestart", stopGesture);
    stage.addEventListener("gesturechange", stopGesture);
    window.addEventListener("blur", onLost);

    return () => {
      cancelAnimationFrame(raf);
      ro.disconnect();
      onLost();
      stage.removeEventListener("pointerdown", onDown);
      stage.removeEventListener("pointermove", onMove);
      stage.removeEventListener("pointerup", release);
      stage.removeEventListener("pointercancel", release);
      stage.removeEventListener("mousedown", onMouseDown);
      stage.removeEventListener("mouseup", onMouseUp);
      window.removeEventListener("mousemove", onWinMouseMove);
      window.removeEventListener("mouseup", onMouseUp);
      stage.removeEventListener("contextmenu", onContext);
      stage.removeEventListener("gesturestart", stopGesture);
      stage.removeEventListener("gesturechange", stopGesture);
      window.removeEventListener("blur", onLost);
    };
  }, [getPose, setPadAccel, setPadTilt, setVirtualSpin]);

  return (
    <div
      ref={stageRef}
      className="stage"
      data-mode="tilt"
      aria-label="tilt the panel"
    >
      <div ref={slotRef} className="board-slot">
        <div ref={shadowRef} className="board-shadow" />
        <div ref={rigRef} className="board-rig">
          <div className="board-box">
            <div className="board-face face-front">
              <MatrixDisplay registerCanvas={props.registerCanvas} />
            </div>
            <div className="board-face face-back" />
            <div className="board-face face-top" />
            <div className="board-face face-bottom" />
            <div className="board-face face-left" />
            <div className="board-face face-right" />
          </div>
        </div>
      </div>
    </div>
  );
}
