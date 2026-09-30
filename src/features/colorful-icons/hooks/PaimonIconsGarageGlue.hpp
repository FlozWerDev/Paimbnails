#pragma once
// thin entry-point header so src/hooks/gjgaragelayer.cpp can call into colorful-icons.

class GJGarageLayer;

namespace paimon::icons::garage {

// runs after paimongjgaragelayer::init: adds gear button, recolors button bar.
void onGarageInit(GJGarageLayer* layer);

// runs after playercolorchanged: re-runs recolor on the button bar.
void onPlayerColorChanged(GJGarageLayer* layer);

}  // namespace paimon::icons::garage
