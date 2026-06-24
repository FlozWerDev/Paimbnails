#include "ClusterClassifier.hpp"

#include "../../colorful-icons/services/IconColorMath.hpp"

#include <algorithm>
#include <cmath>

namespace paimon::texture_studio {

namespace {

constexpr float kFarAway = 1.0e30f;

// Distance from a pixel to a cluster (using ColorClustering's weighting).
// Inlined here to avoid leaking a 1-line helper into the public API.
float pixelToClusterDist(float h, float s, float v, ColorCluster const& c) {
    return ColorClustering::hsvDistance(h, s, v, c.h, c.s, c.v);
}

// Find the index of the nearest cluster for a given (H, S, V).
int nearestCluster(float h, float s, float v,
                   ColorCluster const* clusters, int n) {
    int best = 0;
    float bestD = kFarAway;
    for (int i = 0; i < n; ++i) {
        float d = pixelToClusterDist(h, s, v, clusters[i]);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

}  // anonymous namespace

float ClusterClassifier::computeBorderRatio(ImageBuffer const& sprite,
                                            ColorCluster const& cluster,
                                            ColorCluster const* allClusters,
                                            int clusterCount,
                                            int targetIndex) {
    if (sprite.empty() || cluster.pixelCount == 0 || clusterCount == 0) {
        return 0.0f;
    }
    int W = sprite.width();
    int H = sprite.height();
    constexpr int kAlphaCutoff = 16;  // matches ColorClustering's default

    int countInCluster = 0;
    int countOnBorder  = 0;

    auto isTransparent = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= W || y >= H) return true;  // out of frame = transparent
        auto const* p = sprite.atRef(x, y);
        return p[3] < kAlphaCutoff;
    };

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            auto const* p = sprite.atRef(x, y);
            if (p[3] < kAlphaCutoff) continue;
            // Skip clustering work if we already know this pixel can't be
            // in the target cluster: just compute one distance to the
            // target and one to the rest's minimum. Simpler and faster
            // than full assignment when targetIndex is the only thing we
            // care about.
            auto hsv = paimon::icons::math::toHSV(cocos2d::ccColor3B{p[0], p[1], p[2]});
            int idx = nearestCluster(hsv.h, hsv.s, hsv.v, allClusters, clusterCount);
            if (idx != targetIndex) continue;

            ++countInCluster;
            // 4-connectivity neighbour check.
            if (isTransparent(x - 1, y) || isTransparent(x + 1, y) ||
                isTransparent(x, y - 1) || isTransparent(x, y + 1)) {
                ++countOnBorder;
            }
        }
    }

    if (countInCluster == 0) return 0.0f;
    return static_cast<float>(countOnBorder) / static_cast<float>(countInCluster);
}

ClassifiedSet ClusterClassifier::classify(ClusterSet const& set, ImageBuffer const& sprite) {
    ClassifiedSet out;
    if (set.clusters.empty()) {
        out.needsReview = true;
        return out;
    }

    // Step 1: copy clusters and compute border ratios.
    int n = static_cast<int>(set.clusters.size());
    out.clusters.reserve(n);
    for (int i = 0; i < n; ++i) {
        ClassifiedCluster c;
        c.source = set.clusters[i];
        c.source.borderRatio = computeBorderRatio(sprite, c.source,
            set.clusters.data(), n, i);
        c.role = ClusterRole::Unassigned;
        out.clusters.push_back(c);
    }

    // We don't have access to PackGen's pre-painted overlay files, so we
    // have to *guess* the role of each cluster. The previous version used
    // very strict thresholds (V<0.15 AND S<0.20 for outline, S>0.30 AND
    // borderRatio>0.55 for glow) which rejected most real GD sprites.
    //
    // The relaxed rules below match the empirical patterns of GD's UI:
    //
    //   Outline:  near-black or very dark grey, low saturation. May appear
    //             as multiple clusters (anti-aliased outline produces a
    //             ramp of dark greys); we mark *all* dark-low-sat clusters
    //             as Outline so they survive untouched.
    //
    //   Glow:     bright, high-value, often white-ish. Most GD glows are
    //             white (S near 0, V near 1), but some sprites use vivid
    //             saturated glows (lava, electricity). Detect by high V
    //             AND high border ratio (glow lives at the silhouette).
    //
    //   Color1:   the dominant non-outline, non-glow cluster. Picked by
    //             pixelCount, biased toward saturated colors.
    //
    //   Color2:   the second most distinctive cluster. We prefer one that
    //             is hue-distant from Color1; if none qualify, fall back
    //             to the next-largest cluster regardless of hue.

    // Pre-compute a few useful aggregates.
    int totalAssignablePixels = 0;
    for (auto const& c : out.clusters) totalAssignablePixels += c.source.pixelCount;
    if (totalAssignablePixels <= 0) {
        out.needsReview = true;
        return out;
    }

    // Weighted median value makes the thresholds follow the asset's
    // exposure instead of treating bright and dark sheets identically.
    std::vector<std::pair<float, int>> values;
    values.reserve(out.clusters.size());
    for (auto const& c : out.clusters) {
        values.emplace_back(c.source.v, c.source.pixelCount);
    }
    std::sort(values.begin(), values.end());
    int accumulated = 0;
    float medianV = values.back().first;
    for (auto const& [value, count] : values) {
        accumulated += count;
        if (accumulated * 2 >= totalAssignablePixels) {
            medianV = value;
            break;
        }
    }
    float outlineV = std::clamp(medianV * 0.42f, 0.12f, 0.30f);

    // Step 2: per-cluster role scores. Instead of a cascade of boolean
    // threshold rules, we score every cluster for Outline and Glow (Color1
    // and Color2 already use saliency / hue scores below). Each role then
    // assigns greedily under its constraint (Outline: many; Glow: one).
    // Resolving by evidence strength rather than rule order fixes the edge
    // cases the old cascade mis-handled: white inset glows, colored
    // outlines and near-monochrome buttons. Thresholds stay adaptive via
    // medianV / outlineV so bright and dark sheets behave alike.
    //
    //   outlineScore = 0.50*darkness + 0.35*(1-saturation) + 0.15*border
    //   glowScore    = 0.45*brightness + 0.25*border + 0.20*(1-sat)
    //                  + 0.10*(1-pixelShare)
    //
    // where darkness/brightness are the cluster's value mapped through the
    // adaptive outline/glow reference points.
    auto shareOf = [&](ColorCluster const& c) {
        return static_cast<float>(c.pixelCount) /
               static_cast<float>(totalAssignablePixels);
    };
    float glowFloor = std::max(0.78f, medianV * 1.05f);
    float darkRef   = std::max(outlineV * 1.5f, 1.0e-3f);
    float brightRef = std::max(1.0f - glowFloor, 1.0e-3f);

    std::vector<float> outlineScore(n, 0.0f);
    std::vector<float> glowScore(n, 0.0f);
    for (int i = 0; i < n; ++i) {
        auto const& c = out.clusters[i].source;
        float v      = std::clamp(c.v, 0.0f, 1.0f);
        float s      = std::clamp(c.s, 0.0f, 1.0f);
        float border = std::clamp(c.borderRatio, 0.0f, 1.0f);
        float sh     = shareOf(c);
        float darkness   = std::clamp((darkRef - v) / darkRef, 0.0f, 1.0f);
        float brightness = std::clamp((v - glowFloor) / brightRef, 0.0f, 1.0f);

        // Outline must touch the silhouette, otherwise a dark *interior*
        // accent (legitimate Color2) would be eaten.
        outlineScore[i] = (border > 0.02f)
            ? 0.50f * darkness + 0.35f * (1.0f - s) + 0.15f * border
            : 0.0f;
        // Glow must clear the adaptive brightness floor.
        glowScore[i] = (v > glowFloor)
            ? 0.45f * brightness + 0.25f * border + 0.20f * (1.0f - s)
              + 0.10f * (1.0f - sh)
            : 0.0f;
    }

    // Step 2a: Outline (multiple allowed). Mark every cluster clearing the
    // bar; if that consumed them all (an all-dark sprite, like a shadow),
    // keep only the darkest so something survives for Color1.
    constexpr float kOutlineBar = 0.45f;
    int outlineCount = 0;
    int darkestIdx = -1;
    for (int i = 0; i < n; ++i) {
        if (outlineScore[i] < kOutlineBar) continue;
        out.clusters[i].role = ClusterRole::Outline;
        out.clusters[i].confidence = std::clamp(outlineScore[i], 0.0f, 1.0f);
        ++outlineCount;
        if (darkestIdx < 0 ||
            out.clusters[i].source.v < out.clusters[darkestIdx].source.v) {
            darkestIdx = i;
        }
    }
    if (outlineCount == n && n > 1 && darkestIdx >= 0) {
        for (int i = 0; i < n; ++i) {
            if (i != darkestIdx) {
                out.clusters[i].role = ClusterRole::Unassigned;
                out.clusters[i].confidence = 0.0f;
            }
        }
    }

    // Step 3: Glow (at most one) — highest glow score above the bar among
    // the clusters not already taken by Outline.
    constexpr float kGlowBar = 0.42f;
    int glowIdx = -1;
    for (int i = 0; i < n; ++i) {
        if (out.clusters[i].role != ClusterRole::Unassigned) continue;
        if (glowScore[i] < kGlowBar) continue;
        if (glowIdx < 0 || glowScore[i] > glowScore[glowIdx]) glowIdx = i;
    }
    if (glowIdx >= 0) {
        out.clusters[glowIdx].role = ClusterRole::Glow;
        out.clusters[glowIdx].confidence = std::clamp(glowScore[glowIdx], 0.0f, 1.0f);
    }

    // Step 4: rule C — Color 1 (primary). The most "salient" remaining
    // cluster. Saliency = pixelCount weighted by saturation (so a small
    // bright-red cluster beats a large desaturated grey).
    int c1Idx = -1;
    float c1Score = -1.0f;
    for (int i = 0; i < n; ++i) {
        if (out.clusters[i].role != ClusterRole::Unassigned) continue;
        auto const& c = out.clusters[i].source;
        // Saliency score: pixelCount * (0.4 + 0.6 * saturation). The 0.4
        // floor prevents zero-saturation clusters from being completely
        // ignored (e.g. a flat-grey logo).
        float satFactor = 0.4f + 0.6f * std::clamp(c.s, 0.0f, 1.0f);
        float score = static_cast<float>(c.pixelCount) * satFactor;
        if (score > c1Score) {
            c1Score = score;
            c1Idx = i;
        }
    }
    if (c1Idx >= 0) {
        out.clusters[c1Idx].role = ClusterRole::Color1;
        out.clusters[c1Idx].confidence = 0.80f;
    }

    // Step 5: rule D — Color 2 (secondary). Among remaining clusters,
    // prefer one that is hue-distant from Color1 (so the user's color
    // changes are visually distinct). If no remaining cluster has a
    // distinct hue, fall back to the largest remaining cluster.
    int c2Idx = -1;
    float c2Score = -1.0f;
    if (c1Idx >= 0) {
        auto const& c1 = out.clusters[c1Idx].source;
        for (int i = 0; i < n; ++i) {
            if (out.clusters[i].role != ClusterRole::Unassigned) continue;
            auto const& c = out.clusters[i].source;
            // Hue distance on the circle, normalised to [0, 1].
            float dh = std::fabs(c.h - c1.h);
            if (dh > 180.0f) dh = 360.0f - dh;
            float hueWeight = dh / 180.0f;  // 0 = same hue, 1 = opposite
            // Damp hue weight when both clusters are near-grey.
            hueWeight *= std::min(c.s, c1.s);
            // Score: prefer high pixelCount AND distinct hue.
            float score = static_cast<float>(c.pixelCount) * (0.5f + 0.5f * hueWeight);
            if (score > c2Score) {
                c2Score = score;
                c2Idx = i;
            }
        }
    } else {
        // No Color1 — just pick the largest remaining cluster.
        for (int i = 0; i < n; ++i) {
            if (out.clusters[i].role != ClusterRole::Unassigned) continue;
            if (c2Idx == -1 ||
                out.clusters[i].source.pixelCount > out.clusters[c2Idx].source.pixelCount) {
                c2Idx = i;
            }
        }
    }
    if (c2Idx >= 0) {
        out.clusters[c2Idx].role = ClusterRole::Color2;
        out.clusters[c2Idx].confidence = 0.65f;
    }

    // Step 6: rule E — anything still Unassigned gets folded into the
    // closest existing role by hue+value distance. This is more accurate
    // than blindly folding everything into Color1 (the previous version),
    // which could turn a small bright-cyan accent into part of the green
    // primary.
    int leftover = 0;
    auto roleHueDist = [&](int idx, ClusterRole role) -> float {
        // Find the cluster currently holding `role`; return distance to it.
        for (int j = 0; j < n; ++j) {
            if (out.clusters[j].role == role) {
                auto const& a = out.clusters[idx].source;
                auto const& b = out.clusters[j].source;
                return ColorClustering::hsvDistance(a.h, a.s, a.v, b.h, b.s, b.v);
            }
        }
        return kFarAway;
    };
    for (int i = 0; i < n; ++i) {
        if (out.clusters[i].role != ClusterRole::Unassigned) continue;
        // Try each role; pick the closest. Outline is included only when
        // it's already populated (we never invent new outlines here).
        ClusterRole bestRole = ClusterRole::Color1;
        float bestDist = roleHueDist(i, ClusterRole::Color1);
        if (float d = roleHueDist(i, ClusterRole::Color2); d < bestDist) {
            bestDist = d; bestRole = ClusterRole::Color2;
        }
        if (float d = roleHueDist(i, ClusterRole::Glow); d < bestDist) {
            bestDist = d; bestRole = ClusterRole::Glow;
        }
        if (float d = roleHueDist(i, ClusterRole::Outline); d < bestDist) {
            bestDist = d; bestRole = ClusterRole::Outline;
        }
        out.clusters[i].role = bestRole;
        out.clusters[i].confidence = 0.40f;
        ++leftover;
    }

    // Step 7: needsReview heuristic. Mark sprite as "review me" when:
    //   - We failed to find any Color1 (no salient cluster).
    //   - More than half the clusters were leftover-folded.
    //   - The average assignment confidence is low. Flat icons legitimately
    //     have neither outline nor glow, so that alone is not an error.
    bool hasC1 = false;
    float confidenceSum = 0.f;
    for (auto const& c : out.clusters) {
        if (c.role == ClusterRole::Color1)  hasC1 = true;
        confidenceSum += c.confidence;
    }
    if (!hasC1) out.needsReview = true;
    if (n > 1 && leftover * 2 > n) out.needsReview = true;
    if (!out.clusters.empty() && confidenceSum / out.clusters.size() < 0.52f) {
        out.needsReview = true;
    }

    return out;
}

}  // namespace paimon::texture_studio
