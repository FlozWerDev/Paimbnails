#pragma once
//
// Plain-Old-Data describing one sprite frame in a cocos2d-x .plist atlas.
// Avoids Geode/Cocos types so the data layer is cheap to include and can
// be unit-tested offline.
//

#include <cstdint>
#include <string>
#include <vector>

namespace paimon::texture_studio {

struct SpriteFrameInfo {
    std::string name;

    int rectX = 0;
    int rectY = 0;
    int rectW = 0;
    int rectH = 0;

    int spriteW = 0;
    int spriteH = 0;

    float offsetX = 0.0f;
    float offsetY = 0.0f;

    int sourceW = 0;
    int sourceH = 0;

    bool rotated = false;

    std::vector<std::string> aliases;
};

struct PlistMetadata {
    int format = 3;
    int sizeW = 0;
    int sizeH = 0;
    std::string textureFileName;
    std::string realTextureFileName;
    bool premultiplyAlpha = true;
    std::string smartUpdate;
};

struct ParsedSpritesheet {
    PlistMetadata metadata;
    std::vector<SpriteFrameInfo> frames;
};

}  // namespace paimon::texture_studio
