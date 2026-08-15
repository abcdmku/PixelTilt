# Wiz3 art audit

Wiz3 renders on a 64 by 64 RGB matrix. One source tile becomes 4 by 4 display
pixels, so the original GIF detail cannot carry object identity by itself.

## Findings

- The player and common enemies shared dark outlines, muted fills, and nearly
  identical 4-pixel widths.
- All ten interactive map objects occupied the same 4 by 4 box. Their inherited
  green fill often read as grass or platform trim.
- Moving platform art was 4 pixels wide while its collision body was 8 pixels
  wide.
- Arrows were two pixels tall and blended into platform edges.
- Background tiles used enough blue and purple intensity to compete with actors.
- Foreground edges had no common lighting rule, so walkable surfaces varied by
  source tile rather than gameplay role.

## Redesign

The final-resolution set in `art.h` replaces every gameplay sprite type and all
ten used map objects. It uses these stable color roles:

- turquoise and blue for the player
- red and orange for damage
- gold, turquoise, and alternating blue or red for rewards
- silver and cyan for mechanisms
- green, bone, and violet for distinct enemy families

Sprites may extend one display pixel beyond their collision body when that extra
pixel makes a hat, wing, key tooth, or weapon recognizable. Moving platforms now
match their full collision width. The renderer mirrors directional art and keeps
the existing animation timing. Every collected item keeps its own artwork and
color as it rises six display pixels and shrinks from 5 by 5 to a single pixel.

The generated source tiles remain intact in `assets.h`. The game now grades every
background tile darker and less saturated. It also gives foreground pixels a
shared top highlight, side lift, and lower shadow. The gray stone ledges in the
early castle use background tiles 32 through 34 even though they carry collision.
Those collision-backed stones get a restrained cool-gray body and top edge while
decorative copies keep their darker background value. Tree branch tiles 97
through 103 get a moss-green top edge only when they carry collision and have an
exposed standing surface. Other wood, grass, metal, and moving platforms keep
their original artwork.

## Visual QA

Run this from the repository root:

```text
python tools/render-wiz3-assets.py
npm run wasm
node tools/shot.mjs WIZ3 .shots/wiz3
```

The audit script validates row widths and ink keys, records shipped-level asset
usage, and writes original and redesigned contact sheets to `.shots/wiz3-assets`.
