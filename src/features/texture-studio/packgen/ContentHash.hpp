#pragma once
// PackGen v2 content hashing: FNV-1a64 plus quantized param fingerprints, stable across runs.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace paimon::texture_studio::packgen {

inline constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
inline constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

inline std::uint64_t fnv1a64(void const* data, std::size_t size,
                             std::uint64_t seed = kFnvOffsetBasis) {
    auto const* bytes = static_cast<std::uint8_t const*>(data);
    std::uint64_t h = seed;
    for (std::size_t i = 0; i < size; ++i) {
        h ^= bytes[i];
        h *= kFnvPrime;
    }
    return h;
}

inline std::uint64_t hashBytes(std::uint8_t const* data, std::size_t size) {
    if (!data || size == 0) return kFnvOffsetBasis;
    return fnv1a64(data, size);
}

inline std::uint64_t hashCombine(std::uint64_t a, std::uint64_t b) {
    // SplitMix64-style avalanche on the pair; order-sensitive.
    std::uint64_t z = a + 0x9E3779B97F4A7C15ULL + (b << 6) + (b >> 2);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

inline std::uint64_t hashString(std::string_view s) {
    return fnv1a64(s.data(), s.size());
}

// Quantize to 1/1024 steps; NaN maps to 0 instead of poisoning the key.
inline std::int32_t normalizeFloat(float v) {
    if (!std::isfinite(v)) return 0;
    float q = std::lround(v * 1024.0f);
    if (q > 2147483647.0f) return 2147483647;
    if (q < -2147483648.0f) return INT32_MIN;
    return static_cast<std::int32_t>(q);
}

inline std::uint64_t hashFloat(float v, std::uint64_t seed = kFnvOffsetBasis) {
    std::int32_t q = normalizeFloat(v);
    std::uint8_t buf[4];
    std::memcpy(buf, &q, sizeof(buf));
    return fnv1a64(buf, sizeof(buf), seed);
}

// Key: content hash plus producing pipeline version; bumping the version (see PackGen.hpp) invalidates all.
struct NodeKey {
    std::uint64_t hash = kFnvOffsetBasis;
    int version = 0;

    bool operator==(NodeKey const& o) const {
        return hash == o.hash && version == o.version;
    }
    bool operator!=(NodeKey const& o) const { return !(*this == o); }
};

struct NodeKeyHasher {
    std::size_t operator()(NodeKey const& k) const noexcept {
        return static_cast<std::size_t>(
            hashCombine(k.hash, static_cast<std::uint64_t>(k.version)));
    }
};

// Canonical tint fingerprint shared by pipeline, graph and cache: one definition of "same job".
struct TintParams {
    std::uint8_t c1r = 0, c1g = 0, c1b = 0;
    std::uint8_t c2r = 0, c2g = 0, c2b = 0;
    std::uint8_t glowR = 0, glowG = 0, glowB = 0;
    std::uint8_t detailR = 255, detailG = 255, detailB = 255;
    int brightness = 160;
    float saturation = 1.0f;
    float contrast = 0.0f;
    bool alternativeGlow = false;
    int darkThreshold = 0;
    int clusterK = 5;
    float softness = 0.35f;
    int edgeCleanup = 1;

    std::uint64_t fingerprint() const {
        std::uint64_t h = kFnvOffsetBasis;
        std::uint8_t rgb[12] = {c1r, c1g, c1b, c2r, c2g, c2b,
                                glowR, glowG, glowB, detailR, detailG, detailB};
        h = fnv1a64(rgb, sizeof(rgb), h);
        std::uint8_t ints[16];
        std::memcpy(ints, &brightness, 4);
        std::memcpy(ints + 4, &darkThreshold, 4);
        std::memcpy(ints + 8, &clusterK, 4);
        std::memcpy(ints + 12, &edgeCleanup, 4);
        h = fnv1a64(ints, sizeof(ints), h);
        h = hashFloat(saturation, h);
        h = hashFloat(contrast, h);
        h = hashFloat(softness, h);
        h = fnv1a64(&alternativeGlow, 1, h);
        return h;
    }
};

}  // namespace paimon::texture_studio::packgen
