#pragma once
//
// LuminanceTinter.hpp - Recolors a sprite using the PackGen-style luminance
// algorithm. This is the same math `tintImageWithLuminance()` performs in
// PackGen's script.js, ported to C++ and extended to consume MaskSet inputs.
//
// Algorithm (per pixel):
//
//   if sourceAlpha == 0:
//       outputPixel = (0, 0, 0, 0)
//       continue
//
//   luminance = 0.30*R + 0.59*G + 0.11*B          // Rec.601 (PackGen)
//   factor    = luminance / brightness             // brightness ∈ [100, 300]
//
//   tintedC1 = userC1 * factor                     // applied where mask_c1 > 0
//   tintedC2 = userC2 * factor                     // applied where mask_c2 > 0
//   tintedGlow = userGlow * factor                 // applied where mask_glow > 0
//
// Composition order matches PackGen:
//
//   result = base                                   // start with original pixel
//   if mask_c1 > 0:   result = overlay(result, tintedC1, mask_c1)
//   if mask_c2 > 0:   result = overlay(result, tintedC2, mask_c2)
//   if mask_glow > 0: result = overlay(result, tintedGlow, mask_glow)
//   if mask_outline > 0: leave the original pixel alone (already in result)
//
// `overlay()` is straight-alpha compositing using the mask byte as alpha.
//
// Why preserve luminance? Because the original sprite encodes shadows and
// gradients in its luminance. By tinting only the hue/chroma we keep the
// shading details intact — the result looks like "the same sprite, just
// in a different color" instead of a flat decal.
//

#include "../data/ImageBuffer.hpp"
#include "MaskBuilder.hpp"

#include <Geode/cocos/include/ccTypes.h>

#include <cstdint>

namespace paimon::texture_studio {

struct TintColors {
    cocos2d::ccColor3B color1{149, 226, 3};      // PackGen default green
    cocos2d::ccColor3B color2{28, 233, 255};     // PackGen default cyan
    cocos2d::ccColor3B glow  {255, 255, 255};    // white
};

struct TinterOptions {
    // PackGen's "brightness" parameter. Range 100..300; default 160.
    // Lower = brighter (factor > 1 amplifies user color);
    // Higher = darker  (factor < 1 dampens it).
    // 160 is what PackGen recommends and what we use as default.
    int brightness = 160;

    // When true, treat the glow mask with PackGen's "alternative" overlay:
    // pixels where the glow mask > 0 are REPLACED entirely (no alpha-blend
    // with the underlying base). PackGen offers this as a checkbox for
    // dark glow colors that would otherwise show base-color bleed-through.
    bool alternativeGlowOverlay = false;

    // Preservar outlines oscuros: pixeles cuya luminancia original (Rec.601,
    // escala 0..255) esté por debajo de este umbral NO se tiñen — conservan
    // su RGB original aunque alguna máscara los cubra. Protege los contornos
    // negros de los botones que el clasificador no marcó como Outline, que
    // de otro modo se "ensucian" con el color del usuario y pierden
    // definición. 0 = desactivado.
    // Keep this disabled by default: the outline mask is the source of
    // truth. A global luminance cutoff also protects legitimate dark Color2
    // details and makes them impossible to recolor.
    int darkOutlineThreshold = 0;
};

class LuminanceTinter final {
public:
    // Apply the PackGen-style tint to `source` using the masks and user
    // colors. Returns a freshly allocated RGBA8 ImageBuffer (same size as
    // source). The source itself is not modified.
    //
    // Behaviour mirrors PackGen's processing order: base → C1 overlay →
    // C2 overlay → glow overlay. Pixels assigned to the Outline role are
    // copied through unchanged (they're never tinted).
    //
    // Pixels with alpha == 0 in the source remain transparent in output.
    static ImageBuffer apply(ImageBuffer const& source,
                             MaskSet const& masks,
                             TintColors const& colors,
                             TinterOptions options = {});

private:
    LuminanceTinter() = delete;
};

}  // namespace paimon::texture_studio
