#include <Geode/Geode.hpp>
#include <Geode/modify/CustomListView.hpp>
#include "../features/thumbnails/ui/LevelCellSettingsPopup.hpp"

using namespace geode::prelude;

// Normal GD cell heights for Level types (determined from getCellHeight binding)
static constexpr float NORMAL_LEVEL_CELL_HEIGHT = 90.f;
static constexpr float COMPACT_LEVEL_CELL_HEIGHT = 45.f;

// Cached compact mode value — avoids mutex-locked getSettingValue() on every
// getCellHeight call (hot path during scrolling/layout, called many times/frame).
static bool s_cachedCompactMode = false;
static int s_cachedCompactVersion = -1;

static bool getCachedCompactMode() {
    int ver = LevelCellSettingsPopup::s_settingsVersion;
    if (ver != s_cachedCompactVersion) {
        s_cachedCompactVersion = ver;
        s_cachedCompactMode = Mod::get()->getSettingValue<bool>("compact-list-mode");
    }
    return s_cachedCompactMode;
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
        // use Level4 which renders compact cells natively.
        if (type == BoomListType::Level && getCachedCompactMode()) {
            type = BoomListType::Level4;
        }
        return CustomListView::create(entries, delegate, width, height, count, type, cellHeight);
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

        if (isLevelType && getCachedCompactMode()) {
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

        return original;
    }
};
