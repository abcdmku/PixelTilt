// PUZZLES — eight puzzle types in one cabinet, from a four-by-four Lights Out
// up to a board-filling Flow. Pick one from the grid and it deals boards of
// that type, each a little bigger or busier than the last, until you tilt back
// out with DOWN. Points bank per run, so the persistent top three compares
// whole runs rather than single boards. Where you got to in each type is kept
// for the session, so coming back offers the board you left on or a fresh
// start from board one.
//
// The shell here owns the picker, the run, the HUD strip and the scoring; each
// p_*.h next to it owns one puzzle and reports PLAYING / SOLVED / FAILED plus
// the moves it took. Controls are the same shape everywhere: tilt moves,
// CLICK acts, UP is the puzzle's second action, DOWN leaves.
#include "pixeltilt/pixeltilt.h"
#include "pixeltilt/engine.h"

#include "shared.h"

#include "p_lights.h"
#include "p_slide.h"
#include "p_merge.h"
#include "p_pipes.h"
#include "p_crates.h"
#include "p_mines.h"
#include "p_nono.h"
#include "p_flow.h"

using namespace pt;

namespace {

struct Puzzle {
  const char* name;
  uint8_t tier;      // 1..4, shown as pips and as the name's colour
  const char* hint;  // one line of controls, or nullptr if the board says it
  void (*start)(int stage);
  pz::Status (*update)(float dt);
};

// Read down the left column then the right: the grid runs simple to complex.
const Puzzle PUZZLES[8] = {
    {"LIGHTS", 1, "CLICK-TOGGLE", pz_lights::start, pz_lights::update},
    {"SLIDE", 2, "TILT-SLIDE", pz_slide::start, pz_slide::update},
    {"MERGE", 2, "TILT-SHIFT", pz_merge::start, pz_merge::update},
    {"PIPES", 2, "UP-TURN BACK", pz_pipes::start, pz_pipes::update},
    {"CRATES", 3, "UP-UNDO TAP-NEW", pz_crates::start, pz_crates::update},
    {"MINES", 3, "UP-FLAG", pz_mines::start, pz_mines::update},
    {"NONO", 3, "UP-CROSS", pz_nono::start, pz_nono::update},
    {"FLOW", 4, "CLICK-GRAB", pz_flow::start, pz_flow::update},
};
constexpr int COUNT = 8;
// Two columns, as square a block as the count allows.
constexpr int PICK_ROWS = (COUNT + 1) / 2;
constexpr int GRID_Y = 10 + (40 - PICK_ROWS * 8) / 2;

enum Screen : uint8_t { PICK, RESUME, PLAY, WON, LOST };

Screen screen;
int cursor;      // picker slot, 0..COUNT-1
int stage;       // board number within the current run, 0-based
int runPoints;   // banked at the end of the run
int lastAward;   // points from the board just solved
int lastRun;     // points from the run just finished, for the picker flash
int lastRank;    // submitScore rank, 0 = new best
// Furthest board reached in each type this session, 0 = never played. Kept in
// RAM only: the save blob holds settings and score tables, nothing per-game.
uint8_t reached[COUNT];
int resumeCursor;  // 0 resume, 1 restart, 2 back
float flashT;      // picker flash after banking a run
float hintT;       // control-hint fade in a board
float bannerT;     // time on the solved / failed panel
pz::Stepper pickStep, resumeStep;

// Efficiency is capped, so a board is worth a fixed base for reaching it plus
// up to the same again for solving it near par.
int award(int tier, int board, int moves, int par) {
  int base = tier * (20 + 10 * board);
  int eff = clampi(par * 30 * tier / maxi(moves, 1), 0, 30 * tier);
  return base + eff;
}

void dimFrame() {
  for (int i = 0; i < SCREEN_W * SCREEN_H * 3; i++)
    framebuffer[i] = (uint8_t)(framebuffer[i] * 2 / 5);
}

void startBoard() {
  hintT = stage == 0 ? 6.0f : 0.0f;  // controls once per run, not per board
  PUZZLES[cursor].start(stage);
}

// Leaving a board always banks the run. The board you were on is remembered
// so the type can be picked up again rather than restarted from board one.
void bankRun() {
  reached[cursor] = (uint8_t)mini(stage, 255);
  lastRun = runPoints;
  lastRank = runPoints > 0 ? submitScore(runPoints) : -1;
  runPoints = 0;
}

void toPicker(float flash) {
  screen = PICK;
  flashT = flash;
  pz::stepperInit(pickStep, 0.34f, 0.16f);
}

// A resumed run starts fresh on points: whatever the last one earned was
// banked on the way out. Only the board number carries over.
void beginRun(int fromStage) {
  screen = PLAY;
  stage = fromStage;
  runPoints = 0;
  flashT = 0;
  startBoard();
  sfx(SFX_SELECT, 1.2f);
}

// --- picker ------------------------------------------------------------------

// One slot: just the name, coloured by how involved the puzzle is (green for
// the gentle end through to magenta for Flow).
void drawPickerTile(int i) {
  int col = i / PICK_ROWS, row = i % PICK_ROWS;
  int x = 1 + col * 32, y = GRID_Y + row * 8;
  bool sel = i == cursor;
  if (sel) fillRect(x, y, 30, 8, rgb(26, 26, 54));
  text(x + 2, y + 2, PUZZLES[i].name,
       sel ? WHITE : pz::dim(pz::tierTint(PUZZLES[i].tier), 3, 5));
}

void drawPicker() {
  clear();
  textCentered(2, "PUZZLES", WHITE);
  hline(0, 9, SCREEN_W, rgb(30, 30, 50));
  for (int i = 0; i < COUNT; i++) drawPickerTile(i);

  const int32_t* best = gameScores(currentGame());
  if (best[0] > 0)
    pz::labelNum(51, "BEST", best[0], rgb(70, 70, 90), rgb(255, 200, 60));

  if (flashT > 0) {
    if (lastRank == 0) {
      textCentered(SCREEN_H - 6, "NEW BEST!", rgb(255, 210, 40));
    } else {
      char buf[16];
      buf[0] = '+';
      pz::numToStr(lastRun, buf + 1);
      pz::labelText(SCREEN_H - 6, "BANKED", buf, rgb(60, 130, 80),
                    rgb(70, 220, 110));
    }
  } else if (reached[cursor] > 0) {
    // Progress beats a control reminder you have already read once.
    pz::labelNum(SCREEN_H - 6, "BOARD", reached[cursor] + 1, rgb(60, 60, 80),
                 rgb(120, 160, 220));
  } else {
    pz::hudHint(PUZZLES[cursor].hint, 1.0f);
  }
}

void tickPicker(float dt) {
  if (input.justDown(BTN_UP) || input.justDown(BTN_DOWN)) {
    cursor = (cursor + (input.justDown(BTN_UP) ? COUNT - 1 : 1)) % COUNT;
    sfx(SFX_BLIP, 1.3f);
  }
  int dx = 0, dy = 0;
  if (pz::stepTilt(pickStep, dt, dx, dy)) {
    int col = clampi(cursor / PICK_ROWS + dx, 0, 1);
    int row = clampi(cursor % PICK_ROWS + dy, 0, PICK_ROWS - 1);
    int next = mini(col * PICK_ROWS + row, COUNT - 1);  // short last column
    if (next != cursor) sfx(SFX_BLIP, 1.3f);
    cursor = next;
  }
  if (input.justDown(BTN_CLICK)) {
    if (reached[cursor] > 0) {  // played before: offer the board you left on
      screen = RESUME;
      resumeCursor = 0;
      pz::stepperInit(resumeStep, 0.34f, 0.16f);
      sfx(SFX_SELECT, 1.2f);
    } else {
      beginRun(0);
    }
    return;
  }
  drawPicker();
}

// --- resume or restart -------------------------------------------------------

void tickResume(float dt) {
  constexpr int ITEMS = 3;
  if (input.justDown(BTN_UP) || input.justDown(BTN_DOWN)) {
    resumeCursor = (resumeCursor + (input.justDown(BTN_UP) ? ITEMS - 1 : 1)) % ITEMS;
    sfx(SFX_BLIP, 1.3f);
  }
  int dx = 0, dy = 0;
  if (pz::stepTilt(resumeStep, dt, dx, dy) && dy != 0) {
    int next = clampi(resumeCursor + dy, 0, ITEMS - 1);
    if (next != resumeCursor) sfx(SFX_BLIP, 1.3f);
    resumeCursor = next;
  }
  if (input.justDown(BTN_CLICK)) {
    if (resumeCursor == 0) beginRun(reached[cursor]);
    else if (resumeCursor == 1) beginRun(0);
    else {
      screen = PICK;
      sfx(SFX_BLIP, 0.8f);
    }
    return;
  }

  drawPicker();
  constexpr int BOX_Y = 14, BOX_H = 36;
  pz::banner(BOX_Y, BOX_H, pz::tierTint(PUZZLES[cursor].tier));
  textCentered(BOX_Y + 3, PUZZLES[cursor].name, WHITE);
  hline(10, BOX_Y + 10, SCREEN_W - 20, rgb(30, 30, 50));
  const char* labels[ITEMS] = {"RESUME", "RESTART", "BACK"};
  for (int i = 0; i < ITEMS; i++) {
    int y = BOX_Y + 13 + i * 7;
    bool sel = i == resumeCursor;
    if (sel) text(10, y, ">", YELLOW);
    text(16, y, labels[i], sel ? WHITE : GRAY);
    if (i == 0)
      pz::numRight(SCREEN_W - 11, y, reached[cursor] + 1,
                   sel ? rgb(120, 200, 255) : rgb(60, 90, 130));
  }
}

// --- a board in play ---------------------------------------------------------

void tickPlay(float dt) {
  if (input.justDown(BTN_DOWN)) {  // leave and bank whatever the run earned
    bankRun();
    toPicker(2.2f);
    sfx(lastRun > 0 ? SFX_COIN : SFX_BLIP, 1.0f);
    return;
  }

  pz::bottomRowFree = hintT <= 0.0f;
  pz::Status st = PUZZLES[cursor].update(dt);
  pz::hudTop(PUZZLES[cursor].name, stage + 1, pz::tierTint(PUZZLES[cursor].tier));
  if (hintT > 0) {
    hintT -= dt;
    // The puzzle's own line first, then the way out, then out of the way.
    if (hintT > 2.5f) pz::hudHint(PUZZLES[cursor].hint, hintT - 2.5f);
    else pz::hudHint("DOWN-LIST", hintT);
  }

  if (st == pz::SOLVED) {
    lastAward = award(PUZZLES[cursor].tier, stage, pz::moves, pz::par);
    runPoints += lastAward;
    screen = WON;
    bannerT = 0;
    dimFrame();
    sfx(SFX_WIN);
  } else if (st == pz::FAILED) {
    bankRun();
    screen = LOST;
    bannerT = 0;
    dimFrame();
    sfx(SFX_LOSE);
  }
}

void tickWon(float dt) {
  bannerT += dt;
  pz::banner(17, 30, rgb(40, 190, 90));
  textCentered(21, "SOLVED", WHITE);
  char buf[12];
  int k = 0;
  buf[k++] = '+';
  k += pz::numToStr(lastAward, buf + k);
  buf[k] = 0;
  textCentered(29, buf, rgb(70, 230, 120));
  pz::labelNum(38, "RUN", runPoints, rgb(60, 60, 80), rgb(120, 120, 150));

  if (bannerT > 1.8f || (bannerT > 0.35f && input.justDown(BTN_CLICK))) {
    stage++;
    screen = PLAY;
    startBoard();
  }
}

void tickLost(float dt) {
  bannerT += dt;
  pz::banner(17, 30, rgb(210, 60, 60));
  textCentered(21, "RUN OVER", WHITE);
  if (lastRank == 0) {
    textCentered(29, "NEW BEST!", rgb(255, 210, 40));
  } else {
    pz::labelNum(29, "RUN", lastRun, rgb(90, 60, 60), rgb(255, 150, 60));
  }
  textCentered(38, "CLICK-LIST", rgb(70, 70, 90));
  if (bannerT > 0.4f && input.justDown(BTN_CLICK)) toPicker(0.0f);
}

// --- game hooks --------------------------------------------------------------

void init() {
  setSfxStyle(STYLE_SOFT);
  music(MUS_CHILL);
  screen = PICK;
  cursor = 0;
  stage = 0;
  runPoints = 0;
  lastAward = 0;
  lastRun = 0;
  lastRank = -1;
  resumeCursor = 0;
  for (int i = 0; i < COUNT; i++) reached[i] = 0;
  flashT = 0;
  hintT = 0;
  bannerT = 0;
  pz::stepperInit(pickStep, 0.34f, 0.16f);
  pz::stepperInit(resumeStep, 0.34f, 0.16f);
  pz::beginBoard(1);
}

void update(float dt) {
  if (flashT > 0) flashT -= dt;
  switch (screen) {
    case PICK:   tickPicker(dt); break;
    case RESUME: tickResume(dt); break;
    case PLAY:   tickPlay(dt); break;
    case WON:    tickWon(dt); break;
    case LOST:   tickLost(dt); break;
  }
}

}  // namespace

PT_GAME(puzzles, "PUZZLES", init, update)
