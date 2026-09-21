#pragma once
// Entry point for src/hooks/GJGarageLayer.cpp.

class GJGarageLayer;

namespace paimon::iconcopy::garage {

// Called from PaimonGJGarageLayer::init AFTER the original ran. Adds the button
// that opens the list of copied icon sets.
void onGarageInit(GJGarageLayer* layer);

// Re-syncs the visible garage after a set is applied. Deferred a frame,
// so it is safe to call from a button handler, out of the touch dispatcher.
void refreshVisibleGarage();

}  // namespace paimon::iconcopy::garage
