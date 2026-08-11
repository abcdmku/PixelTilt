#pragma once

// Speaker output for the onboard ES8311 codec + NS4150 amp (board_config.h).
// Renders the core's audio (SFX event ring + music track requests) on a
// dedicated FreeRTOS task. Call audioSetup() once after Wire.begin(); returns
// false (and stays silent) if the codec doesn't answer.
bool audioSetup();
bool audioOk();
