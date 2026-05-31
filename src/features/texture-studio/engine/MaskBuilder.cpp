#include "MaskBuilder.hpp"

#include "../../colorful-icons/services/IconColorMath.hpp"

#include <algorithm>
#include <cmath>

namespace paimon::texture_studio {

namespace {

constexpr float kFarAway = 1.0e30f;

// Initialise an empty mask matching the sprite's dimensions.
MaskBuffer makeMask(int W, int H) {
    MaskBuffer m;
    m.width  = W;
    m.height = H;
    m.data.assign(static_cast<std::size_t>(W) * static_cast<std::size_t>(H), 0);
    return m;
}

// Find nearest cluster (and optionally second-nearest) for a HSV point.
// `idx0`/`d0` get the closest, `idx1`/`d1` the second-closest. We only
// populate idx1/d1 when softness > 0 to avoid wasted work.
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

// Pointer-by-role helper. Returns a pointer into `set` so callers can write
// without checking each role independently.
MaskBuffer* maskPtrForRole(MaskSet& set, ClusterRole role) {
    switch (role) {
        case ClusterRole::Color1:  return &set.color1;
        case ClusterRole::Color2:  return &set.color2;
        case ClusterRole::Glow:    return &set.glow;
        case ClusterRole::Outline: return &set.outline;
        default:                   return nullptr;
    }
}

}  // anonymous namespace

MaskBuffer&       MaskSet::get(ClusterRole r)       {
    switch (r) {
        case ClusterRole::Color1:  return color1;
        case ClusterRole::Color2:  return color2;
        case ClusterRole::Glow:    return glow;
        case ClusterRole::Outline: return outline;
        default:                   return color1;  // shouldn't happen
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
                // Hard assignment: full alpha to the closest cluster's role.
                std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(W) + x;
                m0->data[idx] = static_cast<std::uint8_t>(a);
                continue;
            }

            // Soft assignment: split between i0 and i1 by inverse distance.
            // Avoid divide-by-zero with epsilon.
            float w0 = 1.0f / (d0 + 1e-6f);
            float w1 = 1.0f / (d1 + 1e-6f);
            // Apply softness: scale w1 down toward zero as softness → 0.
            w1 *= softness;
            float total = w0 + w1;
            float share0 = w0 / total;
            float share1 = w1 / total;

            int v0 = static_cast<int>(std::lround(share0 * static_cast<float>(a)));
            int v1 = a - v0;

            std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(W) + x;
            m0->data[idx] = static_cast<std::uint8_t>(std::clamp(v0, 0, 255));

            auto* m1 = maskPtrForRole(out, clusterPtr[i1].role);
            if (m1 && m1 != m0) {
                m1->data[idx] = static_cast<std::uint8_t>(std::clamp(v1, 0, 255));
            } else if (m1 == m0) {
                // Both nearest clusters map to the same role: just add v1
                // back into m0 (avoiding overflow).
                int merged = static_cast<int>(m0->data[idx]) + v1;
                m0->data[idx] = static_cast<std::uint8_t>(std::clamp(merged, 0, 255));
            }
        }
    }

    return out;
}

}  // namespace paimon::texture_studio
