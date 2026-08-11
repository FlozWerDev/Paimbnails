#include "MaskBuilder.hpp"

#include "../../colorful-icons/services/IconColorMath.hpp"

#include <algorithm>
#include <cmath>

namespace paimon::texture_studio {

namespace {

constexpr float kFarAway = 1.0e30f;

MaskBuffer makeMask(int W, int H) {
    MaskBuffer m;
    m.width  = W;
    m.height = H;
    m.data.assign(static_cast<std::size_t>(W) * static_cast<std::size_t>(H), 0);
    return m;
}

// Find the nearest two clusters for a HSV point.
void findNearestTwo(float h, float s, float v,
                    ClassifiedCluster const* clusters, int n,
                    int& idx0, float& d0,
                    int& idx1, float& d1) {
    idx0 = -1; d0 = kFarAway;
    idx1 = -1; d1 = kFarAway;
    for (int i = 0; i < n; ++i) {
        auto const& c = clusters[i].source;
        float d = ColorClustering::hsvDistance(h, s, v, c.h, c.s, c.v);
        if (d < d0) {
            idx1 = idx0; d1 = d0;
            idx0 = i;    d0 = d;
        } else if (d < d1) {
            idx1 = i;    d1 = d;
        }
    }
}

MaskBuffer* maskPtrForRole(MaskSet& set, ClusterRole role) {
    switch (role) {
        case ClusterRole::Color1:  return &set.color1;
        case ClusterRole::Color2:  return &set.color2;
        case ClusterRole::Glow:    return &set.glow;
        case ClusterRole::Outline: return &set.outline;
        default:                   return nullptr;
    }
}

// One 3x3 grayscale morphology pass; useMax selects dilation vs erosion.
void morphPass(MaskBuffer& mask, std::vector<std::uint8_t>& scratch, bool useMax) {
    int W = mask.width;
    int H = mask.height;
    if (W <= 0 || H <= 0) return;
    scratch.resize(mask.data.size());

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int acc = useMax ? 0 : 255;
            for (int dy = -1; dy <= 1; ++dy) {
                int yy = y + dy;
                for (int dx = -1; dx <= 1; ++dx) {
                    int xx = x + dx;
                    int v = (xx < 0 || yy < 0 || xx >= W || yy >= H)
                        ? 0
                        : static_cast<int>(
                              mask.data[static_cast<std::size_t>(yy) * W + xx]);
                    acc = useMax ? std::max(acc, v) : std::min(acc, v);
                }
            }
            scratch[static_cast<std::size_t>(y) * W + x] =
                static_cast<std::uint8_t>(acc);
        }
    }
    mask.data.swap(scratch);
}

// Grayscale opening: erode, then dilate.
void morphOpen(MaskBuffer& mask, MaskMorphology const& morph,
               std::vector<std::uint8_t>& scratch) {
    if (mask.data.empty()) return;
    for (int i = 0; i < morph.erode; ++i)  morphPass(mask, scratch, /*useMax=*/false);
    for (int i = 0; i < morph.dilate; ++i) morphPass(mask, scratch, /*useMax=*/true);
}

constexpr int kRefineMasks = 5;

// Joint-bilateral smoothing follows RGB edges and restores each pixel's alpha
// after averaging, preserving the mask partition.
void edgeRefinePass(ImageBuffer const& sprite,
                    std::array<MaskBuffer*, kRefineMasks> const& masks,
                    int alphaCutoff,
                    std::array<std::vector<std::uint8_t>, kRefineMasks>& scratch) {
    int W = sprite.width();
    int H = sprite.height();
    if (W <= 0 || H <= 0) return;

// Weights use exp(-d² / (2·32²)), quantized to 256 steps.
    static const std::array<float, 256> kSimilarity = [] {
        std::array<float, 256> lut{};
        for (int i = 0; i < 256; ++i) {
            float d2 = static_cast<float>(i) * 256.0f;
            lut[i] = std::exp(-d2 / (2.0f * 32.0f * 32.0f));
        }
        return lut;
    }();

    std::size_t total = static_cast<std::size_t>(W) * static_cast<std::size_t>(H);
    for (int m = 0; m < kRefineMasks; ++m) {
        scratch[m].assign(total, 0);
    }

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            std::size_t idx = static_cast<std::size_t>(y) * W + x;
            auto const* p = sprite.atRef(x, y);
            int a = static_cast<int>(p[3]);
            if (a < alphaCutoff) continue;

            float wSum = 0.0f;
            float acc[kRefineMasks] = {};
            for (int dy = -1; dy <= 1; ++dy) {
                int yy = y + dy;
                if (yy < 0 || yy >= H) continue;
                for (int dx = -1; dx <= 1; ++dx) {
                    int xx = x + dx;
                    if (xx < 0 || xx >= W) continue;
                    auto const* q = sprite.atRef(xx, yy);
                    if (static_cast<int>(q[3]) < alphaCutoff) continue;

                    int dr = static_cast<int>(p[0]) - q[0];
                    int dg = static_cast<int>(p[1]) - q[1];
                    int db = static_cast<int>(p[2]) - q[2];
                    int d2 = dr * dr + dg * dg + db * db;
                    float w = kSimilarity[std::min(d2 >> 8, 255)];

// Normalize the neighbour to its alpha before applying its fractional split.
                    float qa = static_cast<float>(q[3]);
                    if (qa <= 0.0f) continue;
                    std::size_t nIdx = static_cast<std::size_t>(yy) * W + xx;
                    for (int m = 0; m < kRefineMasks; ++m) {
                        acc[m] += w * static_cast<float>(masks[m]->data[nIdx]) / qa;
                    }
                    wSum += w;
                }
            }
            if (wSum <= 0.0f) continue;

// Restore this pixel's alpha after smoothing.
            float fracSum = 0.0f;
            for (int m = 0; m < kRefineMasks; ++m) fracSum += acc[m];
            if (fracSum <= 1e-6f) continue;
            float rescale = static_cast<float>(a) / fracSum;
            for (int m = 0; m < kRefineMasks; ++m) {
                int v = static_cast<int>(std::lround(acc[m] * rescale));
                scratch[m][idx] = static_cast<std::uint8_t>(std::clamp(v, 0, 255));
            }
        }
    }

    for (int m = 0; m < kRefineMasks; ++m) {
        masks[m]->data.swap(scratch[m]);
    }
}

// Move enclosed bright components from the glow mask to detail; only the outer
// ring should receive the glow color.
void splitInteriorGlow(ImageBuffer const& sprite, MaskSet& masks, int alphaCutoff) {
    int W = sprite.width();
    int H = sprite.height();
    if (W <= 0 || H <= 0 || masks.glow.data.empty()) return;

    std::size_t total = static_cast<std::size_t>(W) * static_cast<std::size_t>(H);
    std::vector<std::uint8_t> visited(total, 0);
    std::vector<int> stack;
    std::vector<int> component;

    auto inGlow = [&](int x, int y) {
        return masks.glow.data[static_cast<std::size_t>(y) * W + x] > 0;
    };
    auto isOutside = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= W || y >= H) return true;
        return static_cast<int>(sprite.atRef(x, y)[3]) < alphaCutoff;
    };

    for (int sy = 0; sy < H; ++sy) {
        for (int sx = 0; sx < W; ++sx) {
            std::size_t seed = static_cast<std::size_t>(sy) * W + sx;
            if (visited[seed] || !inGlow(sx, sy)) continue;

            stack.clear();
            component.clear();
            bool touchesOutside = false;

            visited[seed] = 1;
            stack.push_back(static_cast<int>(seed));
            while (!stack.empty()) {
                int idx = stack.back();
                stack.pop_back();
                component.push_back(idx);
                int x = idx % W;
                int y = idx / W;

                constexpr int dx[4] = {1, -1, 0, 0};
                constexpr int dy[4] = {0, 0, 1, -1};
                for (int d = 0; d < 4; ++d) {
                    int nx = x + dx[d];
                    int ny = y + dy[d];
                    if (isOutside(nx, ny)) {
                        touchesOutside = true;
                        continue;
                    }
                    std::size_t nIdx = static_cast<std::size_t>(ny) * W + nx;
                    if (!visited[nIdx] && inGlow(nx, ny)) {
                        visited[nIdx] = 1;
                        stack.push_back(static_cast<int>(nIdx));
                    }
                }
            }

            if (!touchesOutside) {
                for (int idx : component) {
                    std::size_t i = static_cast<std::size_t>(idx);
                    int merged = static_cast<int>(masks.detail.data[i]) +
                                 static_cast<int>(masks.glow.data[i]);
                    masks.detail.data[i] =
                        static_cast<std::uint8_t>(std::clamp(merged, 0, 255));
                    masks.glow.data[i] = 0;
                }
            }
        }
    }
}

}

MaskBuffer&       MaskSet::get(ClusterRole r)       {
    switch (r) {
        case ClusterRole::Color1:  return color1;
        case ClusterRole::Color2:  return color2;
        case ClusterRole::Glow:    return glow;
        case ClusterRole::Outline: return outline;
        default:                   return color1;
    }
}
MaskBuffer const& MaskSet::get(ClusterRole r) const {
    switch (r) {
        case ClusterRole::Color1:  return color1;
        case ClusterRole::Color2:  return color2;
        case ClusterRole::Glow:    return glow;
        case ClusterRole::Outline: return outline;
        default:                   return color1;
    }
}

MaskSet MaskBuilder::build(ImageBuffer const& sprite,
                           ClassifiedSet const& classified,
                           MaskBuilderOptions options) {
    MaskSet out;
    if (sprite.empty() || classified.clusters.empty()) {
        return out;
    }

    int W = sprite.width();
    int H = sprite.height();
    out.color1  = makeMask(W, H);
    out.color2  = makeMask(W, H);
    out.glow    = makeMask(W, H);
    out.outline = makeMask(W, H);
    out.detail  = makeMask(W, H);

    int n = static_cast<int>(classified.clusters.size());
    auto const* clusterPtr = classified.clusters.data();

    float softness = std::clamp(options.softness, 0.0f, 1.0f);
    int alphaCutoff = std::clamp(options.alphaCutoff, 0, 255);

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            auto const* p = sprite.atRef(x, y);
            int a = static_cast<int>(p[3]);
            if (a < alphaCutoff) continue;

            auto hsv = paimon::icons::math::toHSV(cocos2d::ccColor3B{p[0], p[1], p[2]});

            int   i0 = -1, i1 = -1;
            float d0 = 0.0f, d1 = 0.0f;
            findNearestTwo(hsv.h, hsv.s, hsv.v, clusterPtr, n, i0, d0, i1, d1);
            if (i0 < 0) continue;

            auto* m0 = maskPtrForRole(out, clusterPtr[i0].role);
            if (!m0) continue;

            if (softness <= 0.0f || i1 < 0) {
                std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(W) + x;
                m0->data[idx] = static_cast<std::uint8_t>(a);
                continue;
            }

// Give the second cluster weight only when the pixel lies between both centers;
// a pixel on its centroid stays pure.
            float ratio = (d1 > 1e-6f) ? std::clamp(d0 / d1, 0.0f, 1.0f) : 0.0f;
    float share1 = 0.5f * softness * ratio;
            float share0 = 1.0f - share1;

            int v0 = static_cast<int>(std::lround(share0 * static_cast<float>(a)));
            int v1 = a - v0;

            std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(W) + x;
            m0->data[idx] = static_cast<std::uint8_t>(std::clamp(v0, 0, 255));

            auto* m1 = maskPtrForRole(out, clusterPtr[i1].role);
            if (m1 && m1 != m0) {
                m1->data[idx] = static_cast<std::uint8_t>(std::clamp(v1, 0, 255));
            } else if (m1 == m0) {
// Both nearest clusters share a role; merge v1 into m0.
                int merged = static_cast<int>(m0->data[idx]) + v1;
                m0->data[idx] = static_cast<std::uint8_t>(std::clamp(merged, 0, 255));
            }
        }
    }

// Split before smoothing so ring detection sees crisp masks.
    if (options.separateInteriorGlow) {
        splitInteriorGlow(sprite, out, alphaCutoff);
    }

    if (options.morphology.enabled()) {
        std::vector<std::uint8_t> scratch;
        morphOpen(out.color1,  options.morphology, scratch);
        morphOpen(out.color2,  options.morphology, scratch);
        morphOpen(out.glow,    options.morphology, scratch);
        morphOpen(out.outline, options.morphology, scratch);
    }

    if (options.edgeRefine > 0) {
        std::array<MaskBuffer*, kRefineMasks> maskPtrs{
            &out.color1, &out.color2, &out.glow, &out.outline, &out.detail};
        std::array<std::vector<std::uint8_t>, kRefineMasks> scratch;
        int passes = std::clamp(options.edgeRefine, 0, 4);
        for (int i = 0; i < passes; ++i) {
            edgeRefinePass(sprite, maskPtrs, alphaCutoff, scratch);
        }
    }

    return out;
}

}
