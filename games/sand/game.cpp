// Sand — a falling-pixel gravity toy, not a game you can lose. Tilt the
// board and everything pours that way: the tilt vector is real 2D gravity
// (any of 8 directions, steeper = faster). CLICK toggles the center emitter,
// UP cycles what it emits (sand / water / stone / erase), DOWN clears the
// field. Sand piles up, water flows around it and sand sinks through water;
// stone stays put, so you can pour dams and bury them.
#include "pixeltilt/pixeltilt.h"

using namespace pt;

namespace {

constexpr int W = 64, H = 64;
constexpr int EX = 32, EY = 32;  // emitter

// Cell values; the high bit marks a grain that already moved this pass.
constexpr uint8_t EMPTY = 0, STONE = 1, SAND0 = 2, WATER = 6;
constexpr uint8_t MOVED = 0x80;

// Direction ring, 45 degrees apart; index 0 = +x, index 2 = +y (screen down).
const int DX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
const int DY[8] = {0, 1, 1, 1, 0, -1, -1, -1};

const char* MAT_NAME[4] = {"SAND", "WATER", "STONE", "ERASE"};

uint8_t grid[W * H];
bool  pouring;
int   material;   // 0 sand, 1 water, 2 stone, 3 erase
float simAcc;     // fractional simulation passes owed
float hudT;       // seconds left showing the material name
int   frameNo;
int   scanFlip;   // alternates x scan order to hide lateral bias

bool inBounds(int x, int y) { return x >= 0 && y >= 0 && x < W && y < H; }

void init() {
  setSfxStyle(STYLE_SOFT);
  music(MUS_CHILL);
  for (int i = 0; i < W * H; i++) grid[i] = EMPTY;
  pouring = false;
  material = 0;
  simAcc = 0;
  hudT = 0;
  frameNo = 0;
  scanFlip = 0;
}

// One simulation pass: every grain tries a single move biased along gravity
// direction gi. Cells nearest the wall gravity points at go first, so a
// column falls one cell per pass without teleporting.
void pass(int gi) {
  for (int i = 0; i < W * H; i++) grid[i] &= 0x7F;
  int gx = DX[gi], gy = DY[gi];
  scanFlip ^= 1;
  for (int yy = 0; yy < H; yy++) {
    int y = gy > 0 ? H - 1 - yy : yy;
    for (int xx = 0; xx < W; xx++) {
      int x = (gx > 0 || (gx == 0 && scanFlip)) ? W - 1 - xx : xx;
      uint8_t v = grid[y * W + x];
      if (v == EMPTY || v == STONE || (v & MOVED)) continue;
      bool isSand = v != WATER;

      // Candidates in preference order: straight down-gravity, then the two
      // 45-degree slides (random side first), and for water a 90-degree
      // sideways flow so it levels out.
      int cand[4];
      int n = 0;
      int side = (rand_() & 1) ? 1 : -1;
      cand[n++] = gi;
      cand[n++] = (gi + side) & 7;
      cand[n++] = (gi - side) & 7;
      if (!isSand) cand[n++] = (gi + side * 2) & 7;

      for (int c = 0; c < n; c++) {
        int nx = x + DX[cand[c]], ny = y + DY[cand[c]];
        if (!inBounds(nx, ny)) continue;
        uint8_t t = grid[ny * W + nx];
        if (t == EMPTY) {
          grid[ny * W + nx] = v | MOVED;
          grid[y * W + x] = EMPTY;
          break;
        }
        // Sand sinks through water, but only straight along gravity so the
        // swap can't oscillate sideways.
        if (isSand && t == WATER && c == 0) {
          grid[ny * W + nx] = v | MOVED;
          grid[y * W + x] = WATER | MOVED;
          break;
        }
      }
    }
  }
}

void emit() {
  if (material == 3) {  // erase a disc around the emitter
    for (int dy = -3; dy <= 3; dy++)
      for (int dx = -3; dx <= 3; dx++)
        if (dx * dx + dy * dy <= 9) grid[(EY + dy) * W + EX + dx] = EMPTY;
    return;
  }
  if (material == 2) {  // paint a stone dot
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++)
        if (absi(dx) + absi(dy) <= 1) grid[(EY + dy) * W + EX + dx] = STONE;
    return;
  }
  for (int k = 0; k < 2; k++) {  // sprinkle a couple of grains per frame
    int x = EX + randRange(-2, 2), y = EY + randRange(-2, 2);
    if (grid[y * W + x] == EMPTY)
      grid[y * W + x] = material == 0 ? (uint8_t)(SAND0 + (rand_() & 3)) : WATER;
  }
}

Color matColor(int m) {
  switch (m) {
    case 0: return rgb(225, 185, 90);
    case 1: return rgb(45, 120, 230);
    case 2: return rgb(120, 120, 132);
    default: return rgb(200, 60, 60);
  }
}

void draw() {
  clear(rgb(3, 4, 10));

  const Color sandC[4] = {rgb(230, 190, 90), rgb(210, 165, 70),
                          rgb(240, 210, 120), rgb(195, 150, 60)};
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      uint8_t v = grid[y * W + x] & 0x7F;
      if (v == EMPTY) continue;
      if (v == STONE) {
        pixel(x, y, ((x ^ y) & 1) ? rgb(120, 120, 132) : rgb(100, 100, 112));
      } else if (v == WATER) {
        // Sparse shimmer so still water still reads as liquid.
        bool glint = ((x * 31 + y * 17 + (frameNo >> 2)) & 15) < 2;
        pixel(x, y, glint ? rgb(90, 170, 255) : rgb(35, 100, 210));
      } else {
        pixel(x, y, sandC[v - SAND0]);
      }
    }

  // Emitter ticks, bright while pouring.
  Color tick = pouring ? matColor(material) : rgb(45, 45, 55);
  pixel(EX - 3, EY, tick);
  pixel(EX + 3, EY, tick);
  pixel(EX, EY - 3, tick);
  pixel(EX, EY + 3, tick);

  // Current material swatch, always visible in the corner.
  fillRect(1, 1, 3, 3, matColor(material));
  rect(0, 0, 5, 5, rgb(40, 40, 50));

  if (hudT > 0) {
    int w = textWidth(MAT_NAME[material], 1);
    fillRect((SCREEN_W - w) / 2 - 2, 7, w + 4, 9, rgb(8, 10, 20));
    textCentered(9, MAT_NAME[material], WHITE);
  }
}

void update(float dt) {
  frameNo++;
  if (hudT > 0) hudT -= dt;

  if (input.justDown(BTN_UP)) {
    material = (material + 1) & 3;
    hudT = 1.0f;
    sfx(SFX_SELECT);
  }
  if (input.justDown(BTN_DOWN)) {
    for (int i = 0; i < W * H; i++) grid[i] = EMPTY;
    sfx(SFX_HURT);
  }
  if (input.justDown(BTN_CLICK)) {
    pouring = !pouring;
    sfx(SFX_BLIP, pouring ? 1.2f : 0.8f);
  }
  if (pouring) emit();

  // The tilt vector is gravity: direction quantized to 8 ways, magnitude
  // sets how many passes per second the grains fall. No deadzone — the
  // faintest lean starts a slow creep (sqrt steepens the low end so barely-
  // off-level already reads), full tilt is a torrent. The epsilon only
  // guards atan2's undefined direction at exactly level.
  float tx = input.tiltX, ty = input.tiltY;
  float mag = sqrtf_(tx * tx + ty * ty);
  if (mag > 0.005f) {
    int gi = floori(atan2f_(ty, tx) / (PI / 4.0f) + 0.5f);
    gi = ((gi % 8) + 8) % 8;
    simAcc += dt * 70.0f * sqrtf_(fminf_(mag, 1.0f));
    int done = 0;
    while (simAcc >= 1.0f && done < 4) {
      simAcc -= 1.0f;
      pass(gi);
      done++;
    }
    simAcc = fminf_(simAcc, 1.0f);  // don't bank passes across hitches
  } else {
    simAcc = 0;  // dead level: everything holds still
  }

  draw();
}

}  // namespace

PT_GAME_UNSCORED(sand, "SAND", init, update)
