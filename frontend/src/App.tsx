import { useEffect, useState } from "react";
import { Bench } from "./components/Bench";
import { AudioLab } from "./components/AudioLab";
import { initialVariant, rememberVariant, VariantId } from "./components/variants";

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
  const [ui, setUi] = useState<VariantId>(initialVariant);

  useEffect(() => rememberVariant(ui), [ui]);

  return (
    <div className={`app ${lab ? "view-lab" : "view-play"} ui-${ui}`}>
      {lab ? (
        <>
          <header className="lab-bar">
            <a href="#/">back</a>
            <h1>
              PIXEL<span>TILT</span>
            </h1>
          </header>
          <AudioLab />
        </>
      ) : (
        <Bench ui={ui} onUi={setUi} />
      )}
    </div>
  );
}
