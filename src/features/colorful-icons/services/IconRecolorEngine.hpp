#pragma once
// Hooks containers (GJGarageLayer, ShopLayer) instead of GJItemIcon::init (brittle).

#include <Geode/cocos/include/ccTypes.h>

namespace cocos2d {
    class CCNode;
}
class GJItemIcon;
class SimplePlayer;
class ListButtonBar;

namespace paimon::icons {

// Areas where we recolor. Used to gate recoloring on the per-area Apply flag.
enum class RecolorArea {
    IconKit,
    Shop,
    Achievement,
    Reward,
    Profile,
    Comment,
    LevelCell,
};

class IconRecolorEngine final {
public:
    static IconRecolorEngine& get();

    // Pass visible bar/menu, not the whole layer, for correct index-based modes.
    void recolorSubtree(cocos2d::CCNode* root, RecolorArea area);

    void recolorListBar(ListButtonBar* bar, RecolorArea area);

    bool recolorOne(GJItemIcon* icon, int displayIndex, int totalCount, RecolorArea area);

    void applySelectedHighlight(cocos2d::CCNode* root, int equippedID, int gamemodeIndex);

    void applyAnimations(cocos2d::CCNode* root, RecolorArea area);

    void revertSubtree(cocos2d::CCNode* root);

private:
    IconRecolorEngine() = default;
    bool isAreaEnabled(RecolorArea area) const;
    int gamemodeIndexOf(SimplePlayer* sp) const;
};

}  // namespace paimon::icons
