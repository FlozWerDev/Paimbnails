#pragma once
// Sistema propio de aplicación (fallback sin MoreIcons): guarda qué icono
// creado está activo por gamemode, carga sus frames compilados en el
// CCSpriteFrameCache y los inyecta en SimplePlayer tras updatePlayerFrame.
//
// Cuando MoreIcons está instalado este servicio NUNCA toca sprites: la vía
// MoreIcons (MoreIconsBridge) es la dueña de la selección.

#include <Geode/Geode.hpp>

#include <map>
#include <string>
#include <vector>

class SimplePlayer;

namespace paimon::icon_maker {

class IconApplier final {
public:
    static IconApplier& get();

    // Persisted selection (savedValue "icon-maker.active").
    void setActive(IconType type, std::string slotId);
    void clearActive(IconType type);
    std::string activeFor(IconType type);

    // Hook entry: runs after SimplePlayer::updatePlayerFrame.
    void onUpdatePlayerFrame(SimplePlayer* player, int iconId, IconType type);

    // Robot/spider: swap the animated part sprites (transcribed from
    // MoreIcons' updateRobotSprite, MIT).
    void applyToRobotSprite(GJRobotSprite* sprite, IconType type,
                            std::string const& slotId);

    // GD multiplies player colors onto icon layers; when the active project
    // asks for exact colors, re-whiten them so baked gradients/images show
    // as designed. Safe to call any time; no-op when not applicable.
    void applyExactColors(SimplePlayer* player, IconType type);

    // Drop every cached texture/frame; called before GameManager::reloadAll
    // recreates the GL context (registered in core/GLContextReload.cpp).
    void onGLContextReload();

    // Forget the cached sheet of one icon (after recompiling it).
    void invalidate(std::string_view slotId);

private:
    IconApplier() = default;

    struct LoadedSheet {
        geode::Ref<cocos2d::CCTexture2D> texture;
        std::vector<std::string> frameNames;  // registered in CCSpriteFrameCache
        bool valid = false;
    };

    void loadSelection();
    void saveSelection();
    LoadedSheet* ensureLoaded(std::string const& slotId);
    void unloadSheet(LoadedSheet& sheet);

    bool m_selectionLoaded = false;
    std::map<int, std::string> m_active;          // IconType raw -> slotId
    std::map<std::string, LoadedSheet> m_sheets;  // slotId -> loaded sheet
};

}  // namespace paimon::icon_maker
