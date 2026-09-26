#pragma once

#include "../data/ImageBuffer.hpp"
#include "LuminanceTinter.hpp"

namespace paimon::texture_studio {

// PackGen asset overlays, base-sized or empty. Tint recolors overlay pixels by luminance, then composites — PackGen's algorithm.
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
    // PackGen drawImage(img, 0, 0): mismatches paint top-left overlap, in generatePack() order.
    static ImageBuffer apply(ImageBuffer const& base,
                             OverlayImages const& overlays,
                             TintColors const& colors,
                             TinterOptions options = {});

private:
    OverlayTinter() = delete;
};

}  // namespace paimon::texture_studio
