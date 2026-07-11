#pragma once

#include <string>
#include <string_view>

namespace paimon::cache {

// Query params that change every request and must not pollute the URL RAM key.
inline bool isVolatileUrlParam(std::string_view key) {
    return key == "_pv" || key == "_cb" || key == "ts" || key == "v" || key == "t";
}

// Strip volatile cache-buster query params so equivalent URLs share one RAM entry.
// Pure: no allocations beyond the returned string; stable across sessions.
inline std::string normalizeUrlKey(std::string const& url) {
    size_t q = url.find('?');
    if (q == std::string::npos) return url;

    std::string_view base(url.data(), q);
    std::string_view query(url.data() + q + 1, url.size() - q - 1);

    std::string out;
    out.reserve(url.size());
    out.append(base);

    bool first = true;
    size_t start = 0;
    while (start < query.size()) {
        size_t end = query.find('&', start);
        if (end == std::string_view::npos) end = query.size();

        std::string_view pair(query.data() + start, end - start);
        size_t eq = pair.find('=');
        std::string_view key = (eq == std::string_view::npos)
            ? pair
            : std::string_view(pair.data(), eq);
        if (!isVolatileUrlParam(key)) {
            if (first) {
                out.push_back('?');
                first = false;
            } else {
                out.push_back('&');
            }
            out.append(pair);
        }
        start = end + 1;
    }
    return out;
}

// Integer RAM key for level thumbnails: positive = static, negative = GIF.
// Matches CacheKey::toLegacy() / fromLegacy() conventions.
inline int makeLevelRamKey(int levelID, bool isGif) {
    return isGif ? -levelID : levelID;
}

inline int levelIdFromRamKey(int key) {
    return key < 0 ? -key : key;
}

inline bool isGifRamKey(int key) {
    return key < 0;
}

// Blur intensity bucket (0.5 steps) shared by BlurSystem / LevelCell blur keys.
inline int blurIntensityBucket(float intensity) {
    if (intensity <= 0.f) return 0;
    int bucket = static_cast<int>(intensity * 2.0f + 0.5f); // round
    if (bucket < 0) return 0;
    if (bucket > 20) return 20;
    return bucket;
}

// Pure predicate matching ThumbnailLoader::isLoaded / ThumbnailCache::getFromRam
// presence semantics (without I/O or locks):
//   - level-key RAM hit always counts
//   - for static thumbs, a default-URL hit in the URL RAM layer also counts
//   - GIF never falls back to the URL layer
inline bool isLevelTextureLoadedInRam(bool hasLevelRamKey, bool isGif, bool hasDefaultUrlInUrlRam) {
    if (hasLevelRamKey) return true;
    if (isGif) return false;
    return hasDefaultUrlInUrlRam;
}

} // namespace paimon::cache
