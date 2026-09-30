#pragma once
// entry point for src/hooks/gjgaragelayer.cpp.

class GJGarageLayer;

namespace paimon::iconcopy::garage {

// called from paimongjgaragelayer::init after the original ran. adds the button
// that opens the list of copied icon sets.
void onGarageInit(GJGarageLayer* layer);

// re-syncs the visible garage after a set is applied. deferred a frame,
// so it is safe to call from a button handler, out of the touch dispatcher.
void refreshVisibleGarage();

}  // namespace paimon::iconcopy::garage
