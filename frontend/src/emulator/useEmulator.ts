import { useCallback, useEffect, useRef, useState } from "react";
import {
  BTN_CLICK,
  BTN_DOWN,
  BTN_UP,
  Emulator,
  loadEmulator,
  SCREEN_H,
  SCREEN_W,
} from "./wasm";
import {
  LED_CHROMA,
  LED_CORE_WHITE,
  makeLedMask,
  makePrintedPixelMasks,
  makeSquareMask,
  panelTables,
  type PixelStyle,
} from "./panel";
import {
  audioUnlocked,
  installAudioUnlock,
  playPatch,
  setMusicVolume,
  setSfxVolume,
} from "../audio/engine";
import { setMusicTrack, stopMusic } from "../audio/music";

// Keyboard contract (mirrors the hardware):
//   Arrow keys  -> tilt (the BNO08x on the device)
//   Q / E       -> twist the panel counter-/clockwise (yaw spin)
//   Space       -> shake the device (random linear-acceleration burst)
//   A / S / D   -> thumb wheel up / click / down (Enter also clicks)
//   Mouse wheel -> same as A / D; middle-click is S (hold for pause)
//   Right-drag  -> spin; left+right drag -> slide the panel
const BUTTON_KEYS: Record<string, number> = {
  KeyA: BTN_UP,
  KeyS: BTN_CLICK,
  KeyD: BTN_DOWN,
  Enter: BTN_CLICK,
};

const TILT_ATTACK = 6.5; // how fast held arrows ramp tilt (per second-ish)
const TILT_RELEASE = 9.0;
const SPIN_RATE = 3.0; // rad/s of twist while Q/E is held
const SHAKE_G = 1.3; // peak synthetic shake amplitude while Space is held, in g
const PHONE_TILT_DEG = 28;
const PHONE_ACCEL_DEADZONE_G = 0.035;
const PHONE_SHAKE_THRESHOLD_G = 0.5;
const PHONE_SENSOR_STALE_MS = 300;

type SensorPermission = "granted" | "denied";
type PermissionedEventConstructor = {
  requestPermission?: () => Promise<SensorPermission>;
};

function phoneDofsSupported(): boolean {
  if (typeof window === "undefined" || !window.isSecureContext) return false;
  const touchDevice = navigator.maxTouchPoints > 0 || window.matchMedia("(pointer: coarse)").matches;
  const hasMotion = typeof window.DeviceMotionEvent === "function";
  const hasOrientation = typeof window.DeviceOrientationEvent === "function";
  return touchDevice && (hasMotion || hasOrientation);
}

function screenAngle(): number {
  const modern = window.screen.orientation?.angle;
  const legacy = (window as Window & { orientation?: number }).orientation;
  return ((modern ?? legacy ?? 0) * Math.PI) / 180;
}

function toScreenAxes(x: number, y: number): { x: number; y: number } {
  const a = screenAngle();
  const cos = Math.cos(a);
  const sin = Math.sin(a);
  return { x: x * cos + y * sin, y: -x * sin + y * cos };
}

function deadzone(v: number, threshold: number): number {
  if (Math.abs(v) <= threshold) return 0;
  return v - Math.sign(v) * threshold;
}

function angleDelta(a: number, b: number): number {
  let d = a - b;
  while (d > 180) d -= 360;
  while (d < -180) d += 360;
  return d;
}

// ESP32 performance emulation: one desktop-WASM millisecond of tick time is
// treated as this many milliseconds on the 240 MHz ESP32-S3. Rough
// calibration — desktop JIT-compiled WASM runs roughly this much faster than
// the S3's in-order core on this kind of float-heavy code.
const ESP32_SLOWDOWN = 20;
const FRAME_MS = 1000 / 60;
const FRAME_DT = 1 / 60;
const MAX_CATCH_UP_STEPS = 5;
const MAX_FRAME_ELAPSED_MS = 100;
const DEADLINE_EPSILON_MS = 0.5;

// Settings + high scores persist in localStorage, mirroring the device's NVS.
const SAVE_KEY = "pixeltilt.save";
const VOL_KEY = "pixeltilt.vol";
const DEFAULT_CANVAS_SIZE = 640;
const PRINTED_CANVAS_SIZE = 1024;

function readHostVolume(): { sfx: number | null; music: number | null } {
  try {
    const raw = localStorage.getItem(VOL_KEY);
    if (!raw) return { sfx: null, music: null };
    const o = JSON.parse(raw) as { sfx?: unknown; music?: unknown };
    return {
      sfx: typeof o.sfx === "number" ? o.sfx : null,
      music: typeof o.music === "number" ? o.music : null,
    };
  } catch {
    return { sfx: null, music: null };
  }
}

function writeHostVolume(sfx: number | null, music: number | null) {
  try {
    localStorage.setItem(VOL_KEY, JSON.stringify({ sfx, music }));
  } catch {
    // storage unavailable
  }
}

function restoreSave(m: Emulator) {
  try {
    const b64 = localStorage.getItem(SAVE_KEY);
    if (b64) m.loadSave(Uint8Array.from(atob(b64), (ch) => ch.charCodeAt(0)));
  } catch {
    // ignore corrupt saves / storage being unavailable
  }
}

function persistSave(m: Emulator) {
  if (!m.saveDirty()) return;
  try {
    localStorage.setItem(SAVE_KEY, btoa(String.fromCharCode(...m.saveBlob())));
    m.clearSaveDirty();
  } catch {
    m.clearSaveDirty();
  }
}

export interface EmulatorPose {
  tilt: { x: number; y: number };
  /** Twist rate about the screen normal, rad/s (+ = clockwise). */
  spin: number;
  shaking: boolean;
}

export interface EmulatorState {
  ready: boolean;
  error: string | null;
  titles: string[];
  currentGame: number; // -1 = engine menu
  fps: number;
  paused: boolean;
  tilt: { x: number; y: number };
  /** Twist rate about the screen normal, rad/s (+ = clockwise). */
  spin: number;
  /** True while a shake is being applied — key, button, or a real phone. */
  shaking: boolean;
  buttons: number;
  /** False until the browser's autoplay gate is lifted by a click/keypress. */
  audioOn: boolean;
  /** Host-facing volumes, percent 0..100 (core setting, or a HUD override). */
  sfxVolume: number;
  musicVolume: number;
  /** ESP32 performance emulation: ticks paced on a simulated 240 MHz timeline. */
  esp32Perf: boolean;
  /** Simulated device frame rate while esp32Perf is on (60 = keeping up). */
  esp32Fps: number;
  /** Whether this touch device exposes orientation or motion sensors. */
  phoneDofsAvailable: boolean;
  /** Whether available phone sensors currently control the emulator. */
  phoneDofsEnabled: boolean;
}

export interface EmulatorControls {
  registerCanvas(main: HTMLCanvasElement | null): void;
  launch(i: number): void;
  exitToMenu(): void;
  reset(): void;
  setPaused(p: boolean): void;
  /** On-screen wheel buttons (mouse/touch). */
  setVirtualButton(mask: number, down: boolean): void;
  /** Drag pad override; pass null to release back to keyboard control. */
  setPadTilt(t: { x: number; y: number } | null): void;
  /** On-screen twist: -1 counter-clockwise, 0 off, +1 clockwise. */
  setVirtualSpin(dir: number): void;
  /** On-screen shake, held for as long as the button is down. */
  setVirtualShake(on: boolean): void;
  /** Linear accel in g (screen convention). Pass null to release. */
  setPadAccel(a: { x: number; y: number; z: number } | null): void;
  /** Sample the live pose (call from rAF — not React state). */
  getPose(): EmulatorPose;
  /** HUD volume sliders; persist and win over the in-game setting. */
  setHostSfxVolume(percent: number): void;
  setHostMusicVolume(percent: number): void;
  /** Toggle ESP32 performance emulation. */
  setEsp32Perf(on: boolean): void;
  /** Physical treatment used to shape each simulated emitter. */
  setPixelStyle(style: PixelStyle): void;
  /** Request sensor permission and use every phone DOF the browser exposes. */
  setPhoneDofsEnabled(on: boolean): Promise<void>;
}

export function useEmulator(): EmulatorState & EmulatorControls {
  const [ready, setReady] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [titles, setTitles] = useState<string[]>([]);
  const [currentGame, setCurrentGame] = useState(-1);
  const [fps, setFps] = useState(0);
  const [paused, setPausedState] = useState(false);
  const [tiltUi, setTiltUi] = useState({ x: 0, y: 0 });
  const [spinUi, setSpinUi] = useState(0);
  const [shakingUi, setShakingUi] = useState(false);
  const [buttonsUi, setButtonsUi] = useState(0);
  const [audioOn, setAudioOn] = useState(false);
  const [sfxVol, setSfxVol] = useState(() => readHostVolume().sfx ?? 80);
  const [musicVol, setMusicVol] = useState(() => readHostVolume().music ?? 60);
  const [esp32Perf, setEsp32PerfState] = useState(false);
  const [esp32Fps, setEsp32Fps] = useState(60);
  const [phoneDofsAvailable, setPhoneDofsAvailable] = useState(phoneDofsSupported);
  const [phoneDofsEnabled, setPhoneDofsEnabledState] = useState(false);

  const emu = useRef<Emulator | null>(null);
  const keys = useRef<Set<string>>(new Set());
  const virtualButtons = useRef(0);
  const wheelPendingUp = useRef(0);
  const wheelPendingDown = useRef(0);
  const wheelNeedRelease = useRef(false);
  const wheelClick = useRef(false);
  const wheelAcc = useRef(0);
  const virtualSpin = useRef(0);
  const virtualShake = useRef(false);
  const padTilt = useRef<{ x: number; y: number } | null>(null);
  const padAccel = useRef<{ x: number; y: number; z: number } | null>(null);
  const tilt = useRef({ x: 0, y: 0 });
  const spin = useRef(0);
  const poseRef = useRef<EmulatorPose>({ tilt: { x: 0, y: 0 }, spin: 0, shaking: false });
  const savedVol = readHostVolume();
  const hostSfx = useRef<number | null>(savedVol.sfx);
  const hostMusic = useRef<number | null>(savedVol.music);
  // Phone sensors stay off until the user enables them. The refs avoid a
  // render on every high-frequency orientation or motion sample.
  const phoneDofsEnabledRef = useRef(false);
  const phoneSources = useRef({ orientation: false, motion: false });
  const phoneTilt = useRef({ x: 0, y: 0, at: 0 });
  const phoneOrientationZero = useRef<{ x: number; y: number } | null>(null);
  const phoneMotion = useRef({ x: 0, y: 0, z: 0, at: 0 });
  const phoneSpin = useRef({ value: 0, at: 0 });
  const phoneYaw = useRef({ value: 0, at: 0 });
  const phoneRotationRateAt = useRef(0);
  // ESP32 performance emulation state (see ESP32_SLOWDOWN).
  const esp32Ref = useRef(false);
  const devBusyUntil = useRef(0); // host time when the simulated device frame ends
  const pendingDt = useRef(0); // real time accumulated since the last executed tick
  const fixedLagMs = useRef(0);
  const pausedRef = useRef(false);
  const sfxSerial = useRef(0);
  const musicSerial = useRef(0);
  const lastSfxVolume = useRef<number | null>(null);
  const lastMusicVolume = useRef<number | null>(null);
  const renderDirty = useRef(true);
  const audioDirty = useRef(true);
  const canvas = useRef<HTMLCanvasElement | null>(null);
  const pixelStyle = useRef<PixelStyle>("dots");

  const registerCanvas = useCallback((main: HTMLCanvasElement | null) => {
    if (canvas.current === main) return;
    canvas.current = main;
    if (main) {
      const size = pixelStyle.current === "printed" ? PRINTED_CANVAS_SIZE : DEFAULT_CANVAS_SIZE;
      main.width = main.height = size;
      renderDirty.current = true;
    }
  }, []);

  const launch = useCallback((i: number) => {
    emu.current?.launch(i);
    renderDirty.current = true;
    audioDirty.current = true;
  }, []);

  const exitToMenu = useCallback(() => {
    emu.current?.exitToMenu();
    renderDirty.current = true;
    audioDirty.current = true;
  }, []);

  const reset = useCallback(() => {
    const m = emu.current;
    if (!m) return;
    m.init(Date.now() & 0xffffffff);
    restoreSave(m); // reset restarts the engine, not the player's save
    sfxSerial.current = 0; // core serials restarted with the engine
    musicSerial.current = 0;
    lastSfxVolume.current = null;
    lastMusicVolume.current = null;
    fixedLagMs.current = 0;
    pendingDt.current = 0;
    devBusyUntil.current = 0;
    renderDirty.current = true;
    audioDirty.current = true;
    stopMusic();
  }, []);

  const setPaused = useCallback((p: boolean) => {
    pausedRef.current = p;
    fixedLagMs.current = 0;
    pendingDt.current = 0;
    devBusyUntil.current = 0;
    setPausedState(p);
  }, []);

  const setVirtualButton = useCallback((mask: number, down: boolean) => {
    if (down) virtualButtons.current |= mask;
    else virtualButtons.current &= ~mask;
  }, []);

  const setPadTilt = useCallback((t: { x: number; y: number } | null) => {
    padTilt.current = t;
  }, []);

  const setVirtualSpin = useCallback((dir: number) => {
    virtualSpin.current = Math.max(-1, Math.min(1, dir));
  }, []);

  const setVirtualShake = useCallback((on: boolean) => {
    virtualShake.current = on;
  }, []);

  const setPadAccel = useCallback((a: { x: number; y: number; z: number } | null) => {
    padAccel.current = a;
  }, []);

  const getPose = useCallback((): EmulatorPose => poseRef.current, []);

  const setHostSfxVolume = useCallback((percent: number) => {
    const n = Math.max(0, Math.min(100, Math.round(percent)));
    hostSfx.current = n;
    writeHostVolume(n, hostMusic.current);
    lastSfxVolume.current = n;
    setSfxVolume(n);
    setSfxVol(n);
  }, []);

  const setHostMusicVolume = useCallback((percent: number) => {
    const n = Math.max(0, Math.min(100, Math.round(percent)));
    hostMusic.current = n;
    writeHostVolume(hostSfx.current, n);
    lastMusicVolume.current = n;
    setMusicVolume(n);
    setMusicVol(n);
  }, []);

  const setPixelStyle = useCallback((style: PixelStyle) => {
    if (pixelStyle.current === style) return;
    pixelStyle.current = style;
    const main = canvas.current;
    if (main) {
      const size = style === "printed" ? PRINTED_CANVAS_SIZE : DEFAULT_CANVAS_SIZE;
      main.width = main.height = size;
    }
    renderDirty.current = true;
  }, []);

  const setPhoneDofsEnabled = useCallback(async (on: boolean) => {
    const resetSamples = () => {
      phoneOrientationZero.current = null;
      phoneTilt.current = { x: 0, y: 0, at: 0 };
      phoneMotion.current = { x: 0, y: 0, z: 0, at: 0 };
      phoneSpin.current = { value: 0, at: 0 };
      phoneYaw.current = { value: 0, at: 0 };
      phoneRotationRateAt.current = 0;
    };

    if (!on) {
      phoneDofsEnabledRef.current = false;
      phoneSources.current = { orientation: false, motion: false };
      resetSamples();
      setPhoneDofsEnabledState(false);
      return;
    }
    if (!phoneDofsAvailable) return;

    const ask = (supported: boolean, ctor: PermissionedEventConstructor | undefined) => {
      if (!supported || !ctor) return Promise.resolve(false);
      if (!ctor.requestPermission) return Promise.resolve(true);
      try {
        // Start every permission request in this click handler before awaiting.
        return ctor.requestPermission.call(ctor).then((result) => result === "granted", () => false);
      } catch {
        return Promise.resolve(false);
      }
    };

    const hasMotion = typeof window.DeviceMotionEvent === "function";
    const hasOrientation = typeof window.DeviceOrientationEvent === "function";
    const motionCtor = hasMotion
      ? (window.DeviceMotionEvent as typeof DeviceMotionEvent & PermissionedEventConstructor)
      : undefined;
    const orientationCtor = hasOrientation
      ? (window.DeviceOrientationEvent as typeof DeviceOrientationEvent & PermissionedEventConstructor)
      : undefined;
    const motionRequest = ask(hasMotion, motionCtor);
    const orientationRequest = ask(hasOrientation, orientationCtor);
    const [motionAllowed, orientationAllowed] = await Promise.all([
      motionRequest,
      orientationRequest,
    ]);

    if (!motionAllowed && !orientationAllowed) {
      phoneDofsEnabledRef.current = false;
      phoneSources.current = { orientation: false, motion: false };
      resetSamples();
      setPhoneDofsEnabledState(false);
      setPhoneDofsAvailable(false);
      return;
    }

    resetSamples();
    phoneSources.current = { orientation: orientationAllowed, motion: motionAllowed };
    phoneDofsEnabledRef.current = true;
    setPhoneDofsEnabledState(true);
  }, [phoneDofsAvailable]);

  const setEsp32Perf = useCallback((on: boolean) => {
    esp32Ref.current = on;
    fixedLagMs.current = 0;
    pendingDt.current = 0;
    devBusyUntil.current = 0;
    setEsp32PerfState(on);
  }, []);

  useEffect(() => {
    let cancelled = false;
    let raf = 0;

    // Two 64x64 staging buffers: the LED body (its color at the light level
    // the panel really emits) and the brighter die at the centre of a hard-
    // driven LED.
    const makeBuffer = () => {
      const c = document.createElement("canvas");
      c.width = SCREEN_W;
      c.height = SCREEN_H;
      return { canvas: c, ctx: c.getContext("2d")! };
    };
    const body = makeBuffer();
    const core = makeBuffer();
    const fringe = makeBuffer();
    const bodyImage = body.ctx.createImageData(SCREEN_W, SCREEN_H);
    const coreImage = core.ctx.createImageData(SCREEN_W, SCREEN_H);
    const fringeImage = fringe.ctx.createImageData(SCREEN_W, SCREEN_H);
    // Scratch at panel resolution: the core layer needs its own masking pass
    // before it can be added on top of the body layer.
    const scratch = document.createElement("canvas");
    let bodyMask: HTMLCanvasElement | null = null;
    let coreMask: HTMLCanvasElement | null = null;
    let spillMask: HTMLCanvasElement | null = null;
    let fringeMask: HTMLCanvasElement | null = null;
    let lastMaskStyle: PixelStyle | null = null;

    const onKey = (down: boolean) => (ev: KeyboardEvent) => {
      if (ev.repeat) return;
      const arrows = ["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "KeyQ", "KeyE", "Space"];
      if (arrows.includes(ev.code) || ev.code in BUTTON_KEYS) {
        ev.preventDefault();
        if (down) keys.current.add(ev.code);
        else keys.current.delete(ev.code);
      }
    };
    const keyDown = onKey(true);
    const keyUp = onKey(false);
    const onBlur = () => {
      keys.current.clear();
      wheelPendingUp.current = 0;
      wheelPendingDown.current = 0;
      wheelNeedRelease.current = false;
      wheelClick.current = false;
      wheelAcc.current = 0;
    };

    const wheelTargetIsChrome = (t: EventTarget | null) =>
      t instanceof Element && !!t.closest("input, select, textarea, option");

    const onWheel = (ev: WheelEvent) => {
      if (ev.ctrlKey || ev.metaKey) return;
      if (wheelTargetIsChrome(ev.target)) return;
      ev.preventDefault();
      if (ev.deltaMode === WheelEvent.DOM_DELTA_LINE) {
        const n = Math.max(1, Math.round(Math.abs(ev.deltaY)));
        if (ev.deltaY < 0) wheelPendingUp.current += n;
        else if (ev.deltaY > 0) wheelPendingDown.current += n;
      } else if (ev.deltaMode === WheelEvent.DOM_DELTA_PAGE) {
        if (ev.deltaY < 0) wheelPendingUp.current += 1;
        else if (ev.deltaY > 0) wheelPendingDown.current += 1;
      } else if (ev.deltaY !== 0) {
        // Pixel wheels / trackpads: one menu step per ~100px so a notch
        // (~120) is one step and a trackpad flick does not skip the list.
        wheelAcc.current += ev.deltaY;
        const unit = 100;
        while (wheelAcc.current >= unit) {
          wheelPendingDown.current++;
          wheelAcc.current -= unit;
        }
        while (wheelAcc.current <= -unit) {
          wheelPendingUp.current++;
          wheelAcc.current += unit;
        }
      }
    };

    const onMouseDown = (ev: MouseEvent) => {
      if (ev.button !== 1) return;
      if (wheelTargetIsChrome(ev.target)) return;
      ev.preventDefault();
      wheelClick.current = true;
    };
    const onMouseUp = (ev: MouseEvent) => {
      if (ev.button === 1) wheelClick.current = false;
    };
    const onAuxClick = (ev: MouseEvent) => {
      if (ev.button === 1) ev.preventDefault();
    };

    // Device motion supplies translation and rotation rate. Filter the raw
    // acceleration so ordinary hand tremor does not become an in-game shake.
    const onMotion = (ev: DeviceMotionEvent) => {
      if (!phoneDofsEnabledRef.current || !phoneSources.current.motion) return;
      const a = ev.acceleration;
      const rotation = ev.rotationRate;
      const previous = phoneMotion.current;
      const raw = a && a.x != null
        ? toScreenAxes(-a.x / 9.81, (a.y ?? 0) / 9.81)
        : { x: 0, y: 0 };
      const rawZ = a?.z == null ? 0 : -a.z / 9.81;
      const mix = 0.18;
      phoneMotion.current = {
        x: previous.x + (deadzone(raw.x, PHONE_ACCEL_DEADZONE_G) - previous.x) * mix,
        y: previous.y + (deadzone(raw.y, PHONE_ACCEL_DEADZONE_G) - previous.y) * mix,
        z: previous.z + (deadzone(rawZ, PHONE_ACCEL_DEADZONE_G) - previous.z) * mix,
        at: performance.now(),
      };
      if (rotation?.alpha != null) {
        const rawSpin = (rotation.alpha * Math.PI) / 180;
        const previousSpin = phoneSpin.current.value;
        phoneSpin.current = {
          value: previousSpin + (deadzone(rawSpin, 0.06) - previousSpin) * 0.24,
          at: performance.now(),
        };
        phoneRotationRateAt.current = performance.now();
      }
    };

    // Orientation provides the two tilt axes. Enabling the switch captures
    // the current holding angle as neutral, just like the hardware boot zero.
    const onOrientation = (ev: DeviceOrientationEvent) => {
      if (!phoneDofsEnabledRef.current || !phoneSources.current.orientation) return;
      if (ev.beta == null || ev.gamma == null) return;
      const sample = toScreenAxes(ev.gamma, ev.beta);
      if (!phoneOrientationZero.current) phoneOrientationZero.current = sample;
      const zero = phoneOrientationZero.current;
      const targetX = Math.max(-1, Math.min(1, angleDelta(sample.x, zero.x) / PHONE_TILT_DEG));
      const targetY = Math.max(-1, Math.min(1, angleDelta(sample.y, zero.y) / PHONE_TILT_DEG));
      const previous = phoneTilt.current;
      phoneTilt.current = {
        x: previous.x + (deadzone(targetX, 0.025) - previous.x) * 0.2,
        y: previous.y + (deadzone(targetY, 0.025) - previous.y) * 0.2,
        at: performance.now(),
      };

      if (ev.alpha != null) {
        const now = performance.now();
        const previousYaw = phoneYaw.current;
        const dt = (now - previousYaw.at) / 1000;
        // Some browsers omit rotationRate. Derive yaw from orientation there.
        if (
          previousYaw.at > 0 &&
          dt >= 0.008 &&
          dt <= 0.25 &&
          now - phoneRotationRateAt.current >= PHONE_SENSOR_STALE_MS
        ) {
          const rawSpin = (angleDelta(ev.alpha, previousYaw.value) * Math.PI) / 180 / dt;
          const previousSpin = phoneSpin.current.value;
          phoneSpin.current = {
            value: previousSpin + (deadzone(rawSpin, 0.06) - previousSpin) * 0.24,
            at: now,
          };
        }
        phoneYaw.current = { value: ev.alpha, at: now };
      }
    };

    const rezeroPhone = () => {
      phoneOrientationZero.current = null;
      phoneTilt.current = { x: 0, y: 0, at: 0 };
      phoneYaw.current = { value: 0, at: 0 };
    };

    window.addEventListener("keydown", keyDown);
    window.addEventListener("keyup", keyUp);
    window.addEventListener("blur", onBlur);
    window.addEventListener("wheel", onWheel, { passive: false });
    window.addEventListener("mousedown", onMouseDown);
    window.addEventListener("mouseup", onMouseUp);
    window.addEventListener("auxclick", onAuxClick);
    window.addEventListener("devicemotion", onMotion);
    window.addEventListener("deviceorientation", onOrientation);
    window.addEventListener("orientationchange", rezeroPhone);
    window.screen.orientation?.addEventListener("change", rezeroPhone);
    installAudioUnlock();

    let last = performance.now();
    let fpsAccum = 0;
    let fpsFrames = 0;
    let uiSync = 0;

    const frame = (now: number) => {
      const m = emu.current;
      if (!m) return;
      const elapsedMs = Math.min(Math.max(now - last, 0), MAX_FRAME_ELAPSED_MS);
      const dt = elapsedMs / 1000;
      last = now;

      // A direct panel drag wins over phone sensors, which win over keys.
      const k = keys.current;
      if (padTilt.current) {
        tilt.current = { ...padTilt.current };
      } else if (
        phoneDofsEnabledRef.current &&
        now - phoneTilt.current.at < PHONE_SENSOR_STALE_MS
      ) {
        tilt.current = { x: phoneTilt.current.x, y: phoneTilt.current.y };
      } else {
        const tx = (k.has("ArrowRight") ? 1 : 0) - (k.has("ArrowLeft") ? 1 : 0);
        const ty = (k.has("ArrowDown") ? 1 : 0) - (k.has("ArrowUp") ? 1 : 0);
        const rate = (target: number) => (target !== 0 ? TILT_ATTACK : TILT_RELEASE);
        tilt.current.x += (tx - tilt.current.x) * Math.min(1, rate(tx) * dt);
        tilt.current.y += (ty - tilt.current.y) * Math.min(1, rate(ty) * dt);
      }
      // Twist combines direct controls with the phone's screen-normal rate.
      const spinDir = Math.max(
        -1,
        Math.min(1, (k.has("KeyE") ? 1 : 0) - (k.has("KeyQ") ? 1 : 0) + virtualSpin.current),
      );
      const phoneSpinRate = phoneDofsEnabledRef.current &&
        now - phoneSpin.current.at < PHONE_SENSOR_STALE_MS
        ? phoneSpin.current.value
        : 0;
      const spinTarget = Math.max(-6, Math.min(6, spinDir * SPIN_RATE + phoneSpinRate));
      spin.current += (spinTarget - spin.current) * Math.min(1, 12 * dt);

      let buttons = virtualButtons.current;
      for (const code of k) buttons |= BUTTON_KEYS[code] ?? 0;
      if (wheelClick.current) buttons |= BTN_CLICK;

      // Space synthesizes a noisy burst. Panel shoves and opted-in phone
      // acceleration feed the same three translation axes.
      let ax = 0, ay = 0, az = 0;
      let phoneAcceleration = false;
      if (k.has("Space") || virtualShake.current) {
        ax = (Math.random() - 0.5) * 2 * SHAKE_G;
        ay = (Math.random() - 0.5) * 2 * SHAKE_G;
        az = (Math.random() - 0.5) * SHAKE_G;
      } else if (padAccel.current) {
        ({ x: ax, y: ay, z: az } = padAccel.current);
      } else if (
        phoneDofsEnabledRef.current &&
        now - phoneMotion.current.at < PHONE_SENSOR_STALE_MS
      ) {
        ({ x: ax, y: ay, z: az } = phoneMotion.current);
        phoneAcceleration = true;
      }
      const shaking = Math.abs(ax) + Math.abs(ay) + Math.abs(az) >
        (phoneAcceleration ? PHONE_SHAKE_THRESHOLD_G : 0.15);
      poseRef.current = { tilt: { x: tilt.current.x, y: tilt.current.y }, spin: spin.current, shaking };

      let tickCount = 0;
      const runTick = (tickDt: number) => {
        let tickButtons = buttons;
        // Scroll notches are edges, not holds: one justDown per queued step,
        // with a release tick in between so the next notch can fire again.
        if (wheelNeedRelease.current) {
          wheelNeedRelease.current = false;
        } else if (wheelPendingUp.current > 0) {
          tickButtons |= BTN_UP;
          wheelPendingUp.current--;
          wheelNeedRelease.current = true;
        } else if (wheelPendingDown.current > 0) {
          tickButtons |= BTN_DOWN;
          wheelPendingDown.current--;
          wheelNeedRelease.current = true;
        }
        m.setAccel(ax, ay, az);
        // The physics field is tilt + shake in one vector (raw specific
        // force, like the hardware IMU): full arrow press = vertical
        // (1 g), and Space/devicemotion noise rides on top.
        m.setGravity(tilt.current.x + ax, tilt.current.y + ay);
        m.tick(tickDt, tilt.current.x, tilt.current.y, spin.current, tickButtons);
        tickCount++;
        buttons = tickButtons;
      };

      if (pausedRef.current) {
        fixedLagMs.current = 0;
        pendingDt.current = 0;
        devBusyUntil.current = 0;
      } else if (esp32Ref.current) {
        fixedLagMs.current = 0;
        pendingDt.current = Math.min(0.1, pendingDt.current + dt);
        const due = devBusyUntil.current;
        if (due === 0 || now + DEADLINE_EPSILON_MS >= due) {
          const useDt = pendingDt.current;
          pendingDt.current = 0;
          const t0 = performance.now();
          runTick(useDt);
          const frameMs = Math.max(FRAME_MS, (performance.now() - t0) * ESP32_SLOWDOWN);
          // Advance from the previous deadline so a slightly late rAF does
          // not turn a 17 ms simulated frame into a 33 ms frame.
          const base = due !== 0 && now - due <= MAX_FRAME_ELAPSED_MS ? due : now;
          devBusyUntil.current = base + frameMs;
        }
      } else {
        pendingDt.current = 0;
        devBusyUntil.current = 0;
        fixedLagMs.current += elapsedMs;
        while (fixedLagMs.current >= FRAME_MS && tickCount < MAX_CATCH_UP_STEPS) {
          runTick(FRAME_DT);
          fixedLagMs.current -= FRAME_MS;
        }
        // Drop excess whole steps after a suspended tab or long stall. Keep
        // the fractional phase so ordinary pacing remains smooth.
        if (fixedLagMs.current >= FRAME_MS) fixedLagMs.current %= FRAME_MS;
      }

      // External controls can change audio while paused. Otherwise there is
      // nothing new to read unless the core executed a tick.
      if (tickCount > 0 || audioDirty.current) {
        const drained = m.drainSfx(sfxSerial.current);
        sfxSerial.current = drained.head;
        for (const p of drained.patches) playPatch(p);

        const sfxVolume = hostSfx.current ?? m.sfxVolume();
        if (sfxVolume !== lastSfxVolume.current) {
          lastSfxVolume.current = sfxVolume;
          setSfxVolume(sfxVolume);
        }
        const musicVolume = hostMusic.current ?? m.musicVolume();
        if (musicVolume !== lastMusicVolume.current) {
          lastMusicVolume.current = musicVolume;
          setMusicVolume(musicVolume);
        }
        const nextMusicSerial = m.musicSerial();
        if (nextMusicSerial !== musicSerial.current) {
          musicSerial.current = nextMusicSerial;
          setMusicTrack(m.musicTrack());
        }
        audioDirty.current = false;
      }

      const main = canvas.current;
      if (main && (tickCount > 0 || renderDirty.current)) {
      // Blit framebuffer -> 64x64 staging -> the LED grid, through the
      // panel's real color pipeline: CIE 1931 curve, 8-bit BCM duty, then the
      // brightness setting as an OE dim (see emulator/panel.ts). Dark codes
      // crush to black here exactly like they do on the hardware.
      const fb = m.framebuffer();
      const { emit, hot } = panelTables(m.brightness());
      const hardSquare = pixelStyle.current === "squares";
      const printedStyle = pixelStyle.current === "printed";
      const coreWhite = printedStyle ? 0.62 : LED_CORE_WHITE;
      const bp = bodyImage.data;
      const cp = coreImage.data;
      const fp = fringeImage.data;
      for (let i = 0, j = 0; i < fb.length; i += 3, j += 4) {
        let r = emit[fb[i]];
        let g = emit[fb[i + 1]];
        let b = emit[fb[i + 2]];
        let neutralSplit = 0;
        // Narrow-band primaries: pull the smallest channel down and rescale
        // so the brightest is unchanged — the panel's colors are purer than
        // sRGB can express, and washed-out mixes are the tell.
        const lo = r < g ? (r < b ? r : b) : g < b ? g : b;
        if (lo > 0) {
          const hi = r > g ? (r > b ? r : b) : g > b ? g : b;
          if (printedStyle && hi > 0) {
            neutralSplit = Math.max(0, (lo / hi - 0.28) / 0.72);
          }
          const sub = lo * LED_CHROMA;
          const k = hi / (hi - sub);
          r = (r - sub) * k;
          g = (g - sub) * k;
          b = (b - sub) * k;
        }
        if (neutralSplit > 0) {
          // Clear filament reads cool through most of a neutral pixel. A
          // separate warm fringe below restores the visible red die.
          r *= 1 - neutralSplit * 0.42;
          g *= 1 - neutralSplit * 0.22;
          b *= 1 - neutralSplit * 0.04;
        }
        bp[j] = r;
        bp[j + 1] = g;
        bp[j + 2] = b;
        bp[j + 3] = 255;
        // A hard-driven LED's die reads brighter and less saturated than its
        // rim; keyed off the brightest channel so the hue survives.
        const peak = fb[i] > fb[i + 1] ? fb[i] : fb[i + 1];
        const h = hot[fb[i + 2] > peak ? fb[i + 2] : peak];
        const w = (h / 255) * coreWhite;
        cp[j] = r + (255 - r) * w;
        cp[j + 1] = g + (255 - g) * w;
        cp[j + 2] = b + (255 - b) * w;
        cp[j + 3] = h;
        fp[j] = 255;
        fp[j + 1] = 42;
        fp[j + 2] = 8;
        fp[j + 3] = Math.round(h * neutralSplit * 0.82);
      }
      body.ctx.putImageData(bodyImage, 0, 0);
      core.ctx.putImageData(coreImage, 0, 0);
      fringe.ctx.putImageData(fringeImage, 0, 0);

        const size = main.width;
        const ctx = main.getContext("2d")!;
        if (!bodyMask || bodyMask.width !== size || lastMaskStyle !== pixelStyle.current) {
          lastMaskStyle = pixelStyle.current;
          if (hardSquare) {
            bodyMask = makeSquareMask(size, SCREEN_W, 0.82);
            coreMask = null;
            spillMask = null;
            fringeMask = null;
          } else if (printedStyle) {
            const printedMasks = makePrintedPixelMasks(size, SCREEN_W);
            bodyMask = printedMasks.body;
            coreMask = printedMasks.core;
            spillMask = printedMasks.spill;
            fringeMask = printedMasks.fringe;
          } else {
            bodyMask = makeLedMask(size, SCREEN_W, 0.22, 0.44);
            coreMask = makeLedMask(size, SCREEN_W, 0.06, 0.26);
            spillMask = null;
            fringeMask = null;
          }
          scratch.width = scratch.height = size;
        }
        ctx.clearRect(0, 0, size, size);
        ctx.imageSmoothingEnabled = false;
        ctx.drawImage(body.canvas, 0, 0, size, size);
        ctx.globalCompositeOperation = "destination-in";
        ctx.drawImage(bodyMask, 0, 0);
        ctx.globalCompositeOperation = "source-over";
        if (spillMask) {
          // Low colored flare through the clear diffuser and onto its walls.
          const sctx = scratch.getContext("2d")!;
          sctx.globalCompositeOperation = "source-over";
          sctx.clearRect(0, 0, size, size);
          sctx.imageSmoothingEnabled = false;
          sctx.drawImage(body.canvas, 0, 0, size, size);
          sctx.globalCompositeOperation = "destination-in";
          sctx.drawImage(spillMask, 0, 0);
          ctx.globalCompositeOperation = "lighter";
          ctx.globalAlpha = 0.2;
          ctx.drawImage(scratch, 0, 0);
          ctx.globalAlpha = 1;
          ctx.globalCompositeOperation = "source-over";
        }
        if (fringeMask) {
          const sctx = scratch.getContext("2d")!;
          sctx.globalCompositeOperation = "source-over";
          sctx.clearRect(0, 0, size, size);
          sctx.imageSmoothingEnabled = false;
          sctx.drawImage(fringe.canvas, 0, 0, size, size);
          sctx.globalCompositeOperation = "destination-in";
          sctx.drawImage(fringeMask, 0, 0);
          ctx.globalCompositeOperation = "lighter";
          ctx.drawImage(scratch, 0, 0);
          ctx.globalCompositeOperation = "source-over";
        }
        if (coreMask) {
          // Die on top, added rather than painted so it reads as light.
          const sctx = scratch.getContext("2d")!;
          sctx.globalCompositeOperation = "source-over";
          sctx.clearRect(0, 0, size, size);
          sctx.imageSmoothingEnabled = false;
          sctx.drawImage(core.canvas, 0, 0, size, size);
          sctx.globalCompositeOperation = "destination-in";
          sctx.drawImage(coreMask, 0, 0);
          ctx.globalCompositeOperation = "lighter";
          ctx.drawImage(scratch, 0, 0);
          ctx.globalCompositeOperation = "source-over";
        }
        renderDirty.current = false;
      }
      // Cheap UI sync ~10 Hz so React isn't re-rendering at 60 fps.
      fpsAccum += dt;
      fpsFrames += tickCount;
      uiSync += dt;
      if (uiSync > 0.1) {
        uiSync = 0;
        persistSave(m);
        setCurrentGame(m.currentGame());
        setTiltUi({ x: tilt.current.x, y: tilt.current.y });
        setSpinUi(spin.current);
        setShakingUi(shaking);
        setButtonsUi(buttons);
        setAudioOn(audioUnlocked());
        const sv = hostSfx.current ?? m.sfxVolume();
        const mv = hostMusic.current ?? m.musicVolume();
        setSfxVol((cur) => (cur === sv ? cur : sv));
        setMusicVol((cur) => (cur === mv ? cur : mv));
        if (fpsAccum > 0) {
          const measuredFps = Math.round(fpsFrames / fpsAccum);
          setFps(measuredFps);
          setEsp32Fps(measuredFps);
        }
        fpsAccum = 0;
        fpsFrames = 0;
      }

      raf = requestAnimationFrame(frame);
    };

    loadEmulator()
      .then((m) => {
        if (cancelled) return;
        emu.current = m;
        m.init(Date.now() & 0xffffffff);
        restoreSave(m);
        renderDirty.current = true;
        audioDirty.current = true;
        setTitles(m.titles);
        setReady(true);
        last = performance.now();
        raf = requestAnimationFrame(frame);
      })
      .catch((e) => setError(String(e?.message ?? e)));

    return () => {
      cancelled = true;
      cancelAnimationFrame(raf);
      stopMusic();
      window.removeEventListener("keydown", keyDown);
      window.removeEventListener("keyup", keyUp);
      window.removeEventListener("blur", onBlur);
      window.removeEventListener("wheel", onWheel);
      window.removeEventListener("mousedown", onMouseDown);
      window.removeEventListener("mouseup", onMouseUp);
      window.removeEventListener("auxclick", onAuxClick);
      window.removeEventListener("devicemotion", onMotion);
      window.removeEventListener("deviceorientation", onOrientation);
      window.removeEventListener("orientationchange", rezeroPhone);
      window.screen.orientation?.removeEventListener("change", rezeroPhone);
    };
  }, []);

  return {
    ready,
    error,
    titles,
    currentGame,
    fps,
    paused,
    tilt: tiltUi,
    spin: spinUi,
    shaking: shakingUi,
    buttons: buttonsUi,
    audioOn,
    sfxVolume: sfxVol,
    musicVolume: musicVol,
    esp32Perf,
    esp32Fps,
    phoneDofsAvailable,
    phoneDofsEnabled,
    registerCanvas,
    launch,
    exitToMenu,
    reset,
    setPaused,
    setVirtualButton,
    setPadTilt,
    setVirtualSpin,
    setVirtualShake,
    setPadAccel,
    getPose,
    setHostSfxVolume,
    setHostMusicVolume,
    setEsp32Perf,
    setPixelStyle,
    setPhoneDofsEnabled,
  };
}
