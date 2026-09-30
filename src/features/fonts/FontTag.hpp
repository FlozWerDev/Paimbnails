#pragma once

#include <string>

namespace paimon::fonts {

struct FontTagResult {
    std::string fontFile;      // resolved .fnt filename (e.g. "gjfont01.fnt")
    std::string remainingText; // text after the <f:...> prefix
    bool hasTag = false;       // true if a valid font tag was found
};

// leading <f:id> only; unknown files fall back to chatfont.fnt.
FontTagResult parseFontTag(std::string const& text);

std::string extractFontId(std::string const& text);

} // namespace paimon::fonts
