#include <Geode/Geode.hpp>
#include <Geode/modify/CustomListView.hpp>
#include "../features/thumbnails/ui/LevelCellSettingsPopup.hpp"
#include "../framework/compat/ModCompat.hpp"
#include "../core/Settings.hpp"
#include "LevelCellContext.hpp"

using namespace geode::prelude;

// fallback when gd returns no value
static constexpr float NORMAL_LEVEL_CELL_HEIGHT = 90.f;
static constexpr float COMPACT_LEVEL_CELL_HEIGHT = 45.f;

// cached: getsettingvalue() locks, and getcellheight runs per frame while scrolling
static bool s_cachedCompactMode = false;
static int s_cachedCompactVersion = -1;

static bool getCachedCompactMode() {
    // both version counters only grow, so their sum detects changes.
    int ver = LevelCellSettingsPopup::s_settingsVersion + static_cast<int>(
        paimon::settings::internal::g_settingsVersion.load(std::memory_order_relaxed));
    if (ver != s_cachedCompactVersion) {
        s_cachedCompactVersion = ver;
        s_cachedCompactMode = paimon::settings::thumbnails::compactListMode();
    }
    return s_cachedCompactMode;
}

static bool isLevelListType(BoomListType type) {
    return type == BoomListType::Level || type == BoomListType::Level2 ||
        type == BoomListType::Level3 || type == BoomListType::Level4;
}

class $modify(PaimonCustomListView, CustomListView) {
    // compact mode: gd renders level4 half-height, so swap level→level4 at create time
    static CustomListView* create(cocos2d::CCArray* entries, TableViewCellDelegate* delegate,
                                   float width, float height, int count, BoomListType type,
                                   float cellHeight) {
        bool forceCompact = paimon::hooks::g_forceCompactLevelCells;

        // compactlists already swaps; avoid applying it twice
        if (paimon::compat::ModCompat::isCompactListsLoaded()) {
            return CustomListView::create(entries, delegate, width, height, count, type, cellHeight);
        }

        // the suppress flag only skips levelcell enhancements, not this swap
        bool compactEnabled = isLevelListType(type) && (getCachedCompactMode() || forceCompact);

        if (compactEnabled && type == BoomListType::Level) {
            type = BoomListType::Level4;
            // some lists pass an explicit height; halve it too
            if (cellHeight > 0.f && cellHeight <= 200.f) {
                cellHeight *= 0.5f;
            }
        }

        return CustomListView::create(entries, delegate, width, height, count, type, cellHeight);
    }

    // covers lists created before the setting changed
    static float getCellHeight(BoomListType type) {
        float original = CustomListView::getCellHeight(type);

        if (paimon::compat::ModCompat::isCompactListsLoaded()) {
            return original;
        }

        bool compactEnabled = getCachedCompactMode() || paimon::hooks::g_forceCompactLevelCells;
        bool contextSuppressCompact = paimon::hooks::g_suppressCompactLevelCellsInContext;

        if (contextSuppressCompact) {
            return original;
        }

        if (isLevelListType(type) && compactEnabled) {
            // level4 already compact; halving again gives ~22px cells
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
