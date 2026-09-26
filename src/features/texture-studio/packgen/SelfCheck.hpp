#pragma once
// Headless runtime self-check (no assets); SelfTest and regressions assert per sub-check.

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include "ContentHash.hpp"
#include "FrameImage.hpp"
#include "MaxRectsPacker.hpp"
#include "PackCache.hpp"
#include "PackGraph.hpp"
#include "TintEngine.hpp"

namespace paimon::texture_studio::packgen {

struct SelfCheckResult {
    bool ok = false;
    std::string failedStep;
    std::string detail;
};

inline SelfCheckResult runSelfCheck() {
    SelfCheckResult r;

    // 1. Tint kernel: grey 200 at brightness 160 (factor 1.25) through pure red gives (255,0,0).
    {
        PrecomputedTint spec = PrecomputedTint::make(255, 0, 0, 160.0f, 1.0f, 0.0f);
        std::uint8_t oR, oG, oB;
        tintPixelFast(200, 200, 200, spec, oR, oG, oB);
        if (oR != 255 || oG != 0 || oB != 0) {
            r.failedStep = "tint-red";
            r.detail = "expected (255,0,0)";
            return r;
        }
        // Same numbers as the SelfTest overlay case: cross-validates the kernel with TintMath.
        PrecomputedTint scaled = PrecomputedTint::make(160, 80, 40, 160.0f, 1.0f, 0.0f);
        tintPixelFast(200, 200, 200, scaled, oR, oG, oB);
        if (oR != 200 || oG != 100 || oB != 50) {
            r.failedStep = "tint-scale";
            r.detail = "expected (200,100,50)";
            return r;
        }
    }

    // 2. Alpha LUT endpoints + midpoint value match `a/255.0f`.
    {
        AlphaLut lut = AlphaLut::make();
        if (lut.v[0] != 0.0f || lut.v[255] != 1.0f ||
            lut.v[128] != static_cast<float>(128) / 255.0f) {
            r.failedStep = "alpha-lut";
            return r;
        }
    }

    // 3. Packer: fixed set packs, no overlaps, contained, deterministic.
    {
        MaxRectsPacker packer;
        std::vector<PackRect> rects = {
            {64, 64, 0}, {32, 96, 1}, {96, 32, 2}, {16, 16, 3}, {48, 48, 4},
        };
        auto a = packer.pack(rects);
        auto b = MaxRectsPacker().pack(rects);
        if (!a.fits || !b.fits) {
            r.failedStep = "packer-fits";
            return r;
        }
        if (a.placements.size() != rects.size()) {
            r.failedStep = "packer-count";
            return r;
        }
        auto norm = [](PackPlacement const& p) {
            int w = p.rotated ? p.h : p.w;
            int h = p.rotated ? p.w : p.h;
            return std::tuple<int, int, int, int>(p.x, p.y, w, h);
        };
        for (std::size_t i = 0; i < a.placements.size(); ++i) {
            if (a.placements[i].x != b.placements[i].x ||
                a.placements[i].y != b.placements[i].y ||
                a.placements[i].rotated != b.placements[i].rotated) {
                r.failedStep = "packer-determinism";
                return r;
            }
            auto [x0, y0, w0, h0] = norm(a.placements[i]);
            if (x0 < 0 || y0 < 0 || x0 + w0 > a.atlasW || y0 + h0 > a.atlasH) {
                r.failedStep = "packer-containment";
                return r;
            }
            for (std::size_t j = i + 1; j < a.placements.size(); ++j) {
                auto [x1, y1, w1, h1] = norm(a.placements[j]);
                bool overlap = x0 < x1 + w1 && x1 < x0 + w0 && y0 < y1 + h1 && y1 < y0 + h0;
                if (overlap) {
                    r.failedStep = "packer-overlap";
                    return r;
                }
            }
        }
    }

    // 4. Cache roundtrip + graph pruning (second evaluate computes 0).
    {
        PackCache cache(1u << 20);
        PackGraph g;
        g.setCache(&cache);
        int runs = 0;
        auto leaf = g.addNode("leaf", [&] {
            ++runs;
            return PackCache::Bytes{1, 2, 3};
        });
        auto root = g.addNode("root",
                              [&] {
                                  auto const* v = g.result(leaf);
                                  PackCache::Bytes out = *v;
                                  out.push_back(4);
                                  return out;
                              },
                              {leaf});
        PackGraph::EvalStats s1, s2;
        if (!g.evaluate(&s1)) {
            r.failedStep = "graph-eval1";
            return r;
        }
        if (s1.computed != 2 || runs != 1) {
            r.failedStep = "graph-eval1-count";
            return r;
        }
        if (!g.evaluate(&s2)) {
            r.failedStep = "graph-eval2";
            return r;
        }
        if (s2.computed != 0) {
            r.failedStep = "graph-prune";
            return r;
        }
        auto const* res = g.result(root);
        if (!res || res->size() != 4 || (*res)[3] != 4) {
            r.failedStep = "graph-result";
            return r;
        }
    }

    // 5. FrameImage sanity: blit + subrect roundtrip.
    {
        FrameImage img(4, 4);
        img.clear({10, 20, 30, 255});
        FrameImage sub = img.subRect(1, 1, 2, 2);
        if (sub.width() != 2 || sub.height() != 2 || sub.at(0, 0).r != 10) {
            r.failedStep = "frame-subrect";
            return r;
        }
    }

    r.ok = true;
    return r;
}

}  // namespace paimon::texture_studio::packgen
