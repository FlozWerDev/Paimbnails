#pragma once

#include <algorithm>
#include <climits>
#include <cstdint>
#include <string>
#include <string_view>

namespace paimon::video {

// "video-quality" setting to longest-side decode cap px (0 = native).
// auto caps 1440p/4k only.
inline int maxDecodeDimensionForQuality(int quality) {
    switch (quality) {
        case 100: return 0;     // high: native
        case 75:  return 1280;  // medium ~720p
        case 50:  return 854;   // low ~480p
        default:  return 1920;  // auto: only 1440p/4k downscale
    }
}

// stable download key; cache-busters must not duplicate downloads.
inline std::string makeVideoRequestKey(std::string_view url, std::string_view cacheKey) {
    if (!cacheKey.empty()) {
        return std::string("cache:") + std::string(cacheKey);
    }
    if (!url.empty()) {
        return std::string("url:") + std::string(url);
    }
    return {};
}

// admit disk-backed creates ahead of pending downloads.
inline bool shouldPrioritizeDiskCreate(bool hasLocalFile, bool networkPending) {
    return hasLocalFile && !networkPending;
}

// target fps for n sprites; mirrors videothumbnailsprite, no settings access.
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

// on-disk path first so store and lookup agree.
inline std::string playerCacheStoreKey(std::string_view filePath, std::string_view logicalKey) {
    if (!filePath.empty()) return std::string(filePath);
    return std::string(logicalKey);
}

// re-evaluates quality when the settings version advances.
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
