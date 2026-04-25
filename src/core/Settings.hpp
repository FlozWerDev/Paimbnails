#pragma once

// Settings.hpp — Acceso tipado y centralizado a los settings del mod.
// Elimina la duplicacion de string keys y permite autocompletar en IDE.
// Uso: paimon::settings::thumbnails::backgroundType()

#include <Geode/Geode.hpp>
#include <string>
#include <atomic>

namespace paimon::settings {

// ── Internal: settings reactivity ─────────────────────────────────────────
namespace internal {
    inline std::atomic<uint64_t> g_settingsVersion{0};
}

// ── Thumbnails / LevelCell ──────────────────────────────────────────────

namespace thumbnails {
    inline std::string backgroundType() {
        return geode::Mod::get()->getSettingValue<std::string>("levelcell-background-type");
    }
    inline double backgroundBlur() {
        return geode::Mod::get()->getSettingValue<double>("levelcell-background-blur");
    }
    inline double backgroundDarkness() {
        return geode::Mod::get()->getSettingValue<double>("levelcell-background-darkness");
    }
    inline bool showSeparator() {
        return geode::Mod::get()->getSettingValue<bool>("levelcell-show-separator");
    }
    inline bool showViewButton() {
        return geode::Mod::get()->getSettingValue<bool>("levelcell-show-view-button");
    }
    inline bool hoverEffects() {
        return geode::Mod::get()->getSettingValue<bool>("levelcell-hover-effects");
    }
    inline std::string animType() {
        return geode::Mod::get()->getSettingValue<std::string>("levelcell-anim-type");
    }
    inline double animSpeed() {
        return geode::Mod::get()->getSettingValue<double>("levelcell-anim-speed");
    }
    inline std::string animEffect() {
        return geode::Mod::get()->getSettingValue<std::string>("levelcell-anim-effect");
    }
    inline bool animatedGradient() {
        return geode::Mod::get()->getSettingValue<bool>("levelcell-animated-gradient");
    }
    inline bool mythicParticles() {
        return geode::Mod::get()->getSettingValue<bool>("levelcell-mythic-particles");
    }
    inline bool effectOnGradient() {
        return geode::Mod::get()->getSettingValue<bool>("levelcell-effect-on-gradient");
    }
    inline bool compactListMode() {
        return geode::Mod::get()->getSettingValue<bool>("compact-list-mode");
    }
    inline bool transparentListMode() {
        return geode::Mod::get()->getSettingValue<bool>("transparent-list-mode");
    }
    inline double thumbWidth() {
        return geode::Mod::get()->getSettingValue<double>("level-thumb-width");
    }
    inline int64_t concurrentDownloads() {
        return geode::Mod::get()->getSettingValue<int64_t>("thumbnail-concurrent-downloads");
    }
    inline bool enableCapture() {
        return geode::Mod::get()->getSettingValue<bool>("enable-thumbnail-taking");
    }
    inline bool gifRamCache() {
        return geode::Mod::get()->getSettingValue<bool>("gif-ram-cache");
    }
} // namespace thumbnails

// ── LevelInfo ───────────────────────────────────────────────────────────

namespace levelinfo {
    inline std::string backgroundStyle() {
        return geode::Mod::get()->getSettingValue<std::string>("levelinfo-background-style");
    }
    inline int64_t effectIntensity() {
        return geode::Mod::get()->getSettingValue<int64_t>("levelinfo-effect-intensity");
    }
    inline int64_t bgDarkness() {
        return geode::Mod::get()->getSettingValue<int64_t>("levelinfo-bg-darkness");
    }
    inline std::string extraStyles() {
        return geode::Mod::get()->getSettingValue<std::string>("levelinfo-extra-styles");
    }
    inline bool dynamicSong() {
        return geode::Mod::get()->getSettingValue<bool>("dynamic-song");
    }
} // namespace levelinfo

// ── Backgrounds ─────────────────────────────────────────────────────────

namespace backgrounds {
    inline std::string bgType() {
        return geode::Mod::get()->getSavedValue<std::string>("bg-type", "default");
    }
    inline std::string bgCustomPath() {
        return geode::Mod::get()->getSavedValue<std::string>("bg-custom-path", "");
    }
    inline int bgId() {
        return geode::Mod::get()->getSavedValue<int>("bg-id", 0);
    }
    inline bool bgDarkMode() {
        return geode::Mod::get()->getSavedValue<bool>("bg-dark-mode", false);
    }
    inline float bgDarkIntensity() {
        return geode::Mod::get()->getSavedValue<float>("bg-dark-intensity", 0.5f);
    }
    inline bool bgAdaptiveColors() {
        return geode::Mod::get()->getSavedValue<bool>("bg-adaptive-colors", false);
    }
} // namespace backgrounds

// ── Video ───────────────────────────────────────────────────────────────

namespace video {
    inline int fpsLimit() {
        return geode::Mod::get()->getSavedValue<int>("video-fps-limit", 30);
    }
    inline bool audioEnabled() {
        return geode::Mod::get()->getSavedValue<bool>("video-audio-enabled", false);
    }
    inline bool disableVideoChunks() {
        return geode::Mod::get()->getSettingValue<bool>("disable-video-chunks");
    }
    // Video decode quality: 0=Auto, 50=Low, 75=Medium, 100=High
    inline int videoQuality() {
        return geode::Mod::get()->getSavedValue<int>("video-quality", 0);
    }
    inline std::string videoBlurType() {
        return geode::Mod::get()->getSavedValue<std::string>("video-blur-type", "none");
    }
    inline float videoBlurIntensity() {
        return geode::Mod::get()->getSavedValue<float>("video-blur-intensity", 0.5f);
    }
    // Max video chunk memory budget in MB (0 = unlimited)
    // 4K video requires ~180MB per player (RGBA + YUV + ring buffer),
    // so 512MB allows 2-3 concurrent 4K players.
    inline int maxChunkMemoryMB() {
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
        return geode::Mod::get()->getSavedValue<int>("video-max-chunk-memory-mb", 256);
#else
        return geode::Mod::get()->getSavedValue<int>("video-max-chunk-memory-mb", 512);
#endif
    }
    // Max video file size in bytes (2 GB)
    static constexpr size_t kMaxVideoFileSize = 2ULL * 1024 * 1024 * 1024;
} // namespace video

// ── Profiles ────────────────────────────────────────────────────────────

namespace profiles {
    inline std::string scorecellBgType() {
        return geode::Mod::get()->getSavedValue<std::string>("scorecell-background-type", "thumbnail");
    }
    inline float scorecellBlur() {
        return geode::Mod::get()->getSavedValue<float>("scorecell-background-blur", 3.0f);
    }
    inline float scorecellDarkness() {
        return geode::Mod::get()->getSavedValue<float>("scorecell-background-darkness", 0.2f);
    }
    inline float profileThumbWidth() {
        return geode::Mod::get()->getSavedValue<float>("profile-thumb-width", 0.6f);
    }
    inline int64_t profileImgZLayer() {
        return geode::Mod::get()->getSettingValue<int64_t>("profile-img-zlayer");
    }
    inline std::string profileBgType() {
        return geode::Mod::get()->getSavedValue<std::string>("profile-bg-type", "none");
    }
    inline std::string profileBgPath() {
        return geode::Mod::get()->getSavedValue<std::string>("profile-bg-path", "");
    }
} // namespace profiles

// ── Transitions ─────────────────────────────────────────────────────────
// Transition state is managed by TransitionManager::isEnabled() via transitions.json

// ── Moderation ──────────────────────────────────────────────────────────

namespace moderation {
    inline bool isVerifiedModerator() {
        return geode::Mod::get()->getSavedValue<bool>("is-verified-moderator", false);
    }
    inline bool isVerifiedAdmin() {
        return geode::Mod::get()->getSavedValue<bool>("is-verified-admin", false);
    }
    inline bool isVerifiedVip() {
        return geode::Mod::get()->getSavedValue<bool>("is-verified-vip", false);
    }
    inline bool canUploadGIF() {
        return isVerifiedVip() || isVerifiedModerator() || isVerifiedAdmin();
    }
} // namespace moderation

// ── General / Cache ─────────────────────────────────────────────────────

namespace general {
    inline bool clearCacheOnExit() {
        return geode::Mod::get()->getSettingValue<bool>("clear-cache-on-exit");
    }
    inline std::string language() {
        return geode::Mod::get()->getSettingValue<std::string>("language");
    }
    inline bool enableDebugLogs() {
        return geode::Mod::get()->getSettingValue<bool>("enable-debug-logs");
    }
    inline bool enableDiskCache() {
        return geode::Mod::get()->getSettingValue<bool>("enable-disk-cache");
    }
} // namespace general

// ── Cursor ──────────────────────────────────────────────────────────────

namespace cursor {
    inline bool hideInGameplay() {
        return geode::Mod::get()->getSettingValue<bool>("custom-cursor-hide-in-gameplay");
    }
} // namespace cursor

// ── Quality (single resolution — no tiers) ──────────────────────────────

namespace quality {
    // RAM LRU entry limit
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    inline size_t ramCacheEntries() { return 25; }
    // RAM LRU byte cap
    inline size_t ramCacheBytes()   { return 80ull * 1024 * 1024; }
#else
    inline size_t ramCacheEntries() { return 50; }
    // RAM LRU byte cap
    inline size_t ramCacheBytes()   { return 120ull * 1024 * 1024; }
#endif
    // disk cache byte quota
    inline size_t diskCacheBytes()  { return 512ull * 1024 * 1024; }
    // unified cache subdirectory
    inline std::string cacheSubdir() { return "cache"; }
} // namespace quality

} // namespace paimon::settings
