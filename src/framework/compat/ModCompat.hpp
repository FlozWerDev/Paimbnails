#pragma once

// Detect active texture mods to avoid conflicts. Queries the Loader on each
// call (cheap, and the result can change as the user toggles mods at runtime).

#include <Geode/loader/Loader.hpp>

namespace paimon::compat {

struct ModCompat {
    static bool isHappyTexturesLoaded() {
        return geode::Loader::get()->isModLoaded("alphalaneous.happy_textures");
    }

    static bool isTextureLdrLoaded() {
        return geode::Loader::get()->isModLoaded("geode.texture-loader");
    }

    static bool isImagePlusLoaded() {
        return geode::Loader::get()->isModLoaded("prevter.imageplus");
    }

    static bool anyTextureModLoaded() {
        return isHappyTexturesLoaded() || isTextureLdrLoaded() || isImagePlusLoaded();
    }

    static bool isLevelTagsLoaded() {
        return geode::Loader::get()->isModLoaded("kampwski.level_tags");
    }

    // More Icons (hiimjustin000.more_icons): provides the custom-icon API.
    // OPTIONAL dependency; Global Icons degrades gracefully without it.
    static bool isMoreIconsLoaded() {
        return geode::Loader::get()->isModLoaded("hiimjustin000.more_icons");
    }

    // Mods that may collide with our features. We detect them to adjust
    // behavior (e.g. lower hook priority or cede ownership of a UI area).

    // CDC level thumbnails: incompatible; see mod.json + startup popup.
    static bool isCDCLevelThumbnailsLoaded() {
        return geode::Loader::get()->isModLoaded("cdc.level_thumbnails");
    }

    // CompactLists: same as our compact mode. If active, cede and let the user
    // use theirs.
    static bool isCompactListsLoaded() {
        return geode::Loader::get()->isModLoaded("cvolton.compactlists-geode");
    }

    // Compact Pause Menu: shrinks the paused song widget; our CustomSongWidget
    // hook must run after to restore size.
    static bool isCompactPauseMenuLoaded() {
        return geode::Loader::get()->isModLoaded("prevter.compact-pause-menu");
    }

    static bool isPrevterSmoothScrollLoaded() {
        return geode::Loader::get()->isModLoaded("prevter.smooth-scroll");
    }

    static bool isQuickVolumeControlsLoaded() {
        return geode::Loader::get()->isModLoaded("hjfod.quick-volume-controls");
    }

    // BetterInfo: adds lots of UI to info layers / level browser. Not
    // incompatible, but avoid stomping its buttons.
    static bool isBetterInfoLoaded() {
        return geode::Loader::get()->isModLoaded("hjfod.betterinfo");
    }

    // EclipseMenu: has its own popup/blur layer and renders via ImGui on top
    // of cocos2d (through the gd-imgui-cocos library). The real published mod
    // ID is `eclipse.eclipse-menu`; the older aliases are kept as fallbacks
    // for safety in case forks or pre-release builds use them.
    static bool isEclipseMenuLoaded() {
        return geode::Loader::get()->isModLoaded("eclipse.eclipse-menu") ||
               geode::Loader::get()->isModLoaded("eclipsemenu.eclipse-menu") ||
               geode::Loader::get()->isModLoaded("prevter.eclipsemenu");
    }

    // Globed: adds popups and a RoomPopup sharing a parent with our blur;
    // there was a documented crash (see DynamicPopupHook).
    static bool isGlobedLoaded() {
        return geode::Loader::get()->isModLoaded("dankmeme.globed2") ||
               geode::Loader::get()->isModLoaded("dankmeme.globed");
    }

    // Menu Loop Randomizer: shares domain with our Menu Music.
    static bool isMenuLoopRandomizerLoaded() {
        return geode::Loader::get()->isModLoaded("fleym.menuloop_randomizer");
    }

    // Blur mods. Other mods that blur popups. If one is actively blurring (not
    // just exposing an API), we skip ours to avoid duplicate FBO passes and
    // corrupting the scene snapshot.
    static bool isBlurBGLoaded() {
        // alphalaneous.blur_bg blurs ALL game popups.
        return geode::Loader::get()->isModLoaded("alphalaneous.blur_bg");
    }
    static bool isBlurAPILoaded() {
        // thesillydoggo.blur-api just exposes an API; other mods decide whether
        // to blur. Not a conflict on its own.
        return geode::Loader::get()->isModLoaded("thesillydoggo.blur-api");
    }
    // True if a mod is applying global popup blur that already covers our
    // PopupBlurService use case.
    static bool externalGlobalBlurActive() {
        return isBlurBGLoaded();
    }
};

} // namespace paimon::compat
