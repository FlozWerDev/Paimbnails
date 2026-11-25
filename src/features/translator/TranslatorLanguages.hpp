#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace paimon::translate {

// every code here can be drawn with chatFont.fnt, the same bitmap font GD uses
// for comments. CJK and Arabic need glyphs that the game does not ship.
struct LanguageEntry {
    char const* code;
    char const* nativeName;
    char const* englishName;
};

std::vector<LanguageEntry> const& languages();
LanguageEntry const* findLanguage(std::string_view code);

}
