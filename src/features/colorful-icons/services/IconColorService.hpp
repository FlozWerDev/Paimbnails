#pragma once
// stateless: (desc + config + player colors) → color triple.

#include "../PaimonIconsConfig.hpp"

#include <Geode/cocos/include/ccTypes.h>

namespace paimon::icons {

struct IconDescriptor {
    int unlockTypeRaw = 0;  // gjitemicon::m_unlocktype, raw int (unlocktype enum class)
    int iconID        = 1;
    int displayIndex  = 0;  // position in the visible list - used by gradient mode
    int totalCount    = 1;  // total visible icons - used by gradient mode
};

struct IconColorTriple {
    cocos2d::ccColor3B primary  {255, 255, 255};
    cocos2d::ccColor3B secondary{180, 180, 180};
    cocos2d::ccColor3B glow     {255, 255, 255};
    bool hasGlow = false;
};

class IconColorService final {
public:
    static IconColorService& get();

    IconColorTriple resolve(IconDescriptor const& desc) const;

    IconColorTriple resolve(IconDescriptor const& desc, float nowSeconds) const;

    IconColorTriple readPlayerColors() const;

    static float globalTime();

private:
    IconColorService() = default;

    IconColorTriple resolvePlayer(IconColorTriple const& base) const;
    IconColorTriple resolveCustomRGB(PaimonIconConfig const& cfg) const;
    IconColorTriple resolveHueShift(IconColorTriple const& base, PaimonIconConfig const& cfg) const;
    IconColorTriple resolveRandom(IconDescriptor const& desc, PaimonIconConfig const& cfg) const;
    IconColorTriple resolveRainbow(IconDescriptor const& desc, PaimonIconConfig const& cfg, float t) const;
    IconColorTriple resolveGradient(IconDescriptor const& desc, PaimonIconConfig const& cfg) const;
    IconColorTriple resolveInverted(IconColorTriple const& base) const;
    IconColorTriple resolveMonochrome(PaimonIconConfig const& cfg) const;
};

}  // namespace paimon::icons
