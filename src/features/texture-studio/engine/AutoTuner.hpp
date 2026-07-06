#pragma once

#include "SpritePreviewRenderer.hpp"
#include "../data/ImageBuffer.hpp"

namespace paimon::texture_studio {

class AutoTuner final {
public:
    struct Suggestion {
        SpritePreviewOptions options{};
        int  suggestedBrightness = 160;
        bool changed = false;
    };

    static Suggestion tuneForSprite(ImageBuffer const& framePixels,
                                    SpritePreviewOptions const& base);

private:
    AutoTuner() = delete;
};

}  // namespace paimon::texture_studio
