#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <atomic>

namespace paimon::settings {

namespace internal {
    inline std::atomic<uint64_t> g_settingsVersion{0};
}

namespace thumbnails {
    inline std::string backgroundType() {
        return geode::Mod::get()->getSavedValue<std::string>("levelcell-background-type", "thumbnail");
    }
    inline double backgroundBlur() {
        return geode::Mod::get()->getSavedValue<double>("levelcell-background-blur", 3.0);
    }
    inline double backgroundDarkness() {
        return geode::Mod::get()->getSavedValue<double>("levelcell-background-darkness", 0.2);
    }
    inline bool showSeparator() {
        return geode::Mod::get()->getSavedValue<bool>("levelcell-show-separator", true);
    }
    inline bool showViewButton() {
        return geode::Mod::get()->getSavedValue<bool>("levelcell-show-view-button", true);
    }
    inline bool hoverEffects() {
        return geode::Mod::get()->getSettingValue<bool>("levelcell-hover-effects");
    }
    inline std::string animType() {
        return geode::Mod::get()->getSavedValue<std::string>("levelcell-anim-type", "zoom-slide");
    }
    inline double animSpeed() {
        return geode::Mod::get()->getSavedValue<double>("levelcell-anim-speed", 1.0);
    }
    inline std::string animEffect() {
        return geode::Mod::get()->getSavedValue<std::string>("levelcell-anim-effect", "none");
    }
    inline bool animatedGradient() {
        return geode::Mod::get()->getSavedValue<bool>("levelcell-animated-gradient", true);
    }
    inline bool mythicParticles() {
        return geode::Mod::get()->getSavedValue<bool>("levelcell-mythic-particles", true);
    }
    inline bool effectOnGradient() {
        return geode::Mod::get()->getSavedValue<bool>("levelcell-effect-on-gradient", false);
    }
    inline bool compactListMode() {
        return geode::Mod::get()->getSettingValue<bool>("compact-list-mode");
    }
    inline bool transparentListMode() {
        return geode::Mod::get()->getSavedValue<bool>("transparent-list-mode", false);
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
        return geode::Mod::get()->getSavedValue<bool>("gif-ram-cache", true);
    }
} // namespace thumbnails

namespace levelinfo {
    inline std::string backgroundStyle() {
        return geode::Mod::get()->getSettingValue<std::string>("levelinfo-background-style");
    }
    inline int effectIntensity() {
        return geode::Mod::get()->getSavedValue<int>("levelinfo-effect-intensity", 4);
    }
    inline int bgDarkness() {
        return geode::Mod::get()->getSavedValue<int>("levelinfo-bg-darkness", 27);
    }
    inline std::string extraStyles() {
        return geode::Mod::get()->getSavedValue<std::string>("levelinfo-extra-styles", "");
    }
    inline bool dynamicSong() {
        return geode::Mod::get()->getSettingValue<bool>("dynamic-song");
    }
} // namespace levelinfo

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
    inline bool transparentBackgroundMode() {
        return geode::Mod::get()->getSavedValue<bool>("transparent-background-mode", false);
    }
} // namespace backgrounds

namespace popupblur {
    inline bool enabled() {
        return geode::Mod::get()->getSettingValue<bool>("popup-blur-enabled");
    }
    inline std::string style() {
        return geode::Mod::get()->getSavedValue<std::string>("popup-blur-style", "paiblur");
    }
    inline double intensity() {
        return geode::Mod::get()->getSavedValue<double>("popup-blur-intensity", 4.0);
    }
    inline double darkness() {
        return geode::Mod::get()->getSavedValue<double>("popup-blur-darkness", 0.28);
    }
    inline double padding() {
        return geode::Mod::get()->getSavedValue<double>("popup-blur-padding", 4.0);
    }
    inline double cornerRadius() {
        return geode::Mod::get()->getSavedValue<double>("popup-blur-corner-radius", 8.0);
    }
    inline double fadeDuration() {
        return geode::Mod::get()->getSavedValue<double>("popup-blur-fade-duration", 0.18);
    }
    inline bool showPlaceholder() {
        return geode::Mod::get()->getSavedValue<bool>("popup-blur-show-placeholder", true);
    }
    inline std::string uiTheme() {
        return geode::Mod::get()->getSettingValue<std::string>("popup-ui-theme");
    }
} // namespace popupblur

namespace video {
    inline int fpsLimit() {
        return geode::Mod::get()->getSavedValue<int>("video-fps-limit", 30);
    }
    inline bool audioEnabled() {
        return geode::Mod::get()->getSavedValue<bool>("video-audio-enabled", false);
    }
    inline bool disableVideoChunks() {
        return geode::Mod::get()->getSavedValue<bool>("disable-video-chunks", true);
    }
    // 0=Auto, 50=Low, 75=Medium, 100=High
    inline int videoQuality() {
        return geode::Mod::get()->getSavedValue<int>("video-quality", 0);
    }
    // Longest-side cap (px) the decoder downscales to. 0 = native (no cap).
    // Downscaling at decode time shrinks the ring buffer, GL textures, PBOs
    // and the resolve FBO proportionally — the main video RAM consumers.
    inline int videoMaxDecodeDimension() {
        switch (videoQuality()) {
            case 100: return 0;     // High: native resolution
            case 75:  return 1280;  // Medium: ~720p cap
            case 50:  return 854;   // Low: ~480p cap
            default:  return 1920;  // Auto: 1080p native, only 1440p/4K downscale
        }
    }
    inline std::string videoBlurType() {
        return geode::Mod::get()->getSavedValue<std::string>("video-blur-type", "none");
    }
    inline float videoBlurIntensity() {
        return geode::Mod::get()->getSavedValue<float>("video-blur-intensity", 0.5f);
    }
    // Degrees: 0, 90, 180, 270
    inline int videoRotation() {
        return geode::Mod::get()->getSavedValue<int>("video-rotation", 0);
    }
    // 4K video needs ~180MB per player, so 512MB allows 2-3 concurrent 4K players.
    inline int maxChunkMemoryMB() {
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
        return geode::Mod::get()->getSavedValue<int>("video-max-chunk-memory-mb", 256);
#else
        return geode::Mod::get()->getSavedValue<int>("video-max-chunk-memory-mb", 512);
#endif
    }

    inline int maxConcurrentVideos() {
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
        return geode::Mod::get()->getSavedValue<int>("video-max-concurrent", 2);
#else
        return geode::Mod::get()->getSavedValue<int>("video-max-concurrent", 4);
#endif
    }

    // Effective FPS = clamp(fpsLimit() / activeCount, minVideoFPS(), fpsLimit()).
    inline bool adaptiveFPS() {
        return geode::Mod::get()->getSavedValue<bool>("video-adaptive-fps", true);
    }

    inline int minVideoFPS() {
        return geode::Mod::get()->getSavedValue<int>("video-min-fps", 12);
    }

    static constexpr size_t kMaxVideoFileSize = 2ULL * 1024 * 1024 * 1024;
} // namespace video

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
        // -1: background sits behind the comment list (z=0); 1 would cover comments.
        return geode::Mod::get()->getSavedValue<int>("profile-img-zlayer", -1);
    }
    inline std::string profileBgType() {
        return geode::Mod::get()->getSavedValue<std::string>("profile-bg-type", "none");
    }
    inline std::string profileBgPath() {
        return geode::Mod::get()->getSavedValue<std::string>("profile-bg-path", "");
    }
    inline bool redesignEnabled() {
        return geode::Mod::get()->getSettingValue<bool>("profile-redesign-enabled");
    }
} // namespace profiles

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

namespace general {
    inline bool smoothScroll() {
        return geode::Mod::get()->getSettingValue<bool>("smooth-scroll");
    }
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
    inline bool autoUpdate() {
        return geode::Mod::get()->getSettingValue<bool>("auto-update");
    }
} // namespace general

namespace smoothscroll {
    inline constexpr double kSensitivityDefault = 2.0;
    inline constexpr double kSensitivityMin     = 0.25;
    inline constexpr double kSensitivityMax     = 5.0;
    inline constexpr double kSmoothnessDefault  = 1.0;
    inline constexpr double kSmoothnessMin      = 0.25;
    inline constexpr double kSmoothnessMax      = 3.0;
    inline constexpr double kEditorZoomSensitivityDefault = 1.0;
    inline constexpr double kEditorZoomSensitivityMin     = 0.25;
    inline constexpr double kEditorZoomSensitivityMax     = 3.0;
    inline constexpr double kEditorZoomSmoothnessDefault  = 1.15;
    inline constexpr double kEditorZoomSmoothnessMin      = 0.25;
    inline constexpr double kEditorZoomSmoothnessMax      = 3.0;

    inline bool enabled() {
        return geode::Mod::get()->getSettingValue<bool>("smooth-scroll");
    }
    inline double sensitivity() {
        return geode::Mod::get()->getSavedValue<double>("smooth-scroll-sensitivity", kSensitivityDefault);
    }
    inline double smoothness() {
        return geode::Mod::get()->getSavedValue<double>("smooth-scroll-smoothness", kSmoothnessDefault);
    }
    inline bool editorZoomEnabled() {
        return geode::Mod::get()->getSavedValue<bool>("smooth-scroll-editor-zoom", true);
    }
    inline bool fixEditorScroll() {
        return geode::Mod::get()->getSavedValue<bool>("smooth-scroll-fix-editor", true);
    }
    inline double editorZoomSensitivity() {
        return geode::Mod::get()->getSavedValue<double>(
            "smooth-scroll-editor-zoom-sensitivity", kEditorZoomSensitivityDefault);
    }
    inline double editorZoomSmoothness() {
        return geode::Mod::get()->getSavedValue<double>(
            "smooth-scroll-editor-zoom-smoothness", kEditorZoomSmoothnessDefault);
    }
} // namespace smoothscroll

namespace smoothui {
    inline bool enabled() {
        return geode::Mod::get()->getSettingValue<bool>("smooth-ui-enabled");
    }
    inline bool reducedMotion() {
        return geode::Mod::get()->getSavedValue<bool>("smooth-ui-reduced-motion", false);
    }
    inline double globalSpeed() {
        return geode::Mod::get()->getSavedValue<double>("smooth-ui-global-speed", 1.0);
    }
    inline double motionStrength() {
        return geode::Mod::get()->getSavedValue<double>("smooth-ui-motion-strength", 1.0);
    }
    inline bool animateButtons() {
        return geode::Mod::get()->getSavedValue<bool>("smooth-ui-animate-buttons", true);
    }
    inline std::string buttonScope() {
        return geode::Mod::get()->getSavedValue<std::string>("smooth-ui-button-scope", "all");
    }
    inline double buttonPressScale() {
        return geode::Mod::get()->getSavedValue<double>("smooth-ui-button-press-scale", 0.94);
    }
    inline bool buttonReleaseBounce() {
        return geode::Mod::get()->getSavedValue<bool>("smooth-ui-button-release-bounce", true);
    }
} // namespace smoothui

namespace discord_rpc {
    inline bool enabled() {
        return geode::Mod::get()->getSettingValue<bool>("discord-rpc-enabled");
    }
    inline bool privateMode() {
        return geode::Mod::get()->getSavedValue<bool>("discord-rpc-private-mode", false);
    }
    inline bool idleWhenUnfocused() {
        return geode::Mod::get()->getSavedValue<bool>("discord-rpc-idle-when-unfocused", true);
    }
    inline bool showProgress() {
        return geode::Mod::get()->getSavedValue<bool>("discord-rpc-show-progress", true);
    }
    inline bool includePaimbnailsFeatures() {
        return geode::Mod::get()->getSavedValue<bool>("discord-rpc-include-paimbnails-features", true);
    }
    inline std::string largeText() {
        return geode::Mod::get()->getSavedValue<std::string>("discord-rpc-large-text", "");
    }
    inline std::string largeImageKey() {
        return geode::Mod::get()->getSavedValue<std::string>("discord-rpc-large-image-key", "");
    }
    inline std::string smallImageKey() {
        return geode::Mod::get()->getSavedValue<std::string>("discord-rpc-small-image-key", "");
    }
    inline std::string activityType() {
        return geode::Mod::get()->getSavedValue<std::string>("discord-rpc-activity-type", "Playing");
    }
    inline bool showTimestamp() {
        return geode::Mod::get()->getSavedValue<bool>("discord-rpc-show-timestamp", true);
    }
    inline bool overrideDetails() {
        return geode::Mod::get()->getSavedValue<bool>("discord-rpc-override-details", false);
    }
    inline std::string customDetails() {
        return geode::Mod::get()->getSavedValue<std::string>("discord-rpc-custom-details", "");
    }
    inline bool overrideState() {
        return geode::Mod::get()->getSavedValue<bool>("discord-rpc-override-state", false);
    }
    inline std::string customState() {
        return geode::Mod::get()->getSavedValue<std::string>("discord-rpc-custom-state", "");
    }
} // namespace discord_rpc

namespace cursor {
    inline bool hideInGameplay() {
        return geode::Mod::get()->getSavedValue<bool>("custom-cursor-hide-in-gameplay", true);
    }
} // namespace cursor

namespace quality {
    // The 22 main levels are pinned in RAM (exempt from the LRU); this entry cap
    // leaves room beyond them. The byte cap below is the real memory limit.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    inline size_t ramCacheEntries() { return 40; }
    inline size_t ramCacheBytes()   { return 100ull * 1024 * 1024; }
#else
    inline size_t ramCacheEntries() { return 64; }
    inline size_t ramCacheBytes()   { return 200ull * 1024 * 1024; }
#endif
    inline size_t diskCacheBytes()  { return 512ull * 1024 * 1024; }
    inline std::string cacheSubdir() { return "cache"; }
} // namespace quality

} // namespace paimon::settings
