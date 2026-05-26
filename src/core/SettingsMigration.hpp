#pragma once

#include <Geode/Geode.hpp>
#include <string>

// ────────────────────────────────────────────────────────────────────────────
// SettingsMigration — Inicializa saved values con defaults para settings que
// se movieron fuera de mod.json. Se llama una vez al inicio del mod.
//
// Los settings migrados ahora se leen/escriben con getSavedValue/setSavedValue
// y se configuran desde el panel de settings interno de Paimbnails
// (SettingsPanelManager / SettingsCategoryBuilder) en vez del popup nativo de
// Geode.
// ────────────────────────────────────────────────────────────────────────────

namespace paimon::settings {

inline void migrateToSavedValues() {
    auto* mod = geode::Mod::get();

    // Helper: solo setea el default si no existe ya un saved value
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

    // ── Discord RPC (migrado de mod.json settings) ──
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

    // ── PaiDraw / Paimon Emote API server ──
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

    // ── Level Thumbnails (granulares) ──
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

    // ── Level Visual Effects ──
    defStr("levelcell-anim-type", "zoom-slide");
    defDouble("levelcell-anim-speed", 1.0);
    defStr("levelcell-anim-effect", "none");
    defBool("levelcell-effect-on-gradient", false);
    defBool("levelcell-mythic-particles", true);
    defBool("levelcell-animated-gradient", true);

    // ── Level Info (granulares) ──
    defStr("levelinfo-extra-styles", "");
    defInt("levelinfo-effect-intensity", 4);
    defInt("levelinfo-bg-darkness", 27);
    defBool("dynamic-song-stream-preview", true);

    // ── Profile Music (granulares) ──
    defBool("profile-music-crossfade", true);
    defDouble("profile-music-fade-duration", 0.3);

    // ── Realtime Search (granular) ──
    defInt("realtime-search-debounce-ms", 350);

    // ── Dynamic Popup (granulares) ──
    defStr("dynamic-popup-style", "paimonUI");
    defDouble("dynamic-popup-speed", 1.0);
    defDouble("dynamic-exit-speed", 1.0);

    // ── Popup Blur (granulares) ──
    // Default style: "paimonblur" (Dual Kawase multi-pass — look premium, mejor
    // calidad visual que el gaussian 2-pass). Para usuarios existentes que
    // ya tienen "gaussian" guardado del default anterior, se aplica una
    // migracion one-shot mas abajo que respeta cambios manuales.
    defStr("popup-blur-style", "paimonblur");
    defDouble("popup-blur-intensity", 4.0);
    defDouble("popup-blur-darkness", 0.28);
    defDouble("popup-blur-padding", 4.0);
    defDouble("popup-blur-corner-radius", 8.0);
    defDouble("popup-blur-fade-duration", 0.18);
    defBool("popup-blur-show-placeholder", true);

    // One-shot migration: usuarios que instalaron con el default antiguo
    // ("gaussian") tenian ese valor fijado aunque nunca lo cambiaron a mano.
    // Si aun no se aplico esta migracion y el valor actual es "gaussian",
    // asumimos que viene del default viejo y lo subimos a "paimonblur".
    // Si el usuario lo cambio manualmente a "gaussian" despues, esta migracion
    // ejecuta una sola vez y no vuelve a pisarlo.
    if (!mod->hasSavedValue("popup-blur-style-migrated-to-paimonblur")) {
        if (mod->getSavedValue<std::string>("popup-blur-style") == "gaussian") {
            mod->setSavedValue<std::string>("popup-blur-style", "paimonblur");
        }
        mod->setSavedValue<bool>("popup-blur-style-migrated-to-paimonblur", true);
    }

    // ── For You ──
    defBool("enable-for-you", false);
    defInt("for-you-min-levels", 5);
    defBool("for-you-use-tags", true);

    // ── Custom Cursor (granulares) ──
    defDouble("custom-cursor-scale", 0.3);
    defBool("custom-cursor-trail", false);
    defBool("custom-cursor-hide-in-gameplay", true);

    // ── Menu Loop (granulares) ──
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
    // ── Seek + extras from reference mod ──
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

    // ── Menu Music (granulares) ──
    defDouble("menuMusicBlurIntensity", 5.0);
    defDouble("menuMusicBlurDarkness", 0.45);
    defBool("menuMusicAutoplayOnBoot", false);
    defStr("menuMusicDownloadFormat", "mp3");

    // ── Performance (granulares) ──
    defBool("gif-ram-cache", true);
    defBool("disable-video-chunks", true);

    // ── Layout Editor (granulares) ──
    defBool("main-menu-layout-grid-snap", true);
    defInt("main-menu-layout-grid-size", 10);
    defInt("main-menu-layout-snap-distance", 10);
    defBool("main-menu-layout-show-guides", true);
    defBool("main-menu-layout-snap-to-edges", true);

    // ── Zoom (granulares) ──
    defDouble("zoom-sensitivity", 1.0);
    defBool("zoom-auto-hide-menu", true);
    defBool("zoom-auto-show-menu", true);
    defBool("zoom-alt-disables-scroll", true);

    // ── Profile ──
    defInt("profile-img-zlayer", 1);
}

} // namespace paimon::settings
