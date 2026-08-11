# Device background music

Drop converted `.pta` files here (make them on the emulator's **AUDIO LAB**
page: load an MP3/WAV, tune the compression, then **↓ .PTA**) and reflash —
the firmware build embeds them into flash and plays them instead of the
built-in chiptune for that track.

File names map to `pt::MusicTrack`:

| File | Replaces | Used by |
| --- | --- | --- |
| `menu.pta` | MENU tune | main menu / scores / settings |
| `chill.pta` | CHILL tune | flappy, lander, tilt maze |
| `action.pta` | ACTION tune | pong, breakout, hopper, snake, tunnel |
| `tense.pta` | TENSE tune | cycles, invaders, meteors, stacker |

Missing files fall back to the chiptunes. Size is a non-issue (16 MB flash):
a 2-minute song at 11 kHz ADPCM is ~650 KB.

Note: the **ASSIGN** button on the Audio Lab page only affects the browser
emulator (it stores the song in localStorage) — the device path is this
folder + `npm run flash`.
