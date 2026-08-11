#include <Geode/Geode.hpp>
#include <Geode/modify/CustomListView.hpp>
#include "../features/thumbnails/ui/LevelCellSettingsPopup.hpp"
#include "../framework/compat/ModCompat.hpp"
#include "../core/Settings.hpp"
#include "LevelCellContext.hpp"

using namespace geode::prelude;

// Vanilla and compact heights used when GD returns no value.
static constexpr float NORMAL_LEVEL_CELL_HEIGHT = 90.f;
static constexpr float COMPACT_LEVEL_CELL_HEIGHT = 45.f;

// Cached compact mode value — avoids mutex-locked getSettingValue() on every
// getCellHeight call (hot path during scrolling/layout, called many times/frame).
static bool s_cachedCompactMode = false;
static int s_cachedCompactVersion = -1;

static bool getCachedCompactMode() {
    // Both version counters only grow, so their sum detects changes.
    int ver = LevelCellSettingsPopup::s_settingsVersion + static_cast<int>(
        paimon::settings::internal::g_settingsVersion.load(std::memory_order_relaxed));
    if (ver != s_cachedCompactVersion) {
        s_cachedCompactVersion = ver;
        s_cachedCompactMode = paimon::settings::thumbnails::compactListMode();
    }
    return s_cachedCompactMode;
}

class $modify(PaimonCustomListView, CustomListView) {
    // Compact mode: GD renders Level4 as a half-height compact cell, so swap
    // Level→Level4 at create time and let GD handle the layout. The swap applies
    // to all contexts; Paimbnails enhancements are excluded separately in the
    // LevelCell hook to keep those contexts looking vanilla.
    static CustomListView* create(cocos2d::CCArray* entries, TableViewCellDelegate* delegate,
                                   float width, float height, int count, BoomListType type,
                                   float cellHeight) {
        bool isLevelType = type == BoomListType::Level ||
                           type == BoomListType::Level2 ||
                           type == BoomListType::Level3 ||
                           type == BoomListType::Level4;

        bool forceCompact = paimon::hooks::g_forceCompactLevelCells;

        // CompactLists already performs the swap; avoid applying it twice.
        if (paimon::compat::ModCompat::isCompactListsLoaded()) {
            return CustomListView::create(entries, delegate, width, height, count, type, cellHeight);
        }

        // Swap Level → Level4 when compact mode is active. The suppress flag
        // doesn't block the swap here; it only skips mod enhancements in the
        // LevelCell hook.
        bool compactEnabled = isLevelType && (getCachedCompactMode() || forceCompact);

        if (compactEnabled && type == BoomListType::Level) {
            type = BoomListType::Level4;
            // Some lists pass an explicit height; halve it too.
            if (cellHeight > 0.f && cellHeight <= 200.f) {
                cellHeight *= 0.5f;
            }
        }

        return CustomListView::create(entries, delegate, width, height, count, type, cellHeight);
    }

    // Cover lists that were created before the setting changed.
    static float getCellHeight(BoomListType type) {
        float original = CustomListView::getCellHeight(type);

        if (paimon::compat::ModCompat::isCompactListsLoaded()) {
            return original;
        }

        bool compactEnabled = getCachedCompactMode() || paimon::hooks::g_forceCompactLevelCells;
        bool contextSuppressCompact = paimon::hooks::g_suppressCompactLevelCellsInContext;

        bool isLevelType = type == BoomListType::Level ||
                           type == BoomListType::Level2 ||
                           type == BoomListType::Level3 ||
                           type == BoomListType::Level4;

        if (contextSuppressCompact) {
            return original;
        }

        if (isLevelType && compactEnabled) {
            // Level4 is already compact (create() swaps Level→Level4).
            // Don't halve it again or cells become ~22px (unusable).
            if (type == BoomListType::Level4) {
                return original > 0.f ? original : COMPACT_LEVEL_CELL_HEIGHT;
            }
            if (original > 0.f && original <= 200.f) {
                return original * 0.5f;
            }
            return COMPACT_LEVEL_CELL_HEIGHT;
        }

        return original;
    }
};
