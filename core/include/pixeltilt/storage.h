#pragma once
#include <stdint.h>

// Settings + high scores, persisted through a host-agnostic save blob. The
// core never touches a filesystem: it keeps everything in one POD struct and
// raises a dirty flag; each platform host decides where the bytes live
// (ESP32 NVS via Preferences, browser localStorage). Host loop:
//
//   load:  copy stored bytes over saveBlob(), then call saveBlobLoad()
//          (validates + applies; falls back to defaults on a bad blob)
//   save:  when saveDirty(), persist saveBlob() and call clearSaveDirty()

namespace pt {

constexpr int SCORES_PER_GAME = 3;
constexpr int32_t SCORE_EMPTY = -1;

struct Settings {
  uint8_t rotation;      // screen quarter-turns, 0..3 (applied via setRotation)
  uint8_t tiltRotation;  // quarter-turns applied to raw tilt input, 0..3
  uint8_t tiltFlip;      // 1 = mirror tilt X after rotation. Together with
                         // tiltRotation this reaches all 8 mountings of the
                         // IMU (rotated and/or flipped) without a reflash.
  uint8_t brightness;    // percent, 20..100 in steps of 20
};

// Reset everything to defaults (called from engineInit).
void storageInit();

Settings& settings();
// Call after mutating settings(); applies rotation and raises the dirty flag.
void settingsChanged();

// Top scores for a game, best first, SCORE_EMPTY in unused slots.
const int32_t* gameScores(int gameIndex);
// Record a finished run for the currently running game. Ranking direction
// follows the game's ScoreKind. Returns the table rank (0 = new best) or -1
// if the score didn't make the table.
int submitScore(int32_t value);
void resetAllScores();

// --- host persistence hooks ------------------------------------------------
uint8_t* saveBlob();
int      saveBlobSize();
// Validate + apply whatever the host wrote into saveBlob(). Returns false
// (and reverts to defaults) if the bytes aren't a compatible save.
bool saveBlobLoad();
bool saveDirty();
void clearSaveDirty();

}  // namespace pt
