#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace paimon::texture_studio {

struct RectPackInput {
    std::string id;
    int width  = 0;
    int height = 0;
};

struct Placement {
    std::string id;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

struct PackResult {
    std::vector<Placement> placements;
    int sheetWidth  = 0;
    int sheetHeight = 0;
};

struct PackerOptions {
    int gap      = 2;
    int maxWidth = 4096;  // sheet width budget (cocos2d / GL hard limit)
};

class RectPacker final {
public:
    static PackResult pack(std::vector<RectPackInput> rects,
                           PackerOptions options = PackerOptions{});

private:
    RectPacker() = delete;
};

}  // namespace paimon::texture_studio
