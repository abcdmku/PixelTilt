#include "pixeltilt/storage.h"
#include "pixeltilt/engine.h"
#include "pixeltilt/game.h"
#include "pixeltilt/gfx.h"

namespace pt {

namespace {

// Scores are keyed by a hash of the game's directory id, so saves survive
// games being added, removed or reordered in the menu.
constexpr uint32_t SAVE_MAGIC = 0x50545356u;  // "PTSV"
constexpr uint16_t SAVE_VERSION = 5;  // v5: Settings grew panelRefresh

struct GameSlot {
  uint32_t idHash;
  int32_t best[SCORES_PER_GAME];
};

struct SaveData {
  uint32_t magic;
  uint16_t version;
  Settings cfg;
  GameSlot games[SCORE_GAME_CAPACITY];
};

SaveData save;
bool dirty = false;
const int32_t emptyScores[SCORES_PER_GAME] = {
    SCORE_EMPTY, SCORE_EMPTY, SCORE_EMPTY};
static_assert(SCORES_PER_GAME == 3, "update the canonical empty score table");

uint32_t fnv1a(const char* s) {
  uint32_t h = 2166136261u;
  while (*s) {
    h ^= (uint8_t)*s++;
    h *= 16777619u;
  }
  return h ? h : 1u;  // 0 marks a free slot
}

void clearSlot(GameSlot& g) {
  g.idHash = 0;
  for (int i = 0; i < SCORES_PER_GAME; i++) g.best[i] = SCORE_EMPTY;
}

bool pruneUnscoredSlots() {
  bool changed = false;
  for (int game = 0; game < GAME_COUNT; game++) {
    if (GAME_LIST[game]->scoreKind != SCORE_NONE) continue;
    uint32_t h = fnv1a(GAME_LIST[game]->id);
    for (int slot = 0; slot < SCORE_GAME_CAPACITY; slot++) {
      if (save.games[slot].idHash == h) {
        clearSlot(save.games[slot]);
        changed = true;
      }
    }
  }
  return changed;
}

GameSlot* findSlot(int gameIndex) {
  if (gameIndex < 0 || gameIndex >= GAME_COUNT) return nullptr;
  uint32_t h = fnv1a(GAME_LIST[gameIndex]->id);
  for (int i = 0; i < SCORE_GAME_CAPACITY; i++)
    if (save.games[i].idHash == h) return &save.games[i];
  return nullptr;
}

GameSlot* ensureSlot(int gameIndex) {
  if (GameSlot* existing = findSlot(gameIndex)) return existing;
  if (gameIndex < 0 || gameIndex >= GAME_COUNT) return nullptr;
  uint32_t h = fnv1a(GAME_LIST[gameIndex]->id);
  for (int i = 0; i < SCORE_GAME_CAPACITY; i++)
    if (save.games[i].idHash == 0) {
      save.games[i].idHash = h;
      return &save.games[i];
    }
  return nullptr;
}

void applySettings() {
  setRotation(save.cfg.rotation);
}

}  // namespace

void storageInit() {
  save.magic = SAVE_MAGIC;
  save.version = SAVE_VERSION;
  save.cfg.rotation = 0;
  save.cfg.tiltRotation = 0;
  save.cfg.tiltFlip = 0;
  save.cfg.brightness = 100;
  save.cfg.sfxVolume = 80;
  save.cfg.musicVolume = 60;
  save.cfg.panelRefresh = 2;  // 250 Hz: the fastest clean rate measured on the
                              // Seengreat panel (higher needs a faster pixel
                              // clock, which ghosts on this hardware).
  for (int i = 0; i < SCORE_GAME_CAPACITY; i++) clearSlot(save.games[i]);
  dirty = false;
  applySettings();
}

Settings& settings() { return save.cfg; }

void settingsChanged() {
  applySettings();
  dirty = true;
}

const int32_t* gameScores(int gameIndex) {
  if (gameIndex < 0 || gameIndex >= GAME_COUNT ||
      GAME_LIST[gameIndex]->scoreKind == SCORE_NONE) {
    return emptyScores;
  }
  const GameSlot* slot = findSlot(gameIndex);
  return slot ? slot->best : emptyScores;
}

int submitScore(int32_t value) {
  int game = currentGame();
  if (game < 0 || game >= GAME_COUNT || value < 0) return -1;
  if (GAME_LIST[game]->scoreKind == SCORE_NONE) return -1;
  bool lowerBetter = GAME_LIST[game]->scoreKind == SCORE_TIME;
  GameSlot* slot = ensureSlot(game);
  if (!slot) return -1;
  int32_t* best = slot->best;

  int rank = -1;
  for (int i = 0; i < SCORES_PER_GAME; i++) {
    if (best[i] == SCORE_EMPTY || (lowerBetter ? value < best[i] : value > best[i])) {
      rank = i;
      break;
    }
  }
  if (rank < 0) return -1;

  for (int i = SCORES_PER_GAME - 1; i > rank; i--) best[i] = best[i - 1];
  best[rank] = value;
  dirty = true;
  return rank;
}

void resetAllScores() {
  for (int i = 0; i < SCORE_GAME_CAPACITY; i++) clearSlot(save.games[i]);
  dirty = true;
}

uint8_t* saveBlob() { return (uint8_t*)&save; }
int saveBlobSize() { return (int)sizeof(SaveData); }

bool saveBlobLoad() {
  if (save.magic != SAVE_MAGIC || save.version != SAVE_VERSION) {
    storageInit();
    return false;
  }
  save.cfg.rotation &= 3;
  save.cfg.tiltRotation &= 3;
  save.cfg.tiltFlip &= 1;
  if (save.cfg.brightness < 20 || save.cfg.brightness > 100) save.cfg.brightness = 100;
  if (save.cfg.sfxVolume > 100) save.cfg.sfxVolume = 80;
  if (save.cfg.musicVolume > 100) save.cfg.musicVolume = 60;
  save.cfg.sfxVolume -= save.cfg.sfxVolume % 20;    // snap to the menu's steps
  save.cfg.musicVolume -= save.cfg.musicVolume % 20;
  // Older v5 builds could reserve a slot simply by opening a game's score
  // page. Reclaim slots for games that are now explicitly scoreless.
  bool pruned = pruneUnscoredSlots();
  applySettings();
  dirty = pruned;  // let the host persist the compacted blob once
  return true;
}

bool saveDirty() { return dirty; }
void clearSaveDirty() { dirty = false; }

}  // namespace pt
