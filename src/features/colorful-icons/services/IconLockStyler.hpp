#pragma once
// locked/unobtainable icon visibility/opacity only, no CCRenderTexture.

#include "../PaimonIconsConfig.hpp"

namespace cocos2d {
    class CCSprite;
}
class GJItemIcon;
class SimplePlayer;

namespace paimon::icons {

class IconLockStyler final {
public:
    static IconLockStyler& get();

    // Pre: GJItemIcon::changeToLockedState has run; SimplePlayer not yet hidden by us.
    void apply(GJItemIcon* icon);

    // Shared helpers, also used by the popup preview and the vanilla restore.
    static cocos2d::CCSprite* findLockSprite(GJItemIcon* icon);
    static void tintAllParts(SimplePlayer* sp, cocos2d::ccColor3B tint);
    static void fadeAllParts(SimplePlayer* sp, unsigned char opacity);

private:
    IconLockStyler() = default;

    bool isUnobtainable(GJItemIcon* icon) const;

    void applyShowDimmed(GJItemIcon* icon, SimplePlayer* sp, bool unobtainable, PaimonIconConfig const& cfg);
    void applyTinted    (GJItemIcon* icon, SimplePlayer* sp, bool unobtainable, PaimonIconConfig const& cfg);
    void applySilhouette(GJItemIcon* icon, SimplePlayer* sp, bool unobtainable, PaimonIconConfig const& cfg);
    void applyHideBoth  (GJItemIcon* icon, SimplePlayer* sp);
};

}  // namespace paimon::icons
