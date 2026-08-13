#pragma once

#include "../GifImportTypes.hpp"

#include <cstddef>
#include <string_view>

namespace paimon::gifimport {

inline constexpr char kImageWarningModule[] = "paimbnails.imagewarning.level";

struct WatermarkEvidence {
    std::size_t geometryPairs = 0;
    std::size_t signedPairs = 0;
    std::size_t rotationMarks = 0;
    std::size_t signedRotationMarks = 0;

    bool detected() const {
        return signedRotationMarks > 0 ||
            geometryPairs >= 3 || rotationMarks >= 3;
    }
};

void applyImageWatermark(ImportPlan& plan, int objectBudget);
WatermarkEvidence inspectImageWatermark(std::string_view levelString);
WatermarkEvidence inspectStoredImageWatermark(
    std::string_view storedLevelString,
    std::string_view unpackedLevelString);

} // namespace paimon::gifimport
