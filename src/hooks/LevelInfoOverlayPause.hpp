#pragma once

// pause levelinfolayer's heavy background work under full-screen overlays;
// without this infolayer re-blurs on every cycle — progressive lag.

namespace paimon {

void pauseLevelInfoHeavyWorkForOverlay();
void resumeLevelInfoHeavyWorkForOverlay();

} // namespace paimon
