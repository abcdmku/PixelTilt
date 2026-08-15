# Device background music

Drop converted `.pta` files here and reflash. The firmware build embeds them
into flash and plays them instead of the built-in chiptune for that track.
Make the files on the emulator's **AUDIO LAB** page: load an MP3 or WAV, tune
the compression, then hit **↓ .PTA**.

File names map to `pt::MusicTrack`:

| File | Replaces | Used by |
| --- | --- | --- |
| `menu.pta` | MENU tune | main menu, scores, settings |
| `chill.pta` | CHILL tune | flappy, lander, tilt maze |
| `action.pta` | ACTION tune | pong, breakout, hopper, snake, tunnel |
| `tense.pta` | TENSE tune | cycles, invaders, meteors, stacker |
| `wiz3.pta` | WIZ3 tune | Wiz3 |
| `rave.pta` | RAVE tune | rave visualizer |
| `rave_acid.pta` | TECHNO STYLE tune | rave visualizer |
| `rave_dodgems.pta` | DODGEMS tune | rave visualizer |

Missing menu, chill, action, tense, or WIZ3 files fall back to the chiptunes.
The three RAVE tracks are PTA-only and ship with the visualizer. Size is a
non-issue on 16 MB of flash: a 2-minute song at 11 kHz ADPCM is about 650 KB.

The **ASSIGN** button on the Audio Lab page only affects the browser emulator,
where it stores the song in localStorage. The device path is this folder plus
`npm run flash`.
