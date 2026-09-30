#pragma once

#include "../GifImportTypes.hpp"

namespace paimon::gifimport {

// a separate blended channel supplies glow without requiring a native glow object.
void applyGlow(ImportPlan& plan, GlowMode mode, std::size_t objectBudget);

} // namespace paimon::gifimport
