#include "ColorClustering.hpp"

#include "../../colorful-icons/services/IconColorMath.hpp"

#include <algorithm>
#include <cmath>

namespace paimon::texture_studio {

namespace {

// Sentinel "infinity" replacement — the project compiles with /fp:fast
// (and -ffast-math elsewhere), so std::numeric_limits<float>::infinity() is
// undefined behaviour. HSV distances are bounded by ~1.0, so 1e30 is
// effectively "as far as it gets" for nearest-neighbour searches.
constexpr float kFarAway = 1.0e30f;

// SplitMix64 — same constants as the rest of the project (see
// IconColorMath::splitMix64 in colorful-icons). We re-implement here to
// avoid pulling in unrelated headers, but the values match.
std::uint64_t splitMix64(std::uint64_t z) {
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// Pull a deterministic float in [0, 1) out of a SplitMix64 state.
float nextUnit(std::uint64_t& state) {
    state = splitMix64(state);
    // Top 24 bits as a 24-bit fraction. Same recipe as IconColorMath.
    return static_cast<float>(state >> 40) / 16777216.0f;
}

// Convert one RGBA pixel to HSV. We reuse the math from
// `paimon::icons::math::toHSV` rather than duplicate the conversion.
paimon::icons::math::HSV pixelToHSV(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return paimon::icons::math::toHSV(cocos2d::ccColor3B{r, g, b});
}

}  // anonymous namespace

float ColorClustering::hsvDistance(float h1, float s1, float v1,
                                   float h2, float s2, float v2,
                                   float wh, float ws, float wv) {
    // Circular hue distance, normalised to [0, 1].
    float dh_raw = std::fabs(h1 - h2);
    if (dh_raw > 180.0f) dh_raw = 360.0f - dh_raw;
    float dh = dh_raw / 180.0f;

    // For grey/near-grey points hue is meaningless. Damp the hue weight
    // proportionally to min(s1, s2) so two greys with random hues don't
    // get pushed apart artificially.
    float satFactor = std::min(s1, s2);
    dh *= satFactor;

    float ds = std::fabs(s1 - s2);
    float dv = std::fabs(v1 - v2);
    return wh * dh + ws * ds + wv * dv;
}

ClusterSet ColorClustering::compute(ImageBuffer const& sprite, ClusteringOptions options) {
    ClusterSet out;
    if (sprite.empty()) return out;

    int W = sprite.width();
    int H = sprite.height();

    // Step 1: collect the input points. We store one HSV triple per opaque
    // pixel. For 256x256 sprites that's 65k floats × 3 = 768KB worst case
    // — fine for transient memory.
    struct Point {
        float h, s, v;
    };
    std::vector<Point> points;
    points.reserve(static_cast<std::size_t>(W) * static_cast<std::size_t>(H));

    int totalOpaque = 0;
    int rejected = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            auto const* p = sprite.atRef(x, y);
            std::uint8_t a = p[3];
            if (a < options.alphaCutoff) {
                ++rejected;
                continue;
            }
            auto hsv = pixelToHSV(p[0], p[1], p[2]);
            points.push_back({hsv.h, hsv.s, hsv.v});
            ++totalOpaque;
        }
    }
    out.totalPixels = totalOpaque;
    out.rejected    = rejected;

    if (points.empty()) return out;

    // Step 2: pick k initial centroids by k-means++ (deterministic via
    // splitmix64 seeded by the image's pixel count + first/last opaque
    // pixel). Pure random init is fine for k=5 and 65k points but k-means++
    // gives noticeably better separation in 1-2 iterations.
    int k = std::min(options.k, static_cast<int>(points.size()));
    if (k <= 0) return out;

    std::uint64_t seed = static_cast<std::uint64_t>(W) * 73856093u
                       ^ static_cast<std::uint64_t>(H) * 19349663u
                       ^ static_cast<std::uint64_t>(points.size()) * 83492791u;

    std::vector<Point> centroids;
    centroids.reserve(k);

    // First centroid: pick a random point.
    {
        std::size_t idx = static_cast<std::size_t>(nextUnit(seed) * points.size());
        if (idx >= points.size()) idx = points.size() - 1;
        centroids.push_back(points[idx]);
    }

    // Subsequent centroids: weighted by distance² to the nearest existing
    // centroid (k-means++).
    std::vector<float> dist2(points.size(), 0.0f);
    for (int c = 1; c < k; ++c) {
        float total = 0.0f;
        for (std::size_t i = 0; i < points.size(); ++i) {
            float minD = kFarAway;
            for (auto const& cen : centroids) {
                float d = hsvDistance(points[i].h, points[i].s, points[i].v,
                                      cen.h, cen.s, cen.v,
                                      options.weightH, options.weightS, options.weightV);
                if (d < minD) minD = d;
            }
            float d2 = minD * minD;
            dist2[i] = d2;
            total += d2;
        }
        if (total <= 0.0f) {
            // All points coincide with existing centroids — duplicates are
            // fine, the loop below will collapse empty clusters.
            std::size_t idx = static_cast<std::size_t>(nextUnit(seed) * points.size());
            if (idx >= points.size()) idx = points.size() - 1;
            centroids.push_back(points[idx]);
            continue;
        }
        // Cumulative roulette pick.
        float r = nextUnit(seed) * total;
        float acc = 0.0f;
        std::size_t pickIdx = points.size() - 1;
        for (std::size_t i = 0; i < points.size(); ++i) {
            acc += dist2[i];
            if (acc >= r) { pickIdx = i; break; }
        }
        centroids.push_back(points[pickIdx]);
    }

    // Step 3: Lloyd's iteration. We track per-cluster sums so the update
    // step is a single pass over points.
    std::vector<int> assignment(points.size(), 0);
    std::vector<float> sumH(k, 0.0f), sumS(k, 0.0f), sumV(k, 0.0f);
    std::vector<int>   counts(k, 0);

    // Pre-compute cos/sin of hue for circular averaging (avoids trig per iter)
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    std::vector<float> cosH(points.size()), sinH(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        float radH = points[i].h * kDegToRad;
        cosH[i] = std::cos(radH);
        sinH[i] = std::sin(radH);
    }

    for (int iter = 0; iter < options.maxIterations; ++iter) {
        // Assign each point to the nearest centroid.
        for (std::size_t i = 0; i < points.size(); ++i) {
            float bestD = kFarAway;
            int   bestC = 0;
            for (int c = 0; c < k; ++c) {
                float d = hsvDistance(points[i].h, points[i].s, points[i].v,
                                      centroids[c].h, centroids[c].s, centroids[c].v,
                                      options.weightH, options.weightS, options.weightV);
                if (d < bestD) { bestD = d; bestC = c; }
            }
            assignment[i] = bestC;
        }

        // Recompute centroids. Hue is averaged on the unit circle to avoid
        // the 359→0 wrap-around bias.
        std::fill(sumH.begin(), sumH.end(), 0.0f);
        std::fill(sumS.begin(), sumS.end(), 0.0f);
        std::fill(sumV.begin(), sumV.end(), 0.0f);
        std::fill(counts.begin(), counts.end(), 0);

        // For circular hue averaging: sum pre-computed sin/cos.
        std::vector<float> sumHueX(k, 0.0f), sumHueY(k, 0.0f);

        for (std::size_t i = 0; i < points.size(); ++i) {
            int c = assignment[i];
            counts[c]++;
            sumHueX[c] += cosH[i];
            sumHueY[c] += sinH[i];
            sumS[c] += points[i].s;
            sumV[c] += points[i].v;
        }

        float maxDrift = 0.0f;
        for (int c = 0; c < k; ++c) {
            if (counts[c] == 0) {
                // Empty cluster — re-seed it from the point currently
                // furthest from any centroid. This keeps k stable even
                // in pathological inputs (all-grey sprites, etc.).
                std::size_t worstIdx = 0;
                float worstD = -1.0f;
                for (std::size_t i = 0; i < points.size(); ++i) {
                    float minD = kFarAway;
                    for (auto const& cen : centroids) {
                        float d = hsvDistance(points[i].h, points[i].s, points[i].v,
                                              cen.h, cen.s, cen.v,
                                              options.weightH, options.weightS, options.weightV);
                        if (d < minD) minD = d;
                    }
                    if (minD > worstD) { worstD = minD; worstIdx = i; }
                }
                centroids[c] = points[worstIdx];
                continue;
            }
            float invN = 1.0f / static_cast<float>(counts[c]);
            float meanS = sumS[c] * invN;
            float meanV = sumV[c] * invN;
            float meanHueX = sumHueX[c] * invN;
            float meanHueY = sumHueY[c] * invN;
            float meanH = std::atan2(meanHueY, meanHueX) * (180.0f / 3.14159265358979323846f);
            if (meanH < 0.0f) meanH += 360.0f;

            float drift = std::fabs(meanH - centroids[c].h);
            if (drift > 180.0f) drift = 360.0f - drift;
            drift = std::max({drift, std::fabs(meanS - centroids[c].s) * 360.0f,
                                     std::fabs(meanV - centroids[c].v) * 360.0f});
            maxDrift = std::max(maxDrift, drift);

            centroids[c] = {meanH, meanS, meanV};
        }

        if (maxDrift < options.epsilon) break;
    }

    // Step 4: emit clusters with their cached RGB.
    out.clusters.reserve(k);
    for (int c = 0; c < k; ++c) {
        if (counts[c] == 0) continue;  // shouldn't happen after re-seeding
        ColorCluster cl;
        cl.h = centroids[c].h;
        cl.s = centroids[c].s;
        cl.v = centroids[c].v;
        auto rgb = paimon::icons::math::fromHSV({cl.h, cl.s, cl.v});
        cl.r = rgb.r;
        cl.g = rgb.g;
        cl.b = rgb.b;
        cl.pixelCount = counts[c];
        cl.borderRatio = 0.0f;  // populated by ClusterClassifier later
        out.clusters.push_back(cl);
    }

    // Optional: sort by descending pixel count so callers can iterate "most
    // important first". The classifier reorders by role so this is purely
    // for callers that don't care about roles (debug logs, previews).
    std::sort(out.clusters.begin(), out.clusters.end(),
        [](ColorCluster const& a, ColorCluster const& b) {
            return a.pixelCount > b.pixelCount;
        });

    return out;
}

}  // namespace paimon::texture_studio
