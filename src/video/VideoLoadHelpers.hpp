#pragma once

#include <algorithm>
#include <climits>
#include <cstdint>
#include <string>
#include <string_view>

namespace paimon::video {

// Saved "video-quality" (0=Auto, 50=Low, 75=Medium, 100=High) → longest-side
// decode cap in px (0 = native). Shared with Settings and unit tests.
inline int maxDecodeDimensionForQuality(int quality) {
    switch (quality) {
        case 100: return 0;     // High: native
        case 75:  return 1280;  // Medium ~720p
        case 50:  return 854;   // Low ~480p
        default:  return 1920;  // Auto: only 1440p/4K downscale
    }
}

// Stable async download key: cacheKey when present, else normalized URL so
// query cache-busters don't spawn duplicate downloads.
inline std::string makeVideoRequestKey(std::string_view url, std::string_view cacheKey) {
    if (!cacheKey.empty()) {
        return std::string("cache:") + std::string(cacheKey);
    }
    if (!url.empty()) {
        return std::string("url:") + std::string(url);
    }
    return {};
}

// Prefer admitting a create when disk already has the bytes (no network).
// networkPending=true means the create depends on an in-flight download.
inline bool shouldPrioritizeDiskCreate(bool hasLocalFile, bool networkPending) {
    return hasLocalFile && !networkPending;
}

// Adaptive target FPS for N concurrent sprites (mirrors VideoThumbnailSprite).
// Pure: no Mod/settings access.
inline int adaptiveSpriteFpsFromBase(int baseFPS, int minFPS, bool adaptive, int activeCount) {
    if (baseFPS <= 0) baseFPS = 30;
    if (minFPS < 1) minFPS = 1;
    if (minFPS > baseFPS) minFPS = baseFPS;
    if (!adaptive || activeCount <= 1) return baseFPS;
    int target = baseFPS / std::max(activeCount, 1);
    if (target < minFPS) target = minFPS;
    if (target > baseFPS) target = baseFPS;
    return target;
}

// Player-cache lookup key: prefer the on-disk file path so create(path) and
// returnPlayerToCache agree. Falls back to logical cacheKey when path empty.
inline std::string playerCacheStoreKey(std::string_view filePath, std::string_view logicalKey) {
    if (!filePath.empty()) return std::string(filePath);
    return std::string(logicalKey);
}

// Version-gated snapshot for videoMaxDecodeDimension()/adaptiveSpriteFPS():
// when version advances the next call re-evaluates quality.
struct DecodeDimSnapshot {
    int cachedDim = -1;
    uint64_t cachedVer = UINT64_MAX;

    int get(int quality, uint64_t settingsVersion) {
        if (cachedDim >= 0 && settingsVersion == cachedVer) {
            return cachedDim;
        }
        cachedVer = settingsVersion;
        cachedDim = maxDecodeDimensionForQuality(quality);
        return cachedDim;
    }
};

} // namespace paimon::video
