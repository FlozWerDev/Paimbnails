#pragma once

#include <cstdint>
#include <string_view>

// shared string hash for cache/disk keys.
namespace paimon::hash {

// 64-bit FNV-1a.
inline std::uint64_t fnv1a64(std::string_view text) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (char c : text) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace paimon::hash
