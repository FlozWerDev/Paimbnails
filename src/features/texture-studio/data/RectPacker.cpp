#include "RectPacker.hpp"

#include <algorithm>
#include <cstdint>

namespace paimon::texture_studio {

PackResult RectPacker::pack(std::vector<RectPackInput> rects, PackerOptions options) {
    PackResult result;
    if (rects.empty()) {
        return result;  // empty atlas
    }

    // Step 1: stable-sort by descending height. Same secondary keys (id) keep
    // input order so the produced placements are reproducible.
    std::sort(rects.begin(), rects.end(),
        [](RectPackInput const& a, RectPackInput const& b) {
            if (a.height != b.height) return a.height > b.height;
            return a.id < b.id;
        });

    // Step 2: shelf-pack. PackGen's algorithm:
    //
    //   for each rect (tallest first):
    //       try to fit it in any existing bin (= shelf row) by appending
    //       horizontally. A bin is "fittable" if (bin.width + rect.W + gap)
    //       does not exceed maxSheetWidth. The Y-coordinate of a new shelf
    //       row is "max(bin.y + bin.maxHeight + gap) for all existing bins".
    //
    //   The trick this algorithm gets right is that after a rect is added
    //   to a bin, its own height grows the bin's `maxHeight` only up to
    //   that rect's height, not beyond — preserving wasted-space invariant.
    //
    // We faithfully reproduce that so that Texture Loader packs we generate
    // place identically to PackGen's output for the same set of inputs.

    struct Bin {
        int x         = 0;   // unused but kept for parity with PackGen
        int y         = 0;
        int width     = 0;   // current right-edge of this shelf
        int maxHeight = 0;   // height of the tallest rect on this shelf
    };
    std::vector<Bin> bins;
    bins.reserve(8);

    int gap = std::max(0, options.gap);
    int maxW = std::max(1, options.maxWidth);

    auto frameWithGap = [gap](int v) { return v + gap; };

    for (auto const& r : rects) {
        if (r.width <= 0 || r.height <= 0) {
            // Defensive: zero-area rects skipped (shouldn't happen but
            // doesn't make sense to allocate space for them).
            continue;
        }
        int rWG = frameWithGap(r.width);
        int rHG = frameWithGap(r.height);

        // Try to fit into an existing shelf.
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

        // No fit — open a new shelf below all existing ones.
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

    // Step 3: compute output sheet dimensions.
    if (!bins.empty()) {
        int maxBinW = 0;
        int maxBinB = 0;
        for (auto const& bin : bins) {
            maxBinW = std::max(maxBinW, bin.width);
            maxBinB = std::max(maxBinB, bin.y + bin.maxHeight);
        }
        // PackGen subtracts the trailing gap. We do too so sheet sizes match.
        result.sheetWidth  = std::max(0, maxBinW - gap);
        result.sheetHeight = maxBinB;
    }

    return result;
}

}  // namespace paimon::texture_studio
