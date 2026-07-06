#include "RectPacker.hpp"

#include <algorithm>
#include <cstdint>

namespace paimon::texture_studio {

PackResult RectPacker::pack(std::vector<RectPackInput> rects, PackerOptions options) {
    PackResult result;
    if (rects.empty()) {
        return result;
    }

    std::sort(rects.begin(), rects.end(),
        [](RectPackInput const& a, RectPackInput const& b) {
            if (a.height != b.height) return a.height > b.height;
            return a.id < b.id;
        });

    // Faithfully reproduce PackGen's shelf algorithm so output layout matches byte-for-byte.
    struct Bin {
        int x         = 0;   // kept for parity with PackGen
        int y         = 0;
        int width     = 0;
        int maxHeight = 0;
    };
    std::vector<Bin> bins;
    bins.reserve(8);

    int gap = std::max(0, options.gap);
    int maxW = std::max(1, options.maxWidth);

    auto frameWithGap = [gap](int v) { return v + gap; };

    for (auto const& r : rects) {
        if (r.width <= 0 || r.height <= 0) {
            continue;
        }
        int rWG = frameWithGap(r.width);
        int rHG = frameWithGap(r.height);

        bool placed = false;
        for (auto& bin : bins) {
            if (bin.width + rWG <= maxW) {
                Placement p;
                p.id = r.id;
                p.x  = bin.width;
                p.y  = bin.y;
                p.w  = r.width;
                p.h  = r.height;
                result.placements.push_back(p);

                bin.width    += rWG;
                bin.maxHeight = std::max(bin.maxHeight, r.height);
                placed = true;
                break;
            }
        }
        if (placed) continue;

        int newY = 0;
        if (!bins.empty()) {
            int maxBottom = 0;
            for (auto const& bin : bins) {
                maxBottom = std::max(maxBottom, bin.y + bin.maxHeight + gap);
            }
            newY = maxBottom;
        }
        Bin nb;
        nb.x         = 0;
        nb.y         = newY;
        nb.width     = rWG;
        nb.maxHeight = r.height;
        bins.push_back(nb);

        Placement p;
        p.id = r.id;
        p.x  = 0;
        p.y  = newY;
        p.w  = r.width;
        p.h  = r.height;
        result.placements.push_back(p);
    }

    if (!bins.empty()) {
        int maxBinW = 0;
        int maxBinB = 0;
        for (auto const& bin : bins) {
            maxBinW = std::max(maxBinW, bin.width);
            maxBinB = std::max(maxBinB, bin.y + bin.maxHeight);
        }
        // Subtract the trailing gap so sheet sizes match PackGen.
        result.sheetWidth  = std::max(0, maxBinW - gap);
        result.sheetHeight = maxBinB;
    }

    return result;
}

}  // namespace paimon::texture_studio
