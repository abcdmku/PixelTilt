import { useEmulator } from "../emulator/useEmulator";
import { MatrixDisplay } from "./MatrixDisplay";
import { TiltPad } from "./TiltPad";
import { WheelButtons } from "./WheelButtons";
import { GamePicker } from "./GamePicker";

/** The emulator workbench: pick a game, play it, tilt it. One view, no
 *  scrolling — the panel gets whatever height the chrome leaves over.
 *  Mounting/unmounting starts/stops the emulator. */
export function Bench() {
  const emu = useEmulator();

  if (emu.error) {
    return (
      <div className="fault">
        <h2>NO WASM IMAGE</h2>
        <p>{emu.error}</p>
        <code>npm run wasm</code>
      </div>
    );
  }

  return (
    <main className="bench-main">
      <div className="toolbar">
        <GamePicker
          titles={emu.titles}
          currentGame={emu.currentGame}
          launch={emu.launch}
          exitToMenu={emu.exitToMenu}
        />
        <button
          className="tb"
          onClick={() => emu.setPaused(!emu.paused)}
          title={emu.paused ? "Resume" : "Pause"}
          aria-label={emu.paused ? "resume" : "pause"}
        >
          <span className="tb-glyph">{emu.paused ? "▶" : "❚❚"}</span>
          <span className="tb-text">{emu.paused ? "RESUME" : "PAUSE"}</span>
        </button>
        <button
          className="tb"
          onClick={emu.reset}
          title="Restart the engine (scores and settings are kept)"
          aria-label="reset"
        >
          <span className="tb-glyph">↺</span>
          <span className="tb-text">RESET</span>
        </button>
      </div>

      <div className="play">
        <MatrixDisplay registerCanvas={emu.registerCanvas} />
      </div>

      <div className="pad-col">
        <div className="pad-block">
          <TiltPad tilt={emu.tilt} setPadTilt={emu.setPadTilt} />
          <span className="cap">TILT</span>
        </div>
        <div className="pad-block">
          <WheelButtons buttons={emu.buttons} setVirtualButton={emu.setVirtualButton} />
          <span className="cap">WHEEL</span>
        </div>
      </div>

      <div className="statusbar">
        <p className="keys">
          <span><kbd>←</kbd><kbd>→</kbd><kbd>↑</kbd><kbd>↓</kbd> TILT</span>
          <span><kbd>A</kbd><kbd>S</kbd><kbd>D</kbd> WHEEL</span>
          <span>HOLD <kbd>S</kbd> PAUSE MENU</span>
          <span><kbd>Q</kbd><kbd>E</kbd> SPIN</span>
          <span><kbd>SPACE</kbd> SHAKE</span>
        </p>
        {!emu.audioOn && <span className="nudge">TAP FOR SOUND</span>}
        <button
          className={`chip ${emu.esp32Perf ? "on" : ""}`}
          onClick={() => emu.setEsp32Perf(!emu.esp32Perf)}
          title="Pace the simulation like a 240 MHz ESP32-S3: heavy frames stretch and drop exactly as they would on the device"
        >
          ESP32 SPEED
        </button>
        <span className="status">
          <span className={`led ${emu.ready && !emu.paused ? "on" : ""}`} />
          {emu.ready ? `${emu.esp32Perf ? emu.esp32Fps : emu.fps} FPS` : "BOOTING"}
        </span>
      </div>
    </main>
  );
}
