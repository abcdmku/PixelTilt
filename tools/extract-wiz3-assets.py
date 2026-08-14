#!/usr/bin/env python3
"""Extract the original Wiz3 JAR into compact PixelTilt assets.

The source is the original creator-hosted Java runtime, not the HTML remake.
The generated header keeps the 19 complete level records, downsampled original
GIF art, and PTA-wrapped PCM8 versions of every original sound in one freestanding
bundle for the WASM and ESP32 targets.
"""

from __future__ import annotations

import argparse
import struct
import zipfile
from io import BytesIO
from pathlib import Path

from PIL import Image


LEVEL_COUNT = 19
LEVEL_BYTES = 256 * 16 * 9

# Opaque source pixels needed per 4x4 block. Tiles butt against each other so
# they can afford a stricter cut; sprites are small enough that trimming a
# quarter-covered block would eat wands, arrows and hat tips.
TILE_COVERAGE = 5
SPRITE_COVERAGE = 4

# name, native source frame width/height, frame count, source grid columns
SPRITES = [
    ("BobWiz", 16, 24, 32, 32),
    ("BobGuard", 16, 21, 4, 4),
    ("BobKnight", 16, 44, 2, 2),
    ("BobDragon", 32, 42, 4, 4),
    ("BobArrow", 16, 7, 8, 8),
    ("BobStar", 16, 16, 8, 8),
    ("BobVPlatform", 16, 12, 2, 2),
    ("BobHPlatform", 16, 9, 2, 2),
    ("BobBottle", 16, 16, 8, 8),
    ("BobFire", 16, 16, 8, 8),
    ("BobMisc", 16, 16, 48, 8),
    ("BobSentry", 16, 24, 4, 4),
    ("BobGlitter", 32, 32, 4, 4),
    ("BobFireball", 16, 40, 4, 4),
    ("BobFlame", 16, 64, 2, 2),
    ("BobBoulder", 16, 16, 4, 4),
    ("BobShot", 16, 16, 4, 4),
    ("BobFPlatform", 16, 16, 2, 2),
    ("BobFish", 16, 24, 4, 4),
    ("BobArcher", 32, 28, 2, 2),
    ("BobBoss", 64, 64, 2, 2),
    ("BobGhost", 16, 24, 4, 4),
    ("BobBoss2", 64, 32, 2, 2),
]

SOUNDS = [
    "silence", "bottle", "star", "hit", "die", "powerup", "door", "lever",
    "bounce", "extra", "nokey", "key", "jump", "fire", "tap", "walk",
]


def rgb565(r: int, g: int, b: int) -> int:
    value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return value or 1  # reserve zero for transparent pixels


def _dist(a: tuple[int, int, int], b: tuple[int, int, int]) -> int:
    """Cheap perceptual squared distance, weighted the way the eye sees."""
    return 2 * (a[0] - b[0]) ** 2 + 4 * (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2


def _medoid(colors: list[tuple[int, int, int]]) -> tuple[int, int, int]:
    """The block colour that sits closest to every other colour in the block.

    Plain mode-of-the-block splits its vote between near-identical shades, so a
    16-pixel block of five blues loses to three pixels of outline black and the
    sprite dissolves into holes. The medoid always lands on the shade the block
    actually reads as, which is what keeps a 4x6 wizard legible.
    """
    unique = set(colors)
    if len(unique) == 1:
        return colors[0]
    return min(unique, key=lambda c: sum(_dist(c, o) for o in colors))


def downsample(
    im: Image.Image,
    left: int,
    top: int,
    width: int,
    height: int,
    coverage: int = 5,
) -> list[int]:
    """Reduce a 4x4 source block to one LED pixel.

    `coverage` is how many of the 16 source pixels must be opaque before the
    output pixel is drawn at all. Without it a block clipped by a single stray
    pixel turns solid, which fattens every silhouette by up to 4 world pixels.
    """
    rgba = im.convert("RGBA")
    out: list[int] = []
    for oy in range((height + 3) // 4):
        for ox in range((width + 3) // 4):
            colors: list[tuple[int, int, int]] = []
            y0 = top + oy * 4
            x0 = left + ox * 4
            for y in range(y0, min(y0 + 4, top + height, rgba.height)):
                for x in range(x0, min(x0 + 4, left + width, rgba.width)):
                    r, g, b, a = rgba.getpixel((x, y))
                    if a >= 32:
                        colors.append((r, g, b))
            if len(colors) < coverage:
                out.append(0)
                continue
            out.append(rgb565(*_medoid(colors)))
    return out


def ulaw_to_pcm8(value: int) -> int:
    u = (~value) & 0xFF
    magnitude = ((u & 0x0F) << 3) + 132
    magnitude <<= (u & 0x70) >> 4
    sample = magnitude - 132
    if u & 0x80:
        sample = -sample
    return max(0, min(255, (sample >> 8) + 128))


def fmt_values(values: list[int], per_line: int = 24, suffix: str = "") -> str:
    lines = []
    for i in range(0, len(values), per_line):
        lines.append("  " + ", ".join(str(v) for v in values[i : i + per_line]) + suffix)
    return ",\n".join(lines)


def c_ident(name: str) -> str:
    return name.upper().replace("-", "_")


def build_graphics(read) -> tuple[list[int], list[int], list[int], list[tuple]]:
    """Downsample every tile sheet and sprite sheet to the 64x64 LED scale."""
    back = Image.open(BytesIO(read("com/eaborn/wiz3/images/backicons.gif")))
    fore = Image.open(BytesIO(read("com/eaborn/wiz3/images/foreicons.gif")))
    bonus_im = Image.open(BytesIO(read("com/eaborn/wiz3/images/bonusicons.gif")))
    back_tiles: list[int] = []
    fore_tiles: list[int] = []
    bonus_icons: list[int] = []
    for tile in range(256):
        left = (tile & 15) * 16
        top = tile & 0xF0
        back_tiles.extend(downsample(back, left, top, 16, 16, TILE_COVERAGE))
        fore_tiles.extend(downsample(fore, left, top, 16, 16, TILE_COVERAGE))
    for icon in range(16):
        bonus_icons.extend(downsample(bonus_im, icon * 16, 0, 16, 16, SPRITE_COVERAGE))

    sprite_data: list[tuple[str, int, int, int, list[int]]] = []
    for name, width, height, frames, columns in SPRITES:
        im = Image.open(BytesIO(read(f"com/eaborn/wiz3/images/{name}.gif")))
        pixels: list[int] = []
        for frame in range(frames):
            left = (frame % columns) * width
            top = (frame // columns) * height
            pixels.extend(downsample(im, left, top, width, height, SPRITE_COVERAGE))
        sprite_data.append((name, (width + 3) // 4, (height + 3) // 4, frames, pixels))
    return back_tiles, fore_tiles, bonus_icons, sprite_data


def graphics_source(images_dir: Path):
    """A `read` callback that serves JAR-style paths from a loose image dir."""
    def read(path: str) -> bytes:
        return (images_dir / Path(path).name).read_bytes()
    return read


def emit_graphics(back_tiles, fore_tiles, bonus_icons, sprite_data) -> list[str]:
    out = ["static const uint16_t BACK_TILES[256 * 16] = {", fmt_values(back_tiles), "};", ""]
    out.extend(["static const uint16_t FORE_TILES[256 * 16] = {", fmt_values(fore_tiles), "};", ""])
    out.extend(["static const uint16_t BONUS_ICONS[16 * 16] = {", fmt_values(bonus_icons), "};", ""])
    out.extend([
        "struct SpriteSheet {",
        "  uint8_t width;",
        "  uint8_t height;",
        "  uint8_t frames;",
        "  const uint16_t* pixels;",
        "};",
        "",
    ])
    for name, _, _, _, pixels in sprite_data:
        out.extend([f"static const uint16_t SPRITE_{c_ident(name)}[] = {{", fmt_values(pixels), "};", ""])
    out.append("static const SpriteSheet SPRITES[] = {")
    for name, width, height, frames, _ in sprite_data:
        out.append(f"  {{{width}, {height}, {frames}, SPRITE_{c_ident(name)}}},")
    out.extend(["};", ""])
    return out


def regenerate_graphics(images_dir: Path, header_path: Path) -> None:
    """Rewrite only the art arrays of an existing header.

    The 19 level records and the sound bank come from the original JAR, which is
    not always at hand; this path re-renders the art from loose GIFs and splices
    it into the header without touching anything else.
    """
    text = header_path.read_text(encoding="utf-8")
    start = text.index("static const uint16_t BACK_TILES")
    end = text.index("static const uint8_t SAMPLE_")
    graphics = build_graphics(graphics_source(images_dir))
    body = "\n".join(emit_graphics(*graphics)) + "\n"
    header_path.write_text(text[:start] + body + text[end:], encoding="utf-8")


def extract(jar_path: Path, header_path: Path, sounds_dir: Path) -> None:
    header_path.parent.mkdir(parents=True, exist_ok=True)
    sounds_dir.mkdir(parents=True, exist_ok=True)

    with zipfile.ZipFile(jar_path) as jar:
        def read(path: str) -> bytes:
            return jar.read(path)

        levels = [read(f"com/eaborn/wiz3/data/level.{i}") for i in range(1, LEVEL_COUNT + 1)]
        if any(len(data) != LEVEL_BYTES for data in levels):
            raise ValueError("unexpected Wiz3 level size")

        back_tiles, fore_tiles, bonus_icons, sprite_data = build_graphics(read)

        sound_arrays: list[tuple[str, list[int], int]] = []
        for name in SOUNDS:
            raw = read(f"com/eaborn/wiz3/sounds/{name}.au")
            data_offset, _, encoding, sample_rate, _ = struct.unpack(">5I", raw[4:24])
            if encoding != 1:
                raise ValueError(f"{name}.au is not μ-law")
            pcm = [ulaw_to_pcm8(v) for v in raw[data_offset:]]
            sound_arrays.append((name, pcm, sample_rate))
            (sounds_dir / f"{name}.au").write_bytes(raw)

    out: list[str] = [
        "// AUTO-GENERATED by tools/extract-wiz3-assets.py; do not edit.",
        "// Source: the original creator-hosted Wiz3 Java runtime JAR.",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "namespace wiz3_assets {",
        f"constexpr int LEVEL_COUNT = {LEVEL_COUNT};",
        f"constexpr int LEVEL_BYTES = {LEVEL_BYTES};",
        "",
        "static const uint8_t LEVEL_DATA[LEVEL_COUNT][LEVEL_BYTES] = {",
    ]
    for level in levels:
        out.append("  {")
        out.append(fmt_values(list(level)))
        out.append("  },")
    out.extend(["};", ""])
    out.extend(emit_graphics(back_tiles, fore_tiles, bonus_icons, sprite_data))
    # PTA PCM8: 16-byte header + unsigned 8-bit samples (codec=1). Matches
    # frontend/src/audio/pta.ts so sfxSample() can play them on every host.
    for name, pcm, rate in sound_arrays:
        header = list(struct.pack("<4sIIHBB", b"PTA1", rate, len(pcm), 0, 1, 0))
        out.extend([
            f"static const uint8_t SAMPLE_{c_ident(name)}[] = {{",
            fmt_values(header + pcm),
            "};",
            "",
        ])
    out.extend(["}  // namespace wiz3_assets", ""])
    header_path.write_text("\n".join(out), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--jar", type=Path, default=Path("wiz3-original.jar"))
    parser.add_argument("--header", type=Path, default=Path("games/wiz3/assets.h"))
    parser.add_argument("--sounds", type=Path, default=Path("assets/sounds/wiz3"))
    parser.add_argument(
        "--images",
        type=Path,
        help="re-render only the art arrays from a directory of original GIFs, "
             "leaving the level records and sound bank in the header untouched",
    )
    args = parser.parse_args()
    if args.images:
        regenerate_graphics(args.images, args.header)
        print(f"re-rendered the art arrays in {args.header} from {args.images}")
        return
    extract(args.jar, args.header, args.sounds)
    print(f"wrote {args.header} and {len(SOUNDS)} original sound files")


if __name__ == "__main__":
    main()
