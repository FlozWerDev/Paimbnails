#pragma once

#include <string>
#include <string_view>

namespace paimon::cache {

// volatile per-request query params; excluded from the url ram key.
inline bool isVolatileUrlParam(std::string_view key) {
    return key == "_pv" || key == "_cb" || key == "ts" || key == "v" || key == "t";
}

// strip volatile cache-busters so equivalent urls share one ram entry.
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

// level thumbnail ram key: positive = static, negative = gif.
// matches cachekey::tolegacy() / fromlegacy() conventions.
inline int makeLevelRamKey(int levelID, bool isGif) {
    return isGif ? -levelID : levelID;
}

inline int levelIdFromRamKey(int key) {
    return key < 0 ? -key : key;
}

inline bool isGifRamKey(int key) {
    return key < 0;
}

// intensity bucket in 0.5 steps, shared by blursystem / levelcell blur keys.
inline int blurIntensityBucket(float intensity) {
    if (intensity <= 0.f) return 0;
    int bucket = static_cast<int>(intensity * 2.0f + 0.5f); // round
    if (bucket < 0) return 0;
    if (bucket > 20) return 20;
    return bucket;
}

// ram presence check without i/o or locks; only static thumbs use the url layer.
inline bool isLevelTextureLoadedInRam(bool hasLevelRamKey, bool isGif, bool hasDefaultUrlInUrlRam) {
    if (hasLevelRamKey) return true;
    if (isGif) return false;
    return hasDefaultUrlInUrlRam;
}

} // namespace paimon::cache
