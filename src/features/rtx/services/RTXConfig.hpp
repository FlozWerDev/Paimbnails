#pragma once

// JSON propio (rtx_config.json): demasiados campos para saved values.

#include <string>

namespace paimon::rtx {

enum class Preset : int {
    Performance = 0,
    Balanced    = 1,
    Quality     = 2,
    Ultra       = 3,
    Custom      = 4,
};

enum class Tonemap : int {
    None       = 0,
    Reinhard   = 1,
    ACES       = 2,
    Filmic     = 3,
    Uncharted2 = 4,
};

struct RTXConfig {
    // Defaults = preset Balanced (no un hibrido sin nombre).
    bool  enabled          = false;
    float intensity        = 1.00f;

    int   preset           = static_cast<int>(Preset::Balanced);
    float renderScale      = 0.50f;
    int   rayCount         = 3;
    int   raySteps         = 14;
    float rayDistance      = 0.28f;
    float stepGrowth       = 1.28f;
    bool  adaptive         = true;
    int   targetFps        = 60;
    int   frameSkip        = 0;

    float hdrRange         = 6.00f;

    float giStrength       = 0.55f;
    float giSaturation     = 1.00f;
    float lightThreshold   = 0.60f;
    float lightRange       = 0.35f;
    float bounceFalloff    = 2.20f;
    float normalStrength   = 6.00f;
    float thickness        = 0.35f;

    float aoStrength       = 0.35f;
    float aoRadius         = 0.12f;
    float aoPower          = 1.40f;

    float reflectStrength  = 0.20f;
    float reflectRoughness = 0.25f;
    float reflectFresnel   = 0.70f;
    float reflectFade      = 0.45f;

    float bloomStrength    = 0.35f;
    float bloomThreshold   = 0.70f;
    float bloomSoftKnee    = 0.55f;
    float bloomRadius      = 1.20f;
    float bloomBlend       = 0.75f;
    float bloomAnamorphic  = 0.00f;
    int   bloomPasses      = 4;

    float godRayStrength   = 0.15f;
    float godRayDecay      = 0.94f;
    float godRayDensity    = 0.85f;
    float godRayX          = 0.50f;
    float godRayY          = 0.82f;

    float denoise          = 2.00f;
    int   atrousPasses     = 3;
    float temporal         = 0.88f;
    bool  ghostClamp       = true;
    float clampSigma       = 1.40f;

    int   tonemap          = static_cast<int>(Tonemap::ACES);
    float exposure         = 0.00f;
    float contrast         = 1.00f;
    float saturation       = 1.00f;
    float temperature      = 0.00f;
    float tint             = 0.00f;
    float gamma            = 1.00f;

    bool  adaptEnabled     = false;
    float adaptKey         = 0.18f;
    float adaptSpeed       = 1.20f;

    float chromatic        = 0.00f;
    float vignette         = 0.20f;
    float grain            = 0.00f;
    float sharpen          = 0.15f;

    bool  inGameplay       = true;
    bool  inEditor         = false;
    bool  inMenus          = true;
    bool  skipWhenPaused   = false;
};

// Solo toca campos de coste; conserva el resto. Custom no toca nada.
void applyPreset(RTXConfig& cfg, Preset preset);

char const* presetName(int preset);
char const* tonemapName(int tonemap);

} // namespace paimon::rtx
