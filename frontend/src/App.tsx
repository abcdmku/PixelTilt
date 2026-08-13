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
        <h1>
          PIXEL<span className="accent">TILT</span>
        </h1>
        <nav className="bench-nav">
          <a href="#/" className={lab ? "" : "active"}>
            EMULATOR
          </a>
          <a href="#/audio" className={lab ? "active" : ""}>
            AUDIO LAB
          </a>
        </nav>
      </header>

      {lab ? <AudioLab /> : <Bench />}
    </div>
  );
}
