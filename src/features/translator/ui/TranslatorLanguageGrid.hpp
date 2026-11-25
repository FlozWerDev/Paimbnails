#pragma once

#include <Geode/Geode.hpp>
#include "../TranslatorLanguages.hpp"

namespace paimon::translate::ui {

// GD button tile for one language: dark badge with the code in goldFont, the
// native name in chatFont (the only GD font with accents and Cyrillic) and a
// check on the selected language. width is the on-screen width.
ButtonSprite* makeLanguageTile(LanguageEntry const& entry, bool selected, float width,
    float scale = 1.f);

// swaps skin and check in place, so the button keeps its position and callback.
void setLanguageTileSelected(CCMenuItemSpriteExtra* button, bool selected);

}
