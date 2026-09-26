#pragma once

#include "FillSpec.hpp"

#include <Geode/Geode.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

namespace paimon::icon_maker {

// Handpicked swatches, ordered as they read in a grid: neutrals first, then
// the hue wheel. Wide enough to cover most icons without opening the picker.
std::vector<cocos2d::ccColor3B> const& quickColors();

struct GradientPreset {
    std::string_view name;
    GradientSpec spec;
};

std::vector<GradientPreset> const& gradientPresets();

// The player colors GD itself offers, so a custom icon can be made to match
// the user's current kit exactly. Empty if GameManager isn't up yet.
std::vector<cocos2d::ccColor3B> playerColors();

constexpr std::size_t kRecentColorCount = 10;

// last hand-picked colors, so the wheel search happens once. Persisted.
std::vector<cocos2d::ccColor3B> const& recentColors();
void rememberColor(cocos2d::ccColor3B color);

}  // namespace paimon::icon_maker
