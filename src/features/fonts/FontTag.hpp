#pragma once

#include <string>

namespace paimon::fonts {

struct FontTagResult {
    std::string fontFile;      // resolved .fnt filename (e.g. "gjFont01.fnt")
    std::string remainingText; // text after the <f:...> prefix
    bool hasTag = false;       // true if a valid font tag was found
};

// Leading <f:ID> only; unknown files fall back to chatFont.fnt.
FontTagResult parseFontTag(std::string const& text);

// Returns just the font ID from a <f:ID> prefix, or "" if none.
std::string extractFontId(std::string const& text);

} // namespace paimon::fonts
