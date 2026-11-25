#include "LiveMaskAnalyzer.hpp"

#include "ClusterClassifier.hpp"
#include "ColorClustering.hpp"
#include "../../colorful-icons/services/IconColorMath.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <mutex>
#include <thread>
#include <tuple>

namespace paimon::texture_studio {

namespace {

constexpr float kFarAway = 1.0e30f;
constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;

// plane order used by the analyzer.
enum Plane : int { kColor1 = 0, kColor2, kGlow, kOutline, kDetail, kPlaneCount };

int planeForRole(ClusterRole role) {
    switch (role) {
        case ClusterRole::Color1:  return kColor1;
        case ClusterRole::Color2:  return kColor2;
        case ClusterRole::Glow:    return kGlow;
        case ClusterRole::Outline: return kOutline;
        case ClusterRole::Detail:  return kDetail;
        default:                   return -1;
    }
}

std::uint32_t hashRgb(std::uint32_t rgb) {
    rgb ^= rgb >> 15;
    rgb *= 0x2C1B3C6DU;
    rgb ^= rgb >> 12;
    rgb *= 0x297A2D39U;
    rgb ^= rgb >> 15;
    return rgb;
}

std::uint32_t nextPow2(std::uint32_t v) {
    std::uint32_t p = 64;
    while (p < v) p <<= 1;
    return p;
}

// body family of every cluster holding `role`, weighted by pixel mass.
// saturation weights the hue so a white highlight cannot steer it.
bool groupFamily(ClassifiedSet const& set, ClusterRole role, LiveTone& out) {
    double mass = 0.0, hueX = 0.0, hueY = 0.0, sat = 0.0, val = 0.0;
    for (auto const& cluster : set.clusters) {
        if (cluster.role != role) continue;
        double w = cluster.source.pixelCount;
        mass += w;
        double hw = w * cluster.source.s;
        hueX += hw * std::cos(cluster.source.h * kDegToRad);
        hueY += hw * std::sin(cluster.source.h * kDegToRad);
        sat += w * cluster.source.s;
        val += w * cluster.source.v;
    }
    if (mass <= 0.0) return false;
    float hue = static_cast<float>(std::atan2(hueY, hueX)) * kRadToDeg;
    if (hue < 0.0f) hue += 360.0f;
    out = liveToneFamily(hue, static_cast<float>(sat / mass), static_cast<float>(val / mass));
    return true;
}

// joint-bilateral weights: exp(-d2/(2*32^2)), quantized to 256 steps.
std::array<float, 256> const& similarityLut() {
    static const std::array<float, 256> lut = [] {
        std::array<float, 256> table{};
        for (int i = 0; i < 256; ++i) {
            float d2 = static_cast<float>(i) * 256.0f;
            table[i] = std::exp(-d2 / (2.0f * 32.0f * 32.0f));
        }
        return table;
    }();
    return lut;
}

bool overlaps(LiveFrameRect const& a, LiveFrameRect const& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

}  // namespace

LiveTone liveToneFamily(float hueDeg, float sat, float val) {
    if (val < 0.10f || sat < 0.15f) return LiveTone::Neutral;
    float h = std::fmod(hueDeg, 360.0f);
    if (h < 0.0f) h += 360.0f;
    if (h < 12.0f || h >= 345.0f) return LiveTone::Red;
    // vivid orange-red (delete crosses) reads as red; muted browns stay surface.
    if (h < 26.0f && sat >= 0.75f && val >= 0.80f) return LiveTone::Red;
    if (h < 62.0f) return LiveTone::Surface;
    if (h < 160.0f) return LiveTone::Primary;
    if (h < 200.0f) return LiveTone::Secondary;
    if (h < 255.0f) return LiveTone::Blue;
    return LiveTone::Pink;
}

int LiveMaskAnalyzer::findOrInsert(std::uint32_t rgb) {
    std::uint32_t slot = hashRgb(rgb) & m_tableMask;
    while (true) {
        std::int32_t index = m_table[slot];
        if (index < 0) {
            index = static_cast<std::int32_t>(m_colors.size());
            m_table[slot] = index;
            Color color;
            color.rgb = rgb;
            m_colors.push_back(color);
            return index;
        }
        if (m_colors[static_cast<std::size_t>(index)].rgb == rgb) return index;
        slot = (slot + 1) & m_tableMask;
    }
}

void LiveMaskAnalyzer::analyze(std::uint8_t const* atlas, int atlasW, int atlasH,
                               LiveFrameRect rect, LiveMaskOptions const& options,
                               std::uint16_t* out) {
    if (!atlas || !out || rect.w <= 0 || rect.h <= 0) return;
    if (rect.x < 0 || rect.y < 0 || rect.x + rect.w > atlasW || rect.y + rect.h > atlasH) return;

    int const W = rect.w;
    int const H = rect.h;
    std::size_t const total = static_cast<std::size_t>(W) * static_cast<std::size_t>(H);
    std::size_t const atlasStride = static_cast<std::size_t>(atlasW) * 4;
    int const cutoff = std::clamp(options.alphaCutoff, 1, 255);
    auto pixel = [&](int x, int y) {
        return atlas + static_cast<std::size_t>(rect.y + y) * atlasStride +
            static_cast<std::size_t>(rect.x + x) * 4;
    };
    auto outAt = [&](int x, int y) -> std::uint16_t& {
        return out[static_cast<std::size_t>(rect.y + y) * static_cast<std::size_t>(atlasW) +
            static_cast<std::size_t>(rect.x + x)];
    };

    // 1. distinct colors with their alpha-weighted mass.
    m_colors.clear();
    m_pixelColor.assign(total, -1);
    m_alpha.resize(total);
    std::uint32_t tableSize = nextPow2(static_cast<std::uint32_t>(std::min<std::size_t>(total, 1u << 16) * 2));
    m_table.assign(tableSize, -1);
    m_tableMask = tableSize - 1;
    std::size_t opaque = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            auto const* p = pixel(x, y);
            std::size_t i = static_cast<std::size_t>(y) * W + x;
            m_alpha[i] = p[3];
            if (p[3] < cutoff) continue;
            // the table holds at most `total` colors; grow it before it gets dense.
            if (m_colors.size() * 2 >= tableSize) {
                tableSize <<= 1;
                m_table.assign(tableSize, -1);
                m_tableMask = tableSize - 1;
                for (std::size_t c = 0; c < m_colors.size(); ++c) {
                    std::uint32_t slot = hashRgb(m_colors[c].rgb) & m_tableMask;
                    while (m_table[slot] >= 0) slot = (slot + 1) & m_tableMask;
                    m_table[slot] = static_cast<std::int32_t>(c);
                }
            }
            std::uint32_t rgb = (static_cast<std::uint32_t>(p[0]) << 16) |
                (static_cast<std::uint32_t>(p[1]) << 8) | p[2];
            int index = findOrInsert(rgb);
            auto& color = m_colors[static_cast<std::size_t>(index)];
            color.weight += static_cast<float>(p[3]) / 255.0f;
            ++color.count;
            m_pixelColor[i] = index;
            ++opaque;
        }
    }

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) outAt(x, y) = 0;
    }
    if (m_colors.empty()) return;

    // 2. k-means over distinct colors (identical weights to a full-pixel run).
    std::vector<WeightedColor> weighted;
    weighted.reserve(m_colors.size());
    for (auto& color : m_colors) {
        auto hsv = paimon::icons::math::toHSV(cocos2d::ccColor3B{
            static_cast<std::uint8_t>(color.rgb >> 16), static_cast<std::uint8_t>(color.rgb >> 8),
            static_cast<std::uint8_t>(color.rgb)});
        color.h = hsv.h;
        color.s = hsv.s;
        color.v = hsv.v;
        weighted.push_back({hsv.h, hsv.s, hsv.v, color.weight, color.count});
    }
    ClusteringOptions copts;
    copts.k = std::clamp(options.clusterPrecision, 2, 10);
    copts.alphaCutoff = cutoff;
    // distinct colors make restarts cheap; more of them land on the
    // lowest-inertia split more often, which keeps roles stable.
    copts.restarts = 4;
    std::uint64_t seed = static_cast<std::uint64_t>(W) * 73856093u
        ^ static_cast<std::uint64_t>(H) * 19349663u
        ^ static_cast<std::uint64_t>(opaque) * 83492791u;
    auto clusters = ColorClustering::computeWeighted(weighted, seed, copts);
    if (clusters.clusters.empty()) return;
    int const n = static_cast<int>(clusters.clusters.size());

    // 3. nearest two clusters per color: the per-pixel passes only read these.
    float const softness = std::clamp(options.softness, 0.0f, 1.0f);
    for (auto& color : m_colors) {
        int i0 = -1, i1 = -1;
        float d0 = kFarAway, d1 = kFarAway;
        for (int c = 0; c < n; ++c) {
            auto const& cl = clusters.clusters[static_cast<std::size_t>(c)];
            float d = ColorClustering::hsvDistance(color.h, color.s, color.v, cl.h, cl.s, cl.v);
            if (d < d0) {
                i1 = i0; d1 = d0;
                i0 = c;  d0 = d;
            } else if (d < d1) {
                i1 = c;  d1 = d;
            }
        }
        color.nearest = i0;
        color.second = i1;
        float ratio = (d1 > 1e-6f) ? std::clamp(d0 / d1, 0.0f, 1.0f) : 0.0f;
        color.secondShare = (softness > 0.0f && i1 >= 0) ? 0.5f * softness * ratio : 0.0f;
    }

    // 4. silhouette contact per cluster (outline/glow evidence for the classifier).
    std::vector<int> inCluster(static_cast<std::size_t>(n), 0);
    std::vector<int> onBorder(static_cast<std::size_t>(n), 0);
    auto transparent = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= W || y >= H) return true;
        return m_alpha[static_cast<std::size_t>(y) * W + x] < 16;
    };
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            std::size_t i = static_cast<std::size_t>(y) * W + x;
            if (m_alpha[i] < 16 || m_pixelColor[i] < 0) continue;
            int c = m_colors[static_cast<std::size_t>(m_pixelColor[i])].nearest;
            if (c < 0) continue;
            ++inCluster[static_cast<std::size_t>(c)];
            if (transparent(x - 1, y) || transparent(x + 1, y) ||
                transparent(x, y - 1) || transparent(x, y + 1)) {
                ++onBorder[static_cast<std::size_t>(c)];
            }
        }
    }
    std::vector<float> ratios(static_cast<std::size_t>(n), 0.0f);
    for (int c = 0; c < n; ++c) {
        auto k = static_cast<std::size_t>(c);
        if (clusters.clusters[k].pixelCount > 0 && inCluster[k] > 0) {
            ratios[k] = static_cast<float>(onBorder[k]) / static_cast<float>(inCluster[k]);
        }
    }
    auto classified = ClusterClassifier::classifyUiWithRatios(clusters, ratios);
    if (classified.clusters.size() != clusters.clusters.size()) return;

    LiveTone family1 = LiveTone::Primary;
    LiveTone family2 = LiveTone::Secondary;
    bool hasBody = groupFamily(classified, ClusterRole::Color1, family1);
    if (!groupFamily(classified, ClusterRole::Color2, family2)) family2 = family1;
    else if (hasBody && family2 == family1) {
        // a second body color in the same family keeps its two-tone look.
        family2 = family1 == LiveTone::Secondary ? LiveTone::Primary : LiveTone::Secondary;
    }

    std::vector<int> clusterPlane(static_cast<std::size_t>(n), -1);
    for (int c = 0; c < n; ++c) {
        clusterPlane[static_cast<std::size_t>(c)] =
            planeForRole(classified.clusters[static_cast<std::size_t>(c)].role);
    }

    // 5. role planes in alpha units, soft-split between the two nearest roles.
    for (auto& plane : m_planes) plane.assign(total, 0);
    for (std::size_t i = 0; i < total; ++i) {
        int a = m_alpha[i];
        if (a < cutoff || m_pixelColor[i] < 0) continue;
        auto const& color = m_colors[static_cast<std::size_t>(m_pixelColor[i])];
        if (color.nearest < 0) continue;
        int p0 = clusterPlane[static_cast<std::size_t>(color.nearest)];
        if (p0 < 0) continue;
        if (color.secondShare <= 0.0f) {
            m_planes[p0][i] = static_cast<std::uint8_t>(a);
            continue;
        }
        int v0 = static_cast<int>(std::lround((1.0f - color.secondShare) * static_cast<float>(a)));
        int v1 = a - v0;
        m_planes[p0][i] = static_cast<std::uint8_t>(std::clamp(v0, 0, 255));
        int p1 = clusterPlane[static_cast<std::size_t>(color.second)];
        if (p1 >= 0 && p1 != p0) {
            m_planes[p1][i] = static_cast<std::uint8_t>(std::clamp(v1, 0, 255));
        } else if (p1 == p0) {
            m_planes[p0][i] = static_cast<std::uint8_t>(std::clamp(v0 + v1, 0, 255));
        }
    }

    // 6. enclosed glow belongs to the glyph (detail); only the outer ring is glow.
    {
        auto& glow = m_planes[kGlow];
        auto& detail = m_planes[kDetail];
        m_visited.assign(total, 0);
        auto outside = [&](int x, int y) {
            if (x < 0 || y < 0 || x >= W || y >= H) return true;
            return m_alpha[static_cast<std::size_t>(y) * W + x] < cutoff;
        };
        for (std::size_t seed0 = 0; seed0 < total; ++seed0) {
            if (m_visited[seed0] || glow[seed0] == 0) continue;
            m_stack.clear();
            m_component.clear();
            bool touchesOutside = false;
            m_visited[seed0] = 1;
            m_stack.push_back(static_cast<int>(seed0));
            while (!m_stack.empty()) {
                int idx = m_stack.back();
                m_stack.pop_back();
                m_component.push_back(idx);
                int x = idx % W;
                int y = idx / W;
                constexpr int dx[4] = {1, -1, 0, 0};
                constexpr int dy[4] = {0, 0, 1, -1};
                for (int d = 0; d < 4; ++d) {
                    int nx = x + dx[d];
                    int ny = y + dy[d];
                    if (outside(nx, ny)) {
                        touchesOutside = true;
                        continue;
                    }
                    std::size_t ni = static_cast<std::size_t>(ny) * W + nx;
                    if (!m_visited[ni] && glow[ni] > 0) {
                        m_visited[ni] = 1;
                        m_stack.push_back(static_cast<int>(ni));
                    }
                }
            }
            if (touchesOutside) continue;
            for (int idx : m_component) {
                auto k = static_cast<std::size_t>(idx);
                detail[k] = static_cast<std::uint8_t>(std::min(255, detail[k] + glow[k]));
                glow[k] = 0;
            }
        }
    }

    // 7. edge-aware cleanup: follows color edges and keeps each pixel's coverage.
    auto const& lut = similarityLut();
    int passes = std::clamp(options.edgeCleanup, 0, 4);
    for (int pass = 0; pass < passes; ++pass) {
        for (auto& plane : m_scratch) plane.assign(total, 0);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                std::size_t idx = static_cast<std::size_t>(y) * W + x;
                int a = m_alpha[idx];
                if (a < cutoff) continue;
                auto const* p = pixel(x, y);
                float acc[kPlaneCount] = {};
                float wSum = 0.0f;
                for (int dy = -1; dy <= 1; ++dy) {
                    int yy = y + dy;
                    if (yy < 0 || yy >= H) continue;
                    for (int dx = -1; dx <= 1; ++dx) {
                        int xx = x + dx;
                        if (xx < 0 || xx >= W) continue;
                        std::size_t nIdx = static_cast<std::size_t>(yy) * W + xx;
                        int qa = m_alpha[nIdx];
                        if (qa < cutoff || qa <= 0) continue;
                        auto const* q = pixel(xx, yy);
                        int dr = static_cast<int>(p[0]) - q[0];
                        int dg = static_cast<int>(p[1]) - q[1];
                        int db = static_cast<int>(p[2]) - q[2];
                        float w = lut[static_cast<std::size_t>(std::min((dr * dr + dg * dg + db * db) >> 8, 255))];
                        float scale = w / static_cast<float>(qa);
                        for (int m = 0; m < kPlaneCount; ++m) {
                            acc[m] += scale * static_cast<float>(m_planes[m][nIdx]);
                        }
                        wSum += w;
                    }
                }
                if (wSum <= 0.0f) continue;
                float fracSum = 0.0f;
                for (float v : acc) fracSum += v;
                if (fracSum <= 1e-6f) continue;
                float rescale = static_cast<float>(a) / fracSum;
                for (int m = 0; m < kPlaneCount; ++m) {
                    int v = static_cast<int>(std::lround(acc[m] * rescale));
                    m_scratch[m][idx] = static_cast<std::uint8_t>(std::clamp(v, 0, 255));
                }
            }
        }
        for (int m = 0; m < kPlaneCount; ++m) m_planes[m].swap(m_scratch[m]);
    }

    // 8. pack fractions of the pixel's own coverage plus the body tone code.
    auto nibble = [](int value, int alpha) {
        return static_cast<std::uint16_t>(std::clamp((value * 15 + alpha / 2) / alpha, 0, 15));
    };
    auto code1 = static_cast<std::uint16_t>(family1);
    auto code2 = static_cast<std::uint16_t>(family2);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            std::size_t i = static_cast<std::size_t>(y) * W + x;
            int a = m_alpha[i];
            if (a < cutoff) continue;
            int c1 = m_planes[kColor1][i];
            int c2 = m_planes[kColor2][i];
            int glow = m_planes[kGlow][i];
            int detail = m_planes[kDetail][i];
            std::uint16_t body = nibble(c1 + c2, a);
            std::uint16_t g = nibble(detail, a);
            std::uint16_t b = nibble(glow, a);
            if ((body | g | b) == 0) continue;
            std::uint16_t code = c1 >= c2 ? code1 : code2;
            outAt(x, y) = static_cast<std::uint16_t>((body << 12) | (g << 8) | (b << 4) | code);
        }
    }
}

bool LiveMaskAnalyzer::analyzeAll(std::uint8_t const* atlas, int atlasW, int atlasH,
                                  std::vector<LiveFrameRect> rects,
                                  LiveMaskOptions const& options, std::uint16_t* out,
                                  unsigned threads, std::function<bool()> const& cancelled,
                                  std::atomic<int>* framesDone, std::atomic<int>* framesTotal) {
    std::erase_if(rects, [&](LiveFrameRect const& r) {
        return r.w <= 0 || r.h <= 0 || r.x < 0 || r.y < 0 ||
            r.x + r.w > atlasW || r.y + r.h > atlasH;
    });
    std::sort(rects.begin(), rects.end(), [](auto const& a, auto const& b) {
        return std::tie(a.x, a.y, a.w, a.h) < std::tie(b.x, b.y, b.w, b.h);
    });
    rects.erase(std::unique(rects.begin(), rects.end(), [](auto const& a, auto const& b) {
        return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
    }), rects.end());
    if (framesTotal) framesTotal->store(static_cast<int>(rects.size()));
    if (rects.empty()) return !(cancelled && cancelled());

    // frames write disjoint texels. Packers never overlap slots, but a
    // malformed plist must not turn into a data race.
    bool disjoint = true;
    for (std::size_t i = 0; i < rects.size() && disjoint; ++i) {
        for (std::size_t j = i + 1; j < rects.size(); ++j) {
            if (rects[j].x >= rects[i].x + rects[i].w) break;
            if (overlaps(rects[i], rects[j])) { disjoint = false; break; }
        }
    }
    // largest first keeps the workers balanced at the end of the batch.
    std::sort(rects.begin(), rects.end(), [](auto const& a, auto const& b) {
        return static_cast<long long>(a.w) * a.h > static_cast<long long>(b.w) * b.h;
    });

    unsigned workers = disjoint ? std::clamp(threads, 1u, static_cast<unsigned>(rects.size())) : 1u;
    std::atomic<std::size_t> next{0};
    std::atomic<bool> stop{false};
    std::atomic<bool> wasCancelled{false};
    std::mutex errorMutex;
    std::exception_ptr error;

    auto work = [&] {
        LiveMaskAnalyzer analyzer;
        try {
            while (!stop.load(std::memory_order_relaxed)) {
                std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
                if (index >= rects.size()) break;
                if (cancelled && cancelled()) {
                    wasCancelled = true;
                    stop = true;
                    break;
                }
                analyzer.analyze(atlas, atlasW, atlasH, rects[index], options, out);
                if (framesDone) framesDone->fetch_add(1, std::memory_order_relaxed);
            }
        } catch (...) {
            std::lock_guard lock(errorMutex);
            if (!error) error = std::current_exception();
            stop = true;
        }
    };

    std::vector<std::thread> pool;
    pool.reserve(workers - 1);
    for (unsigned i = 1; i < workers; ++i) {
        try {
            pool.emplace_back(work);
        } catch (...) {
            break;  // fewer threads is still correct.
        }
    }
    work();
    for (auto& thread : pool) thread.join();
    if (error) std::rethrow_exception(error);
    return !wasCancelled.load();
}

}  // namespace paimon::texture_studio
