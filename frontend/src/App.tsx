import { useEmulator } from "./emulator/useEmulator";
import { MatrixDisplay } from "./components/MatrixDisplay";
import { TiltPad } from "./components/TiltPad";
import { WheelButtons } from "./components/WheelButtons";
import { GameList } from "./components/GameList";

export default function App() {
  const emu = useEmulator();

  return (
    <div className="bench">
      <header className="bench-header">
        <div className="title-block">
          <h1>
            PIXEL<span className="accent">TILT</span>
          </h1>
          <span className="subtitle">HARDWARE EMULATOR · SEENGREAT HUB75-S3 · 64×64</span>
        </div>
        <div className="header-meta">
          <span className={`led ${emu.ready ? "on" : ""}`} />
          <span>{emu.error ? "FAULT" : emu.ready ? "RUNNING" : "BOOT"}</span>
          <span className="meta-div">|</span>
          <span>{emu.fps} FPS</span>
        </div>
      </header>

      {emu.error ? (
        <div className="fault">
          <h2>NO WASM IMAGE</h2>
          <p>{emu.error}</p>
          <code>npm run wasm</code>
        </div>
      ) : (
        <main className="bench-main">
          <section className="stage">
            <MatrixDisplay registerCanvases={emu.registerCanvases} />
            <div className="transport">
              <button onClick={() => emu.setPaused(!emu.paused)}>
                {emu.paused ? "RESUME" : "PAUSE"}
              </button>
              <button onClick={emu.reset}>RESET</button>
              <button onClick={emu.exitToMenu}>MENU</button>
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
          </aside>
        </main>
      )}

      <footer className="bench-footer">
        <span>ESP32-S3-WROOM-1-N16R8 · BNO08X IMU · PCA9557 KEYS</span>
        <span>SAME BYTES ON DEVICE AND BROWSER — ONE C++ CORE</span>
      </footer>
    </div>
  );
}
