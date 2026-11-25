#pragma once

#include <Geode/ui/Label.hpp>
#include <string>
#include <utility>

namespace paimon::ui {

// preserve the requested GD font and use chatFont for missing Unicode glyphs.
// Geode normalizes fallback glyphs to the primary font's line height.
inline geode::Label* createUnicodeLabel(std::string text, geode::ZStringView font = "bigFont.fnt") {
    auto* label = geode::Label::create(std::move(text), font);
    if (label) label->registerFont("chatFont.fnt");
    return label;
}

} // namespace paimon::ui
