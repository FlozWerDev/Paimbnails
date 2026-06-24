#pragma once
//
// AutoTuner.hpp - Suggests a "best fit" brightness for a sprite so the
// luminance tinter reproduces the user's chosen colors with maximum hue
// fidelity.
//
// The luminance tinter computes, per pixel:
//
//   factor = luminance(src) / brightness
//   tinted = userColor * factor          (rescaled if a channel exceeds 255)
//
// When `brightness` is much lower than the source's dominant luminance the
// multiply saturates and the per-channel rescale kicks in — that preserves
// the hue but throws away the brightness gradient at the top end (flat,
// "decal"-looking recolors). When `brightness` is much higher everything
// comes out dim. The sweet spot puts `brightness` near the luminance of the
// dominant colored cluster, so the brightest fill pixels map close to the
// user's literal color and darker pixels scale down proportionally.
//
// This is a pure, off-main-thread analysis (clustering + classification),
// safe to run from a ThreadTracker worker.
//

#include "SpritePreviewRenderer.hpp"
#include "../data/ImageBuffer.hpp"

namespace paimon::texture_studio {

class AutoTuner final {
public:
    struct Suggestion {
        // A copy of the supplied base options with `brightness` tuned.
        SpritePreviewOptions options{};
        // The brightness the tuner settled on (already inside options).
        int  suggestedBrightness = 160;
        // True when the suggestion differs meaningfully from the base
        // brightness (so the UI can skip a no-op "Auto" press).
        bool changed = false;
    };

    // Analyse the sprite and return tuned options. On an empty sprite or a
    // sprite with no salient colored cluster, returns `base` unchanged with
    // `changed == false`.
    static Suggestion tuneForSprite(ImageBuffer const& framePixels,
                                    SpritePreviewOptions const& base);

private:
    AutoTuner() = delete;
};

}  // namespace paimon::texture_studio
