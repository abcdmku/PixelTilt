import { useEffect } from "react";
import { useEmulator } from "../emulator/useEmulator";
import { Board3D } from "./Board3D";
import { Hud } from "./Hud";
import { Help } from "./Help";
import type { VariantId } from "./variants";

/** Full-screen play view: the 3D panel plus a thin overlay for game, wheel,
 *  volume, and the audio-lab link. */
export function Bench(props: { ui: VariantId; onUi(id: VariantId): void }) {
  const emu = useEmulator();

  useEffect(() => {
    emu.setPixelStyle(props.ui === "frame" ? "squares" : "dots");
  }, [props.ui, emu.setPixelStyle]);

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
    <>
      <Board3D
        registerCanvas={emu.registerCanvas}
        setPadTilt={emu.setPadTilt}
        setVirtualSpin={emu.setVirtualSpin}
        setPadAccel={emu.setPadAccel}
        getPose={emu.getPose}
      />
      <Hud
        ui={props.ui}
        onUi={props.onUi}
        titles={emu.titles}
        currentGame={emu.currentGame}
        launch={emu.launch}
        exitToMenu={emu.exitToMenu}
        buttons={emu.buttons}
        setVirtualButton={emu.setVirtualButton}
        sfxVolume={emu.sfxVolume}
        musicVolume={emu.musicVolume}
        setSfxVolume={emu.setHostSfxVolume}
        setMusicVolume={emu.setHostMusicVolume}
        audioOn={emu.audioOn}
        phoneDofsAvailable={emu.phoneDofsAvailable}
        phoneDofsEnabled={emu.phoneDofsEnabled}
        setPhoneDofsEnabled={emu.setPhoneDofsEnabled}
      />
      <Help phoneDofsAvailable={emu.phoneDofsAvailable} />
    </>
  );
}
