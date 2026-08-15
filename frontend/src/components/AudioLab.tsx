import { useCallback, useEffect, useRef, useState } from "react";
import { loadEmulator, SfxLibrary } from "../emulator/wasm";
import { SfxPatch } from "../audio/patch";
import { ensureAudio, playBuffer, previewPatch } from "../audio/engine";
import { decodePta, encodePta, PtaCodec, ptaToAudioBuffer, toMono } from "../audio/pta";
import {
  MUSIC_TRACK_NAMES,
  musicOverride,
  setMusicOverride,
  setMusicTrack,
  stopMusic,
} from "../audio/music";

const RATES = [8000, 11025, 16000, 22050];
const MUSIC_SLOTS = [1, 2, 3, 4, 5]; // pt::MusicTrack ids with actual audio

interface LoadedFile {
  name: string;
  bytes: number;
  duration: number;
  sampleRate: number;
  mono: Float32Array;
  buffer: AudioBuffer;
}

function fmtSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

/** MP3/WAV → PTA converter + the core's programmatic SFX patch browser. */
export function AudioLab() {
  const [lib, setLib] = useState<SfxLibrary | null>(null);
  const [libError, setLibError] = useState<string | null>(null);
  const [style, setStyle] = useState(0);

  const [file, setFile] = useState<LoadedFile | null>(null);
  const [fileError, setFileError] = useState<string | null>(null);
  const [rate, setRate] = useState(11025);
  const [codec, setCodec] = useState<PtaCodec>("adpcm");
  const [normalize, setNormalize] = useState(true);
  const [trim, setTrim] = useState(true);
  const [pta, setPta] = useState<Uint8Array | null>(null);
  const [playingWhat, setPlayingWhat] = useState<"src" | "pta" | null>(null);
  const [assignTo, setAssignTo] = useState(1);
  const [assignNote, setAssignNote] = useState<string | null>(null);
  const [overrides, setOverrides] = useState<number[]>([]);
  const [auditioning, setAuditioning] = useState(0);

  const stopHandle = useRef<(() => void) | null>(null);
  const waveCanvas = useRef<HTMLCanvasElement | null>(null);

  useEffect(() => {
    loadEmulator()
      .then((m) => setLib(m.sfxLibrary))
      .catch((e) => setLibError(String(e?.message ?? e)));
    refreshOverrides();
    return () => {
      stopHandle.current?.();
      stopMusic();
    };
  }, []);

  const refreshOverrides = () =>
    setOverrides(MUSIC_SLOTS.filter((t) => musicOverride(t) !== null));

  const stopPreview = useCallback(() => {
    stopHandle.current?.();
    stopHandle.current = null;
    setPlayingWhat(null);
  }, []);

  const onFile = async (f: File | undefined) => {
    if (!f) return;
    stopPreview();
    setFileError(null);
    setPta(null);
    const ctx = ensureAudio();
    if (!ctx) {
      setFileError("WEB AUDIO UNAVAILABLE IN THIS BROWSER");
      return;
    }
    try {
      const buffer = await ctx.decodeAudioData(await f.arrayBuffer());
      setFile({
        name: f.name,
        bytes: f.size,
        duration: buffer.duration,
        sampleRate: buffer.sampleRate,
        mono: toMono(buffer),
        buffer,
      });
    } catch {
      setFileError("COULD NOT DECODE — IS THIS AN AUDIO FILE?");
      setFile(null);
    }
  };

  // Re-encode whenever the source or a knob changes.
  useEffect(() => {
    if (!file) return;
    setPta(encodePta(file.mono, file.sampleRate, {
      sampleRate: rate,
      codec,
      normalize,
      trimSilence: trim,
    }));
  }, [file, rate, codec, normalize, trim]);

  // Waveform of the *converted* audio, so artifacts are visible.
  useEffect(() => {
    const canvas = waveCanvas.current;
    if (!canvas || !pta) return;
    const ctx2d = canvas.getContext("2d")!;
    const { width: w, height: h } = canvas;
    ctx2d.clearRect(0, 0, w, h);
    ctx2d.fillStyle = "rgba(255, 176, 0, 0.08)";
    ctx2d.fillRect(0, h / 2, w, 1);
    try {
      const { samples } = decodePta(pta);
      ctx2d.fillStyle = "#ffb000";
      const per = Math.max(1, Math.floor(samples.length / w));
      for (let x = 0; x < w; x++) {
        let lo = 0;
        let hi = 0;
        const base = x * per;
        for (let i = base; i < Math.min(base + per, samples.length); i++) {
          if (samples[i] < lo) lo = samples[i];
          if (samples[i] > hi) hi = samples[i];
        }
        const y0 = h / 2 - hi * (h / 2 - 2);
        const y1 = h / 2 - lo * (h / 2 - 2);
        ctx2d.fillRect(x, y0, 1, Math.max(1, y1 - y0));
      }
    } catch {
      // nothing to draw
    }
  }, [pta]);

  const playSource = () => {
    if (!file) return;
    stopPreview();
    ensureAudio();
    stopHandle.current = playBuffer(file.buffer, { bus: "master" });
    if (stopHandle.current) setPlayingWhat("src");
  };

  const playConverted = () => {
    if (!pta) return;
    stopPreview();
    const ctx = ensureAudio();
    if (!ctx) return;
    try {
      stopHandle.current = playBuffer(ptaToAudioBuffer(ctx, pta), { bus: "master" });
      if (stopHandle.current) setPlayingWhat("pta");
    } catch {
      setFileError("PTA DECODE FAILED");
    }
  };

  const download = () => {
    if (!pta || !file) return;
    const blob = new Blob([pta.buffer.slice(pta.byteOffset, pta.byteOffset + pta.byteLength)], {
      type: "application/octet-stream",
    });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = file.name.replace(/\.[^.]+$/, "") + ".pta";
    a.click();
    URL.revokeObjectURL(a.href);
  };

  const assign = () => {
    if (!pta) return;
    const ok = setMusicOverride(assignTo, pta);
    setAssignNote(
      ok
        ? `SAVED AS ${MUSIC_TRACK_NAMES[assignTo]} MUSIC`
        : "STORAGE FULL — TRY A LOWER RATE",
    );
    refreshOverrides();
  };

  const audition = (track: number) => {
    ensureAudio();
    if (auditioning === track) {
      stopMusic();
      setAuditioning(0);
    } else {
      setMusicTrack(track);
      setAuditioning(track);
    }
  };

  const kbps = pta && file ? (pta.length * 8) / Math.max(file.duration, 0.01) / 1000 : 0;

  return (
    <main className="lab-main">
      <section className="module lab-converter">
        <h2 className="module-title">MP3 → PTA CONVERTER</h2>
        <p className="lab-blurb">
          PTA IS THE CONSOLE'S NATIVE FORMAT: MONO, LOW-RATE, 4-BIT ADPCM — A ~40-LINE
          INTEGER DECODER THE ESP32 RUNS WITHOUT AN MP3 LIBRARY.
        </p>

        <label
          className="lab-drop"
          onDragOver={(e) => e.preventDefault()}
          onDrop={(e) => {
            e.preventDefault();
            onFile(e.dataTransfer.files?.[0]);
          }}
        >
          <input
            type="file"
            accept="audio/*,.mp3,.wav,.ogg,.m4a"
            onChange={(e) => onFile(e.target.files?.[0])}
          />
          {file ? (
            <span>
              {file.name} · {fmtSize(file.bytes)} · {file.duration.toFixed(1)}S ·{" "}
              {file.sampleRate} HZ
            </span>
          ) : (
            <span>DROP AN MP3 / WAV HERE OR CLICK TO BROWSE</span>
          )}
        </label>
        {fileError && <p className="lab-error">{fileError}</p>}

        <div className="lab-controls">
          <div className="lab-control">
            <span className="lab-label">SAMPLE RATE</span>
            <div className="lab-seg">
              {RATES.map((r) => (
                <button
                  key={r}
                  className={rate === r ? "on" : ""}
                  onClick={() => setRate(r)}
                >
                  {r >= 16000 ? `${r / 1000}K` : r}
                </button>
              ))}
            </div>
          </div>
          <div className="lab-control">
            <span className="lab-label">CODEC</span>
            <div className="lab-seg">
              <button className={codec === "adpcm" ? "on" : ""} onClick={() => setCodec("adpcm")}>
                ADPCM 4-BIT
              </button>
              <button className={codec === "pcm8" ? "on" : ""} onClick={() => setCodec("pcm8")}>
                PCM 8-BIT
              </button>
            </div>
          </div>
          <div className="lab-control">
            <span className="lab-label">CLEANUP</span>
            <div className="lab-seg">
              <button className={normalize ? "on" : ""} onClick={() => setNormalize(!normalize)}>
                NORMALIZE
              </button>
              <button className={trim ? "on" : ""} onClick={() => setTrim(!trim)}>
                TRIM SILENCE
              </button>
            </div>
          </div>
        </div>

        <canvas ref={waveCanvas} className="lab-wave" width={640} height={110} />

        {file && pta && (
          <div className="lab-stats">
            <div className="lab-stat">
              <span className="lab-stat-value">{fmtSize(pta.length)}</span>
              <span className="lab-stat-label">OUTPUT</span>
            </div>
            <div className="lab-stat">
              <span className="lab-stat-value">{kbps.toFixed(1)} KBPS</span>
              <span className="lab-stat-label">BITRATE</span>
            </div>
            <div className="lab-stat">
              <span className="lab-stat-value">
                {(file.bytes / Math.max(pta.length, 1)).toFixed(1)}×
              </span>
              <span className="lab-stat-label">SMALLER</span>
            </div>
          </div>
        )}

        <div className="lab-actions">
          <button disabled={!file} className={playingWhat === "src" ? "on" : ""} onClick={playSource}>
            ► SOURCE
          </button>
          <button disabled={!pta} className={playingWhat === "pta" ? "on" : ""} onClick={playConverted}>
            ► CONVERTED
          </button>
          <button disabled={!playingWhat} onClick={stopPreview}>
            ■ STOP
          </button>
          <button disabled={!pta} onClick={download}>
            ↓ .PTA
          </button>
        </div>

        <div className="lab-assign">
          <span className="lab-label">USE AS BACKGROUND MUSIC</span>
          <div className="lab-assign-row">
            <div className="lab-seg">
              {MUSIC_SLOTS.map((t) => (
                <button
                  key={t}
                  className={assignTo === t ? "on" : ""}
                  onClick={() => setAssignTo(t)}
                >
                  {MUSIC_TRACK_NAMES[t]}
                </button>
              ))}
            </div>
            <button className="lab-primary" disabled={!pta} onClick={assign}>
              ASSIGN
            </button>
          </div>
          {assignNote && <p className="lab-note">{assignNote}</p>}
          <p className="lab-blurb">
            ASSIGN AFFECTS THE BROWSER EMULATOR. FOR THE DEVICE: DOWNLOAD THE .PTA,
            SAVE IT AS assets/music/&lt;TRACK&gt;.pta IN THE REPO, THEN `NPM RUN FLASH`
            — THE BUILD EMBEDS IT INTO FIRMWARE FLASH.
          </p>
        </div>
      </section>

      <aside className="console">
        <div className="module">
          <h2 className="module-title">SFX LIBRARY</h2>
          {libError && <p className="lab-error">{libError}</p>}
          {lib && (
            <>
              <div className="lab-seg lab-styles">
                {lib.styles.map((s, i) => (
                  <button key={s} className={style === i ? "on" : ""} onClick={() => setStyle(i)}>
                    {s}
                  </button>
                ))}
              </div>
              <div className="sfx-grid">
                {lib.names.map((name, id) => (
                  <button
                    key={name}
                    className="sfx-chip"
                    onClick={() => previewPatch(lib.patch(style, id) as SfxPatch)}
                  >
                    ► {name}
                  </button>
                ))}
              </div>
              <p className="hint">PARAMETRIC PATCHES FROM THE C++ CORE — SAME ON DEVICE</p>
            </>
          )}
        </div>

        <div className="module">
          <h2 className="module-title">MUSIC TRACKS</h2>
          <div className="track-list">
            {MUSIC_SLOTS.map((t) => (
              <div key={t} className="track-row">
                <button
                  className={`sfx-chip ${auditioning === t ? "on" : ""}`}
                  onClick={() => audition(t)}
                >
                  {auditioning === t ? "■" : "►"} {MUSIC_TRACK_NAMES[t]}
                </button>
                {overrides.includes(t) ? (
                  <button
                    className="track-clear"
                    onClick={() => {
                      setMusicOverride(t, null);
                      refreshOverrides();
                    }}
                    title="Remove the assigned PTA and go back to the built-in tune"
                  >
                    PTA ✕
                  </button>
                ) : (
                  <span className="track-kind">CHIP</span>
                )}
              </div>
            ))}
          </div>
          <p className="hint">GAMES REQUEST A MOOD; TRACKS ARE CHIPTUNES OR YOUR PTA FILES</p>
        </div>
      </aside>
    </main>
  );
}
