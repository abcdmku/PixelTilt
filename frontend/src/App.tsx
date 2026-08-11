import { useEffect, useState } from "react";
import { Bench } from "./components/Bench";
import { AudioLab } from "./components/AudioLab";

// Tiny hash router: "#/audio" is the Audio Lab, anything else the bench.
function useRoute(): string {
  const [route, setRoute] = useState(location.hash);
  useEffect(() => {
    const onHash = () => setRoute(location.hash);
    window.addEventListener("hashchange", onHash);
    return () => window.removeEventListener("hashchange", onHash);
  }, []);
  return route;
}

export default function App() {
  const lab = useRoute() === "#/audio";

  return (
    <div className="bench">
      <header className="bench-header">
        <div className="title-block">
          <h1>
            PIXEL<span className="accent">TILT</span>
          </h1>
          <span className="subtitle">
            {lab
              ? "AUDIO LAB · SFX SYNTH + PTA CONVERTER"
              : "HARDWARE EMULATOR · SEENGREAT HUB75-S3 · 64×64"}
          </span>
        </div>
        <nav className="bench-nav">
          <a href="#/" className={lab ? "" : "active"}>
            BENCH
          </a>
          <a href="#/audio" className={lab ? "active" : ""}>
            AUDIO LAB
          </a>
        </nav>
      </header>

      {lab ? <AudioLab /> : <Bench />}

      <footer className="bench-footer">
        <span>ESP32-S3-WROOM-1-N16R8 · BNO08X IMU · PCA9557 KEYS</span>
        <span>SAME BYTES ON DEVICE AND BROWSER — ONE C++ CORE</span>
      </footer>
    </div>
  );
}
