#pragma once

#include "../GifImportTypes.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace paimon::gifimport {

struct PaletteRefineResult {
    bool changed = false;
    double errorBefore = 0.0;
    double errorAfter = 0.0;
};

// re-fit every palette entry against the pixels it actually paints in the
// final plan. Geometry never changes, so this can only reduce color error.
PaletteRefineResult refinePaletteAgainstRender(
    ImportPlan& plan,
    std::span<std::uint8_t const> sourceRgba,
    int alphaThreshold
);

struct DetailStats {
    std::size_t added = 0;
    double errorBefore = 0.0;
    double errorAfter = 0.0;
};

// spend leftover budget on 1x1 patches where the final plan paints the wrong
// palette index. The caller passes the *object* budget, not the visual count.
DetailStats restorePlanDetail(
    ImportPlan& plan,
    std::size_t objectBudget,
    std::span<std::uint8_t const> sourceRgba,
    int alphaThreshold,
    int detailPercent = 5
);

struct TrimStats {
    bool fitted = false;
    std::size_t removed = 0;
    double errorBefore = 0.0;
    double errorAfter = 0.0;
};

// drop the objects with the smallest visible contribution until the static
// plan fits the budget. Never lowers resolution or frames.
TrimStats trimPlanToBudget(
    ImportPlan& plan,
    std::size_t objectBudget,
    std::span<std::uint8_t const> sourceRgba,
    int alphaThreshold
);

// similarity against a fixed-resolution reference: plans built at different
// grid sizes stay comparable. `referenceRgba` is referenceWidth x referenceHeight.
float absolutePlanSimilarity(
    ImportPlan const& plan,
    std::span<std::uint8_t const> referenceRgba,
    int referenceWidth,
    int referenceHeight
);

} // namespace paimon::gifimport
