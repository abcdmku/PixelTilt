import { GamePicker } from "./GamePicker";
import { WheelButtons } from "./WheelButtons";
import { VARIANTS, VariantId } from "./variants";

function Vol(props: {
  label: string;
  value: number;
  onChange(n: number): void;
}) {
  return (
    <label className="vol">
      <span>{props.label}</span>
      <input
        type="range"
        min={0}
        max={100}
        value={props.value}
        aria-label={`${props.label} volume`}
        onChange={(ev) => props.onChange(Number(ev.target.value))}
      />
    </label>
  );
}

export function Hud(props: {
  ui: VariantId;
  onUi(id: VariantId): void;
  titles: string[];
  currentGame: number;
  launch(i: number): void;
  exitToMenu(): void;
  buttons: number;
  setVirtualButton(mask: number, down: boolean): void;
  sfxVolume: number;
  musicVolume: number;
  setSfxVolume(n: number): void;
  setMusicVolume(n: number): void;
  audioOn: boolean;
}) {
  return (
    <div className="hud">
      <div className="hud-top">
        <h1 className="brand">
          PIXEL<span>TILT</span>
        </h1>
        <GamePicker
          titles={props.titles}
          currentGame={props.currentGame}
          launch={props.launch}
          exitToMenu={props.exitToMenu}
        />
      </div>

      <div className="hud-bottom">
        <WheelButtons buttons={props.buttons} setVirtualButton={props.setVirtualButton} />

        <div className="hud-mid">
          <div className="ui-dots" role="radiogroup" aria-label="panel look">
            {VARIANTS.map((v) => (
              <button
                key={v.id}
                type="button"
                role="radio"
                aria-checked={props.ui === v.id}
                className={props.ui === v.id ? "on" : ""}
                title={v.blurb}
                onClick={() => props.onUi(v.id)}
              >
                {v.label}
              </button>
            ))}
          </div>
          {!props.audioOn && <span className="nudge">tap for sound</span>}
        </div>

        <div className="vols">
          <Vol label="sfx" value={props.sfxVolume} onChange={props.setSfxVolume} />
          <Vol label="mus" value={props.musicVolume} onChange={props.setMusicVolume} />
        </div>
      </div>
    </div>
  );
}
