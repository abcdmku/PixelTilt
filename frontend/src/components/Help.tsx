import { useEffect, useState } from "react";

const KEY = "pixeltilt.help";

function isPhone(): boolean {
  return window.matchMedia("(pointer: coarse)").matches || window.matchMedia("(hover: none)").matches;
}

export function Help(props: { phoneDofsAvailable: boolean }) {
  const [open, setOpen] = useState(false);
  const [phone, setPhone] = useState(false);

  const close = () => {
    setOpen(false);
    try {
      localStorage.setItem(KEY, "1");
    } catch {
      // ignore
    }
  };

  useEffect(() => {
    setPhone(isPhone());
    try {
      if (localStorage.getItem(KEY)) return;
    } catch {
      // show anyway
    }
    setOpen(true);
  }, []);

  useEffect(() => {
    if (!open) return;
    const onKey = (ev: KeyboardEvent) => {
      if (ev.key === "Escape" || ev.key === "Enter") close();
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [open]);

  return (
    <>
      <div className="hud-links">
        <a href="#/audio" className="lab-link">
          music
        </a>
        <a
          className="gh-link"
          href="https://github.com/abcdmku/PixelTilt"
          target="_blank"
          rel="noopener noreferrer"
          aria-label="GitHub"
          title="GitHub"
          onPointerDown={(ev) => ev.stopPropagation()}
          onClick={(ev) => {
            ev.stopPropagation();
            ev.preventDefault();
            window.open("https://github.com/abcdmku/PixelTilt", "_blank", "noopener,noreferrer");
          }}
        >
          <svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true">
            <path
              fill="currentColor"
              d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27s1.36.09 2 .27c1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.01 8.01 0 0 0 16 8c0-4.42-3.58-8-8-8"
            />
          </svg>
        </a>
        <button
          type="button"
          className="help-toggle"
          aria-label="controls"
          title="controls"
          onClick={() => setOpen(true)}
        >
          ?
        </button>
      </div>
      {open && (
        <div className="help" role="dialog" aria-labelledby="help-title" aria-modal="true">
          <div className="help-card">
            <h2 id="help-title">
              PIXEL<span>TILT</span>
            </h2>
            <p className="help-lede">Simulator for the real tilt handheld.</p>
            {phone ? (
              <ul>
                <li>Drag to tilt</li>
                <li>Drag a corner to spin</li>
                <li>Hold, then drag to move the device</li>
                {props.phoneDofsAvailable && (
                  <li>Enable sensors for phone tilt, spin, and movement</li>
                )}
                <li>▲ ● ▼ is the wheel</li>
                <li>Hold ● for the device menu</li>
              </ul>
            ) : (
              <ul>
                <li>Left click to tilt</li>
                <li>Right click to spin</li>
                <li>Left+right to drag device</li>
                <li>Scroll for device up and down</li>
                <li>Middle click for device select</li>
                <li>Hold middle click or S for the device menu</li>
              </ul>
            )}
            <button type="button" className="help-ok" onClick={close}>
              play
            </button>
          </div>
        </div>
      )}
    </>
  );
}
