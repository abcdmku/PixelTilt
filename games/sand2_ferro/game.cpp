#include "pixeltilt/pixeltilt.h"
#include "pixeltilt/sand2.h"

namespace {
void init() { pt::sand2Init(pt::SAND2_FERRO); }
void update(float dt) { pt::sand2Update(dt); }
}

PT_GAME_UNSCORED(sand2_ferro, "FERROFLOW", init, update)
