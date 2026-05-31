#pragma once
//
// IconRecolorEngine.hpp - High-level driver. Walks a layer (or any node tree)
// looking for GJItemIcon instances, builds an IconDescriptor for each, and
// asks IconColorService for a color triple, then applies it.
//
// Why a separate engine instead of doing this in the GJItemIcon hook?
//
//   - The GJItemIcon::init hook is famously fragile (the reference Colorful
//     Icons mod even ships a README warning). Instead of fighting that hook
//     we hook the *containers* that show icon collections (GJGarageLayer,
//     ShopLayer, AchievementsLayer, etc.) and call into the engine after
//     they've finished initialising.
//
//   - Doing it from a single place means we can pre-compute totalCount /
//     displayIndex (needed by Gradient mode) without each icon having to
//     introspect its parent.
//

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

    // Recolor a whole subtree. Walks every GJItemIcon found and applies the
    // configured mode. Pass the visible bar/menu, not the whole layer, when
    // you want index-based modes (Gradient) to align with what's on screen.
    //
    // The area flag is checked against ApplyToFlags; if disabled, no work
    // happens.
    void recolorSubtree(cocos2d::CCNode* root, RecolorArea area);

    // Walk the icon button-bar style used by GJGarageLayer. We treat its
    // first ListButtonPage as the visible page.
    void recolorListBar(ListButtonBar* bar, RecolorArea area);

    // Apply the configured colors to a single SimplePlayer-backed icon.
    // Returns true if anything changed; false if the icon is locked or
    // not a SimplePlayer (in which case the caller may want to skip).
    bool recolorOne(GJItemIcon* icon, int displayIndex, int totalCount, RecolorArea area);

    // Apply the chosen highlight ring to the icon currently equipped (if
    // animation flag enabled). The "current" detection is layer-specific
    // (in the kit it's the icon equal to GameManager::get()->getPlayerFrame())
    // so we accept the equippedID as a parameter.
    void applySelectedHighlight(cocos2d::CCNode* root, int equippedID, int gamemodeIndex);

    // Toggle/refresh idle-float and pulse animations on icons in a subtree.
    // Animations are applied as cocos2d actions so the engine itself doesn't
    // need a per-frame schedule.
    void applyAnimations(cocos2d::CCNode* root, RecolorArea area);

    // Clear all custom actions/colors we may have applied to a subtree. Used
    // when feature is disabled at runtime.
    void revertSubtree(cocos2d::CCNode* root);

private:
    IconRecolorEngine() = default;
    bool isAreaEnabled(RecolorArea area) const;
    int gamemodeIndexOf(SimplePlayer* sp) const;
};

}  // namespace paimon::icons
