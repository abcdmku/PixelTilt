#pragma once

#include <stdint.h>

// Purpose-built art for the 64x64 LED version. The original GIFs remain in
// assets.h as archival source material, but their 4:1 reduction erased the
// silhouettes of most gameplay objects. These bitmaps are drawn directly at
// display resolution, where every character has a job.
namespace wiz3_art {

struct Bitmap {
  uint8_t width;
  uint8_t height;
  int8_t offsetX;
  int8_t offsetY;
  const char* const* rows;
};

// Ink key used by the row art below:
// k outline, t turquoise, b blue, n navy, s skin, g/y gold,
// r/o fire, m/v magic, w white, a/d metal, h/f green, q ghost, u wood.

static const char* const PICKUP_STAR[] = {
  "..g..",
  ".gyg.",
  "gywyg",
  ".gyg.",
  "..g..",
};
static const char* const PICKUP_POTION[] = {
  "..aa.",
  "..aa.",
  ".bbb.",
  "bbwbb",
  ".bbb.",
};
static const char* const PICKUP_LIFE[] = {
  "..t..",
  ".ttt.",
  ".tst.",
  ".bbb.",
  "b.b.b",
};
static const char* const PICKUP_KEY[] = {
  "...gg",
  "..gwg",
  "gggg.",
  ".g...",
  "gg...",
};
static const char* const PICKUP_CHECKPOINT[] = {
  ".g...",
  ".gyy.",
  ".ggg.",
  ".g...",
  "ggg..",
};
static const char* const PICKUP_INVINCIBLE[] = {
  "..c..",
  ".cmc.",
  "cmwmc",
  ".cmc.",
  "..c..",
};
static const char* const PICKUP_DOOR[] = {
  ".uuu.",
  "uuguu",
  "u...u",
  "u...u",
  "u..gu",
  "uuuuu",
};
static const char* const PICKUP_LEVER[] = {
  "...r.",
  "..r..",
  ".a...",
  ".a...",
  "aaaa.",
};
static const char* const PICKUP_EXIT[] = {
  ".ccc.",
  "c...c",
  "c.y.c",
  "c.yy.",
  "c.y.c",
  "ccccc",
};
static const char* const PICKUP_SPRING[] = {
  ".c.c.",
  "..c..",
  ".c.c.",
  "aaaaa",
};

static const Bitmap PICKUPS[] = {
  {0, 0, 0, 0, nullptr},
  {5, 5, -1, -1, PICKUP_STAR},
  {5, 5, -1, -1, PICKUP_POTION},
  {5, 5, -1, -1, PICKUP_LIFE},
  {5, 5, -1, -1, PICKUP_KEY},
  {5, 5, -1, -1, PICKUP_CHECKPOINT},
  {5, 5, -1, -1, PICKUP_INVINCIBLE},
  {5, 6, -1, -2, PICKUP_DOOR},
  {5, 5, -1, -1, PICKUP_LEVER},
  {5, 6, -1, -2, PICKUP_EXIT},
  {5, 4, -1, 0, PICKUP_SPRING},
};

static const char* const WIZARD[] = {
  "...t.",
  "..tt.",
  ".ttt.",
  "ttttt",
  ".sks.",
  ".bbg.",
  "b.b.b",
};
static const char* const WIZARD_STEP[] = {
  "...t.",
  "..tt.",
  ".ttt.",
  "ttttt",
  ".sks.",
  "gbbb.",
  ".b.bb",
};
static const char* const GUARD[] = {
  ".yyy.",
  ".yys.",
  ".sss.",
  ".rrr.",
  "rrkrr",
  ".rrr.",
  ".r.r.",
};
static const char* const KNIGHT[] = {
  "..r..",
  ".rrr.",
  "..a..",
  ".awa.",
  ".aaa.",
  "daaad",
  ".dad.",
  ".aaa.",
  ".a.a.",
  ".a.a.",
  "d...d",
};
static const char* const DRAGON[] = {
  "....h....",
  "...hhh...",
  "h..hkh..h",
  "hh.hhh.hh",
  ".hhhhhhh.",
  "..foof...",
  "..fooof..",
  "..fffff..",
  ".f.f.f.f.",
  "f..f.f..f",
  "...f.f...",
};
static const char* const ARROW[] = {
  "..r..",
  "ggggy",
  "..r..",
};
static const char* const STAR_EFFECT[] = {
  "..g..",
  ".gwg.",
  "gwwwg",
  ".gwg.",
  "..g..",
};
static const char* const PLATFORM_VERTICAL[] = {
  "aaaaaaaa",
  "dcggggcd",
  "dd....dd",
};
static const char* const PLATFORM_HORIZONTAL[] = {
  "aaaaaaaa",
  "dggccggd",
  "dd....dd",
};
static const char* const BOTTLE_EFFECT[] = {
  "..aa.",
  ".awwa",
  ".bbb.",
  "bbwbb",
  ".bbb.",
};
static const char* const BOTTLE_EFFECT_MEDIUM[] = {
  ".a.",
  "bbb",
  "bwb",
  ".b.",
};
static const char* const BOTTLE_EFFECT_SMALL[] = {
  ".a.",
  "bwb",
  ".b.",
};
static const char* const BOTTLE_EFFECT_TINY[] = {
  "a",
  "b",
};
static const char* const BOTTLE_EFFECT_SPARK[] = {
  "w",
};
static const char* const FIRE[] = {
  "..r..",
  ".ror.",
  "rooor",
  "oyoyo",
  "rrrrr",
};
static const char* const MAGIC_BURST[] = {
  "c...m",
  ".v.c.",
  "..w..",
  ".m.v.",
  "y...c",
};
static const char* const SENTRY[] = {
  ".rrr.",
  "rrwrr",
  ".rkr.",
  ".ddd.",
  ".drd.",
  ".ddd.",
  "d.d.d",
};
static const char* const GLITTER[] = {
  "c.......m",
  ".........",
  "...y.....",
  "......c..",
  ".m..w....",
  ".......y.",
  "..c......",
  ".....m...",
  "y.......c",
};
static const char* const FIREBALL[] = {
  "..r..",
  "..o..",
  ".ror.",
  "..o..",
  ".ror.",
  "rooor",
  "oyyyo",
  "oywyo",
  ".oyo.",
  "..r..",
};
static const char* const FLAME[] = {
  "..r..",
  "..o..",
  ".ror.",
  "..o..",
  ".ror.",
  ".ooo.",
  "rooor",
  "royor",
  "rooor",
  "oyyyo",
  "oywyo",
  "oyyyo",
  "rooor",
  "royor",
  "rrrrr",
  ".rrr.",
};
static const char* const BOULDER[] = {
  ".ddd.",
  "daaad",
  "adwad",
  "daaad",
  ".ddd.",
};
static const char* const HELPER_SHOT[] = {
  "..c..",
  ".cwc.",
  "cwwcc",
  ".cwc.",
  "..c..",
};
static const char* const FALLING_PLATFORM[] = {
  "dddddddd",
  "aaaadaaa",
  "d.d..d.d",
};
static const char* const FISH[] = {
  "..h..",
  ".hhy.",
  "hhwhh",
  "fhkhf",
  ".fff.",
  "..f..",
  ".f.f.",
};
static const char* const ARCHER[] = {
  "...h.....",
  "..hsh..a.",
  "..fff..a.",
  ".ufff.a..",
  ".ufkf.a..",
  "..f.f..a.",
  "..f.f....",
  ".f...f...",
};
static const char* const BOSS_HEAD[] = {
  "....hhhhhhhh....",
  "..hhhhhhhhhhhh..",
  ".hhhffffffffhhh.",
  "hhhffhffffhffhhh",
  "hhffwwffffwwffhh",
  "hfffwkffffkwfffh",
  "hffffffffffffffh",
  "hhfffyyyyyyfffhh",
  ".hhfykyyyykyfhh.",
  ".hhfyyyyyyyyfhh.",
  "..hhfyyyyyyfhh..",
  "...hhfrrrrfhh...",
  "....hhrrrrhh....",
  ".....hrrrrh.....",
  "......hhhh......",
  ".......hh.......",
};
static const char* const BOSS_SEGMENT[] = {
  "..hhhh..",
  ".hhffhh.",
  "hhffffhh",
  "hffffffh",
  "hffffffh",
  "hhffffhh",
  ".hhffhh.",
  "..hhhh..",
};
static const char* const GHOST[] = {
  ".qqq.",
  "qwwwq",
  "qwkwq",
  "qqkqq",
  ".qqq.",
  "q.q.q",
  ".q.q.",
};
static const char* const BOSS2_HEAD[] = {
  "...aaaaaaaaaa...",
  ".aaawaaawaaaaaa.",
  "aaakaaakaaaaaaaa",
  "aaaaddddddddaaaa",
  ".aaaddddddddaaa.",
  "..aaarrddrraaa..",
  "...aaaarraaaa...",
  ".....aaaaaa.....",
};
static const char* const BOSS2_SEGMENT[] = {
  "..aaaa..",
  ".aaddaa.",
  "aaddddaa",
  "ad....da",
  "ad....da",
  "aaddddaa",
  ".aaddaa.",
  "..aaaa..",
};

static const Bitmap WIZARD_ART = {5, 7, -1, -1, WIZARD};
static const Bitmap WIZARD_STEP_ART = {5, 7, -1, -1, WIZARD_STEP};
static const Bitmap GUARD_ART = {5, 7, -1, -1, GUARD};
static const Bitmap KNIGHT_ART = {5, 11, -1, 0, KNIGHT};
static const Bitmap DRAGON_ART = {9, 11, -1, 0, DRAGON};
static const Bitmap ARROW_ART = {5, 3, -1, -1, ARROW};
static const Bitmap STAR_EFFECT_ART = {5, 5, -1, -1, STAR_EFFECT};
static const Bitmap PLATFORM_VERTICAL_ART = {8, 3, 0, 0, PLATFORM_VERTICAL};
static const Bitmap PLATFORM_HORIZONTAL_ART = {8, 3, 0, 0, PLATFORM_HORIZONTAL};
static const Bitmap BOTTLE_EFFECT_ART = {5, 5, -1, -1, BOTTLE_EFFECT};
static const Bitmap BOTTLE_EFFECT_MEDIUM_ART = {3, 4, 0, -1, BOTTLE_EFFECT_MEDIUM};
static const Bitmap BOTTLE_EFFECT_SMALL_ART = {3, 3, 0, -1, BOTTLE_EFFECT_SMALL};
static const Bitmap BOTTLE_EFFECT_TINY_ART = {1, 2, 1, -1, BOTTLE_EFFECT_TINY};
static const Bitmap BOTTLE_EFFECT_SPARK_ART = {1, 1, 1, -1, BOTTLE_EFFECT_SPARK};
static const Bitmap FIRE_ART = {5, 5, -1, -1, FIRE};
static const Bitmap MAGIC_BURST_ART = {5, 5, -1, -1, MAGIC_BURST};
static const Bitmap SENTRY_ART = {5, 7, -1, -1, SENTRY};
static const Bitmap GLITTER_ART = {9, 9, -1, -1, GLITTER};
static const Bitmap FIREBALL_ART = {5, 10, -1, 0, FIREBALL};
static const Bitmap FLAME_ART = {5, 16, -1, 0, FLAME};
static const Bitmap BOULDER_ART = {5, 5, -1, -1, BOULDER};
static const Bitmap HELPER_SHOT_ART = {5, 5, -1, -1, HELPER_SHOT};
static const Bitmap FALLING_PLATFORM_ART = {8, 3, 0, 0, FALLING_PLATFORM};
static const Bitmap FISH_ART = {5, 7, -1, -1, FISH};
static const Bitmap ARCHER_ART = {9, 8, -1, -1, ARCHER};
static const Bitmap BOSS_HEAD_ART = {16, 16, 0, 0, BOSS_HEAD};
static const Bitmap BOSS_SEGMENT_ART = {8, 8, 4, 4, BOSS_SEGMENT};
static const Bitmap GHOST_ART = {5, 7, -1, -1, GHOST};
static const Bitmap BOSS2_HEAD_ART = {16, 8, 0, 0, BOSS2_HEAD};
static const Bitmap BOSS2_SEGMENT_ART = {8, 8, 4, 0, BOSS2_SEGMENT};

inline const Bitmap& sprite(uint8_t type, int frame) {
  switch (type) {
    case 0: return (frame & 1) ? WIZARD_STEP_ART : WIZARD_ART;
    case 1: return GUARD_ART;
    case 2: return KNIGHT_ART;
    case 3: return DRAGON_ART;
    case 4: return ARROW_ART;
    case 5: return STAR_EFFECT_ART;
    case 6: return PLATFORM_VERTICAL_ART;
    case 7: return PLATFORM_HORIZONTAL_ART;
    case 8:
      if (frame < 2) return BOTTLE_EFFECT_ART;
      if (frame < 4) return BOTTLE_EFFECT_MEDIUM_ART;
      if (frame < 6) return BOTTLE_EFFECT_SMALL_ART;
      if (frame < 7) return BOTTLE_EFFECT_TINY_ART;
      return BOTTLE_EFFECT_SPARK_ART;
    case 9: return FIRE_ART;
    case 10: return MAGIC_BURST_ART;
    case 11: return SENTRY_ART;
    case 12: return GLITTER_ART;
    case 13: return FIREBALL_ART;
    case 14: return FLAME_ART;
    case 15: return BOULDER_ART;
    case 16: return HELPER_SHOT_ART;
    case 17: return FALLING_PLATFORM_ART;
    case 18: return FISH_ART;
    case 19: return ARCHER_ART;
    case 20: return frame ? BOSS_SEGMENT_ART : BOSS_HEAD_ART;
    case 21: return GHOST_ART;
    default: return frame ? BOSS2_SEGMENT_ART : BOSS2_HEAD_ART;
  }
}

}  // namespace wiz3_art
