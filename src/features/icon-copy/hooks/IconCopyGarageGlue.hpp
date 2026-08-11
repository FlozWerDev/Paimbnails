#pragma once
// Entry point for src/hooks/GJGarageLayer.cpp.

class GJGarageLayer;

namespace paimon::iconcopy::garage {

// Called from PaimonGJGarageLayer::init AFTER the original ran. Adds the button
// that opens the list of copied icon sets.
void onGarageInit(GJGarageLayer* layer);

// Pulls the garage that is on screen (if any) back in sync with GameManager
// after a set was applied. Safe to call from a button handler: it runs on the
// next frame, out of the touch dispatcher.
void refreshVisibleGarage();

}  // namespace paimon::iconcopy::garage
