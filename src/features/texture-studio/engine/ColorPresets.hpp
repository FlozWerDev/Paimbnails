#pragma once

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
    static std::vector<ColorPreset> const& list();

private:
    ColorPresets() = delete;
};

}  // namespace paimon::texture_studio
