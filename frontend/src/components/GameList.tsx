export function GameList(props: {
  titles: string[];
  currentGame: number;
  launch(i: number): void;
  exitToMenu(): void;
}) {
  return (
    <div className="game-list">
      <button
        className={`game-row ${props.currentGame === -1 ? "current" : ""}`}
        onClick={props.exitToMenu}
      >
        <span className="game-index">--</span>
        <span className="game-name">SYSTEM MENU</span>
        {props.currentGame === -1 && <span className="game-live">LIVE</span>}
      </button>
      {props.titles.map((t, i) => (
        <button
          key={i}
          className={`game-row ${props.currentGame === i ? "current" : ""}`}
          onClick={() => props.launch(i)}
        >
          <span className="game-index">{String(i).padStart(2, "0")}</span>
          <span className="game-name">{t}</span>
          {props.currentGame === i && <span className="game-live">LIVE</span>}
        </button>
      ))}
    </div>
  );
}
