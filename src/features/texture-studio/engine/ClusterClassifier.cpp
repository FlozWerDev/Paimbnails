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

    // Step 2: rule A — Outline. A cluster is "outline-ish" if it's very
    // dark (V < 0.25) AND not very saturated (S < 0.35). We allow more
    // than one to handle anti-aliased outlines that fragment into
    // multiple grey clusters.
    int outlineCount = 0;
    int darkestIdx = -1;
    for (int i = 0; i < n; ++i) {
        auto const& c = out.clusters[i].source;
        bool isOutline =
            (c.v < 0.25f && c.s < 0.35f) ||
            (c.v < 0.40f && c.s < 0.15f);
        if (isOutline) {
            out.clusters[i].role = ClusterRole::Outline;
            out.clusters[i].confidence = 0.85f;
            ++outlineCount;
            if (darkestIdx < 0 || c.v < out.clusters[darkestIdx].source.v) {
                darkestIdx = i;
            }
        }
    }
    // If we accidentally marked everything as outline (a sprite that's
    // entirely dark grey, like a shadow), keep only the darkest one so
    // there's at least one cluster left to be Color1.
    if (outlineCount == n && n > 1 && darkestIdx >= 0) {
        for (int i = 0; i < n; ++i) {
            if (i != darkestIdx) {
                out.clusters[i].role = ClusterRole::Unassigned;
                out.clusters[i].confidence = 0.0f;
            }
        }
    }

    // Step 3: rule B — Glow. Bright pixels at the silhouette edge. We
    // accept either:
    //   (a) high V (> 0.85) AND high border ratio (> 0.40) — typical for
    //       white/colored glows that rim the sprite, or
    //   (b) very high V (> 0.92) regardless of border, since some sprites
    //       have inset glow halos that don't reach the silhouette edge.
    int glowIdx = -1;
    for (int i = 0; i < n; ++i) {
        if (out.clusters[i].role != ClusterRole::Unassigned) continue;
        auto const& c = out.clusters[i].source;
        bool glowLike =
            (c.v > 0.85f && c.borderRatio > 0.40f) ||
            (c.v > 0.92f);
        if (glowLike) {
            // Prefer the cluster with the smallest pixelCount (glow is
            // usually a thin ring) AND highest V (brightest).
            if (glowIdx == -1) {
                glowIdx = i;
            } else {
                auto const& cur = out.clusters[glowIdx].source;
                // Score: prefer brighter & smaller. Higher score wins.
                float scoreNew = c.v - 0.5f * (c.pixelCount / static_cast<float>(totalAssignablePixels));
                float scoreCur = cur.v - 0.5f * (cur.pixelCount / static_cast<float>(totalAssignablePixels));
                if (scoreNew > scoreCur) glowIdx = i;
            }
        }
    }
    if (glowIdx >= 0) {
        out.clusters[glowIdx].role = ClusterRole::Glow;
        out.clusters[glowIdx].confidence = 0.75f;
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
    //   - There's no Outline AND no Glow (very unusual for GD sprites).
    bool hasC1 = false;
    bool hasOutline = false;
    bool hasGlow = false;
    for (auto const& c : out.clusters) {
        if (c.role == ClusterRole::Color1)  hasC1 = true;
        if (c.role == ClusterRole::Outline) hasOutline = true;
        if (c.role == ClusterRole::Glow)    hasGlow = true;
    }
    if (!hasC1) out.needsReview = true;
    if (n > 1 && leftover * 2 > n) out.needsReview = true;
    if (!hasOutline && !hasGlow) out.needsReview = true;

    return out;
}

}  // namespace paimon::texture_studio
