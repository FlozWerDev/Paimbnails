#pragma once
// Hooks containers (GJGarageLayer, GJShopLayer) instead of GJItemIcon::init (brittle).

#include <Geode/cocos/include/ccTypes.h>

namespace cocos2d {
    class CCNode;
}
class GJItemIcon;
class ListButtonBar;

namespace paimon::icons {

// UserObject keys stamped on icons we touch.
inline constexpr char const* kIconRecoloredKey = "paimbnails/icon-recolored";
// Set by the GJItemIcon::changeToLockedState hook. Opacity-based lock
// detection breaks as soon as a lock style changes the opacity, so the flag
// is the durable source of truth.
inline constexpr char const* kIconLockedKey = "paimbnails/locked-icon";

// Areas where we recolor. Only areas with an actual hook exist here.
enum class RecolorArea {
    IconKit,
    Shop,
};

class IconRecolorEngine final {
public:
    static IconRecolorEngine& get();

    // Pass visible bar/menu, not the whole layer, for correct index-based modes.
    void recolorSubtree(cocos2d::CCNode* root, RecolorArea area);

    void recolorListBar(ListButtonBar* bar, RecolorArea area);

    bool recolorOne(GJItemIcon* icon, int displayIndex, int totalCount, RecolorArea area);

    // Put every icon we touched back to its stock look (master switch off).
    void restoreVanilla(cocos2d::CCNode* root);
    void restoreListBar(ListButtonBar* bar);

private:
    IconRecolorEngine() = default;
    bool isAreaEnabled(RecolorArea area) const;
    void restoreOne(GJItemIcon* icon);
};

}  // namespace paimon::icons
