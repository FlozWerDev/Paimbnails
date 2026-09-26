#pragma once

#include "../GifImportTypes.hpp"

namespace paimon::gifimport {

// Duplicates glowing colors' figures slightly bigger and behind, on their own
// blended half-opacity channel. Same trick as hand-made editor glow, so it
// needs no concrete glow object in the player's GD version.
void applyGlow(ImportPlan& plan, GlowMode mode, std::size_t objectBudget);

} // namespace paimon::gifimport
