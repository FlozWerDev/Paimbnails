#include <Geode/Geode.hpp>
#include <Geode/modify/CustomListView.hpp>
#include <Geode/binding/LevelBrowserLayer.hpp>
#include <Geode/binding/LevelListLayer.hpp>
#include "../features/thumbnails/ui/LevelCellSettingsPopup.hpp"
#include "LevelCellContext.hpp"

using namespace geode::prelude;

// Normal GD cell heights for Level types (determined from getCellHeight binding)
static constexpr float NORMAL_LEVEL_CELL_HEIGHT = 90.f;
static constexpr float COMPACT_LEVEL_CELL_HEIGHT = 45.f;

// Cached compact mode value — avoids mutex-locked getSettingValue() on every
// getCellHeight call (hot path during scrolling/layout, called many times/frame).
static bool s_cachedCompactMode = false;
static int s_cachedCompactVersion = -1;
static bool s_suppressCompactHeight = false;
static bool s_inLevelListLayer = false;

static bool getCachedCompactMode() {
    int ver = LevelCellSettingsPopup::s_settingsVersion;
    if (ver != s_cachedCompactVersion) {
        s_cachedCompactVersion = ver;
        s_cachedCompactMode = Mod::get()->getSettingValue<bool>("compact-list-mode");
    }
    return s_cachedCompactMode;
}

static bool shouldCompactForDelegate(TableViewCellDelegate* delegate) {
    if (!delegate) return true;

    // Use typeid name check instead of typeinfo_cast because other mods may
    // modify LevelListLayer, changing its RTTI type info and breaking the cast
    std::string_view className(typeid(*delegate).name());
    if (className.find("LevelListLayer") != std::string_view::npos) {
        return false;
    }

    if (auto browser = typeinfo_cast<LevelBrowserLayer*>(delegate)) {
        if (browser->m_searchObject && browser->m_searchObject->m_searchType == SearchType::MyLevels) {
            return false;
        }
    }

    return true;
}

class $modify(PaimonCustomListView, CustomListView) {
    // CompactLists-inspired: use Level4 type for compact mode.
    // GD natively renders Level4 as a compact cell (half height) with
    // m_compactView=true. This is the same approach Cvolton/compactlists-geode
    // uses — swap the BoomListType at create time so GD handles the layout.
    static CustomListView* create(cocos2d::CCArray* entries, TableViewCellDelegate* delegate,
                                   float width, float height, int count, BoomListType type,
                                   float cellHeight) {
        // When compact mode is enabled and the list is a Level type,
        // use Level4 which renders compact cells natively, and halve any
        // explicit cellHeight so the table rows match the compact layout.
        bool isLevelType = type == BoomListType::Level ||
                           type == BoomListType::Level2 ||
                           type == BoomListType::Level3 ||
                           type == BoomListType::Level4;
        
        // LevelListLayer should NEVER use compact mode - check this first
        // Use local variable instead of modifying global state
        bool forceCompact = paimon::hooks::g_forceCompactLevelCells;
        bool isLevelList = !shouldCompactForDelegate(delegate);
        if (isLevelList) {
            // Force compact mode off for LevelListLayer regardless of settings
            forceCompact = false;
        }
        
        bool compactEnabled = isLevelType && (getCachedCompactMode() || forceCompact);
        bool compactAllowed = compactEnabled && shouldCompactForDelegate(delegate);

        if (compactAllowed) {
            if (type == BoomListType::Level) {
                type = BoomListType::Level4;
            }
            // Halve explicit cellHeight (used for "My Levels" / created lists
            // which pass cellHeight=90 directly, overriding getCellHeight).
            if (type != BoomListType::Level4 && cellHeight > 0.f && cellHeight <= 200.f) {
                cellHeight *= 0.5f;
            }
        }

        bool oldSuppress = s_suppressCompactHeight;
        bool oldInLevelList = s_inLevelListLayer;
        s_suppressCompactHeight = compactEnabled && !compactAllowed;
        s_inLevelListLayer = isLevelList;
        auto* ret = CustomListView::create(entries, delegate, width, height, count, type, cellHeight);
        s_suppressCompactHeight = oldSuppress;
        s_inLevelListLayer = oldInLevelList;
        return ret;
    }

    // Also hook getCellHeight as a fallback for lists that are already created
    // (e.g. when toggling the setting without reloading the page)
    static float getCellHeight(BoomListType type) {
        float original = CustomListView::getCellHeight(type);

        // Only override for Level-type cells
        bool isLevelType = type == BoomListType::Level ||
                           type == BoomListType::Level2 ||
                           type == BoomListType::Level3 ||
                           type == BoomListType::Level4;

        // Check if compact mode should be suppressed for this context
        // LevelListLayer should NEVER use compact mode
        if (s_suppressCompactHeight) {
            // For LevelListLayer, ensure we return at least the normal height
            if (s_inLevelListLayer && isLevelType && original < NORMAL_LEVEL_CELL_HEIGHT) {
                return NORMAL_LEVEL_CELL_HEIGHT;
            }
            return original;
        }

        if (isLevelType && (getCachedCompactMode() || paimon::hooks::g_forceCompactLevelCells)) {
            // Level4 is already compact (create() swaps Level→Level4).
            // Don't halve it again or cells become ~22px (unusable).
            if (type == BoomListType::Level4) {
                return original > 0.f ? original : COMPACT_LEVEL_CELL_HEIGHT;
            }
            // Return compact height: half the normal height
            if (original > 0.f && original <= 200.f) {
                return original * 0.5f;
            }
            return COMPACT_LEVEL_CELL_HEIGHT;
        }

        // For LevelListLayer in normal mode, ensure we return at least the normal height
        if (s_inLevelListLayer && isLevelType && original < NORMAL_LEVEL_CELL_HEIGHT) {
            return NORMAL_LEVEL_CELL_HEIGHT;
        }

        return original;
    }
};
