#pragma once
//
// IconLockStyler.hpp - Applies the chosen LockStyle to an icon shown as
// "locked" or "unobtainable".
//
// Crucially this never uses CCRenderTexture: every effect is achieved by
// flipping visibility, opacity, color and z-order of existing children. This
// is cheaper at runtime and keeps the implementation independent from the
// reference mod's approach.
//

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

    // Apply the configured lock style to a GJItemIcon. The padlock sprite is
    // looked up by index because GJItemIcon doesn't expose it directly.
    // Safe to call on any icon: if the icon isn't locked we no-op.
    //
    // Pre-conditions:
    //   - GJItemIcon::changeToLockedState has already run (so internal child
    //     visibility is in the "locked" state).
    //   - The associated SimplePlayer (if any) hasn't yet been hidden by us.
    //
    // We try to detect "unobtainable" via GameStatsManager.
    void apply(GJItemIcon* icon);

private:
    IconLockStyler() = default;

    // Is the (unlockType, unlockID) currently impossible to obtain in the
    // running build of GD? We check GameStatsManager::getItemUnlockState().
    bool isUnobtainable(GJItemIcon* icon) const;

    // Helper to fetch the padlock CCSprite GJItemIcon spawns at child index 1
    // when going into the locked state.
    cocos2d::CCSprite* findLockSprite(GJItemIcon* icon) const;

    // Apply each style. They each take whether this is an "unobtainable"
    // icon so they can use the deeper opacity/tint configured for those.
    void applyShowDimmed(GJItemIcon* icon, SimplePlayer* sp, bool unobtainable, PaimonIconConfig const& cfg);
    void applyTinted    (GJItemIcon* icon, SimplePlayer* sp, bool unobtainable, PaimonIconConfig const& cfg);
    void applySilhouette(GJItemIcon* icon, SimplePlayer* sp, bool unobtainable, PaimonIconConfig const& cfg);
    void applyCustomMix (GJItemIcon* icon, SimplePlayer* sp, bool unobtainable, PaimonIconConfig const& cfg);
    void applyHideBoth  (GJItemIcon* icon, SimplePlayer* sp);
};

}  // namespace paimon::icons
