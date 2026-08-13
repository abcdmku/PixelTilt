#pragma once
#include <stdint.h>

namespace pt {

// All flavors share one particle arena; selecting a flavor only swaps the
// compact force/material profile used by the runtime.
enum Sand2Flavor : uint8_t {
  SAND2_CLASSIC = 0,
  SAND2_LAVA,
  SAND2_SNOW,
  SAND2_STAR,
  SAND2_FERRO,
  SAND2_NEON,
};

void sand2Init(Sand2Flavor flavor);
void sand2Update(float dt);

}  // namespace pt
