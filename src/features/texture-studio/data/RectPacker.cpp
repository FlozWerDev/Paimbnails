#include "RectPacker.hpp"

#include "../packgen/MaxRectsPacker.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace paimon::texture_studio {

namespace {

// Opt-in MaxRects path: no rotation, shelf-compatible orientation, new arrangement only.
PackResult packBestFit(std::vector<RectPackInput> const& rects,
                       PackerOptions const& options) {
    using packgen::MaxRectsPacker;
    using packgen::PackRect;
    MaxRectsPacker::Options mopts;
    mopts.maxSize = std::max(1, options.maxWidth);
    mopts.padding = std::max(0, options.gap);
    mopts.allowRotate = false;
    MaxRectsPacker packer(mopts);

    std::vector<PackRect> in;
    in.reserve(rects.size());
    for (std::size_t i = 0; i < rects.size(); ++i) {
        if (rects[i].width <= 0 || rects[i].height <= 0) continue;
        PackRect r;
        r.w = rects[i].width;
        r.h = rects[i].height;
        r.id = static_cast<int>(i);
        in.push_back(r);
    }
    auto res = packer.pack(in);

    PackResult out;
    out.sheetWidth  = res.atlasW;
    out.sheetHeight = res.atlasH;
    out.placements.reserve(res.placements.size());
    for (auto const& p : res.placements) {
        Placement q;
        q.id = rects[static_cast<std::size_t>(p.id)].id;
        q.x = p.x;
        q.y = p.y;
        q.w = p.w;
        q.h = p.h;
        out.placements.push_back(std::move(q));
    }
    return out;
}

}  // namespace

PackResult RectPacker::pack(std::vector<RectPackInput> rects, PackerOptions options) {
    PackResult result;
    if (rects.empty()) {
        return result;
    }

    if (options.bestFit) {
        return packBestFit(rects, options);
    }

    // Sort indices, not string-heavy inputs: visit order (hence placements) unchanged.
    std::vector<std::size_t> order(rects.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(),
        [&rects](std::size_t a, std::size_t b) {
            if (rects[a].height != rects[b].height) return rects[a].height > rects[b].height;
            return rects[a].id < rects[b].id;
        });

    // Shelf layout matching PackGen.
    struct Bin {
        int x         = 0;
        int y         = 0;
        int width     = 0;
        int maxHeight = 0;
    };
    std::vector<Bin> bins;
    bins.reserve(8);

    int gap = std::max(0, options.gap);
    int maxW = std::max(1, options.maxWidth);

    auto frameWithGap = [gap](int v) { return v + gap; };

    for (std::size_t oi : order) {
        auto const& r = rects[oi];
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
        // Drop trailing gap to match sheet size.
        result.sheetWidth  = std::max(0, maxBinW - gap);
        result.sheetHeight = maxBinB;
    }

    return result;
}

}  // namespace paimon::texture_studio
