#pragma once
// Thin entry-point header so src/hooks/GJGarageLayer.cpp can call into colorful-icons.

class GJGarageLayer;

namespace paimon::icons::garage {

// Called from PaimonGJGarageLayer::init AFTER the original ran. Adds the
// gear button + recolors the visible button bar.
void onGarageInit(GJGarageLayer* layer);

// Called from PaimonGJGarageLayer::playerColorChanged AFTER the original.
// Re-runs the recolor pass on the visible button bar.
void onPlayerColorChanged(GJGarageLayer* layer);

}  // namespace paimon::icons::garage
