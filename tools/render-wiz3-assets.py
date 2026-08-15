#!/usr/bin/env python3
"""Render Wiz3's embedded LED art as labelled nearest-neighbour contact sheets.

This is a visual QA aid for the 64x64 target. It also reports which generated
tiles and gameplay objects occur in the shipped levels, so the art audit stays
focused on pixels that players can actually encounter.
"""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


SPRITES = [
    ("wizard", 4, 6, 32),
    ("guard", 4, 6, 4),
    ("knight", 4, 11, 2),
    ("dragon", 8, 11, 4),
    ("arrow", 4, 2, 8),
    ("star", 4, 4, 8),
    ("vertical platform", 4, 3, 2),
    ("horizontal platform", 4, 3, 2),
    ("bottle", 4, 4, 8),
    ("fire", 4, 4, 8),
    ("misc effect", 4, 4, 48),
    ("sentry", 4, 6, 4),
    ("glitter", 8, 8, 4),
    ("fireball", 4, 10, 4),
    ("flame", 4, 16, 2),
    ("boulder", 4, 4, 4),
    ("helper shot", 4, 4, 4),
    ("falling platform", 4, 4, 2),
    ("fish", 4, 6, 4),
    ("archer", 8, 7, 2),
    ("serpent boss", 16, 16, 2),
    ("ghost", 4, 6, 4),
    ("wyrm boss", 16, 8, 2),
]

BONUSES = [
    "unused", "star", "potion", "extra life", "key", "checkpoint",
    "invincibility", "door", "lever", "exit", "spring",
    "unused 11", "unused 12", "unused 13", "unused 14", "unused 15",
]

DESIGNED_SPRITES = [
    "WIZARD", "GUARD", "KNIGHT", "DRAGON", "ARROW", "STAR_EFFECT",
    "PLATFORM_VERTICAL", "PLATFORM_HORIZONTAL", "BOTTLE_EFFECT", "FIRE",
    "MAGIC_BURST", "SENTRY", "GLITTER", "FIREBALL", "FLAME", "BOULDER",
    "HELPER_SHOT", "FALLING_PLATFORM", "FISH", "ARCHER", "BOSS_HEAD",
    "GHOST", "BOSS2_HEAD",
]

DESIGNED_PICKUPS = [
    "PICKUP_STAR", "PICKUP_POTION", "PICKUP_LIFE", "PICKUP_KEY",
    "PICKUP_CHECKPOINT", "PICKUP_INVINCIBLE", "PICKUP_DOOR", "PICKUP_LEVER",
    "PICKUP_EXIT", "PICKUP_SPRING",
]

INKS = {
    "k": (5, 6, 12, 255),
    "t": (45, 238, 220, 255),
    "b": (48, 92, 232, 255),
    "n": (25, 39, 112, 255),
    "s": (255, 188, 116, 255),
    "g": (255, 181, 43, 255),
    "y": (255, 238, 100, 255),
    "r": (241, 51, 72, 255),
    "o": (255, 116, 38, 255),
    "m": (238, 65, 202, 255),
    "v": (143, 88, 255, 255),
    "w": (250, 250, 234, 255),
    "a": (194, 210, 216, 255),
    "d": (73, 86, 99, 255),
    "h": (80, 211, 105, 255),
    "f": (24, 108, 66, 255),
    "q": (201, 184, 255, 255),
    "u": (139, 79, 34, 255),
    "c": (58, 207, 255, 255),
}

SHEET_SCALE = 12
PAD = 8
LABEL_H = 22


def array(text: str, name: str) -> list[int]:
    match = re.search(
        rf"static const (?:uint8_t|uint16_t) {re.escape(name)}[^=]*=\s*\{{(.*?)\n\}};",
        text,
        re.S,
    )
    if not match:
        raise ValueError(f"could not find {name}")
    return [int(value) for value in re.findall(r"\b\d+\b", match.group(1))]


def rgb565(value: int) -> tuple[int, int, int, int]:
    if value == 0:
        return 0, 0, 0, 0
    return (
        ((value >> 11) & 31) * 255 // 31,
        ((value >> 5) & 63) * 255 // 63,
        (value & 31) * 255 // 31,
        255,
    )


def frame(values: list[int], width: int, height: int) -> Image.Image:
    image = Image.new("RGBA", (width, height))
    image.putdata([rgb565(value) for value in values])
    return image


def row_art(text: str) -> dict[str, list[str]]:
    result = {}
    for match in re.finditer(
        r"static const char\* const ([A-Z0-9_]+)\[\]\s*=\s*\{(.*?)\n\};", text, re.S
    ):
        rows = re.findall(r'"([^"\\]*)"', match.group(2))
        if not rows:
            continue
        width = len(rows[0])
        if any(len(row) != width for row in rows):
            widths = ", ".join(str(len(row)) for row in rows)
            raise ValueError(f"{match.group(1)} has inconsistent row widths: {widths}")
        bad = sorted({ink for row in rows for ink in row if ink != "." and ink not in INKS})
        if bad:
            raise ValueError(f"{match.group(1)} has unknown ink keys: {bad}")
        result[match.group(1)] = rows
    return result


def render_rows(rows: list[str], potion_red: bool = False) -> Image.Image:
    image = Image.new("RGBA", (len(rows[0]), len(rows)))
    image.putdata([
        (0, 0, 0, 0) if ink == "." else INKS["r" if potion_red and ink == "b" else ink]
        for row in rows
        for ink in row
    ])
    return image


def render_shrunk(rows: list[str], target_size: int, potion_red: bool = False) -> Image.Image:
    source = render_rows(rows, potion_red)
    target_width = min(source.width, target_size)
    target_height = min(source.height, target_size)
    image = Image.new("RGBA", (target_width, target_height))
    for source_y in range(source.height):
        for source_x in range(source.width):
            color = source.getpixel((source_x, source_y))
            if color[3] == 0:
                continue
            x = (
                0
                if target_width == 1 or source.width == 1
                else (source_x * (target_width - 1) + (source.width - 1) // 2)
                // (source.width - 1)
            )
            y = (
                0
                if target_height == 1 or source.height == 1
                else (source_y * (target_height - 1) + (source.height - 1) // 2)
                // (source.height - 1)
            )
            image.putpixel((x, y), color)
    return image


def rising_strip(images: list[Image.Image]) -> Image.Image:
    strip = Image.new(
        "RGBA",
        (
            sum(image.width for image in images) + len(images) - 1,
            max(image.height for image in images) + len(images) - 1,
        ),
    )
    x = 0
    for index, image in enumerate(images):
        bottom = strip.height - 1 - index
        strip.paste(image, (x, bottom - image.height + 1), image)
        x += image.width + 1
    return strip


def checker(width: int, height: int) -> Image.Image:
    image = Image.new("RGB", (width, height), (13, 15, 25))
    draw = ImageDraw.Draw(image)
    cell = 6
    for y in range(0, height, cell):
        for x in range(0, width, cell):
            if (x // cell + y // cell) & 1:
                draw.rectangle((x, y, x + cell - 1, y + cell - 1), fill=(23, 27, 42))
    return image


def contact_sheet(
    entries: list[tuple[str, Image.Image]], columns: int, cell_w: int, cell_h: int
) -> Image.Image:
    rows = (len(entries) + columns - 1) // columns
    sheet = checker(columns * cell_w, rows * cell_h)
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default()
    for index, (label, source) in enumerate(entries):
        col, row = index % columns, index // columns
        x, y = col * cell_w, row * cell_h
        art = source.resize(
            (source.width * SHEET_SCALE, source.height * SHEET_SCALE), Image.Resampling.NEAREST
        )
        ax = x + (cell_w - art.width) // 2
        ay = y + PAD
        sheet.paste(art, (ax, ay), art)
        draw.text((x + PAD, y + cell_h - LABEL_H), label, fill=(235, 238, 244), font=font)
    return sheet


def usage(level_data: list[int]) -> dict[str, Counter[int]]:
    result = {
        name: Counter()
        for name in (
            "back", "fore", "sprite", "bonus", "collision_back", "stone_collision_back",
            "tree_ledge_fore"
        )
    }
    for offset in range(0, len(level_data), 9):
        result["back"][level_data[offset]] += 1
        result["fore"][level_data[offset + 1]] += 1
        result["sprite"][level_data[offset + 6]] += 1
        result["bonus"][level_data[offset + 7]] += 1
        back = level_data[offset]
        if level_data[offset + 1] == 0 and level_data[offset + 8] & 3:
            result["collision_back"][back] += 1
        if level_data[offset + 1] == 0 and level_data[offset + 8] & 3 and 32 <= back <= 34:
            result["stone_collision_back"][back] += 1
        fore = level_data[offset + 1]
        if level_data[offset + 8] & 3 and 97 <= fore <= 103:
            result["tree_ledge_fore"][fore] += 1
    return result


def save_tiles(
    values: list[int], ids: list[int], path: Path, title: str,
    stone_collision: bool = False, tree_ledge: bool = False
) -> None:
    entries = []
    for tile_id in ids:
        tile = frame(values[tile_id * 16 : (tile_id + 1) * 16], 4, 4)
        if stone_collision:
            body = (132, 144, 156, 255)
            top = (184, 199, 206, 255)
            for y in range(4):
                for x in range(4):
                    base = tile.getpixel((x, y))
                    color = tuple(
                        round(base[channel] + (body[channel] - base[channel]) * 0.50)
                        for channel in range(4)
                    )
                    if y == 3:
                        color = tuple(round(channel * 0.72) for channel in color[:3]) + (255,)
                    tile.putpixel((x, y), color)
            for x in range(4):
                tile.putpixel((x, 0), top)
        if tree_ledge:
            moss = (132, 181, 78, 255)
            for x in range(4):
                base = tile.getpixel((x, 0))
                tile.putpixel(
                    (x, 0),
                    tuple(
                        round(base[channel] + (moss[channel] - base[channel]) * 0.72)
                        for channel in range(4)
                    ),
                )
        entries.append((f"{title} {tile_id}", tile))
    sheet = contact_sheet(entries, 8, 76, 86)
    sheet.save(path)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--header", type=Path, default=Path("games/wiz3/assets.h"))
    parser.add_argument("--art", type=Path, default=Path("games/wiz3/art.h"))
    parser.add_argument("--out", type=Path, default=Path(".shots/wiz3-assets"))
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    text = args.header.read_text(encoding="utf-8")
    back = array(text, "BACK_TILES")
    fore = array(text, "FORE_TILES")
    bonuses = array(text, "BONUS_ICONS")
    levels = array(text, "LEVEL_DATA")
    counts = usage(levels)

    save_tiles(back, sorted(key for key in counts["back"] if key), args.out / "back-tiles.png", "back")
    save_tiles(fore, sorted(key for key in counts["fore"] if key), args.out / "fore-tiles.png", "fore")
    save_tiles(
        back,
        sorted(key for key in counts["collision_back"] if key),
        args.out / "collision-back-tiles.png",
        "collision back",
    )
    save_tiles(
        back,
        sorted(key for key in counts["stone_collision_back"] if key),
        args.out / "stone-collision-tiles.png",
        "stone collision",
        stone_collision=True,
    )
    save_tiles(
        fore,
        sorted(key for key in counts["tree_ledge_fore"] if key),
        args.out / "tree-ledge-tiles.png",
        "tree ledge",
        tree_ledge=True,
    )

    bonus_entries = [
        (f"{index}: {BONUSES[index]}", frame(bonuses[index * 16 : (index + 1) * 16], 4, 4))
        for index in range(1, 16)
    ]
    contact_sheet(bonus_entries, 5, 130, 86).save(args.out / "bonuses.png")

    sprite_entries: list[tuple[str, Image.Image]] = []
    table = re.search(r"static const SpriteSheet SPRITES\[\][^=]*=\s*\{(.*?)\n\};", text, re.S)
    if not table:
        raise ValueError("could not find sprite pointer table")
    symbols = re.findall(r"SPRITE_[A-Z0-9_]+", table.group(1))
    for index, (name, width, height, frame_count) in enumerate(SPRITES):
        # The generated symbols retain the original Bob names. Read the source
        # pointer table so this QA tool cannot drift when a display label changes.
        values = array(text, symbols[index])
        size = width * height
        shown = min(frame_count, 8)
        strip = Image.new("RGBA", (width * shown, height))
        for frame_index in range(shown):
            art = frame(values[frame_index * size : (frame_index + 1) * size], width, height)
            strip.paste(art, (frame_index * width, 0), art)
        sprite_entries.append((f"{index}: {name} ({frame_count}f)", strip))
    contact_sheet(sprite_entries, 2, 410, 220).save(args.out / "sprites.png")

    designed = row_art(args.art.read_text(encoding="utf-8"))
    designed_pickups = [
        (f"{index}: {BONUSES[index]}", render_rows(designed[symbol]))
        for index, symbol in enumerate(DESIGNED_PICKUPS, 1)
    ]
    designed_pickups[1] = ("2a: potion blue", render_rows(designed["PICKUP_POTION"]))
    designed_pickups.insert(
        2, ("2b: potion red", render_rows(designed["PICKUP_POTION"], potion_red=True))
    )
    contact_sheet(designed_pickups, 5, 130, 110).save(args.out / "designed-pickups.png")

    collection_effects = []
    for index, symbol in enumerate(DESIGNED_PICKUPS[:6], 1):
        rows = designed[symbol]
        phases = [render_shrunk(rows, size) for size in (5, 3, 2, 1)]
        collection_effects.append((f"{BONUSES[index]} pickup", rising_strip(phases)))
        if index == 2:
            red_phases = [render_shrunk(rows, size, potion_red=True) for size in (5, 3, 2, 1)]
            collection_effects.append(("red potion pickup", rising_strip(red_phases)))
    contact_sheet(collection_effects, 4, 180, 120).save(args.out / "collection-effects.png")

    designed_sprites = [
        (f"{index}: {SPRITES[index][0]}", render_rows(designed[symbol]))
        for index, symbol in enumerate(DESIGNED_SPRITES)
    ]
    designed_sprites.extend([
        ("player walk alternate", render_rows(designed["WIZARD_STEP"])),
        ("serpent body segment", render_rows(designed["BOSS_SEGMENT"])),
        ("wyrm body segment", render_rows(designed["BOSS2_SEGMENT"])),
    ])
    contact_sheet(designed_sprites, 3, 260, 230).save(args.out / "designed-sprites.png")

    report = {
        category: {str(key): value for key, value in sorted(counter.items()) if key}
        for category, counter in counts.items()
    }
    (args.out / "usage.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"rendered Wiz3 asset audit to {args.out}")


if __name__ == "__main__":
    main()
