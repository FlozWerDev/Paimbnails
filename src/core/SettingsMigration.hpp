#pragma once

#include <Geode/Geode.hpp>
#include <string>

// Initialize saved-value defaults for settings moved out of mod.json.

namespace paimon::settings {

inline void migrateToSavedValues() {
    auto* mod = geode::Mod::get();

    auto defBool = [&](const char* key, bool val) {
        if (!mod->hasSavedValue(key)) mod->setSavedValue(key, val);
    };
    auto defInt = [&](const char* key, int val) {
        if (!mod->hasSavedValue(key)) mod->setSavedValue(key, val);
    };
    auto defFloat = [&](const char* key, float val) {
        if (!mod->hasSavedValue(key)) mod->setSavedValue(key, val);
    };
    auto defDouble = [&](const char* key, double val) {
        if (!mod->hasSavedValue(key)) mod->setSavedValue(key, val);
    };
    auto defStr = [&](const char* key, std::string val) {
        if (!mod->hasSavedValue(key)) mod->setSavedValue(key, val);
    };

    // Discord RPC (migrated from mod.json settings)
    defBool("discord-rpc-private-mode", false);
    defBool("discord-rpc-idle-when-unfocused", true);
    defBool("discord-rpc-show-progress", true);
    defBool("discord-rpc-include-paimbnails-features", true);
    defStr("discord-rpc-large-text", "");
    defStr("discord-rpc-large-image-key", "");
    defStr("discord-rpc-small-image-key", "");
    defStr("discord-rpc-activity-type", "Playing");
    defBool("discord-rpc-show-timestamp", true);
    defBool("discord-rpc-override-details", false);
    defStr("discord-rpc-custom-details", "");
    defBool("discord-rpc-override-state", false);
    defStr("discord-rpc-custom-state", "");

    // PaiDraw / Paimon Emote API server
    // Both PaiDraw and the emote/ytlinks APIs live on the same host. Saved
    // value `paidraw-server-url` is preserved for backwards compatibility;
    // `paimon-emote-server-url` is the canonical key going forward.
    defStr("paidraw-server-url", "https://paimbnailsbot.onrender.com");
    defStr("paimon-emote-server-url", "https://paimbnailsbot.onrender.com");

    // One-shot migration: rewrite saved values that still point at the
    // retired Vercel host (`paimbnails-emote.vercel.app`) or the original
    // placeholder host (`paidraw.example.com`) so existing installs
    // transparently pick up the Render endpoint without the user having to
    // edit settings by hand.
    //
    // Two sets of keys exist for historical reasons:
    //   - `paidraw-server-url` / `paimon-emote-server-url` (kebab-case):
    //     surface in the in-mod settings panel.
    //   - `paidraw_server_url` (snake_case): consumed by PaiDrawManager
    //     directly, persisted from the manager's own state.
    // We migrate all of them so users coming from older builds with stale
    // placeholder hosts converge on the Render endpoint.
    {
        constexpr const char* NEW_URL = "https://paimbnailsbot.onrender.com";
        const char* legacyHosts[] = {
            "paimbnails-emote.vercel.app",
            "paidraw.example.com",
            "example.com",
        };
        const char* keys[] = {
            "paidraw-server-url",
            "paimon-emote-server-url",
            "paidraw_server_url",
        };
        for (const char* key : keys) {
            auto current = mod->getSavedValue<std::string>(key, "");
            for (const char* legacy : legacyHosts) {
                if (current.find(legacy) != std::string::npos) {
                    mod->setSavedValue<std::string>(key, NEW_URL);
                    break;
                }
            }
        }
    }

    defStr("paidraw-display-name", "");
    defStr("paidraw-word-language", "es");
    defBool("paidraw-sound-effects", true);
    defBool("paidraw-invite-notifications", true);
    defBool("paidraw-show-ping", true);

    // Level Thumbnails (granular)
    defStr("levelcell-background-type", "thumbnail");
    defDouble("levelcell-background-blur", 3.0);
    defDouble("levelcell-background-darkness", 0.2);
    defBool("levelcell-show-separator", true);
    defBool("levelcell-show-view-button", true);
    defBool("compact-list-show-toggle", true);
    defBool("transparent-list-mode", false);
    defBool("transparent-background-mode", false);
    defBool("levelcell-gallery-autocycle", true);
    defStr("levelcell-gallery-transition", "crossfade");
    defDouble("levelcell-gallery-transition-duration", 0.6);
    defStr("popup-gallery-transition", "directional-elastic");
    defDouble("popup-gallery-transition-duration", 0.45);
    defStr("levelinfo-bg-transition", "crossfade");
    defDouble("levelinfo-bg-transition-duration", 0.5);

    // Level Visual Effects
    defStr("levelcell-anim-type", "zoom-slide");
    defDouble("levelcell-anim-speed", 1.0);
    defStr("levelcell-anim-effect", "none");
    defBool("levelcell-effect-on-gradient", false);
    defBool("levelcell-mythic-particles", true);
    defBool("levelcell-animated-gradient", true);

    // Level Info (granular)
    defStr("levelinfo-extra-styles", "");
    defInt("levelinfo-effect-intensity", 4);
    defInt("levelinfo-bg-darkness", 27);
    defBool("dynamic-song-stream-preview", true);

    // Profile Music (granular)
    defBool("profile-music-crossfade", true);
    defDouble("profile-music-fade-duration", 0.3);

    // Realtime Search (granular)
    defInt("realtime-search-debounce-ms", 350);

    // Dynamic Popup (granular)
    defStr("dynamic-popup-style", "paimonUI");
    defDouble("dynamic-popup-speed", 1.0);
    defDouble("dynamic-exit-speed", 1.0);

    // Popup Blur (granular).
    // Default style "paimonblur" (Dual Kawase multi-pass): better visual quality
    // than the 2-pass gaussian. A one-shot migration below moves existing
    // installs off the old "gaussian" default while respecting manual changes.
    defStr("popup-blur-style", "paimonblur");
    defDouble("popup-blur-intensity", 4.0);
    defDouble("popup-blur-darkness", 0.28);
    defDouble("popup-blur-padding", 4.0);
    defDouble("popup-blur-corner-radius", 8.0);
    defDouble("popup-blur-fade-duration", 0.18);
    defBool("popup-blur-show-placeholder", true);

    // One-shot migration: convert obsolete styles to the default static blur.
    if (!mod->hasSavedValue("popup-blur-style-migrated-to-paimonblur")) {
        auto const style = mod->getSavedValue<std::string>("popup-blur-style");
        if (style == "gaussian" || style == "paimonblur-dynamic") {
            mod->setSavedValue<std::string>("popup-blur-style", "paimonblur");
        }
        mod->setSavedValue<bool>("popup-blur-style-migrated-to-paimonblur", true);
    }

    // For You
    defBool("enable-for-you", false);
    defInt("for-you-min-levels", 5);
    defBool("for-you-use-tags", true);

    // Custom Cursor (granular)
    defDouble("custom-cursor-scale", 0.3);
    defBool("custom-cursor-trail", false);
    defBool("custom-cursor-hide-in-gameplay", true);

    // Menu Loop (granular)
    defBool("menuLoopSaveSongOnGameClose", false);
    defStr("menuLoopButtonMode", "Reduced");
    defBool("menuLoopEnableShuffleButton", true);
    defBool("menuLoopEnableBlacklistButton", true);
    defBool("menuLoopEnableFavoriteButton", true);
    defBool("menuLoopEnableHoldSongButton", true);
    defBool("menuLoopEnablePreviousButton", true);
    defBool("menuLoopEnableAddToPlaylistButton", true);
    defBool("menuLoopEnableViewSongListButton", true);
    defBool("menuLoopEnableCopySongID", true);
    defBool("menuLoopEnableNotification", true);
    defBool("menuLoopEnableNewNotification", true);
    defDouble("menuLoopNotificationTime", 2.0);
    defStr("menuLoopCustomPrefix", "Now Playing");
    defStr("menuLoopSongFormatNGML", "Song Name, Artist, Song ID");
    defBool("menuLoopLoadPlaylistFile", false);
    defStr("menuLoopPlaylistFile", "");
    defStr("menuLoopAdditionalFolder", "");
    defBool("menuLoopAdvancedLogs", false);
    // Seek + extras from reference mod
    defInt("menuLoopSeekAmountMs", 5000);
    defBool("menuLoopShowPlaybackProgress", true);
    defBool("menuLoopEnableKeyboardShortcuts", true);
    defBool("menuLoopRandomizeOnLevelExit", false);
    defBool("menuLoopRandomizeOnEditorExit", false);
    defBool("menuLoopRestoreOnLevelExit", true);
    defBool("menuLoopRestoreOnEditorExit", true);
    defBool("menuLoopSongIndicators", true);
    defBool("menuLoopCompactSongList", false);
    defBool("menuLoopFavoritesOnlyFilter", false);
    defStr("menuLoopSortMode", "alphabetical");
    defBool("menuLoopSortReverse", false);

    // Menu Music (granular)
    defDouble("menuMusicBlurIntensity", 5.0);
    defDouble("menuMusicBlurDarkness", 0.45);
    defBool("menuMusicAutoplayOnBoot", false);
    defStr("menuMusicDownloadFormat", "mp3");

    // Performance (granular)
    defBool("gif-ram-cache", true);
    defBool("disable-video-chunks", true);

    // Layout Editor (granular)
    defBool("main-menu-layout-grid-snap", true);
    defInt("main-menu-layout-grid-size", 10);
    defInt("main-menu-layout-snap-distance", 10);
    defBool("main-menu-layout-show-guides", true);
    defBool("main-menu-layout-snap-to-edges", true);

    // Zoom (granular)
    defDouble("zoom-sensitivity", 1.0);
    defBool("zoom-auto-hide-menu", true);
    defBool("zoom-auto-show-menu", true);
    defBool("zoom-alt-disables-scroll", true);

    // Profile background must sit behind the popup and the comment list (which
    // lives at z=0), so the correct default is -1, like InfoLayer (addChild(clip,
    // -1)). The original mod.json default was -1; migrating to saved values set
    // it to 1 by mistake, putting the background above the comments and hiding
    // them on profiles with a custom background.
    defInt("profile-img-zlayer", -1);

    // One-shot migration: installs that got the wrong default (1) and never
    // changed it ended up with the image covering comments. If the value is
    // still 1 and this fix hasn't run, drop it to -1. Runs once, so a manual
    // change afterward is preserved.
    if (!mod->hasSavedValue("profile-img-zlayer-fixed-default")) {
        if (mod->getSavedValue<int>("profile-img-zlayer", -1) == 1) {
            mod->setSavedValue<int>("profile-img-zlayer", -1);
        }
        mod->setSavedValue<bool>("profile-img-zlayer-fixed-default", true);
    }
}

// Force all migrated saved values back to clean-install defaults. Used by the
// Hub's factory reset to discard corrupt configuration.
inline void forceResetSavedValuesToDefaults() {
    auto* mod = geode::Mod::get();

    auto defBool = [&](const char* key, bool val) {
        mod->setSavedValue(key, val);
    };
    auto defInt = [&](const char* key, int val) {
        mod->setSavedValue(key, val);
    };
    auto defFloat = [&](const char* key, float val) {
        mod->setSavedValue(key, val);
    };
    auto defDouble = [&](const char* key, double val) {
        mod->setSavedValue(key, val);
    };
    auto defStr = [&](const char* key, std::string val) {
        mod->setSavedValue(key, val);
    };

    defBool("discord-rpc-private-mode", false);
    defBool("discord-rpc-idle-when-unfocused", true);
    defBool("discord-rpc-show-progress", true);
    defBool("discord-rpc-include-paimbnails-features", true);
    defStr("discord-rpc-large-text", "");
    defStr("discord-rpc-large-image-key", "");
    defStr("discord-rpc-small-image-key", "");
    defStr("discord-rpc-activity-type", "Playing");
    defBool("discord-rpc-show-timestamp", true);
    defBool("discord-rpc-override-details", false);
    defStr("discord-rpc-custom-details", "");
    defBool("discord-rpc-override-state", false);
    defStr("discord-rpc-custom-state", "");

    defStr("paidraw-server-url", "https://paimbnailsbot.onrender.com");
    defStr("paimon-emote-server-url", "https://paimbnailsbot.onrender.com");
    defStr("paidraw_server_url", "https://paimbnailsbot.onrender.com");
    defStr("paidraw-display-name", "");
    defStr("paidraw-word-language", "es");
    defBool("paidraw-sound-effects", true);
    defBool("paidraw-invite-notifications", true);
    defBool("paidraw-show-ping", true);

    defStr("levelcell-background-type", "thumbnail");
    defDouble("levelcell-background-blur", 3.0);
    defDouble("levelcell-background-darkness", 0.2);
    defBool("levelcell-show-separator", true);
    defBool("levelcell-show-view-button", true);
    defBool("compact-list-show-toggle", true);
    defBool("transparent-list-mode", false);
    defBool("transparent-background-mode", false);
    defBool("levelcell-gallery-autocycle", true);
    defStr("levelcell-gallery-transition", "crossfade");
    defDouble("levelcell-gallery-transition-duration", 0.6);
    defStr("popup-gallery-transition", "directional-elastic");
    defDouble("popup-gallery-transition-duration", 0.45);
    defStr("levelinfo-bg-transition", "crossfade");
    defDouble("levelinfo-bg-transition-duration", 0.5);

    defStr("levelcell-anim-type", "zoom-slide");
    defDouble("levelcell-anim-speed", 1.0);
    defStr("levelcell-anim-effect", "none");
    defBool("levelcell-effect-on-gradient", false);
    defBool("levelcell-mythic-particles", true);
    defBool("levelcell-animated-gradient", true);

    defStr("levelinfo-extra-styles", "");
    defInt("levelinfo-effect-intensity", 4);
    defInt("levelinfo-bg-darkness", 27);
    defBool("dynamic-song-stream-preview", true);

    defBool("profile-music-crossfade", true);
    defDouble("profile-music-fade-duration", 0.3);

    defInt("realtime-search-debounce-ms", 350);

    defStr("dynamic-popup-style", "paimonUI");
    defDouble("dynamic-popup-speed", 1.0);
    defDouble("dynamic-exit-speed", 1.0);

    defStr("popup-blur-style", "paimonblur");
    defDouble("popup-blur-intensity", 4.0);
    defDouble("popup-blur-darkness", 0.28);
    defDouble("popup-blur-padding", 4.0);
    defDouble("popup-blur-corner-radius", 8.0);
    defDouble("popup-blur-fade-duration", 0.18);
    defBool("popup-blur-show-placeholder", true);
    defBool("popup-blur-style-migrated-to-paimonblur", true);

    defBool("enable-for-you", false);
    defInt("for-you-min-levels", 5);
    defBool("for-you-use-tags", true);

    defDouble("custom-cursor-scale", 0.3);
    defBool("custom-cursor-trail", false);
    defBool("custom-cursor-hide-in-gameplay", true);

    defBool("menuLoopSaveSongOnGameClose", false);
    defStr("menuLoopButtonMode", "Reduced");
    defBool("menuLoopEnableShuffleButton", true);
    defBool("menuLoopEnableBlacklistButton", true);
    defBool("menuLoopEnableFavoriteButton", true);
    defBool("menuLoopEnableHoldSongButton", true);
    defBool("menuLoopEnablePreviousButton", true);
    defBool("menuLoopEnableAddToPlaylistButton", true);
    defBool("menuLoopEnableViewSongListButton", true);
    defBool("menuLoopEnableCopySongID", true);
    defBool("menuLoopEnableNotification", true);
    defBool("menuLoopEnableNewNotification", true);
    defDouble("menuLoopNotificationTime", 2.0);
    defStr("menuLoopCustomPrefix", "Now Playing");
    defStr("menuLoopSongFormatNGML", "Song Name, Artist, Song ID");
    defBool("menuLoopLoadPlaylistFile", false);
    defStr("menuLoopPlaylistFile", "");
    defStr("menuLoopAdditionalFolder", "");
    defBool("menuLoopAdvancedLogs", false);
    defInt("menuLoopSeekAmountMs", 5000);
    defBool("menuLoopShowPlaybackProgress", true);
    defBool("menuLoopEnableKeyboardShortcuts", true);
    defBool("menuLoopRandomizeOnLevelExit", false);
    defBool("menuLoopRandomizeOnEditorExit", false);
    defBool("menuLoopRestoreOnLevelExit", true);
    defBool("menuLoopRestoreOnEditorExit", true);
    defBool("menuLoopSongIndicators", true);
    defBool("menuLoopCompactSongList", false);
    defBool("menuLoopFavoritesOnlyFilter", false);
    defStr("menuLoopSortMode", "alphabetical");
    defBool("menuLoopSortReverse", false);

    defDouble("menuMusicBlurIntensity", 5.0);
    defDouble("menuMusicBlurDarkness", 0.45);
    defBool("menuMusicAutoplayOnBoot", false);
    defStr("menuMusicDownloadFormat", "mp3");

    defBool("gif-ram-cache", true);
    defBool("disable-video-chunks", true);

    defBool("main-menu-layout-grid-snap", true);
    defInt("main-menu-layout-grid-size", 10);
    defInt("main-menu-layout-snap-distance", 10);
    defBool("main-menu-layout-show-guides", true);
    defBool("main-menu-layout-snap-to-edges", true);

    defDouble("zoom-sensitivity", 1.0);
    defBool("zoom-auto-hide-menu", true);
    defBool("zoom-auto-show-menu", true);
    defBool("zoom-alt-disables-scroll", true);

    defInt("profile-img-zlayer", -1);
    defBool("profile-img-zlayer-fixed-default", true);
}

} // namespace paimon::settings
