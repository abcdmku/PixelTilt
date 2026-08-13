// One-line game chooser. The device itself only has the wheel-driven menu, so
// this is the emulator's shortcut: pick a title and it launches immediately.
export function GamePicker(props: {
  titles: string[];
  currentGame: number; // -1 = engine menu
  launch(i: number): void;
  exitToMenu(): void;
}) {
  return (
    <label className="picker">
      <span className="picker-label">GAME</span>
      <select
        className="picker-select"
        value={props.currentGame}
        aria-label="game"
        onChange={(ev) => {
          const i = Number(ev.target.value);
          if (i < 0) props.exitToMenu();
          else props.launch(i);
          // Hand the keyboard back to the emulator — a focused select would
          // otherwise eat the arrow keys that steer the game.
          ev.target.blur();
        }}
      >
        <option value={-1}>SYSTEM MENU</option>
        {props.titles.map((t, i) => (
          <option key={i} value={i}>
            {t}
          </option>
        ))}
      </select>
    </label>
  );
}
