#pragma once

#include "../engine/MaskBuilder.hpp"

#include <cstdint>
#include <vector>

namespace paimon::texture_studio {

// little-endian codec shared by the on-disk stores.
inline void writeU16LE(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

inline void writeU32LE(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

inline std::uint16_t readU16LE(std::uint8_t const* p) {
    return static_cast<std::uint16_t>(p[0])
         | (static_cast<std::uint16_t>(p[1]) << 8);
}

inline std::uint32_t readU32LE(std::uint8_t const* p) {
    return static_cast<std::uint32_t>(p[0])
         | (static_cast<std::uint32_t>(p[1]) << 8)
         | (static_cast<std::uint32_t>(p[2]) << 16)
         | (static_cast<std::uint32_t>(p[3]) << 24);
}

inline bool maskNonEmpty(MaskBuffer const& m) {
    if (m.empty()) return false;
    for (auto v : m.data) {
        if (v != 0) return true;
    }
    return false;
}

} // namespace paimon::texture_studio
