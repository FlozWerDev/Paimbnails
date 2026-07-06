#pragma once

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::settings {

namespace internal {

// Single source of truth for every saved-value default. Applied either as
// set-if-missing (migration on startup) or force-set (factory reset), so the
// two paths can never drift apart.
inline void applyDefaults(bool force) {
    auto* mod = geode::Mod::get();

    auto setB = [&](const char* key, bool val) {
        if (force || !mod->hasSavedValue(key)) mod->setSavedValue(key, val);
    };
    auto setI = [&](const char* key, int val) {
        if (force || !mod->hasSavedValue(key)) mod->setSavedValue(key, val);
    };
    auto setF = [&](const char* key, float val) {
        if (force || !mod->hasSavedValue(key)) mod->setSavedValue(key, val);
    };
    auto setD = [&](const char* key, double val) {
        if (force || !mod->hasSavedValue(key)) mod->setSavedValue(key, val);
    };
    auto setS = [&](const char* key, std::string val) {
        if (force || !mod->hasSavedValue(key)) mod->setSavedValue(key, val);
    };

    setB("discord-rpc-private-mode", false);
    setB("discord-rpc-idle-when-unfocused", true);
    setB("discord-rpc-show-progress", true);
    setB("discord-rpc-include-paimbnails-features", true);
    setS("discord-rpc-large-text", "");
    setS("discord-rpc-large-image-key", "");
    setS("discord-rpc-small-image-key", "");
    setS("discord-rpc-activity-type", "Playing");
    setB("discord-rpc-show-timestamp", true);
    setB("discord-rpc-override-details", false);
    setS("discord-rpc-custom-details", "");
    setB("discord-rpc-override-state", false);
    setS("discord-rpc-custom-state", "");

    setS("paidraw-server-url", "https://paimbnailsbot.onrender.com");
    setS("paimon-emote-server-url", "https://paimbnailsbot.onrender.com");
    setS("paidraw-display-name", "");
    setS("paidraw-word-language", "es");
    setB("paidraw-sound-effects", true);
    setB("paidraw-invite-notifications", true);
    setB("paidraw-show-ping", true);

    setS("levelcell-background-type", "thumbnail");
    setD("levelcell-background-blur", 3.0);
    setD("levelcell-background-darkness", 0.2);
    setB("levelcell-show-separator", true);
    setB("levelcell-show-view-button", true);
    setB("compact-list-show-toggle", true);
    setB("transparent-list-mode", false);
    setB("transparent-background-mode", false);
    setB("levelcell-gallery-autocycle", true);
    setS("levelcell-gallery-transition", "crossfade");
    setD("levelcell-gallery-transition-duration", 0.6);
    setS("popup-gallery-transition", "directional-elastic");
    setD("popup-gallery-transition-duration", 0.45);
    setS("levelinfo-bg-transition", "crossfade");
    setD("levelinfo-bg-transition-duration", 0.5);

    setS("levelcell-anim-type", "zoom-slide");
    setD("levelcell-anim-speed", 1.0);
    setS("levelcell-anim-effect", "none");
    setB("levelcell-effect-on-gradient", false);
    setB("levelcell-mythic-particles", true);
    setB("levelcell-animated-gradient", true);

    setS("levelinfo-extra-styles", "");
    setI("levelinfo-effect-intensity", 4);
    setI("levelinfo-bg-darkness", 27);
    setB("dynamic-song-stream-preview", true);

    setB("profile-music-crossfade", true);
    setD("profile-music-fade-duration", 0.3);

    setI("realtime-search-debounce-ms", 350);

    setS("dynamic-popup-style", "paimonUI");
    setD("dynamic-popup-speed", 1.0);
    setD("dynamic-exit-speed", 1.0);

    setS("smooth-ui-preset", "balanced");
    setB("smooth-ui-reduced-motion", false);
    setD("smooth-ui-global-speed", 1.0);
    setD("smooth-ui-motion-strength", 1.0);
    setB("smooth-ui-animate-buttons", true);
    setS("smooth-ui-button-scope", "all");
    setD("smooth-ui-button-press-scale", 0.94);
    setB("smooth-ui-button-release-bounce", true);

    setS("popup-blur-style", "paiblur");
    setD("popup-blur-intensity", 4.0);
    setD("popup-blur-darkness", 0.28);
    setD("popup-blur-padding", 4.0);
    setD("popup-blur-corner-radius", 8.0);
    setD("popup-blur-fade-duration", 0.18);
    setB("popup-blur-show-placeholder", true);

    setB("enable-for-you", false);
    setI("for-you-min-levels", 5);
    setB("for-you-use-tags", true);

    setD("custom-cursor-scale", 0.3);
    setB("custom-cursor-trail", false);
    setB("custom-cursor-hide-in-gameplay", true);

    setB("menuLoopSaveSongOnGameClose", false);
    setS("menuLoopButtonMode", "Reduced");
    setB("menuLoopEnableShuffleButton", true);
    setB("menuLoopEnableBlacklistButton", true);
    setB("menuLoopEnableFavoriteButton", true);
    setB("menuLoopEnableHoldSongButton", true);
    setB("menuLoopEnablePreviousButton", true);
    setB("menuLoopEnableAddToPlaylistButton", true);
    setB("menuLoopEnableViewSongListButton", true);
    setB("menuLoopEnableCopySongID", true);
    setB("menuLoopEnableNotification", true);
    setB("menuLoopEnableNewNotification", true);
    setD("menuLoopNotificationTime", 2.0);
    setS("menuLoopCustomPrefix", "Now Playing");
    setS("menuLoopSongFormatNGML", "Song Name, Artist, Song ID");
    setB("menuLoopLoadPlaylistFile", false);
    setS("menuLoopPlaylistFile", "");
    setS("menuLoopAdditionalFolder", "");
    setB("menuLoopAdvancedLogs", false);
    setI("menuLoopSeekAmountMs", 5000);
    setB("menuLoopShowPlaybackProgress", true);
    setB("menuLoopEnableKeyboardShortcuts", true);
    setB("menuLoopRandomizeOnLevelExit", false);
    setB("menuLoopRandomizeOnEditorExit", false);
    setB("menuLoopRestoreOnLevelExit", true);
    setB("menuLoopRestoreOnEditorExit", true);
    setB("menuLoopSongIndicators", true);
    setB("menuLoopCompactSongList", false);
    setB("menuLoopFavoritesOnlyFilter", false);
    setS("menuLoopSortMode", "alphabetical");
    setB("menuLoopSortReverse", false);

    setD("menuMusicBlurIntensity", 5.0);
    setD("menuMusicBlurDarkness", 0.45);
    setB("menuMusicAutoplayOnBoot", false);
    setS("menuMusicDownloadFormat", "mp3");

    setB("gif-ram-cache", true);
    setB("disable-video-chunks", true);

    setB("main-menu-layout-grid-snap", true);
    setI("main-menu-layout-grid-size", 10);
    setI("main-menu-layout-snap-distance", 10);
    setB("main-menu-layout-show-guides", true);
    setB("main-menu-layout-snap-to-edges", true);

    setD("zoom-sensitivity", 1.0);
    setB("zoom-auto-hide-menu", true);
    setB("zoom-auto-show-menu", true);
    setB("zoom-alt-disables-scroll", true);

    // Default -1: profile background sits behind the comment list (z=0). A
    // value of 1 once put it above the comments and hid them.
    setI("profile-img-zlayer", -1);
}

// One-shot data migrations that rewrite existing values rather than just filling
// in defaults. Idempotent and guarded by their own flags where needed.
inline void runOneShotMigrations() {
    auto* mod = geode::Mod::get();

    // Rewrite saved URLs still pointing at retired/placeholder hosts so existing
    // installs converge on the Render endpoint.
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

    // Convert obsolete popup blur styles to the lightweight default.
    if (!mod->hasSavedValue("popup-blur-style-migrated-to-paimonblur")) {
        auto const style = mod->getSavedValue<std::string>("popup-blur-style");
        if (style == "gaussian" || style == "paimonblur-dynamic") {
            mod->setSavedValue<std::string>("popup-blur-style", "paiblur");
        }
        mod->setSavedValue<bool>("popup-blur-style-migrated-to-paimonblur", true);
    }

    // The old default ("paimonblur") snapshots and blurs the whole scene when a
    // popup opens. Move existing installs to the dynamic style; users can still
    // pick the static styles manually from the settings UI.
    if (!mod->hasSavedValue("popup-blur-style-migrated-to-paiblur")) {
        auto const style = mod->getSavedValue<std::string>("popup-blur-style", "paiblur");
        if (style == "gaussian" || style == "paimonblur" || style == "paimonblur-dynamic") {
            mod->setSavedValue<std::string>("popup-blur-style", "paiblur");
        }
        mod->setSavedValue<bool>("popup-blur-style-migrated-to-paiblur", true);
    }

    // A migration default of 1 once put the profile background above the comments.
    if (!mod->hasSavedValue("profile-img-zlayer-fixed-default")) {
        if (mod->getSavedValue<int>("profile-img-zlayer", -1) == 1) {
            mod->setSavedValue<int>("profile-img-zlayer", -1);
        }
        mod->setSavedValue<bool>("profile-img-zlayer-fixed-default", true);
    }
}

} // namespace internal

inline void migrateToSavedValues() {
    internal::applyDefaults(false);
    internal::runOneShotMigrations();
}

// Force all saved values back to clean-install defaults (factory reset).
inline void forceResetSavedValuesToDefaults() {
    auto* mod = geode::Mod::get();

    internal::applyDefaults(true);

    // Legacy underscore variant of the server URL plus the one-shot flags, so a
    // reset install matches a freshly migrated one.
    mod->setSavedValue<std::string>("paidraw_server_url", "https://paimbnailsbot.onrender.com");
    mod->setSavedValue<bool>("popup-blur-style-migrated-to-paimonblur", true);
    mod->setSavedValue<bool>("popup-blur-style-migrated-to-paiblur", true);
    mod->setSavedValue<bool>("profile-img-zlayer-fixed-default", true);
}

} // namespace paimon::settings
