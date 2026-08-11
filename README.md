# PixelTilt

An open-source, modular game framework for a tilt-controlled 64×64 LED matrix
handheld built from off-the-shelf parts. Write a game once in ~100 lines of
C++, play it instantly in a browser emulator, then flash the **same bytes** to
the real hardware.

```
┌───────────────┐        ┌──────────────────────────────────────────┐
│  games/*/     │        │ core/  (freestanding C++ — no libc)      │
│  game.cpp     ├───────►│ gfx · input · math · engine + menu       │
└───────────────┘        └───────────┬──────────────────┬───────────┘
                                     │                  │
                         clang → WASM (18 KB)    PlatformIO → ESP32-S3
                                     │                  │
                          React/Vite emulator     HUB75 panel + BNO08x
```

## Hardware

| Part | Buy | Notes |
| --- | --- | --- |
| Seengreat RGB Matrix HUB75 S3 | [Amazon](https://www.amazon.com/dp/B0H69DTZVH) · [Seengreat direct](https://seengreat.com/product/359/rgb-matrix-hub75-s3-esp32-s3-based-led-matrix-controller-board-with-a) | ESP32-S3-WROOM-1-N16R8 controller with HUB75 port, 3-way thumb wheel, dual USB-C |
| GY-BNO08x 9-DOF IMU | [Amazon](https://www.amazon.com/dp/B0D2RB2TYC) (or [SparkFun BNO086 Qwiic](https://www.amazon.com/dp/B0CG5XXQ5Y)) | BNO080/BNO085 breakout, I2C |
| 64×64 HUB75 RGB matrix | [Amazon — Waveshare P3](https://www.amazon.com/dp/B0B3F7WKJ1) (or [P2.5](https://www.amazon.com/dp/B0BQYFRVTR)) | Any 64×64 HUB75/HUB75E panel works |

### Wiring

1. **Panel** → the board's two HUB75 connectors (top box header for a
   ribbon, back pin header for direct plug-in) are the *same* port wired in
   parallel — "dual HUB75" in the listings means these two, not two
   channels. **Use the ribbon into the top header.** The direct plug-in
   header only lines up on panels with the matching connector geometry
   (Seengreat/WatangTech's own); on other 64×64 panels it lands on the OUT
   socket or misaligned, and the board body can block the power hookup.
   Panel power to the VH-4P 5 V terminal (max 5 V/4 A).
2. **IMU** → the 4-pin 1 mm I2C header: `SDA→GPIO1`, `SCL→GPIO2`, 3V3, GND.
   The BNO08x sits at address 0x4A or 0x4B — both are probed, and neither
   conflicts with the onboard peripherals on that shared bus.
3. **Power/flash** → the main USB-C port. The second "Power" USB-C can feed
   the matrix separately.

The thumb wheel (up / press / down) is read through the onboard PCA9557 I2C
expander at 0x19 — already handled by the firmware. All pin definitions live
in [`firmware/src/board_config.h`](firmware/src/board_config.h) and match
Seengreat's wiki and demo code.

## Quick start (no hardware needed)

```sh
npm install
npm run dev        # builds the wasm, starts the emulator at localhost:5173
```

The first build downloads a pinned [wasi-sdk](https://github.com/WebAssembly/wasi-sdk)
into `.toolchain/` (~100 MB, one time). No Emscripten, no CMake, no global
compiler install.

**Emulator controls** (mirroring the hardware):

| Hardware | Emulator |
| --- | --- |
| Tilt (BNO08x gravity) | Arrow keys, or drag the tilt pad |
| Wheel up / click / down | `A` / `S` / `D` (Enter also clicks) |
| Exit game to menu | hold `A`+`D` (device: hold wheel up+down ~1 s) |

## Flash the device

```sh
npm run flash                  # auto-detects the S3, builds, uploads
npm run flash -- --monitor     # same, then opens the serial console
npm run flash -- --port COM7   # pin a specific port
npm run monitor                # serial console at 115200
```

The flasher is self-sufficient: it offers to `pip install platformio` if it's
missing, scans serial ports for the S3 (Espressif's native USB id), asks
before using an ambiguous port, walks you through BOOT+RESET if an upload
fails, and finishes with a RAM/Flash usage report:

```
------------------------------------------------------------------------
  FLASHED OK -> COM10
  RAM    [##..................]  10.8%   35,400 / 327,680 bytes
  Flash  [#...................]   5.1%   331,197 / 6,553,600 bytes
------------------------------------------------------------------------
```

The firmware boots into the same menu you see in the emulator: wheel to
navigate, press to launch. Tilt zero is captured at boot (leave the device
resting until the menu appears; press RESET to re-zero).

## Write a game

```sh
npm run new-game -- space_dodge "SPACE DODGE"
```

That scaffolds `games/space_dodge/game.cpp` from the annotated template and
registers it. `npm run dev` and it's already in the menu. A game is one file:

```cpp
#include "pixeltilt/pixeltilt.h"
using namespace pt;

namespace {
float x;

void init() { x = 32; }                    // runs on every launch

void update(float dt) {                    // runs every frame
  x += input.tiltX * 40.0f * dt;           // tilt is [-1, 1]
  if (input.justDown(BTN_CLICK)) x = 32;   // wheel press
  clear();
  fillCircle((int)x, 32, 3, hsv(x * 4, 0.9f, 1.0f));
}
}  // namespace

PT_GAME(space_dodge, "SPACE DODGE", init, update)
```

The API (see [`core/include/pixeltilt/`](core/include/pixeltilt)):

- **`gfx.h`** — 64×64 RGB888 framebuffer: `clear`, `pixel`, `line`, `rect`,
  `fillRect`, `circle`, `fillCircle`, `text`/`textCentered` (3×5 font),
  colors via `rgb()` / `hsv()`.
- **`input.h`** — `input.tiltX/.tiltY` in [-1, 1]; `held` / `justDown` /
  `justUp` for `BTN_UP`, `BTN_CLICK`, `BTN_DOWN`.
- **`ptmath.h`** — `sinf_`, `cosf_`, `sqrtf_`, `atan2f_`, `clampf`, `lerpf`,
  deterministic RNG (`randRange`, `randf`). Games use these instead of
  `<math.h>` so the exact same code compiles freestanding for WASM and for
  the ESP32.

Rules of the road: no heap, no static constructors, keep state in plain
globals and reset it in `init()`. The engine owns the menu and the
hold-UP+DOWN exit combo — games never need to handle either.

Ships with three examples: **Tilt Maze** (roll to the goal, avoid holes),
**Snake** (dominant tilt axis steers), **Breakout** (tilt = paddle position).

## Repo layout

```
core/       engine, gfx, input, math — shared, freestanding C++
games/      one folder per game + _template/ + generated/ registry
emulator/   WASM export shim (the browser "hardware")
frontend/   React + Vite test bench (LED renderer, tilt pad, game registry)
firmware/   ESP32-S3 platform layer (HUB75 DMA, BNO08x, PCA9557 keys)
tools/      build-wasm, gen-games, new-game, smoke-test (Node, zero deps)
```

Both platforms drive the identical core loop:
`engineTick(tiltX, tiltY, buttons, dt)` → game draws into a shared
`framebuffer[64*64*3]` → platform blits it (DMA to the panel, or canvas in
the browser).

`npm test` compiles the wasm and runs a headless smoke test that boots the
engine, launches every registered game, and feeds it 300 frames of input —
also run in CI, so a broken game can't land silently.

## License

MIT
