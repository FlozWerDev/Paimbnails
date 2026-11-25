#pragma once

#include <algorithm>
#include <cmath>

namespace paimon::glass {

// quality tiers for the Liquid Glass UI material. Header-only and pure so
// the portable regression suite can pin the budgets.
enum class GlassTier : int {
    Off = 0,
    Simulated = 1, // tint + rim only, no framebuffer copy at all
    Lite = 2,      // two copies per frame, each refreshed ~12 times per second
    Balanced = 3,  // live copies every frame, layered panel copies
    High = 4,      // deeper blur chain, more copies per frame
};

struct GlassSettings {
    bool enabled = false;
    int tier = 0; // 0 = Auto, then GlassTier values + 1 (Simulated=2 ...)
    float blur = 0.65f;        // 0..1 -> frost amount and blur spread
    float refraction = 0.6f;   // 0..1 -> bezel thickness
    float tintOpacity = 0.22f; // 0..0.8
    float rim = 0.7f;          // 0..1 -> specular rim
    float cornerRadius = 12.f; // panel corners, points, 0..24
    int tintR = 150;
    int tintG = 170;
    int tintB = 210;
    bool popups = true;         // popup and Hub panels
    bool buttons = true;        // every ButtonSprite
    bool coloredButtons = true; // buttons keep the GJ_button colour as tint
};

inline float sanitizeGlassFloat(float value, float fallback, float lo, float hi) {
    if (!std::isfinite(value)) return fallback;
    return std::clamp(value, lo, hi);
}

inline GlassSettings sanitizeGlassSettings(GlassSettings s) {
    GlassSettings const defaults;
    s.tier = std::clamp(s.tier, 0, 5);
    s.blur = sanitizeGlassFloat(s.blur, defaults.blur, 0.f, 1.f);
    s.refraction = sanitizeGlassFloat(s.refraction, defaults.refraction, 0.f, 1.f);
    s.tintOpacity = sanitizeGlassFloat(s.tintOpacity, defaults.tintOpacity, 0.f, 0.8f);
    s.rim = sanitizeGlassFloat(s.rim, defaults.rim, 0.f, 1.f);
    s.cornerRadius = sanitizeGlassFloat(s.cornerRadius, defaults.cornerRadius, 0.f, 24.f);
    s.tintR = std::clamp(s.tintR, 0, 255);
    s.tintG = std::clamp(s.tintG, 0, 255);
    s.tintB = std::clamp(s.tintB, 0, 255);
    return s;
}

inline GlassTier resolveTier(GlassSettings const& settings, bool isMobile) {
    if (!settings.enabled) return GlassTier::Off;
    if (settings.tier == 0) return isMobile ? GlassTier::Lite : GlassTier::Balanced;
    // stored selector is 1-based above GlassTier::Off.
    int raw = std::clamp(settings.tier - 1, 1, 4);
    return static_cast<GlassTier>(raw);
}

// preserve saved selectors (2 = Simulated ... 5 = High). They are not
// positions in the popup's Auto -> High -> Balanced -> Lite -> Simulated cycle.
inline char const* glassTierName(int selector) {
    switch (selector) {
        case 5: return "High";
        case 4: return "Balanced";
        case 3: return "Lite";
        case 1:
        case 2: return "Simulated";
        default: return "Auto";
    }
}

inline int nextGlassTier(int selector) {
    switch (selector) {
        case 0: return 5;
        case 5: return 4;
        case 4: return 3;
        case 3: return 2;
        default: return 0;
    }
}

struct GlassProfile {
    bool live = false;            // copies the framebuffer behind each plate
    int blurLevels = 0;           // dual-kawase downsample passes (result at 1/2 res)
    bool layered = false;         // a panel drawn over a glass panel gets its own copy
    int maxCapturesPerFrame = 0;  // hard budget; extra plates reuse the last copy
    float refreshSeconds = 0.f;   // 0 = refresh every frame
    bool dispersion = false;      // per-channel refraction at the rim
    bool interactiveLight = false; // desktop: the specular follows the cursor
};

inline GlassProfile profileFor(GlassTier tier) {
    switch (tier) {
        case GlassTier::High:     return {true, 4, true, 12, 0.f, true, true};
        case GlassTier::Balanced: return {true, 3, true, 6, 0.f, true, true};
        case GlassTier::Lite:     return {true, 2, false, 2, 1.f / 12.f, false, false};
        default:                  return {};
    }
}

// kawase tap distance, in source texels, for the saved blur amount.
inline float blurSpreadFor(float blur) {
    return 0.75f + 1.75f * sanitizeGlassFloat(blur, 0.65f, 0.f, 1.f);
}

// share of the blurred copy in the final colour. Panels never drop below
// half frost so popup text stays readable; buttons can go fully clear,
// like Apple's controls.
inline float frostFor(float blur, bool isPanel) {
    float const b = sanitizeGlassFloat(blur, 0.65f, 0.f, 1.f);
    return isPanel ? 0.5f + 0.5f * b : 0.8f * b;
}

} // namespace paimon::glass
