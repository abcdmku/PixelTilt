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

  const emu = useRef<Emulator | null>(null);
  const keys = useRef<Set<string>>(new Set());
  const virtualButtons = useRef(0);
  const padTilt = useRef<{ x: number; y: number } | null>(null);
  const tilt = useRef({ x: 0, y: 0 });
  const spin = useRef(0);
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
      const arrows = ["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "KeyQ", "KeyE"];
      if (arrows.includes(ev.code) || ev.code in BUTTON_KEYS) {
        ev.preventDefault();
        if (down) keys.current.add(ev.code);
        else keys.current.delete(ev.code);
      }
    };
    const keyDown = onKey(true);
    const keyUp = onKey(false);
    const onBlur = () => keys.current.clear();

    window.addEventListener("keydown", keyDown);
    window.addEventListener("keyup", keyUp);
    window.addEventListener("blur", onBlur);
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

      if (!pausedRef.current) {
        m.tick(dt, tilt.current.x, tilt.current.y, spin.current, buttons);
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
  };
}
