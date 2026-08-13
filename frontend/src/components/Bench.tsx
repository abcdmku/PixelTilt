import { useEmulator } from "../emulator/useEmulator";
import { MatrixDisplay } from "./MatrixDisplay";
import { TiltPad } from "./TiltPad";
import { WheelButtons } from "./WheelButtons";
import { GameList } from "./GameList";

function VolumeBar({ label, percent }: { label: string; percent: number }) {
  const cells = 5;
  const lit = Math.round((percent / 100) * cells);
  return (
    <div className="volume-row">
      <span className="volume-label">{label}</span>
      <span className="volume-cells">
        {Array.from({ length: cells }, (_, i) => (
          <span key={i} className={`volume-cell ${i < lit ? "lit" : ""}`} />
        ))}
      </span>
      <span className="volume-value">{percent}%</span>
    </div>
  );
}

/** The emulator workbench — everything that was the whole app before the
 *  Audio Lab page existed. Mounting/unmounting starts/stops the emulator. */
export function Bench() {
  const emu = useEmulator();

  return (
    <>
      {emu.error ? (
        <div className="fault">
          <h2>NO WASM IMAGE</h2>
          <p>{emu.error}</p>
          <code>npm run wasm</code>
        </div>
      ) : (
        <main className="bench-main">
          <section className="stage">
            <MatrixDisplay registerCanvas={emu.registerCanvas} />
            <div className="transport">
              <button onClick={() => emu.setPaused(!emu.paused)}>
                {emu.paused ? "RESUME" : "PAUSE"}
              </button>
              <button onClick={emu.reset}>RESET</button>
              <button onClick={emu.exitToMenu}>MENU</button>
              <button
                onClick={() => emu.setEsp32Perf(!emu.esp32Perf)}
                title="Pace the simulation like a 240 MHz ESP32-S3: heavy frames stretch and drop exactly as they would on the device"
              >
                {emu.esp32Perf ? "ESP32 PERF: ON" : "ESP32 PERF: OFF"}
              </button>
              <span className="bench-status">
                <span className={`led ${emu.ready ? "on" : ""}`} />
                <span>{emu.error ? "FAULT" : emu.ready ? "RUNNING" : "BOOT"}</span>
                <span className="meta-div">|</span>
                <span>
                  {emu.esp32Perf ? `SIM ${emu.esp32Fps} FPS` : `${emu.fps} FPS`}
                </span>
              </span>
            </div>
          </section>

          <aside className="console">
            <div className="module">
              <h2 className="module-title">GAME REGISTRY</h2>
              <GameList
                titles={emu.titles}
                currentGame={emu.currentGame}
                launch={emu.launch}
                exitToMenu={emu.exitToMenu}
              />
            </div>

            <div className="module">
              <h2 className="module-title">IMU · TILT</h2>
              <TiltPad tilt={emu.tilt} setPadTilt={emu.setPadTilt} />
              <p className="hint">ARROW KEYS OR DRAG</p>
            </div>

            <div className="module">
              <h2 className="module-title">INPUT</h2>
              <WheelButtons buttons={emu.buttons} setVirtualButton={emu.setVirtualButton} />
              <p className="hint">
                HOLD <kbd>A</kbd>+<kbd>D</kbd> TO EXIT A GAME
              </p>
            </div>

            <div className="module">
              <h2 className="module-title">AUDIO</h2>
              <VolumeBar label="SFX" percent={emu.sfxVolume} />
              <VolumeBar label="MUS" percent={emu.musicVolume} />
              <p className="hint">
                {emu.audioOn ? "SET LEVELS IN SETTINGS MENU" : "PRESS ANY KEY TO ENABLE AUDIO"}
              </p>
            </div>
          </aside>
        </main>
      )}
    </>
  );
}
