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
    static OffscreenRenderResult render(GJGameLevel* level, int width, int height);
    // shared level-string size / object-count guards (logs the skip reason);
    // callers keep their own busy-context checks.
    static bool passesSizeGuards(GJGameLevel* level, int levelID);
    // reentrancy guard: true while a render owns PlayLayer::create on the main thread.
    static bool isRendering();
};

} // namespace paimon::autopreview
