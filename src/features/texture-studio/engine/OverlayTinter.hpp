#pragma once

#include "../data/ImageBuffer.hpp"
#include "LuminanceTinter.hpp"

namespace paimon::texture_studio {

// Hand-drawn overlay layers from the PackGen asset pack, all the same size
// as the base image (or empty). Each overlay carries the actual artwork of
// its region; tinting recolors the overlay's own pixels by luminance and
// alpha-composites them over the base — exactly PackGen's algorithm.
struct OverlayImages {
    ImageBuffer overlay1;  // tinted with color1
    ImageBuffer overlay2;  // tinted with color2
    ImageBuffer gold;      // tinted with color2 (gold titles)
    ImageBuffer demon1;    // tinted with color1 (demon faces)
    ImageBuffer demon2;    // tinted with color2 (demon faces)
    ImageBuffer glow;      // tinted with glow; replace mode when alternative

    bool anyUsable(int width, int height) const;
};

class OverlayTinter final {
public:
    // Apply the PackGen overlay tint. Returns a fresh buffer; overlays whose
    // size differs from the base paint their top-left overlap, mirroring
    // PackGen's drawImage(img, 0, 0) compositing. Application order matches
    // PackGen's generatePack(): overlay1 → overlay2 → gold → demon1 →
    // demon2 → glow.
    static ImageBuffer apply(ImageBuffer const& base,
                             OverlayImages const& overlays,
                             TintColors const& colors,
                             TinterOptions options = {});

private:
    OverlayTinter() = delete;
};

}  // namespace paimon::texture_studio
