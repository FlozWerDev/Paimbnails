#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace paimon::text {

// text from servers, save files and user input: keep it valid UTF-8, drop
// control chars (except newlines), never cut a multi-byte sequence.
inline bool isContinuationByte(unsigned char c) {
    return (c & 0xC0) == 0x80;
}

inline std::size_t utf8SequenceLength(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 0;
}

inline std::string sanitizeDisplayText(std::string_view input, std::size_t maxBytes) {
    if (maxBytes == 0) return {};

    std::string out;
    out.reserve(std::min(input.size(), maxBytes));

    for (std::size_t i = 0; i < input.size() && out.size() < maxBytes;) {
        unsigned char c = static_cast<unsigned char>(input[i]);

        if (c == '\r') {
            ++i;
            continue;
        }
        if (c == '\n') {
            // collapse runs of blank lines and never start with one.
            if (!out.empty() && out.back() != '\n' && out.size() < maxBytes) {
                out.push_back('\n');
            }
            ++i;
            continue;
        }
        if (c == '\t') {
            c = ' ';
        }
        if (c < 0x20 || c == 0x7f) {
            ++i;
            continue;
        }

        std::size_t const length = utf8SequenceLength(c);
        if (length == 0) {
            // invalid lead byte: replace with '?' to keep text readable.
            if (out.size() < maxBytes) out.push_back('?');
            ++i;
            continue;
        }
        if (length > 1) {
            if (i + length > input.size()) {
                if (out.size() < maxBytes) out.push_back('?');
                break;
            }
            bool valid = true;
            for (std::size_t j = 1; j < length; ++j) {
                if (!isContinuationByte(static_cast<unsigned char>(input[i + j]))) {
                    valid = false;
                    break;
                }
            }
            if (!valid) {
                if (out.size() < maxBytes) out.push_back('?');
                ++i;
                continue;
            }
        }
        if (out.size() + length > maxBytes) break;
        if (length == 1) {
            // push the normalized byte (tabs became spaces above).
            out.push_back(static_cast<char>(c));
            ++i;
            continue;
        }

        out.append(input.substr(i, length));
        i += length;
    }

    auto const first = out.find_first_not_of(" \t\n");
    if (first == std::string::npos) return {};
    auto const last = out.find_last_not_of(" \t\n");
    return out.substr(first, last - first + 1);
}

// account names are single-line: reject control chars outright instead of
// silently editing a name the server will look up.
inline bool isSafeAccountName(std::string_view name) {
    if (name.empty() || name.size() > 64) return false;
    if (name.find_first_not_of(" \t") == std::string_view::npos) return false;
    for (unsigned char c : name) {
        if (c < 0x20 || c == 0x7f) return false;
    }
    return true;
}

} // namespace paimon::text
