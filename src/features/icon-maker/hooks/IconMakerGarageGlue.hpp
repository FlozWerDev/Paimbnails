#pragma once

class GJGarageLayer;

namespace paimon::icon_maker::garage {

// No own garage button; entry is the Paimon Icons popup bottom strip.

// Re-applies exact colors on the garage's main preview after GD re-tints it.
void onPlayerColorChanged(GJGarageLayer* layer);

}  // namespace paimon::icon_maker::garage
