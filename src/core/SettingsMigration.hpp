#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include "../features/discord-presence/model/PresenceCustomization.hpp"

namespace paimon::settings {

namespace internal {

// single source for saved-value defaults, used by startup migration and reset.
inline void applyDefaults(bool force) {
    auto* mod = geode::Mod::get();
    if (!mod) return;

    auto set = [&](const char* key, auto val) {
        if (force || !mod->hasSavedValue(key)) mod->setSavedValue(key, val);
    };
    auto setS = [&](const char* key, std::string val) { set(key, val); };

    // preserve the old progress opt-out when introducing a separate best toggle.
    if (!force && !mod->hasSavedValue("discord-rpc-show-best")) {
        mod->setSavedValue("discord-rpc-show-best", mod->getSavedValue<bool>("discord-rpc-show-progress", true));
    }
    for (auto const& option : paimon::discord::kBooleanOptions) set(option.key, option.value);
    for (auto const& option : paimon::discord::kTextOptions) setS(option.key, option.value);

    setS("paidraw-server-url", "https://paimbnailsbot.onrender.com");
    setS("paimon-emote-server-url", "https://paimbnailsbot.onrender.com");
    setS("paidraw-display-name", "");
    setS("paidraw-word-language", "es");
    set("paidraw-sound-effects", true);
    set("paidraw-invite-notifications", true);
    set("paidraw-show-ping", true);

    setS("levelcell-background-type", "thumbnail");
    set("levelcell-background-blur", 3.0);
    set("levelcell-background-darkness", 0.2);
    set("levelcell-thumbnail-edge-blend", 0.65);
    set("levelcell-show-separator", true);
    set("levelcell-show-view-button", true);
    set("compact-list-show-toggle", true);
    set("transparent-list-mode", false);
    set("transparent-background-mode", false);
    set("levelcell-gallery-autocycle", true);
    setS("levelcell-gallery-transition", "crossfade");
    set("levelcell-gallery-transition-duration", 0.6);
    setS("popup-gallery-transition", "directional-elastic");
    set("popup-gallery-transition-duration", 0.45);
    setS("levelinfo-bg-transition", "crossfade");
    set("levelinfo-bg-transition-duration", 0.5);

    setS("levelcell-anim-type", "zoom-slide");
    set("levelcell-anim-speed", 1.0);
    setS("levelcell-anim-effect", "none");
    set("levelcell-effect-on-gradient", false);
    set("levelcell-mythic-particles", true);
    set("levelcell-animated-gradient", true);

    setS("levelinfo-extra-styles", "");
    set("levelinfo-effect-intensity", 4);
    set("levelinfo-bg-darkness", 27);
    set("dynamic-song-stream-preview", true);
    setS("dynsong-start-mode", "random");
    set("dynsong-random-min-pct", 15);
    set("dynsong-random-max-pct", 85);
    setS("dynsong-rotation-mode", "rotate");
    set("dynsong-volume-pct", 100);
    set("dynsong-fade-seconds", 0.35f);
    set("dynsong-in-level-select", true);
    set("dynsong-submerge-enabled", true);
    setS("dynsong-submerge-preset", "underwater");
    set("dynsong-submerge-dive-seconds", 0.55f);
    set("dynsong-submerge-surface-seconds", 1.10f);
    set("dynsong-submerge-on-level-exit", true);
    set("dynsong-submerge-hold-seconds", 0.7f);

    set("profile-music-crossfade", true);
    set("profile-music-fade-duration", 0.3);

    set("realtime-search-debounce-ms", 350);

    setS("dynamic-popup-style", "paimonUI");
    set("dynamic-popup-speed", 1.0);
    set("dynamic-exit-speed", 1.0);
    set("dynamic-transition-style", 0);
    set("dynamic-transition-curve", 0);
    set("dynamic-transition-origin", 0);
    set("dynamic-transition-reduced-motion", 0);
    set("dynamic-transition-panel-style", 0);
    set("dynamic-transition-back", true);
    set("dynamic-transition-keyboard-back", true);
    set("dynamic-transition-popups", true);
    set("dynamic-transition-dropdowns", true);
    set("dynamic-transition-blocking-layers", true);
    set("dynamic-transition-dialogs", true);
    set("dynamic-transition-editor-panels", true);
    set("dynamic-transition-gameplay-panels", true);
    set("dynamic-transition-other-mods", false);
    set("dynamic-transition-instant", true);
    set("dynamic-transition-only-buttons", false);
    set("dynamic-transition-sync-smooth-ui", true);
    set("dynamic-transition-duration", .42);
    set("dynamic-transition-back-duration", .32);
    set("dynamic-transition-corners", 18.0);
    set("dynamic-transition-background-scale", .96);
    set("dynamic-transition-dim", .22);
    set("dynamic-transition-spring", .035);
    set("dynamic-transition-button-blend", .24);
    set("dynamic-transition-shadow", .5);
    set("dynamic-transition-quality", 1.0);

    setS("smooth-ui-preset", "balanced");
    set("smooth-ui-reduced-motion", false);
    set("smooth-ui-global-speed", 1.0);
    set("smooth-ui-motion-strength", 1.0);
    set("smooth-ui-animate-buttons", true);
    setS("smooth-ui-button-scope", "all");
    set("smooth-ui-button-press-scale", 0.94);
    set("smooth-ui-button-release-bounce", true);

    setS("popup-blur-style", "paiblur");
    set("popup-blur-intensity", 4.0);
    set("popup-blur-darkness", 0.28);
    set("popup-blur-padding", 4.0);
    set("popup-blur-corner-radius", 8.0);
    set("popup-blur-fade-duration", 0.18);
    set("popup-blur-show-placeholder", true);

    set("for-you-use-tags", true);
    set("for-you-feed-size", 12);
// searches per refresh: tag lookups first, gd search for the rest.
    set("for-you-query-budget", 6);
// feed share reserved for uncertain recommendations.
    set("for-you-exploration", 0.2);

    set("custom-cursor-scale", 0.3);
    set("custom-cursor-trail", false);
    set("custom-cursor-hide-in-gameplay", true);

    set("menuLoopSaveSongOnGameClose", false);
    setS("menuLoopButtonMode", "Reduced");
    set("menuLoopEnableShuffleButton", true);
    set("menuLoopEnableBlacklistButton", true);
    set("menuLoopEnableFavoriteButton", true);
    set("menuLoopEnableHoldSongButton", true);
    set("menuLoopEnablePreviousButton", true);
    set("menuLoopEnableAddToPlaylistButton", true);
    set("menuLoopEnableViewSongListButton", true);
    set("menuLoopEnableCopySongID", true);
    set("menuLoopEnableNotification", true);
    set("menuLoopEnableNewNotification", true);
    set("menuLoopNotificationTime", 2.0);
    setS("menuLoopCustomPrefix", "Now Playing");
    setS("menuLoopSongFormatNGML", "Song Name, Artist, Song ID");
    set("menuLoopLoadPlaylistFile", false);
    setS("menuLoopPlaylistFile", "");
    setS("menuLoopAdditionalFolder", "");
    set("menuLoopAdvancedLogs", false);
    set("menuLoopSeekAmountMs", 5000);
    set("menuLoopShowPlaybackProgress", true);
    set("menuLoopEnableKeyboardShortcuts", true);
    set("menuLoopRandomizeOnLevelExit", false);
    set("menuLoopRandomizeOnEditorExit", false);
    set("menuLoopRestoreOnLevelExit", true);
    set("menuLoopRestoreOnEditorExit", true);
    set("menuLoopSongIndicators", true);
    set("menuLoopCompactSongList", false);
    set("menuLoopFavoritesOnlyFilter", false);
    set("menuLoopShowBlacklisted", false);
    set("menuLoopAutoScrollCurrent", true);
    setS("menuLoopSortMode", "alphabetical");
    set("menuLoopSortReverse", false);

    set("menuMusicBlurIntensity", 5.0);
    set("menuMusicBlurDarkness", 0.45);
    set("menuMusicAutoplayOnBoot", false);
    setS("menuMusicDownloadFormat", "mp3");

    set("gif-ram-cache", true);
    set("disable-video-chunks", true);

    set("main-menu-layout-grid-snap", true);
    set("main-menu-layout-grid-size", 10);
    set("main-menu-layout-snap-distance", 10);
    set("main-menu-layout-show-guides", true);
    set("main-menu-layout-snap-to-edges", true);

    set("zoom-sensitivity", 1.0);
    set("zoom-auto-hide-menu", true);
    set("zoom-auto-show-menu", true);
    set("zoom-alt-disables-scroll", true);

// -1 keeps profile backgrounds behind the comment list.
    set("profile-img-zlayer", -1);
}

// idempotent migrations that rewrite existing values, guarded when needed.
inline void runOneShotMigrations() {
    auto* mod = geode::Mod::get();
    if (!mod) return;

// move saved urls from retired hosts to the render endpoint.
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

// migrate the old full-scene "paimonblur" default to the dynamic style.
    if (!mod->hasSavedValue("popup-blur-style-migrated-to-paiblur")) {
        auto const style = mod->getSavedValue<std::string>("popup-blur-style", "paiblur");
        if (style == "gaussian" || style == "paimonblur" || style == "paimonblur-dynamic") {
            mod->setSavedValue<std::string>("popup-blur-style", "paiblur");
        }
        mod->setSavedValue<bool>("popup-blur-style-migrated-to-paiblur", true);
    }

// keep migrated profile backgrounds behind comments.
    if (!mod->hasSavedValue("profile-img-zlayer-fixed-default")) {
        if (mod->getSavedValue<int>("profile-img-zlayer", -1) == 1) {
            mod->setSavedValue<int>("profile-img-zlayer", -1);
        }
        mod->setSavedValue<bool>("profile-img-zlayer-fixed-default", true);
    }
}

}

inline void migrateToSavedValues() {
    internal::applyDefaults(false);
    internal::runOneShotMigrations();
}

inline void forceResetSavedValuesToDefaults() {
    auto* mod = geode::Mod::get();

    internal::applyDefaults(true);

    mod->setSavedValue<std::string>("paidraw_server_url", "https://paimbnailsbot.onrender.com");
    mod->setSavedValue<bool>("popup-blur-style-migrated-to-paimonblur", true);
    mod->setSavedValue<bool>("popup-blur-style-migrated-to-paiblur", true);
    mod->setSavedValue<bool>("profile-img-zlayer-fixed-default", true);
}

}
