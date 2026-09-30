#pragma once

// detect active mods at call time so runtime toggles are respected.

#include <Geode/loader/Loader.hpp>

namespace paimon::compat {

struct ModCompat {
    static bool isMoreIconsLoaded() {
        return geode::Loader::get()->isModLoaded("hiimjustin000.more_icons");
    }

// known conflicts: lower hook priority or cede ui ownership.

// cdc level thumbnails are incompatible.
    static bool isCDCLevelThumbnailsLoaded() {
        return geode::Loader::get()->isModLoaded("cdc.level_thumbnails");
    }

// compactlists owns compact mode when active.
    static bool isCompactListsLoaded() {
        return geode::Loader::get()->isModLoaded("cvolton.compactlists-geode");
    }

// compact pause menu requires our song-widget hook to run after it.
    static bool isCompactPauseMenuLoaded() {
        return geode::Loader::get()->isModLoaded("prevter.compact-pause-menu");
    }

    static bool isPrevterSmoothScrollLoaded() {
        return geode::Loader::get()->isModLoaded("prevter.smooth-scroll");
    }

    static bool isQuickVolumeControlsLoaded() {
        return geode::Loader::get()->isModLoaded("hjfod.quick-volume-controls");
    }

// betterinfo is compatible, but overlapping buttons must be left alone.
    static bool isBetterInfoLoaded() {
        return geode::Loader::get()->isModLoaded("cvolton.betterinfo");
    }

// eclipsemenu owns its imgui popup/blur layer.
    static bool isEclipseMenuLoaded() {
        return geode::Loader::get()->isModLoaded("eclipse.eclipse-menu") ||
               geode::Loader::get()->isModLoaded("eclipsemenu.eclipse-menu") ||
               geode::Loader::get()->isModLoaded("prevter.eclipsemenu");
    }

// globed shares popup parents with our blur and has a known crash path.
    static bool isGlobedLoaded() {
        return geode::Loader::get()->isModLoaded("dankmeme.globed2") ||
               geode::Loader::get()->isModLoaded("dankmeme.globed");
    }

// these mods render from their hooks; revisiting playlayer in our fbo
// would re-enter them with a foreign viewport.
    static bool isTinkerLoaded() {
        return geode::Loader::get()->isModLoaded("alphalaneous.tinker");
    }

    static bool isMegaHackLoaded() {
        return geode::Loader::get()->isModLoaded("absolllute.megahack");
    }

    static bool needsConservativeGameplayCapture() {
        return isGlobedLoaded() || isCDCLevelThumbnailsLoaded() ||
               isEclipseMenuLoaded() || isTinkerLoaded() || isMegaHackLoaded();
    }

// editor ui owners from the crash corpus; hooks stay no-op unless we own an operation.
    static bool isBetterEditLoaded() {
        return geode::Loader::get()->isModLoaded("hjfod.betteredit");
    }

    static bool isEditorTabApiLoaded() {
        return geode::Loader::get()->isModLoaded("alphalaneous.editortab_api");
    }

    static bool isEditorCollabLoaded() {
        return geode::Loader::get()->isModLoaded("alk.editor-collab") ||
               geode::Loader::get()->isModLoaded("alk.editor-collab-ui");
    }

// menu loop randomizer overlaps with menu music.
    static bool isMenuLoopRandomizerLoaded() {
        return geode::Loader::get()->isModLoaded("fleym.menuloop_randomizer");
    }

// active blur mods disable ours to avoid duplicate fbo passes.
    static bool isBlurBGLoaded() {
        return geode::Loader::get()->isModLoaded("alphalaneous.blur_bg");
    }
    static bool isBlurBehindPopupsLoaded() {
        return geode::Loader::get()->isModLoaded("malikhw47.blur-behind-popups");
    }
    static bool externalGlobalBlurActive() {
        return isBlurBGLoaded() || isBlurBehindPopupsLoaded();
    }
};

}
