#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace paimon {

// standard padded base64 for upload payloads.
inline std::string base64Encode(std::vector<uint8_t> const& data) {
    static char const* T =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t n = data.size(), i = 0;
    for (; i + 2 < n; i += 3) {
        unsigned v = (static_cast<unsigned>(data[i]) << 16) |
                     (static_cast<unsigned>(data[i + 1]) << 8) |
                      static_cast<unsigned>(data[i + 2]);
        out += T[(v >> 18) & 0x3F];
        out += T[(v >> 12) & 0x3F];
        out += T[(v >> 6) & 0x3F];
        out += T[v & 0x3F];
    }
    if (i < n) {
        unsigned v = static_cast<unsigned>(data[i]) << 16;
        if (i + 1 < n) v |= static_cast<unsigned>(data[i + 1]) << 8;
        out += T[(v >> 18) & 0x3F];
        out += T[(v >> 12) & 0x3F];
        out += (i + 1 < n) ? T[(v >> 6) & 0x3F] : '=';
        out += '=';
    }
    return out;
}

}
