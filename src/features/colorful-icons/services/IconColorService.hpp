#pragma once
//
// IconColorService.hpp - Decides which color triple a given icon should use.
//
// The service is a stateless transformer: given (icon descriptor, current
// config, current player colors, animation phase) it returns a (primary,
// secondary, glow, hasGlow) triple. The hooks then apply that to the
// SimplePlayer.
//
// Keeping color decisions out of the hook itself lets us:
//   - unit-test color logic without GD
//   - reuse it from the config popup live preview
//   - swap modes without recompiling hooks
//

#include "../PaimonIconsConfig.hpp"

#include <Geode/cocos/include/ccTypes.h>

#include <cstdint>

namespace paimon::icons {

// Identifying info for one icon. We keep this minimal; the full SimplePlayer*
// is passed separately to the hook layer.
struct IconDescriptor {
    int unlockTypeRaw = 0;   // GJItemIcon::m_unlockType, raw int (UnlockType enum class)
    int iconID        = 1;
    int gamemodeIndex = 0;   // 0..8, IconType - used by ColorMode::PerGamemode
    int displayIndex  = 0;   // Position in the visible list - used by Gradient mode
    int totalCount    = 1;   // Total visible icons - used by Gradient mode
    bool isLocked     = false;
};

// Resolved color triple to apply.
struct IconColorTriple {
    cocos2d::ccColor3B primary  {255, 255, 255};
    cocos2d::ccColor3B secondary{180, 180, 180};
    cocos2d::ccColor3B glow     {255, 255, 255};
    bool hasGlow = false;
};

class IconColorService final {
public:
    static IconColorService& get();

    // Compute the color triple for one icon.
    // Reads the current config and the live player colors from GameManager.
    IconColorTriple resolve(IconDescriptor const& desc) const;

    // Same but accepts an explicit "now" (in seconds since the engine started)
    // for the animated modes (Rainbow, etc). The default overload uses the
    // global director time.
    IconColorTriple resolve(IconDescriptor const& desc, float nowSeconds) const;

    // Convenience: read the current player colors from GameManager.
    // Centralised so the rest of the codebase doesn't have to know about the
    // exact GM API surface.
    IconColorTriple readPlayerColors() const;

    // Helper: time in seconds used by the animated modes (CCDirector total
    // running time).
    static float globalTime();

private:
    IconColorService() = default;

    // Mode resolvers, one per ColorMode. They take the player base colors
    // already read so we don't hit GameManager 60 times per frame.
    IconColorTriple resolvePlayer(IconColorTriple const& base) const;
    IconColorTriple resolveCustomRGB(PaimonIconConfig const& cfg) const;
    IconColorTriple resolveHueShift(IconColorTriple const& base, PaimonIconConfig const& cfg) const;
    IconColorTriple resolveSatBoost(IconColorTriple const& base, PaimonIconConfig const& cfg) const;
    IconColorTriple resolveRandom(IconDescriptor const& desc, PaimonIconConfig const& cfg) const;
    IconColorTriple resolveRainbow(IconDescriptor const& desc, PaimonIconConfig const& cfg, float t) const;
    IconColorTriple resolveGradient(IconDescriptor const& desc, PaimonIconConfig const& cfg) const;
    IconColorTriple resolvePerGamemode(IconDescriptor const& desc, IconColorTriple const& base, PaimonIconConfig const& cfg) const;
    IconColorTriple resolveInverted(IconColorTriple const& base) const;
    IconColorTriple resolveMonochrome(PaimonIconConfig const& cfg) const;
};

}  // namespace paimon::icons
