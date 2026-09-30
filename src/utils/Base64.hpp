#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <string>
#include <string_view>
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

inline bool base64Decode(std::string_view input, std::vector<uint8_t>& out) {
    static constexpr auto table = [] {
        std::array<int8_t, 256> values{};
        values.fill(-1);
        constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (size_t i = 0; i < alphabet.size(); ++i) {
            values[static_cast<unsigned char>(alphabet[i])] = static_cast<int8_t>(i);
        }
        return values;
    }();

    out.clear();
    out.reserve(input.size() / 4 * 3 + 3);
    auto fail = [&out] {
        out.clear();
        return false;
    };
    uint32_t value = 0;
    unsigned bits = 0;
    size_t symbols = 0;
    unsigned padding = 0;
    for (unsigned char c : input) {
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
        if (c == '=') {
            if (++padding > 2) return fail();
            continue;
        }
        if (padding || table[c] < 0) return fail();
        value = (value << 6) | static_cast<uint32_t>(table[c]);
        bits += 6;
        ++symbols;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((value >> bits) & 0xff));
        }
    }
    if (symbols % 4 == 1 || (padding && (symbols + padding) % 4 != 0)
        || (bits && (value & ((1u << bits) - 1u)) != 0)) return fail();
    return true;
}

inline std::string base64UrlDecode(std::string input) {
    for (char& c : input) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    std::vector<uint8_t> bytes;
    if (!base64Decode(input, bytes) || bytes.empty()) return {};
    return std::string(bytes.begin(), bytes.end());
}

}
