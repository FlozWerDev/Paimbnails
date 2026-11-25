#pragma once

#include <cstdint>
#include <vector>

namespace cocos2d { class CCTexture2D; }

namespace paimon::thumbviewer {

struct TexturePixels {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;

    explicit operator bool() const { return !rgba.empty(); }
};

// main/GL thread only. Read the uploaded image without drawing popup content.
TexturePixels readTexturePixels(cocos2d::CCTexture2D* texture);

}
