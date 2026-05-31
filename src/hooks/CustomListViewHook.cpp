#include <Geode/Geode.hpp>
#include <Geode/modify/CustomListView.hpp>
#include <Geode/binding/LevelBrowserLayer.hpp>
#include <Geode/binding/LevelListLayer.hpp>
#include "../features/thumbnails/ui/LevelCellSettingsPopup.hpp"
#include "../framework/compat/ModCompat.hpp"
#include "LevelCellContext.hpp"

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
    //
    // IMPORTANT: We swap Level→Level4 for ALL contexts (incluyendo My Levels y
    // LevelListLayer) cuando compact mode esta activo. Esto coincide con el
    // comportamiento de Cvolton's CompactLists. Los enhancements de Paimbnails
    // (thumbnails, gradients, etc.) se siguen excluyendo via short-circuit en
    // el LevelCell hook para mantener la apariencia vanilla en esos contextos.
    static CustomListView* create(cocos2d::CCArray* entries, TableViewCellDelegate* delegate,
                                   float width, float height, int count, BoomListType type,
                                   float cellHeight) {
        bool isLevelType = type == BoomListType::Level ||
                           type == BoomListType::Level2 ||
                           type == BoomListType::Level3 ||
                           type == BoomListType::Level4;

        bool forceCompact = paimon::hooks::g_forceCompactLevelCells;

        // Compatibilidad con Cvolton/CompactLists: si su mod esta
        // cargado, ya hace este mismo swap. Aplicarlo dos veces termina
        // halveando el cellHeight dos veces (45->22) y rompe los items.
        // Cedemos y dejamos que su mod maneje el modo compacto.
        if (paimon::compat::ModCompat::isCompactListsLoaded()) {
            return CustomListView::create(entries, delegate, width, height, count, type, cellHeight);
        }

        // Convertir Level -> Level4 si compact mode esta activo (Cvolton pattern).
        // No excluimos MyLevels ni LevelListLayer del swap. El flag de suppress
        // (g_suppressCompactLevelCellsInContext) NO bloquea el swap aqui; ese
        // flag solo se usa para skip de enhancements del mod en el LevelCell hook.
        bool compactEnabled = isLevelType && (getCachedCompactMode() || forceCompact);

        if (compactEnabled && type == BoomListType::Level) {
            type = BoomListType::Level4;
            // Para listas que pasan cellHeight explicito (ej. My Levels pasa 90),
            // halvear el cellHeight para que coincida con el layout compact.
            if (cellHeight > 0.f && cellHeight <= 200.f) {
                cellHeight *= 0.5f;
            }
        }

        return CustomListView::create(entries, delegate, width, height, count, type, cellHeight);
    }

    // Also hook getCellHeight as a fallback for lists that are already created
    // (e.g. when toggling the setting without reloading the page)
    static float getCellHeight(BoomListType type) {
        float original = CustomListView::getCellHeight(type);

        // Si CompactLists esta cargado, ceder y no halvear nada.
        if (paimon::compat::ModCompat::isCompactListsLoaded()) {
            return original;
        }

        bool compactEnabled = getCachedCompactMode() || paimon::hooks::g_forceCompactLevelCells;
        bool contextSuppressCompact = paimon::hooks::g_suppressCompactLevelCellsInContext;

        // Only override for Level-type cells
        bool isLevelType = type == BoomListType::Level ||
                           type == BoomListType::Level2 ||
                           type == BoomListType::Level3 ||
                           type == BoomListType::Level4;

        // Si el contexto explicitamente suprime compact mode (forzado externo),
        // respetar el cellHeight natural de GD.
        if (contextSuppressCompact) {
            return original;
        }

        if (isLevelType && compactEnabled) {
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
