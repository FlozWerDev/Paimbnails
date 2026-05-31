#pragma once
//
// ColorPresets.hpp - A small, hand-curated set of named palettes the
// user can apply with one click instead of fiddling with the three color
// pickers. The list intentionally stays short — this is a guideline, not
// a complete palette database. Users who want more variety can save their
// own slots.
//

#include <Geode/cocos/include/ccTypes.h>

#include <string>
#include <vector>

namespace paimon::texture_studio {

struct ColorPreset {
    std::string name;
    cocos2d::ccColor3B color1;
    cocos2d::ccColor3B color2;
    cocos2d::ccColor3B colorGlow;
    int brightness;
};

class ColorPresets final {
public:
    // Returns a stable, ordered list of presets. Index 0 = "Default"
    // (PackGen's defaults), so calling apply(0) recovers a known-good
    // baseline.
    static std::vector<ColorPreset> const& list();

private:
    ColorPresets() = delete;
};

}  // namespace paimon::texture_studio
