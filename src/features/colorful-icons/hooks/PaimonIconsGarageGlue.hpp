#pragma once
// Thin entry-point header so src/hooks/GJGarageLayer.cpp can call into colorful-icons.

class GJGarageLayer;

namespace paimon::icons::garage {

// Runs after PaimonGJGarageLayer::init: adds gear button, recolors button bar.
void onGarageInit(GJGarageLayer* layer);

// Runs after playerColorChanged: re-runs recolor on the button bar.
void onPlayerColorChanged(GJGarageLayer* layer);

}  // namespace paimon::icons::garage
