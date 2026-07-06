#include "ColorClustering.hpp"

#include "../../colorful-icons/services/IconColorMath.hpp"

#include <algorithm>
#include <cmath>

namespace paimon::texture_studio {

namespace {

// Sentinel "infinity": the project compiles with fast-math, so
// std::numeric_limits<float>::infinity() is UB. HSV distances are bounded
// by ~1.0, so 1e30 is effectively "as far as it gets".
constexpr float kFarAway = 1.0e30f;
constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;

// SplitMix64 — same constants as IconColorMath::splitMix64, re-implemented
// to avoid pulling in unrelated headers.
std::uint64_t splitMix64(std::uint64_t z) {
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

float nextUnit(std::uint64_t& state) {
    state = splitMix64(state);
    return static_cast<float>(state >> 40) / 16777216.0f;
}

paimon::icons::math::HSV pixelToHSV(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return paimon::icons::math::toHSV(cocos2d::ccColor3B{r, g, b});
}

// One weighted sample. `w` in (0,1]: alpha-weighting keeps anti-aliased edge
// pixels from dragging centroids toward background-blended colors.
struct Point {
    float h, s, v;
    float w;
    float cosH, sinH;
};

struct Centroid {
    float h = 0.0f, s = 0.0f, v = 0.0f;
    float weight = 0.0f;  // accumulated sample weight (merge bookkeeping)
};

float pointDist(Point const& p, Centroid const& c, ClusteringOptions const& o) {
    return ColorClustering::hsvDistance(p.h, p.s, p.v, c.h, c.s, c.v,
                                        o.weightH, o.weightS, o.weightV);
}

// One full weighted k-means run (k-means++ seeding + Lloyd) over `pts`.
// Returns the weighted inertia; centroids land in `outCentroids`.
float runKMeans(std::vector<Point> const& pts, int k,
                ClusteringOptions const& options, std::uint64_t seed,
                std::vector<Centroid>& outCentroids) {
    outCentroids.clear();
    outCentroids.reserve(k);

    // --- k-means++ seeding, weighted by w * d² ---
    {
        // First centroid: weighted random pick.
        float totalW = 0.0f;
        for (auto const& p : pts) totalW += p.w;
        float r = nextUnit(seed) * totalW;
        std::size_t idx = pts.size() - 1;
        float acc = 0.0f;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            acc += pts[i].w;
            if (acc >= r) { idx = i; break; }
        }
        outCentroids.push_back({pts[idx].h, pts[idx].s, pts[idx].v, 0.0f});
    }

    std::vector<float> nearestD(pts.size(), kFarAway);
    for (int c = 1; c < k; ++c) {
        // Incremental: only the latest centroid can lower a point's distance.
        auto const& last = outCentroids.back();
        float total = 0.0f;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            float d = pointDist(pts[i], last, options);
            if (d < nearestD[i]) nearestD[i] = d;
            total += pts[i].w * nearestD[i] * nearestD[i];
        }
        if (total <= 0.0f) {
            std::size_t idx = static_cast<std::size_t>(nextUnit(seed) * pts.size());
            if (idx >= pts.size()) idx = pts.size() - 1;
            outCentroids.push_back({pts[idx].h, pts[idx].s, pts[idx].v, 0.0f});
            continue;
        }
        float r = nextUnit(seed) * total;
        float acc = 0.0f;
        std::size_t pickIdx = pts.size() - 1;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            acc += pts[i].w * nearestD[i] * nearestD[i];
            if (acc >= r) { pickIdx = i; break; }
        }
        outCentroids.push_back({pts[pickIdx].h, pts[pickIdx].s, pts[pickIdx].v, 0.0f});
    }

    // --- Lloyd iterations (weighted, circular hue means) ---
    std::vector<int> assignment(pts.size(), 0);
    std::vector<float> sumHueX(k), sumHueY(k), sumS(k), sumV(k), sumW(k);

    for (int iter = 0; iter < options.maxIterations; ++iter) {
        for (std::size_t i = 0; i < pts.size(); ++i) {
            float bestD = kFarAway;
            int   bestC = 0;
            for (int c = 0; c < k; ++c) {
                float d = pointDist(pts[i], outCentroids[c], options);
                if (d < bestD) { bestD = d; bestC = c; }
            }
            assignment[i] = bestC;
        }

        std::fill(sumHueX.begin(), sumHueX.end(), 0.0f);
        std::fill(sumHueY.begin(), sumHueY.end(), 0.0f);
        std::fill(sumS.begin(), sumS.end(), 0.0f);
        std::fill(sumV.begin(), sumV.end(), 0.0f);
        std::fill(sumW.begin(), sumW.end(), 0.0f);

        for (std::size_t i = 0; i < pts.size(); ++i) {
            int c = assignment[i];
            float w = pts[i].w;
            sumHueX[c] += w * pts[i].cosH;
            sumHueY[c] += w * pts[i].sinH;
            sumS[c]    += w * pts[i].s;
            sumV[c]    += w * pts[i].v;
            sumW[c]    += w;
        }

        float maxDrift = 0.0f;
        for (int c = 0; c < k; ++c) {
            if (sumW[c] <= 0.0f) {
                // Re-seed empty cluster from the point furthest from any
                // centroid, keeping k stable on pathological inputs.
                std::size_t worstIdx = 0;
                float worstD = -1.0f;
                for (std::size_t i = 0; i < pts.size(); ++i) {
                    float minD = kFarAway;
                    for (auto const& cen : outCentroids) {
                        float d = pointDist(pts[i], cen, options);
                        if (d < minD) minD = d;
                    }
                    if (minD > worstD) { worstD = minD; worstIdx = i; }
                }
                outCentroids[c] = {pts[worstIdx].h, pts[worstIdx].s,
                                   pts[worstIdx].v, 0.0f};
                maxDrift = 360.0f;  // force another iteration
                continue;
            }
            float invW = 1.0f / sumW[c];
            float meanS = sumS[c] * invW;
            float meanV = sumV[c] * invW;
            float meanH = std::atan2(sumHueY[c] * invW, sumHueX[c] * invW) * kRadToDeg;
            if (meanH < 0.0f) meanH += 360.0f;

            float drift = std::fabs(meanH - outCentroids[c].h);
            if (drift > 180.0f) drift = 360.0f - drift;
            drift = std::max({drift,
                              std::fabs(meanS - outCentroids[c].s) * 360.0f,
                              std::fabs(meanV - outCentroids[c].v) * 360.0f});
            maxDrift = std::max(maxDrift, drift);

            outCentroids[c] = {meanH, meanS, meanV, sumW[c]};
        }

        if (maxDrift < options.epsilon) break;
    }

    float inertia = 0.0f;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        inertia += pts[i].w * pointDist(pts[i], outCentroids[assignment[i]], options);
    }
    return inertia;
}

// Merge centroid pairs closer than mergeThreshold. k-means with a fixed k
// happily splits one visual color into two clusters; downstream that makes
// the classifier hand the same surface to two different roles.
void mergeCloseCentroids(std::vector<Centroid>& cents,
                         ClusteringOptions const& options) {
    if (options.mergeThreshold <= 0.0f) return;
    bool merged = true;
    while (merged && cents.size() > 1) {
        merged = false;
        int bi = -1, bj = -1;
        float bestD = kFarAway;
        for (int i = 0; i < static_cast<int>(cents.size()); ++i) {
            for (int j = i + 1; j < static_cast<int>(cents.size()); ++j) {
                float d = ColorClustering::hsvDistance(
                    cents[i].h, cents[i].s, cents[i].v,
                    cents[j].h, cents[j].s, cents[j].v,
                    options.weightH, options.weightS, options.weightV);
                if (d < bestD) { bestD = d; bi = i; bj = j; }
            }
        }
        if (bi < 0 || bestD >= options.mergeThreshold) break;

        auto& a = cents[bi];
        auto& b = cents[bj];
        float wa = std::max(a.weight, 1e-6f);
        float wb = std::max(b.weight, 1e-6f);
        float total = wa + wb;
        float hx = wa * std::cos(a.h * kDegToRad) + wb * std::cos(b.h * kDegToRad);
        float hy = wa * std::sin(a.h * kDegToRad) + wb * std::sin(b.h * kDegToRad);
        float h = std::atan2(hy, hx) * kRadToDeg;
        if (h < 0.0f) h += 360.0f;
        a.h = h;
        a.s = (wa * a.s + wb * b.s) / total;
        a.v = (wa * a.v + wb * b.v) / total;
        a.weight = total;
        cents.erase(cents.begin() + bj);
        merged = true;
    }
}

}  // anonymous namespace

float ColorClustering::hsvDistance(float h1, float s1, float v1,
                                   float h2, float s2, float v2,
                                   float wh, float ws, float wv) {
    float dh_raw = std::fabs(h1 - h2);
    if (dh_raw > 180.0f) dh_raw = 360.0f - dh_raw;
    float dh = dh_raw / 180.0f;

    // Hue is meaningless for near-grey points: damp it by min saturation so
    // two greys with random hues aren't pushed apart.
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
            Point pt;
            pt.h = hsv.h;
            pt.s = hsv.s;
            pt.v = hsv.v;
            pt.w = options.alphaWeighting
                ? static_cast<float>(a) / 255.0f
                : 1.0f;
            float radH = pt.h * kDegToRad;
            pt.cosH = std::cos(radH);
            pt.sinH = std::sin(radH);
            points.push_back(pt);
            ++totalOpaque;
        }
    }
    out.totalPixels = totalOpaque;
    out.rejected    = rejected;

    if (points.empty()) return out;

    // Deterministic stride subsample for the iteration phase: same centroids,
    // fraction of the cost on large sprites.
    std::vector<Point> sampled;
    std::vector<Point> const* iterPts = &points;
    if (options.maxSamples > 0 &&
        static_cast<int>(points.size()) > options.maxSamples) {
        std::size_t step = points.size() / static_cast<std::size_t>(options.maxSamples) + 1;
        sampled.reserve(points.size() / step + 1);
        for (std::size_t i = 0; i < points.size(); i += step) {
            sampled.push_back(points[i]);
        }
        iterPts = &sampled;
    }

    int k = std::min(options.k, static_cast<int>(iterPts->size()));
    if (k <= 0) return out;

    std::uint64_t baseSeed = static_cast<std::uint64_t>(W) * 73856093u
                           ^ static_cast<std::uint64_t>(H) * 19349663u
                           ^ static_cast<std::uint64_t>(points.size()) * 83492791u;

    // Best-of-N restarts: a bad k-means++ draw can wedge a centroid between
    // two real colors; the lowest-inertia run avoids it.
    std::vector<Centroid> best;
    float bestInertia = kFarAway;
    int restarts = std::max(1, options.restarts);
    for (int r = 0; r < restarts; ++r) {
        std::vector<Centroid> cents;
        std::uint64_t seed = splitMix64(baseSeed + static_cast<std::uint64_t>(r) * 0x9E3779B97F4A7C15ULL);
        float inertia = runKMeans(*iterPts, k, options, seed, cents);
        if (inertia < bestInertia) {
            bestInertia = inertia;
            best = std::move(cents);
        }
    }
    if (best.empty()) return out;

    mergeCloseCentroids(best, options);

    // Final pass over ALL pixels: exact per-cluster counts and one polished
    // weighted mean per centroid (the iteration phase may have subsampled).
    int n = static_cast<int>(best.size());
    std::vector<int>   counts(n, 0);
    std::vector<float> sumHueX(n, 0.0f), sumHueY(n, 0.0f);
    std::vector<float> sumS(n, 0.0f), sumV(n, 0.0f), sumW(n, 0.0f);
    for (auto const& p : points) {
        float bestD = kFarAway;
        int   bestC = 0;
        for (int c = 0; c < n; ++c) {
            float d = pointDist(p, best[c], options);
            if (d < bestD) { bestD = d; bestC = c; }
        }
        counts[bestC] += 1;
        sumHueX[bestC] += p.w * p.cosH;
        sumHueY[bestC] += p.w * p.sinH;
        sumS[bestC]    += p.w * p.s;
        sumV[bestC]    += p.w * p.v;
        sumW[bestC]    += p.w;
    }

    out.clusters.reserve(n);
    for (int c = 0; c < n; ++c) {
        if (counts[c] == 0) continue;
        ColorCluster cl;
        if (sumW[c] > 0.0f) {
            float invW = 1.0f / sumW[c];
            float h = std::atan2(sumHueY[c] * invW, sumHueX[c] * invW) * kRadToDeg;
            if (h < 0.0f) h += 360.0f;
            cl.h = h;
            cl.s = sumS[c] * invW;
            cl.v = sumV[c] * invW;
        } else {
            cl.h = best[c].h;
            cl.s = best[c].s;
            cl.v = best[c].v;
        }
        auto rgb = paimon::icons::math::fromHSV({cl.h, cl.s, cl.v});
        cl.r = rgb.r;
        cl.g = rgb.g;
        cl.b = rgb.b;
        cl.pixelCount = counts[c];
        cl.borderRatio = 0.0f;
        out.clusters.push_back(cl);
    }

    std::sort(out.clusters.begin(), out.clusters.end(),
        [](ColorCluster const& a, ColorCluster const& b) {
            return a.pixelCount > b.pixelCount;
        });

    return out;
}

}  // namespace paimon::texture_studio
