#pragma once
// no-moreicons fallback: applies the built icon via CCSpriteFrameCache.
// with More Icons installed this service defers frame loading to it and only
// handles exact-color whitening.

#include <Geode/Geode.hpp>

#include <map>
#include <string>
#include <string_view>
#include <vector>

class SimplePlayer;
class PlayerObject;
class GJRobotSprite;

namespace paimon::icon_maker {

class IconApplier final {
public:
    // RAII guard: while active on the main thread, SimplePlayer::updatePlayerFrame
    // will NOT replace vanilla frames with the user's active custom icon.
    // Used around vanilla grid builders & template/reference previews.
    struct VanillaScope {
        VanillaScope();
        ~VanillaScope();
        VanillaScope(VanillaScope const&) = delete;
        VanillaScope& operator=(VanillaScope const&) = delete;
    };

    static bool isVanillaSuppressed();

    static IconApplier& get();

    // persisted selection (savedValue "icon-maker.active").
    void setActive(IconType type, std::string slotId);
    void clearActive(IconType type);
    std::string activeFor(IconType type);

    // cleanly un-equip and purge caches when a project is deleted.
    void onDeleteProject(std::string_view slotId, IconType type);

    // hook entry: runs after SimplePlayer::updatePlayerFrame.
    void onUpdatePlayerFrame(SimplePlayer* player, int iconId, IconType type);

    // directly apply a specific custom icon slot onto any SimplePlayer (e.g. garage bar preview).
    bool applySlotToSimplePlayer(SimplePlayer* player, IconType type,
                                 std::string const& slotId, bool forceApply = false);

    // gameplay hook entry: applies active custom icon onto PlayerObject sprites.
    void applyToPlayerObject(PlayerObject* player, IconType type);

    // robot/spider: swap the animated part sprites (transcribed from
    // More Icons' updateRobotSprite, MIT).
    void applyToRobotSprite(GJRobotSprite* sprite, IconType type,
                            std::string const& slotId);

    // re-whitening bypasses the player tint for projects that request exact colors.
    void applyExactColors(SimplePlayer* player, IconType type);
    void applyExactColorsForSlot(SimplePlayer* player, IconType type,
                                 std::string const& slotId);
    void applyExactColorsToPlayerObject(PlayerObject* player, IconType type);

    // drop every cached texture/frame; called before GameManager::reloadAll
    // recreates the GL context (registered in core/GLContextReload.cpp).
    void onGLContextReload();

    // forget the cached sheet of one icon (after recompiling it).
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
