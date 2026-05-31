#pragma once
//
// RectPacker.hpp - Simple "shelf" packer used to assemble the output atlas
// after re-tinting. We deliberately mirror PackGen's algorithm (sort by
// descending height, place each rect on the first shelf that has room) so
// our output sheet layout is byte-identical when the same set of rects is
// given.
//
// We don't use maxrects or guillotine: shelf is ~30% worse on packing
// efficiency, but it's deterministic, fast, and trivial to debug. Texture
// Loader doesn't care about packing quality — only that the rects don't
// overlap and the sheet fits inside 4096x4096.
//
// All numbers are pixels.
//

#include <cstdint>
#include <string>
#include <vector>

namespace paimon::texture_studio {

// Input to the packer: rects to place. `id` is opaque (we use the original
// frame name as id) so callers can map placements back to their data.
struct RectPackInput {
    std::string id;
    int width  = 0;
    int height = 0;
};

// Where each rect ended up in the final sheet.
struct Placement {
    std::string id;
    int x = 0;
    int y = 0;
    int w = 0;  // copied from input for convenience
    int h = 0;
};

// Output: list of placements + computed sheet dimensions.
struct PackResult {
    std::vector<Placement> placements;
    int sheetWidth  = 0;
    int sheetHeight = 0;
};

struct PackerOptions {
    int gap      = 2;     // pixels of empty space between rects (PackGen default)
    int maxWidth = 4096;  // sheet width budget (cocos2d / GL hard limit)
};

class RectPacker final {
public:
    // Pack the given rects. Output is deterministic for the same input.
    static PackResult pack(std::vector<RectPackInput> rects,
                           PackerOptions options = PackerOptions{});

private:
    RectPacker() = delete;
};

}  // namespace paimon::texture_studio
