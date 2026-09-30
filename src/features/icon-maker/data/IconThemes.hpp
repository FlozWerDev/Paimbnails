#pragma once

#include "FillSpec.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace paimon::icon_maker {

// white ("extra") is the game's icon eyes and glints, so it only paints on request.
struct IconTheme {
    std::string name;
    FillSpec main;
    FillSpec secondary;
    FillSpec tertiary;
    FillSpec glow;
    bool paintExtra = false;
    FillSpec extra;
};

std::vector<IconTheme> const& iconThemes();

// theme from the current player colors. false when gamemanager isn't up yet.
bool currentKitTheme(IconTheme& out);

// fill for a zone; false when the theme leaves it unpainted.
bool themeFillFor(IconTheme const& theme, std::string_view slotKey, FillSpec& out);

}  // namespace paimon::icon_maker
