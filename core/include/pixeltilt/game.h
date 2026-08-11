#pragma once
#include <stdint.h>

namespace pt {

// How a game's high scores are ranked and displayed.
enum ScoreKind : uint8_t {
  SCORE_POINTS = 0,  // higher is better, plain number
  SCORE_TIME,        // lower is better, value in deciseconds (e.g. a speedrun)
  SCORE_LEVEL,       // higher is better, displayed as "LV n"
};

// A game is a title for the menu, an init() called every time the game is
// launched, and an update(dt) called once per frame. update() is responsible
// for both simulation and drawing into pt::framebuffer. Games report results
// to the engine's high-score table with pt::submitScore() (see storage.h).
struct Game {
  const char* id;     // directory name under games/; keys the saved scores
  const char* title;
  void (*init)();
  void (*update)(float dt);
  ScoreKind scoreKind;
};

// Populated by the generated games/generated/game_list.cpp (run
// `npm run gen` / `npm run new-game` to refresh it).
extern const Game* const GAME_LIST[];
extern const int GAME_COUNT;

}  // namespace pt

// Define a game and give it external linkage under a predictable symbol name.
// `id` must match the game's directory name under games/. Use the _SCORED
// variant when the game's high scores aren't plain points.
#define PT_GAME_SCORED(id, gameTitle, initFn, updateFn, kind) \
  extern const pt::Game pt_game_##id;                         \
  const pt::Game pt_game_##id = {#id, gameTitle, initFn, updateFn, kind};

#define PT_GAME(id, gameTitle, initFn, updateFn) \
  PT_GAME_SCORED(id, gameTitle, initFn, updateFn, pt::SCORE_POINTS)
