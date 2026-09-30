#pragma once
// hooks containers (gjgaragelayer, gjshoplayer) instead of gjitemicon::init (brittle).

#include <Geode/cocos/include/ccTypes.h>

namespace cocos2d {
    class CCNode;
}
class GJItemIcon;
class ListButtonBar;

namespace paimon::icons {

// userobject keys stamped on icons we touch.
inline constexpr char const* kIconRecoloredKey = "paimbnails/icon-recolored";
// lock flag stamped by the changetolockedstate hook; opacity-based detection
// breaks as soon as a lock style changes the opacity.
inline constexpr char const* kIconLockedKey = "paimbnails/locked-icon";
// stock locked look captured right after vanilla runs; replayed verbatim on
// restore (rebuilding from constants washed locked icons out to white).
inline constexpr char const* kIconLockSnapshotKey = "paimbnails/locked-vanilla-snapshot";

// areas where we recolor. only areas with an actual hook exist here.
enum class RecolorArea {
    IconKit,
    Shop,
};

class IconRecolorEngine final {
public:
    static IconRecolorEngine& get();

    // pass visible bar/menu, not the whole layer, for correct index-based modes.
    void recolorSubtree(cocos2d::CCNode* root, RecolorArea area);

    void recolorListBar(ListButtonBar* bar, RecolorArea area);

    bool recolorOne(GJItemIcon* icon, int displayIndex, int totalCount, RecolorArea area);

    // call right after vanilla changetolockedstate (before any lock style is
    // applied) so restore can replay the exact stock locked look.
    void snapshotLockedVanilla(GJItemIcon* icon);

    // put every icon we touched back to its stock look (master switch off).
    void restoreVanilla(cocos2d::CCNode* root);
    void restoreListBar(ListButtonBar* bar);

private:
    IconRecolorEngine() = default;
    bool isAreaEnabled(RecolorArea area) const;
    void restoreOne(GJItemIcon* icon);
};

}  // namespace paimon::icons
