#pragma once

namespace pt {

// A game is three things: a title for the menu, an init() called every time
// the game is launched, and an update(dt) called once per frame. update() is
// responsible for both simulation and drawing into pt::framebuffer.
struct Game {
  const char* title;
  void (*init)();
  void (*update)(float dt);
};

// Populated by the generated games/generated/game_list.cpp (run
// `npm run gen` / `npm run new-game` to refresh it).
extern const Game* const GAME_LIST[];
extern const int GAME_COUNT;

}  // namespace pt

// Define a game and give it external linkage under a predictable symbol name.
// `id` must match the game's directory name under games/.
#define PT_GAME(id, gameTitle, initFn, updateFn)      \
  extern const pt::Game pt_game_##id;                 \
  const pt::Game pt_game_##id = {gameTitle, initFn, updateFn};
