#pragma once

// Pause LevelInfoLayer's heavy background work (gallery cycle, shaders, video)
// while a full-screen overlay like InfoLayer comments is open. Without this the
// level keeps cycling thumbnails / decoding video under the popup and InfoLayer
// re-blurs on every cycle — progressive lag until the comments close.

namespace paimon {

void pauseLevelInfoHeavyWorkForOverlay();
void resumeLevelInfoHeavyWorkForOverlay();

} // namespace paimon
