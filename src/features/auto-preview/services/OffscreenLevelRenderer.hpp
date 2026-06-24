#pragma once

#include <Geode/DefaultInclude.hpp>
#include <cstdint>
#include <vector>

class GJGameLevel;

namespace paimon::autopreview {

struct OffscreenRenderResult {
    bool success = false;
    std::vector<uint8_t> rgba; // width*height*4, top-to-bottom
    int width = 0;
    int height = 0;
};

class OffscreenLevelRenderer {
public:
    // size. Returns success=false if the level can't be rendered safely.
    static OffscreenRenderResult render(GJGameLevel* level, int width, int height);
};

} // namespace paimon::autopreview
