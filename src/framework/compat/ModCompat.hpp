#pragma once

// Detect active mods at call time so runtime toggles are respected.

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

// More Icons provides the optional custom-icon API.
    static bool isMoreIconsLoaded() {
        return geode::Loader::get()->isModLoaded("hiimjustin000.more_icons");
    }

// Known conflicts that require lower hook priority or ceded UI ownership.

// CDC level thumbnails are incompatible.
    static bool isCDCLevelThumbnailsLoaded() {
        return geode::Loader::get()->isModLoaded("cdc.level_thumbnails");
    }

// CompactLists owns compact mode when active.
    static bool isCompactListsLoaded() {
        return geode::Loader::get()->isModLoaded("cvolton.compactlists-geode");
    }

// Compact Pause Menu requires our song-widget hook to run after it.
    static bool isCompactPauseMenuLoaded() {
        return geode::Loader::get()->isModLoaded("prevter.compact-pause-menu");
    }

    static bool isPrevterSmoothScrollLoaded() {
        return geode::Loader::get()->isModLoaded("prevter.smooth-scroll");
    }

    static bool isQuickVolumeControlsLoaded() {
        return geode::Loader::get()->isModLoaded("hjfod.quick-volume-controls");
    }

// BetterInfo is compatible, but overlapping buttons must be left alone.
    static bool isBetterInfoLoaded() {
        return geode::Loader::get()->isModLoaded("cvolton.betterinfo");
    }

// EclipseMenu owns its ImGui popup/blur layer; detect its published and legacy IDs.
    static bool isEclipseMenuLoaded() {
        return geode::Loader::get()->isModLoaded("eclipse.eclipse-menu") ||
               geode::Loader::get()->isModLoaded("eclipsemenu.eclipse-menu") ||
               geode::Loader::get()->isModLoaded("prevter.eclipsemenu");
    }

// Globed shares popup parents with our blur and has a known crash path.
    static bool isGlobedLoaded() {
        return geode::Loader::get()->isModLoaded("dankmeme.globed2") ||
               geode::Loader::get()->isModLoaded("dankmeme.globed");
    }

// Menu Loop Randomizer overlaps with Menu Music.
    static bool isMenuLoopRandomizerLoaded() {
        return geode::Loader::get()->isModLoaded("fleym.menuloop_randomizer");
    }

// Active blur mods disable ours to avoid duplicate FBO passes and bad snapshots.
    static bool isBlurBGLoaded() {
// alphalaneous.blur_bg blurs all popups.
        return geode::Loader::get()->isModLoaded("alphalaneous.blur_bg");
    }
    static bool isBlurAPILoaded() {
// thesillydoggo.blur-api only exposes an API; it is not a conflict alone.
        return geode::Loader::get()->isModLoaded("thesillydoggo.blur-api");
    }
    static bool isBlurBehindPopupsLoaded() {
// malikhw47.blur-behind-popups applies BlurAPI to every FLAlertLayer.
        return geode::Loader::get()->isModLoaded("malikhw47.blur-behind-popups");
    }
// True when another mod already covers our popup blur use case.
    static bool externalGlobalBlurActive() {
        return isBlurBGLoaded() || isBlurBehindPopupsLoaded();
    }
};

}
