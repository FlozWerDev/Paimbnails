#pragma once

#include <string>

namespace paimon::fonts {

struct FontTagResult {
    std::string fontFile;      // resolved .fnt filename (e.g. "gjFont01.fnt")
    std::string remainingText; // text after the <f:...> prefix
    bool hasTag = false;       // true if a valid font tag was found
};

// Parse a comment string for a leading <f:ID> font tag.
// Known IDs: "01"-"59" → gjFontXX.fnt, "big" → bigFont.fnt,
// "chat" → chatFont.fnt, "gold" → goldFont.fnt.
// Custom IDs: if it ends with ".fnt" use as-is, otherwise append ".fnt".
// If the resolved font file doesn't exist, falls back to chatFont.fnt.
// If no tag found, returns {chatFont.fnt, originalText, false}.
FontTagResult parseFontTag(std::string const& text);

// Returns just the font ID from a <f:ID> prefix, or "" if none.
std::string extractFontId(std::string const& text);

} // namespace paimon::fonts
