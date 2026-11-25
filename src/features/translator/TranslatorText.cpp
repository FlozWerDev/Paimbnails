#include "TranslatorText.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace paimon::translate {
namespace {

bool isColorTagInner(std::string_view inner) {
    if (inner == "c" || inner == "/c") return true;
    return inner.size() == 2 && inner[0] == 'c';
}

// returns the number of bytes in the glyph that starts at index.
std::size_t glyphLength(std::string const& text, std::size_t index) {
    auto first = static_cast<unsigned char>(text[index]);
    std::size_t length = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
    if (first < 0xc2 || first > 0xf4 || index + length > text.size()) return 1;
    for (std::size_t offset = 1; offset < length; ++offset) {
        if ((static_cast<unsigned char>(text[index + offset]) & 0xc0) != 0x80) return 1;
    }
    return length;
}

unsigned int decodeGlyph(std::string const& text, std::size_t index, std::size_t length) {
    auto first = static_cast<unsigned char>(text[index]);
    if (length == 1) return first;
    unsigned int value = first & (length == 2 ? 0x1f : length == 3 ? 0x0f : 0x07);
    for (std::size_t offset = 1; offset < length; ++offset) {
        value = (value << 6) | (static_cast<unsigned char>(text[index + offset]) & 0x3f);
    }
    return value;
}

bool isPrivateUse(unsigned int codepoint) {
    return (codepoint >= 0xe000 && codepoint <= 0xf8ff) ||
           (codepoint >= 0xf0000 && codepoint <= 0x10fffd);
}

constexpr std::size_t kMaxEmoteName = 32;

bool isEmoteNameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-';
}

// byte length of the ":name:" or "<name>" code that starts at index, 0 if none.
std::size_t emoteCodeLength(std::string const& text, std::size_t index) {
    char const open = text[index];
    char const close = open == ':' ? ':' : open == '<' ? '>' : '\0';
    if (close == '\0') return 0;
    std::size_t end = index + 1;
    while (end < text.size() && end - index <= kMaxEmoteName && isEmoteNameChar(text[end])) {
        ++end;
    }
    std::size_t const nameLength = end - index - 1;
    if (end >= text.size() || text[end] != close) return 0;
    if (nameLength == 0 || nameLength > kMaxEmoteName) return 0;
    return nameLength + 2;
}

// colored squares, circles and diamonds (U+1F7E0..U+1F7EB, U+1F534..U+1F53B).
// comments rarely contain them and Google keeps emoji as they are.
constexpr std::array<char const*, 20> kPlaceholders = {
    "\xF0\x9F\x9F\xA0", "\xF0\x9F\x9F\xA1", "\xF0\x9F\x9F\xA2", "\xF0\x9F\x9F\xA3",
    "\xF0\x9F\x9F\xA4", "\xF0\x9F\x9F\xA5", "\xF0\x9F\x9F\xA6", "\xF0\x9F\x9F\xA7",
    "\xF0\x9F\x9F\xA8", "\xF0\x9F\x9F\xA9", "\xF0\x9F\x9F\xAA", "\xF0\x9F\x9F\xAB",
    "\xF0\x9F\x94\xB4", "\xF0\x9F\x94\xB5", "\xF0\x9F\x94\xB6", "\xF0\x9F\x94\xB7",
    "\xF0\x9F\x94\xB8", "\xF0\x9F\x94\xB9", "\xF0\x9F\x94\xBA", "\xF0\x9F\x94\xBB",
};

// emoji variation selector the engine may append after a placeholder
constexpr std::string_view kVariationSelector = "\xEF\xB8\x8F";

}

std::string stripColorTags(std::string const& text) {
    std::string result;
    result.reserve(text.size());
    for (std::size_t index = 0; index < text.size();) {
        if (text[index] == '<') {
            auto close = text.find('>', index + 1);
            if (close != std::string::npos && close - index <= 4 &&
                isColorTagInner(std::string_view(text.data() + index + 1, close - index - 1))) {
                index = close + 1;
                continue;
            }
        }
        result.push_back(text[index++]);
    }
    return result;
}

std::string sanitizeForTranslation(std::string const& text) {
    std::string stripped = stripColorTags(text);
    std::string result;
    result.reserve(stripped.size());
    for (std::size_t index = 0; index < stripped.size();) {
        auto length = glyphLength(stripped, index);
        auto codepoint = decodeGlyph(stripped, index, length);
        bool const control = codepoint < 0x20 && codepoint != '\n' && codepoint != '\t';
        bool const remove = codepoint == 0x7f || control || isPrivateUse(codepoint);
        if (!remove) result.append(stripped, index, length);
        index += length;
    }
    return result;
}

bool isTranslatable(std::string const& text) {
    int letters = 0;
    for (std::size_t index = 0; index < text.size();) {
        if (auto const code = emoteCodeLength(text, index)) {
            index += code;
            continue;
        }
        auto length = glyphLength(text, index);
        auto codepoint = decodeGlyph(text, index, length);
        if ((codepoint >= 'a' && codepoint <= 'z') || (codepoint >= 'A' && codepoint <= 'Z') ||
            codepoint >= 0x00c0) {
            ++letters;
        }
        index += length;
    }
    return letters >= 2;
}

std::string clampUtf8(std::string const& text, std::size_t maxBytes) {
    if (text.size() <= maxBytes) return text;
    std::size_t end = maxBytes;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) --end;
    return text.substr(0, end);
}

ProtectedText protectEmoteCodes(std::string const& text) {
    ProtectedText result;
    result.text.reserve(text.size());

    std::vector<char const*> available;
    for (auto const* placeholder : kPlaceholders) {
        if (text.find(placeholder) == std::string::npos) available.push_back(placeholder);
    }

    for (std::size_t index = 0; index < text.size();) {
        auto const length = emoteCodeLength(text, index);
        if (length == 0) {
            result.text.push_back(text[index++]);
            continue;
        }

        auto code = text.substr(index, length);
        auto known = std::find_if(result.codes.begin(), result.codes.end(),
            [&](auto const& entry) { return entry.second == code; });
        if (known != result.codes.end()) {
            result.text += known->first;
        } else if (result.codes.size() < available.size()) {
            std::string placeholder = available[result.codes.size()];
            result.text += placeholder;
            result.codes.emplace_back(std::move(placeholder), std::move(code));
        } else {
            // out of placeholders: the rest go as plain text
            result.text += code;
        }
        index += length;
    }
    return result;
}

std::string restoreEmoteCodes(std::string text,
    std::vector<std::pair<std::string, std::string>> const& codes) {
    for (auto const& [placeholder, code] : codes) {
        for (auto at = text.find(placeholder); at != std::string::npos;
             at = text.find(placeholder, at + code.size())) {
            auto length = placeholder.size();
            if (std::string_view(text).substr(at + length, kVariationSelector.size()) ==
                kVariationSelector) {
                length += kVariationSelector.size();
            }
            text.replace(at, length, code);
        }
    }
    return text;
}

}
