#pragma once
// no-moreicons fallback: applies the built icon via ccspriteframecache. with
// moreicons installed this service never touches sprites.

#include <Geode/Geode.hpp>

#include <map>
#include <string>
#include <vector>

class SimplePlayer;

namespace paimon::icon_maker {

class IconApplier final {
public:
    static IconApplier& get();

    // persisted selection (savedvalue "icon-maker.active").
    void setActive(IconType type, std::string slotId);
    void clearActive(IconType type);
    std::string activeFor(IconType type);

    // hook entry: runs after simpleplayer::updateplayerframe.
    void onUpdatePlayerFrame(SimplePlayer* player, int iconId, IconType type);

    // robot/spider: swap the animated part sprites (transcribed from
    // moreicons' updaterobotsprite, mit).
    void applyToRobotSprite(GJRobotSprite* sprite, IconType type,
                            std::string const& slotId);

    // re-whitening bypasses the player tint for projects that request exact colors.
    void applyExactColors(SimplePlayer* player, IconType type);

    // drop every cached texture/frame; called before gamemanager::reloadall
    // recreates the gl context (registered in core/glcontextreload.cpp).
    void onGLContextReload();

    // forget the cached sheet of one icon (after recompiling it).
    void invalidate(std::string_view slotId);

private:
    IconApplier() = default;

    struct LoadedSheet {
        geode::Ref<cocos2d::CCTexture2D> texture;
        std::vector<std::string> frameNames;  // registered in ccspriteframecache
        bool valid = false;
    };

    void loadSelection();
    void saveSelection();
    LoadedSheet* ensureLoaded(std::string const& slotId);
    void unloadSheet(LoadedSheet& sheet);

    bool m_selectionLoaded = false;
    std::map<int, std::string> m_active;          // icontype raw -> slotid
    std::map<std::string, LoadedSheet> m_sheets;  // slotid -> loaded sheet
};

}  // namespace paimon::icon_maker
