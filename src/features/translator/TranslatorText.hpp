#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace paimon::translate {

// GD color tags: <c>, </c> and the four-char variants (<cg>, <cy>, ...).
std::string stripColorTags(std::string const& text);

// strips color tags, control bytes and private-use glyphs before the text
// leaves the game. Newlines and tabs survive so paragraphs keep their shape.
std::string sanitizeForTranslation(std::string const& text);

// true when there is at least one letter worth translating. Emote codes such
// as ":exclaim:" do not count.
bool isTranslatable(std::string const& text);

// clamps to maxBytes without splitting a UTF-8 sequence.
std::string clampUtf8(std::string const& text, std::size_t maxBytes);

// emote codes (":exclaim:", ":normal:", "<name>") swapped for emoji that the
// translation engines pass through unchanged. Each distinct code gets one
// placeholder; restoreEmoteCodes puts the codes back in the translated text.
struct ProtectedText {
    std::string text;
    std::vector<std::pair<std::string, std::string>> codes; // placeholder, code
};

ProtectedText protectEmoteCodes(std::string const& text);
std::string restoreEmoteCodes(std::string text,
    std::vector<std::pair<std::string, std::string>> const& codes);

}
