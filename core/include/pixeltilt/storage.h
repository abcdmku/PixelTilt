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
// Save format v5 reserves this many keyed score tables. Unscored games do not
// consume a slot; the generated registry enforces that the scored subset fits.
constexpr int SCORE_GAME_CAPACITY = 16;

struct Settings {
  uint8_t rotation;      // screen quarter-turns, 0..3 (applied via setRotation)
  uint8_t tiltRotation;  // quarter-turns applied to raw tilt input, 0..3
  uint8_t tiltFlip;      // 1 = mirror tilt X after rotation. Together with
                         // tiltRotation this reaches all 8 mountings of the
                         // IMU (rotated and/or flipped) without a reflash.
  uint8_t brightness;    // percent, 20..100 in steps of 20
  uint8_t sfxVolume;     // percent, 0..100 in steps of 20 (0 = mute)
  uint8_t musicVolume;   // percent, 0..100 in steps of 20 (0 = mute)
  uint8_t panelRefresh;  // HUB75 scan-rate preset, index into PANEL_REFRESH_HZ.
                         // Hardware-only (the browser has no panel); applied
                         // by the firmware when it brings the panel up, so a
                         // change takes effect on the next boot.
};

// Selectable panel refresh (scan) rates. Higher = less flicker; the ceiling
// is panel-specific, so this is a user setting rather than a constant. The
// firmware asks the HUB75 driver for at least this rate and reports what it
// actually achieved.
constexpr int PANEL_REFRESH_COUNT = 4;
constexpr uint16_t PANEL_REFRESH_HZ[PANEL_REFRESH_COUNT] = {60, 150, 250, 255};
// ESP32-HUB75-MatrixPanel-I2S-DMA 3.x stores min_refresh_rate in a uint8_t.
// Keep this explicit so a future preset cannot silently wrap at assignment.
constexpr bool panelRefreshFitsDriver(int i = 0) {
  return i == PANEL_REFRESH_COUNT ||
         (PANEL_REFRESH_HZ[i] <= 255 && panelRefreshFitsDriver(i + 1));
}
static_assert(panelRefreshFitsDriver(),
              "panel refresh presets must fit the HUB75 driver's uint8_t field");

// Reset everything to defaults (called from engineInit).
void storageInit();

Settings& settings();
// Call after mutating settings(); applies rotation and raises the dirty flag.
void settingsChanged();

// Top scores for a game, best first, SCORE_EMPTY in unused slots. A missing or
// unscored table reads as all-empty; querying scores never reserves a slot.
const int32_t* gameScores(int gameIndex);
// Record a finished run for the currently running game. Ranking direction
// follows the game's ScoreKind. Returns the table rank (0 = new best) or -1
// if the score didn't make the table or the game is SCORE_NONE.
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
