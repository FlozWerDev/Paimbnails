#pragma once

// Pause LevelInfoLayer's heavy background work under full-screen overlays;
// without this InfoLayer re-blurs on every cycle — progressive lag.

namespace paimon {

void pauseLevelInfoHeavyWorkForOverlay();
void resumeLevelInfoHeavyWorkForOverlay();

} // namespace paimon
