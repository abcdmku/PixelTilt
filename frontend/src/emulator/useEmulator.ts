import { useEffect, useRef, useState } from "react";
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

// ESP32 performance emulation: one desktop-WASM millisecond of tick time is
// treated as this many milliseconds on the 240 MHz ESP32-S3. Rough
// calibration — desktop JIT-compiled WASM runs roughly this much faster than
// the S3's in-order core on this kind of float-heavy code.
const ESP32_SLOWDOWN = 20;

// Settings + high scores persist in localStorage, mirroring the device's NVS.
const SAVE_KEY = "pixeltilt.save";

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

export interface EmulatorState {
  ready: boolean;
  error: string | null;
  titles: string[];
  currentGame: number; // -1 = engine menu
  fps: number;
  paused: boolean;
  tilt: { x: number; y: number };
  buttons: number;
  /** False until the browser's autoplay gate is lifted by a click/keypress. */
  audioOn: boolean;
  /** Device settings volumes, percent (set inside the emulated settings menu). */
  sfxVolume: number;
  musicVolume: number;
  /** ESP32 performance emulation: ticks paced on a simulated 240 MHz timeline. */
  esp32Perf: boolean;
  /** Simulated device frame rate while esp32Perf is on (60 = keeping up). */
  esp32Fps: number;
}

export interface EmulatorControls {
  registerCanvases(main: HTMLCanvasElement | null, glow: HTMLCanvasElement | null): void;
  launch(i: number): void;
  exitToMenu(): void;
  reset(): void;
  setPaused(p: boolean): void;
  /** On-screen wheel buttons (mouse/touch). */
  setVirtualButton(mask: number, down: boolean): void;
  /** Drag pad override; pass null to release back to keyboard control. */
  setPadTilt(t: { x: number; y: number } | null): void;
  /** Toggle ESP32 performance emulation. */
  setEsp32Perf(on: boolean): void;
}

export function useEmulator(): EmulatorState & EmulatorControls {
  const [ready, setReady] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [titles, setTitles] = useState<string[]>([]);
  const [currentGame, setCurrentGame] = useState(-1);
  const [fps, setFps] = useState(0);
  const [paused, setPausedState] = useState(false);
  const [tiltUi, setTiltUi] = useState({ x: 0, y: 0 });
  const [buttonsUi, setButtonsUi] = useState(0);
  const [audioOn, setAudioOn] = useState(false);
  const [volumesUi, setVolumesUi] = useState({ sfx: 80, music: 60 });
  const [esp32Perf, setEsp32PerfState] = useState(false);
  const [esp32Fps, setEsp32Fps] = useState(60);

  const emu = useRef<Emulator | null>(null);
  const keys = useRef<Set<string>>(new Set());
  const virtualButtons = useRef(0);
  const padTilt = useRef<{ x: number; y: number } | null>(null);
  const tilt = useRef({ x: 0, y: 0 });
  const spin = useRef(0);
  // Real device motion (phones): pseudo-force in g, screen axes; stamped so a
  // stalled event stream decays to zero instead of sticking.
  const motion = useRef({ x: 0, y: 0, z: 0, at: 0 });
  // ESP32 performance emulation state (see ESP32_SLOWDOWN).
  const esp32Ref = useRef(false);
  const devBusyUntil = useRef(0); // host time when the simulated device frame ends
  const pendingDt = useRef(0); // real time accumulated since the last executed tick
  const devMs = useRef(1000 / 60); // smoothed simulated frame duration
  const pausedRef = useRef(false);
  const sfxSerial = useRef(0);
  const musicSerial = useRef(0);
  const canvases = useRef<{ main: HTMLCanvasElement | null; glow: HTMLCanvasElement | null }>({
    main: null,
    glow: null,
  });

  useEffect(() => {
    let cancelled = false;
    let raf = 0;

    const offscreen = document.createElement("canvas");
    offscreen.width = SCREEN_W;
    offscreen.height = SCREEN_H;
    const offCtx = offscreen.getContext("2d")!;
    const image = offCtx.createImageData(SCREEN_W, SCREEN_H);
    let dotMask: HTMLCanvasElement | null = null;

    const makeDotMask = (size: number) => {
      const mask = document.createElement("canvas");
      mask.width = mask.height = size;
      const ctx = mask.getContext("2d")!;
      const cell = size / SCREEN_W;
      ctx.fillStyle = "#fff";
      for (let y = 0; y < SCREEN_H; y++) {
        for (let x = 0; x < SCREEN_W; x++) {
          ctx.beginPath();
          ctx.arc((x + 0.5) * cell, (y + 0.5) * cell, cell * 0.42, 0, Math.PI * 2);
          ctx.fill();
        }
      }
      return mask;
    };

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
    const onBlur = () => keys.current.clear();

    // Phones: devicemotion's `acceleration` is the device's kinematic
    // acceleration a; the pseudo-force a loose object feels is -a. Map device
    // axes (x right, y toward top of screen) onto screen axes (x right,
    // y toward the player) and convert m/s^2 -> g.
    const onMotion = (ev: DeviceMotionEvent) => {
      const a = ev.acceleration;
      if (!a || a.x == null) return;
      motion.current = {
        x: -a.x / 9.81,
        y: (a.y ?? 0) / 9.81,
        z: -(a.z ?? 0) / 9.81,
        at: performance.now(),
      };
    };

    window.addEventListener("keydown", keyDown);
    window.addEventListener("keyup", keyUp);
    window.addEventListener("blur", onBlur);
    window.addEventListener("devicemotion", onMotion);
    installAudioUnlock();

    let last = performance.now();
    let fpsAccum = 0;
    let fpsFrames = 0;
    let uiSync = 0;

    const frame = (now: number) => {
      const m = emu.current;
      if (!m) return;
      const dt = Math.min((now - last) / 1000, 0.1);
      last = now;

      // Smooth keyboard tilt toward its target; a pad drag overrides it.
      const k = keys.current;
      if (padTilt.current) {
        tilt.current = { ...padTilt.current };
      } else {
        const tx = (k.has("ArrowRight") ? 1 : 0) - (k.has("ArrowLeft") ? 1 : 0);
        const ty = (k.has("ArrowDown") ? 1 : 0) - (k.has("ArrowUp") ? 1 : 0);
        const rate = (target: number) => (target !== 0 ? TILT_ATTACK : TILT_RELEASE);
        tilt.current.x += (tx - tilt.current.x) * Math.min(1, rate(tx) * dt);
        tilt.current.y += (ty - tilt.current.y) * Math.min(1, rate(ty) * dt);
      }
      const spinTarget =
        ((k.has("KeyE") ? 1 : 0) - (k.has("KeyQ") ? 1 : 0)) * SPIN_RATE;
      spin.current += (spinTarget - spin.current) * Math.min(1, 12 * dt);

      let buttons = virtualButtons.current;
      for (const code of k) buttons |= BUTTON_KEYS[code] ?? 0;

      // Shake: held Space synthesizes a noisy burst; otherwise pass through
      // real device motion (zeroed once the event stream goes stale).
      let ax = 0, ay = 0, az = 0;
      if (k.has("Space")) {
        ax = (Math.random() - 0.5) * 2 * SHAKE_G;
        ay = (Math.random() - 0.5) * 2 * SHAKE_G;
        az = (Math.random() - 0.5) * SHAKE_G;
      } else if (now - motion.current.at < 250) {
        ({ x: ax, y: ay, z: az } = motion.current);
      }

      if (!pausedRef.current) {
        // ESP32 performance emulation: each tick's real WASM cost is scaled
        // to a simulated 240 MHz duration; while that simulated frame is
        // still "running" no further ticks execute, and the next tick sees
        // the stretched dt — reproducing device lag (dropped frames AND the
        // larger timesteps the game would experience) in real time.
        pendingDt.current += dt;
        if (!esp32Ref.current || now >= devBusyUntil.current) {
          const useDt = Math.min(pendingDt.current, 0.1);
          pendingDt.current = 0;
          m.setAccel(ax, ay, az);
          // The physics field is tilt + shake in one vector (raw specific
          // force, like the hardware IMU): full arrow press = vertical
          // (1 g), and Space/devicemotion noise rides on top — swinging it
          // past 1 g is what lifts a pile off the floor mid-shake.
          m.setGravity(tilt.current.x + ax, tilt.current.y + ay);
          const t0 = performance.now();
          m.tick(useDt, tilt.current.x, tilt.current.y, spin.current, buttons);
          if (esp32Ref.current) {
            const frameMs = Math.max(1000 / 60, (performance.now() - t0) * ESP32_SLOWDOWN);
            devBusyUntil.current = now + frameMs;
            devMs.current += (frameMs - devMs.current) * 0.15;
          } else {
            devMs.current = 1000 / 60;
          }
        }
      } else {
        pendingDt.current = 0;
      }

      // Audio: drain the core's SFX ring every frame (latency matters), keep
      // the bus volumes in sync with the device settings, and follow the
      // engine's music-track requests.
      const drained = m.drainSfx(sfxSerial.current);
      sfxSerial.current = drained.head;
      for (const p of drained.patches) playPatch(p);
      setSfxVolume(m.sfxVolume());
      setMusicVolume(m.musicVolume());
      if (m.musicSerial() !== musicSerial.current) {
        musicSerial.current = m.musicSerial();
        setMusicTrack(m.musicTrack());
      }

      // Blit framebuffer -> offscreen 64x64 -> scaled canvases. Brightness is
      // applied here, like the panel PWM on the device.
      const fb = m.framebuffer();
      const px = image.data;
      const br = m.brightness() / 100;
      for (let i = 0, j = 0; i < fb.length; i += 3, j += 4) {
        px[j] = fb[i] * br;
        px[j + 1] = fb[i + 1] * br;
        px[j + 2] = fb[i + 2] * br;
        px[j + 3] = 255;
      }
      offCtx.putImageData(image, 0, 0);

      const { main, glow } = canvases.current;
      if (main) {
        const ctx = main.getContext("2d")!;
        if (!dotMask || dotMask.width !== main.width) dotMask = makeDotMask(main.width);
        ctx.clearRect(0, 0, main.width, main.height);
        ctx.imageSmoothingEnabled = false;
        ctx.drawImage(offscreen, 0, 0, main.width, main.height);
        ctx.globalCompositeOperation = "destination-in";
        ctx.drawImage(dotMask, 0, 0);
        ctx.globalCompositeOperation = "source-over";
      }
      if (glow) {
        const gtx = glow.getContext("2d")!;
        gtx.imageSmoothingEnabled = true;
        gtx.clearRect(0, 0, glow.width, glow.height);
        gtx.drawImage(offscreen, 0, 0, glow.width, glow.height);
      }

      // Cheap UI sync ~10 Hz so React isn't re-rendering at 60 fps.
      fpsAccum += dt;
      fpsFrames++;
      uiSync += dt;
      if (uiSync > 0.1) {
        uiSync = 0;
        persistSave(m);
        setCurrentGame(m.currentGame());
        setTiltUi({ x: tilt.current.x, y: tilt.current.y });
        setButtonsUi(buttons);
        setAudioOn(audioUnlocked());
        setEsp32Fps(Math.round(1000 / devMs.current));
        setVolumesUi((v) => {
          const sfx = m.sfxVolume();
          const music = m.musicVolume();
          return v.sfx === sfx && v.music === music ? v : { sfx, music };
        });
        if (fpsAccum > 0) setFps(Math.round(fpsFrames / fpsAccum));
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
      window.removeEventListener("devicemotion", onMotion);
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
    buttons: buttonsUi,
    audioOn,
    sfxVolume: volumesUi.sfx,
    musicVolume: volumesUi.music,
    esp32Perf,
    esp32Fps,
    registerCanvases: (main, glow) => {
      canvases.current = { main, glow };
    },
    launch: (i) => emu.current?.launch(i),
    exitToMenu: () => emu.current?.exitToMenu(),
    reset: () => {
      const m = emu.current;
      if (!m) return;
      m.init(Date.now() & 0xffffffff);
      restoreSave(m); // reset restarts the engine, not the player's save
      sfxSerial.current = 0; // core serials restarted with the engine
      musicSerial.current = 0;
      stopMusic();
    },
    setPaused: (p) => {
      pausedRef.current = p;
      setPausedState(p);
    },
    setVirtualButton: (mask, down) => {
      if (down) virtualButtons.current |= mask;
      else virtualButtons.current &= ~mask;
    },
    setPadTilt: (t) => {
      padTilt.current = t;
      if (!t) tilt.current = { ...tilt.current }; // springs back via release rate
    },
    setEsp32Perf: (on) => {
      esp32Ref.current = on;
      devBusyUntil.current = 0;
      devMs.current = 1000 / 60;
      setEsp32PerfState(on);
    },
  };
}
