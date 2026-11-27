#include "StructureOptimizerCore.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace paimon::optimizer {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.f;
constexpr std::size_t kMaxHananGridCells = 16384;
constexpr std::size_t kMaxHistogramCells = 2500;
constexpr std::size_t kExactSearchNodeBudget = 4096;

struct Axes {
    float ux = 1.f;
    float uy = 0.f;
    float vx = 0.f;
    float vy = 1.f;
};

Axes localAxes(float rotationDeg) {
    float const rad = -rotationDeg * kDegToRad;
    float const c = std::cos(rad);
    float const s = std::sin(rad);
    return {c, s, -s, c};
}

float wrapDeg360(float deg) {
    float r = std::fmod(deg, 360.f);
    if (r < 0.f) r += 360.f;
    if (std::abs(r - 360.f) < 0.05f) r = 0.f;
    return r;
}

int quarterTurnOffset(float rotDeg, float baseRotDeg) {
    float const delta = wrapDeg360(rotDeg - baseRotDeg);
    return static_cast<int>(std::lround(delta / 90.f)) % 4;
}

bool isOrthogonalRotationDelta(float rotDeg, float baseRotDeg) {
    float const delta = wrapDeg360(rotDeg - baseRotDeg);
    float const nearest = std::round(delta / 90.f) * 90.f;
    return std::abs(delta - nearest) <= 0.1f;
}

struct LocalItem {
    std::size_t recordIdx = 0;
    std::vector<std::size_t> sources;
    float u = 0.f;
    float v = 0.f;
    float spanU = 30.f;
    float spanV = 30.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
    float baseWidth = 30.f;
    float baseHeight = 30.f;
    StretchCapability stretch = StretchCapability::None;
    bool isSolid = false;
    float maxScale = 4.f;
    float maxSpan = 120.f;
};

enum class Axis : std::uint8_t { U, V };

bool approxEqual(float a, float b, float tol) {
    return std::abs(a - b) <= tol;
}

float effectiveMaxScale(ObjectRecord const& rec, OptimizerOptions const& opts) {
    float const raw = rec.isSolid ? opts.maxCollisionScale : opts.maxDecoScale;
    float const cap = rec.isSolid ? 12.f : 16.f;
    return std::clamp(raw, 1.f, cap);
}

float effectiveMaxSpan(ObjectRecord const& rec, OptimizerOptions const& opts, float maxScale) {
    float const hardCap = std::clamp(opts.maxPixelSpan, 30.f, 960.f);
    float const solidCap = rec.isSolid ? std::min(hardCap, 360.f) : hardCap;
    float const baseMax = std::max({rec.baseWidth, rec.baseHeight, 1.f});
    return std::min(solidCap, maxScale * std::max(baseMax, 30.f));
}

bool canStretchAlong(LocalItem const& item, Axis axis) {
    switch (item.stretch) {
        case StretchCapability::None:
            return false;
        case StretchCapability::HorizontalOnly:
            return axis == Axis::U;
        case StretchCapability::VerticalOnly:
            return axis == Axis::V;
        case StretchCapability::Axis1D:
            // Do not stretch an Axis1D piece along both axes (which distorts borders).
            if (axis == Axis::U) return item.scaleY <= item.scaleX * 1.05f + 0.01f;
            return item.scaleX <= item.scaleY * 1.05f + 0.01f;
        case StretchCapability::Uniform2D:
            return true;
    }
    return false;
}

bool canMergePairInRun(LocalItem const& acc, float newSpan, Axis axis) {
    float const basePar = axis == Axis::U ? acc.baseWidth : acc.baseHeight;
    float const newScale = newSpan / std::max(basePar, 1.f);
    return newScale <= acc.maxScale + 1e-4f && newSpan <= acc.maxSpan + 0.5f;
}

LocalItem mergeTwoItems(LocalItem acc, LocalItem const& cand, Axis axis) {
    float const parA = axis == Axis::U ? acc.u : acc.v;
    float const spanA = axis == Axis::U ? acc.spanU : acc.spanV;
    float const parB = axis == Axis::U ? cand.u : cand.v;
    float const spanB = axis == Axis::U ? cand.spanU : cand.spanV;

    float const runMin = std::min(parA - spanA * 0.5f, parB - spanB * 0.5f);
    float const runMax = std::max(parA + spanA * 0.5f, parB + spanB * 0.5f);
    float const newSpan = runMax - runMin;
    float const basePar = axis == Axis::U ? acc.baseWidth : acc.baseHeight;
    float const newScale = newSpan / std::max(basePar, 1.f);

    acc.sources.insert(acc.sources.end(), cand.sources.begin(), cand.sources.end());
    if (axis == Axis::U) {
        acc.u = (runMin + runMax) * 0.5f;
        acc.spanU = newSpan;
        acc.scaleX = newScale;
    } else {
        acc.v = (runMin + runMax) * 0.5f;
        acc.spanV = newSpan;
        acc.scaleY = newScale;
    }
    return acc;
}

// Strict-weak-ordering band partitioning + 1D greedy run merge.
std::vector<LocalItem> mergePass1D(
    std::vector<LocalItem> items,
    Axis axis,
    OptimizerOptions const& opts
) {
    if (items.size() < 2) return items;

    std::vector<LocalItem> passthrough;
    std::vector<LocalItem> candidates;
    candidates.reserve(items.size());
    for (auto& item : items) {
        if (canStretchAlong(item, axis)) {
            candidates.push_back(std::move(item));
        } else {
            passthrough.push_back(std::move(item));
        }
    }
    if (candidates.size() < 2) {
        passthrough.insert(passthrough.end(),
            std::make_move_iterator(candidates.begin()),
            std::make_move_iterator(candidates.end()));
        return passthrough;
    }

    auto perpCenter = [axis](LocalItem const& it) { return axis == Axis::U ? it.v : it.u; };
    auto perpSpan = [axis](LocalItem const& it) { return axis == Axis::U ? it.spanV : it.spanU; };
    auto parCenter = [axis](LocalItem const& it) { return axis == Axis::U ? it.u : it.v; };
    auto parSpan = [axis](LocalItem const& it) { return axis == Axis::U ? it.spanU : it.spanV; };

    // Strict total order on perpSpan first, then cluster equal-span bands safely.
    std::sort(candidates.begin(), candidates.end(), [&](LocalItem const& a, LocalItem const& b) {
        float const sa = perpSpan(a);
        float const sb = perpSpan(b);
        if (sa != sb) return sa < sb;
        return perpCenter(a) < perpCenter(b);
    });

    std::vector<LocalItem> result = std::move(passthrough);
    std::size_t spanStart = 0;
    while (spanStart < candidates.size()) {
        float const spanAnchor = perpSpan(candidates[spanStart]);
        std::size_t spanEnd = spanStart + 1;
        while (spanEnd < candidates.size() &&
               approxEqual(perpSpan(candidates[spanEnd]), spanAnchor, opts.posTolerance)) {
            ++spanEnd;
        }

        // Strict total order on perpCenter within the span cluster.
        std::sort(candidates.begin() + static_cast<std::ptrdiff_t>(spanStart),
                  candidates.begin() + static_cast<std::ptrdiff_t>(spanEnd),
                  [&](LocalItem const& a, LocalItem const& b) {
                      float const ca = perpCenter(a);
                      float const cb = perpCenter(b);
                      if (ca != cb) return ca < cb;
                      return parCenter(a) < parCenter(b);
                  });

        std::size_t i = spanStart;
        while (i < spanEnd) {
            float const perpAnchor = perpCenter(candidates[i]);
            std::size_t bandEnd = i + 1;
            while (bandEnd < spanEnd &&
                   approxEqual(perpCenter(candidates[bandEnd]), perpAnchor, opts.posTolerance)) {
                ++bandEnd;
            }

            std::sort(candidates.begin() + static_cast<std::ptrdiff_t>(i),
                      candidates.begin() + static_cast<std::ptrdiff_t>(bandEnd),
                      [&](LocalItem const& a, LocalItem const& b) {
                          return parCenter(a) < parCenter(b);
                      });

            std::size_t cursor = i;
            while (cursor < bandEnd) {
                LocalItem acc = std::move(candidates[cursor]);
                float runMin = parCenter(acc) - parSpan(acc) * 0.5f;
                float runMax = parCenter(acc) + parSpan(acc) * 0.5f;

                std::size_t next = cursor + 1;
                while (next < bandEnd) {
                    LocalItem const& cand = candidates[next];
                    float const candSpan = parSpan(cand);
                    float const candMin = parCenter(cand) - candSpan * 0.5f;
                    float const candMax = parCenter(cand) + candSpan * 0.5f;

                    if (!approxEqual(candMin, runMax, opts.posTolerance)) break;

                    float const newSpan = candMax - runMin;
                    if (!canMergePairInRun(acc, newSpan, axis)) break;

                    runMax = candMax;
                    acc = mergeTwoItems(std::move(acc), cand, axis);
                    ++next;
                }
                result.push_back(std::move(acc));
                cursor = next;
            }
            i = bandEnd;
        }
        spanStart = spanEnd;
    }

    return result;
}

std::vector<LocalItem> mergeIterative1D(
    std::vector<LocalItem> items,
    Axis firstAxis,
    OptimizerOptions const& opts
) {
    Axis cur = firstAxis;
    for (int iter = 0; iter < 8; ++iter) {
        auto next = mergePass1D(items, cur, opts);
        cur = (cur == Axis::U) ? Axis::V : Axis::U;
        auto next2 = mergePass1D(next, cur, opts);
        if (next2.size() >= items.size()) {
            return next.size() < items.size() ? next : items;
        }
        items = std::move(next2);
        cur = (cur == Axis::U) ? Axis::V : Axis::U;
    }
    return items;
}

std::vector<LocalItem> mergeGreedyRect2D(
    std::vector<LocalItem> items,
    bool uFirst,
    OptimizerOptions const& opts
) {
    if (items.size() < 2) return items;

    // Strict total order on exact coordinates (avoid non-transitive approxEqual in sort).
    std::sort(items.begin(), items.end(), [uFirst](LocalItem const& a, LocalItem const& b) {
        if (uFirst) {
            if (a.v != b.v) return a.v < b.v;
            return a.u < b.u;
        }
        if (a.u != b.u) return a.u < b.u;
        return a.v < b.v;
    });

    std::vector<bool> used(items.size(), false);
    std::vector<LocalItem> result;
    result.reserve(items.size());

    auto findFlushAt = [&](float targetU, float targetV, float expSpanU, float expSpanV) -> int {
        for (std::size_t idx = 0; idx < items.size(); ++idx) {
            if (used[idx]) continue;
            if (approxEqual(items[idx].u, targetU, opts.posTolerance) &&
                approxEqual(items[idx].v, targetV, opts.posTolerance) &&
                approxEqual(items[idx].spanU, expSpanU, opts.posTolerance) &&
                approxEqual(items[idx].spanV, expSpanV, opts.posTolerance)) {
                return static_cast<int>(idx);
            }
        }
        return -1;
    };

    for (std::size_t seed = 0; seed < items.size(); ++seed) {
        if (used[seed]) continue;
        LocalItem const& first = items[seed];
        if (first.stretch != StretchCapability::Uniform2D) {
            used[seed] = true;
            result.push_back(first);
            continue;
        }

        float const cellU = first.spanU;
        float const cellV = first.spanV;

        if (uFirst) {
            std::vector<std::size_t> row0{seed};
            float runSpanU = cellU;
            while (true) {
                float const nextScaleX = (runSpanU + cellU) / std::max(first.baseWidth, 1.f);
                if (nextScaleX > first.maxScale + 1e-4f || runSpanU + cellU > first.maxSpan + 0.5f) {
                    break;
                }
                int const cand = findFlushAt(first.u + runSpanU, first.v, cellU, cellV);
                if (cand < 0) break;
                row0.push_back(static_cast<std::size_t>(cand));
                runSpanU += cellU;
            }

            std::vector<std::size_t> rectIndices = row0;
            float runSpanV = cellV;
            while (true) {
                float const nextScaleY = (runSpanV + cellV) / std::max(first.baseHeight, 1.f);
                if (nextScaleY > first.maxScale + 1e-4f || runSpanV + cellV > first.maxSpan + 0.5f) {
                    break;
                }
                std::vector<std::size_t> nextRow;
                nextRow.reserve(row0.size());
                bool ok = true;
                for (std::size_t col = 0; col < row0.size(); ++col) {
                    int const cand = findFlushAt(first.u + col * cellU, first.v + runSpanV, cellU, cellV);
                    if (cand < 0) {
                        ok = false;
                        break;
                    }
                    nextRow.push_back(static_cast<std::size_t>(cand));
                }
                if (!ok) break;
                rectIndices.insert(rectIndices.end(), nextRow.begin(), nextRow.end());
                runSpanV += cellV;
            }

            LocalItem merged = first;
            merged.sources.clear();
            for (std::size_t idx : rectIndices) {
                used[idx] = true;
                merged.sources.insert(merged.sources.end(),
                    items[idx].sources.begin(), items[idx].sources.end());
            }
            merged.u = first.u + (runSpanU - cellU) * 0.5f;
            merged.v = first.v + (runSpanV - cellV) * 0.5f;
            merged.spanU = runSpanU;
            merged.spanV = runSpanV;
            merged.scaleX = runSpanU / std::max(first.baseWidth, 1.f);
            merged.scaleY = runSpanV / std::max(first.baseHeight, 1.f);
            result.push_back(std::move(merged));
        } else {
            std::vector<std::size_t> col0{seed};
            float runSpanV = cellV;
            while (true) {
                float const nextScaleY = (runSpanV + cellV) / std::max(first.baseHeight, 1.f);
                if (nextScaleY > first.maxScale + 1e-4f || runSpanV + cellV > first.maxSpan + 0.5f) {
                    break;
                }
                int const cand = findFlushAt(first.u, first.v + runSpanV, cellU, cellV);
                if (cand < 0) break;
                col0.push_back(static_cast<std::size_t>(cand));
                runSpanV += cellV;
            }

            std::vector<std::size_t> rectIndices = col0;
            float runSpanU = cellU;
            while (true) {
                float const nextScaleX = (runSpanU + cellU) / std::max(first.baseWidth, 1.f);
                if (nextScaleX > first.maxScale + 1e-4f || runSpanU + cellU > first.maxSpan + 0.5f) {
                    break;
                }
                std::vector<std::size_t> nextCol;
                nextCol.reserve(col0.size());
                bool ok = true;
                for (std::size_t row = 0; row < col0.size(); ++row) {
                    int const cand = findFlushAt(first.u + runSpanU, first.v + row * cellV, cellU, cellV);
                    if (cand < 0) {
                        ok = false;
                        break;
                    }
                    nextCol.push_back(static_cast<std::size_t>(cand));
                }
                if (!ok) break;
                rectIndices.insert(rectIndices.end(), nextCol.begin(), nextCol.end());
                runSpanU += cellU;
            }

            LocalItem merged = first;
            merged.sources.clear();
            for (std::size_t idx : rectIndices) {
                used[idx] = true;
                merged.sources.insert(merged.sources.end(),
                    items[idx].sources.begin(), items[idx].sources.end());
            }
            merged.u = first.u + (runSpanU - cellU) * 0.5f;
            merged.v = first.v + (runSpanV - cellV) * 0.5f;
            merged.spanU = runSpanU;
            merged.spanV = runSpanV;
            merged.scaleX = runSpanU / std::max(first.baseWidth, 1.f);
            merged.scaleY = runSpanV / std::max(first.baseHeight, 1.f);
            result.push_back(std::move(merged));
        }
    }

    return result;
}

// Disjoint-Set Union for O(N log N) sweep-line component extraction.
struct DSU {
    std::vector<std::size_t> parent;
    std::vector<std::uint8_t> rank;
    explicit DSU(std::size_t n) : parent(n), rank(n, 0) {
        std::iota(parent.begin(), parent.end(), 0);
    }
    std::size_t find(std::size_t x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    }
    void unite(std::size_t a, std::size_t b) {
        a = find(a);
        b = find(b);
        if (a == b) return;
        if (rank[a] < rank[b]) std::swap(a, b);
        parent[b] = a;
        if (rank[a] == rank[b]) ++rank[a];
    }
};

struct Component {
    std::vector<LocalItem> items;
    bool hasOverlap = false;
};

std::vector<Component> splitConnectedComponents(
    std::vector<LocalItem> const& bucket,
    float tol
) {
    std::size_t const n = bucket.size();
    std::vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        float const ua = bucket[a].u - bucket[a].spanU * 0.5f;
        float const ub = bucket[b].u - bucket[b].spanU * 0.5f;
        if (ua != ub) return ua < ub;
        return bucket[a].v < bucket[b].v;
    });

    DSU dsu(n);
    std::vector<bool> overlapFlag(n, false);

    for (std::size_t i = 0; i < n; ++i) {
        std::size_t const idxA = order[i];
        auto const& a = bucket[idxA];
        float const uMinA = a.u - a.spanU * 0.5f;
        float const uMaxA = a.u + a.spanU * 0.5f;
        float const vMinA = a.v - a.spanV * 0.5f;
        float const vMaxA = a.v + a.spanV * 0.5f;

        for (std::size_t j = i + 1; j < n; ++j) {
            std::size_t const idxB = order[j];
            auto const& b = bucket[idxB];
            float const uMinB = b.u - b.spanU * 0.5f;
            if (uMinB > uMaxA + tol) break;

            float const uMaxB = b.u + b.spanU * 0.5f;
            float const vMinB = b.v - b.spanV * 0.5f;
            float const vMaxB = b.v + b.spanV * 0.5f;

            float const du = std::max(uMinA, uMinB) - std::min(uMaxA, uMaxB);
            float const dv = std::max(vMinA, vMinB) - std::min(vMaxA, vMaxB);

            // Adjacent if sharing an edge (one gap <= tol, other strictly overlapping > tol)
            // or strictly overlapping in both axes. Corner-only contact (du >= -tol && dv >= -tol) is excluded.
            bool const touching = (du <= tol && dv < -tol) || (dv <= tol && du < -tol);
            if (touching) {
                dsu.unite(idxA, idxB);
                if (du < -tol && dv < -tol) {
                    overlapFlag[idxA] = true;
                    overlapFlag[idxB] = true;
                }
            }
        }
    }

    std::map<std::size_t, std::size_t> rootToComp;
    std::vector<Component> components;
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t const root = dsu.find(i);
        auto [it, inserted] = rootToComp.emplace(root, components.size());
        if (inserted) {
            components.emplace_back();
        }
        auto& comp = components[it->second];
        comp.items.push_back(bucket[i]);
        if (overlapFlag[i]) comp.hasOverlap = true;
    }
    return components;
}

// --- Hanan-grid 2D Optimal Rectangle Partitioning for Uniform2D ---

struct GridRect {
    std::size_t c0 = 0;
    std::size_t c1 = 0;
    std::size_t r0 = 0;
    std::size_t r1 = 0;
};

struct HananGrid {
    std::vector<float> uCoords; // size W + 1
    std::vector<float> vCoords; // size H + 1
    std::size_t W = 0;
    std::size_t H = 0;
    std::vector<std::uint8_t> occ; // H * W
    float baseWidth = 30.f;
    float baseHeight = 30.f;
    float maxScale = 8.f;
    float maxSpan = 240.f;
    std::size_t occupiedCount = 0;

    bool validDims(std::size_t c0, std::size_t c1, std::size_t r0, std::size_t r1) const {
        float const spanU = uCoords[c1] - uCoords[c0];
        float const spanV = vCoords[r1] - vCoords[r0];
        float const sx = spanU / std::max(baseWidth, 1.f);
        float const sy = spanV / std::max(baseHeight, 1.f);
        return sx <= maxScale + 1e-4f && spanU <= maxSpan + 0.5f &&
               sy <= maxScale + 1e-4f && spanV <= maxSpan + 0.5f;
    }
};

std::vector<float> clusterUniqueCoords(std::vector<float> vals, float tol) {
    if (vals.empty()) return vals;
    std::sort(vals.begin(), vals.end());
    std::vector<float> out;
    out.reserve(vals.size());
    std::size_t i = 0;
    while (i < vals.size()) {
        float const anchor = vals[i];
        double sum = anchor;
        std::size_t cnt = 1;
        std::size_t j = i + 1;
        while (j < vals.size() && std::abs(vals[j] - anchor) <= tol) {
            sum += vals[j];
            ++cnt;
            ++j;
        }
        out.push_back(static_cast<float>(sum / static_cast<double>(cnt)));
        i = j;
    }
    return out;
}

int findNearestCoord(std::vector<float> const& coords, float target, float tol) {
    auto it = std::lower_bound(coords.begin(), coords.end(), target);
    int bestIdx = -1;
    float bestDist = tol + 1.f;
    if (it != coords.end()) {
        float const d = std::abs(*it - target);
        if (d <= tol && d < bestDist) {
            bestDist = d;
            bestIdx = static_cast<int>(it - coords.begin());
        }
    }
    if (it != coords.begin()) {
        auto prev = std::prev(it);
        float const d = std::abs(*prev - target);
        if (d <= tol && d < bestDist) {
            bestIdx = static_cast<int>(prev - coords.begin());
        }
    }
    return bestIdx;
}

bool buildHananGrid(
    std::vector<LocalItem> const& items,
    float tol,
    HananGrid& out
) {
    if (items.size() < 2) return false;
    out.baseWidth = items.front().baseWidth;
    out.baseHeight = items.front().baseHeight;
    out.maxScale = items.front().maxScale;
    out.maxSpan = items.front().maxSpan;

    std::vector<float> us;
    std::vector<float> vs;
    us.reserve(items.size() * 2);
    vs.reserve(items.size() * 2);
    for (auto const& it : items) {
        if (!approxEqual(it.baseWidth, out.baseWidth, 0.05f) ||
            !approxEqual(it.baseHeight, out.baseHeight, 0.05f)) {
            return false;
        }
        out.maxScale = std::min(out.maxScale, it.maxScale);
        out.maxSpan = std::min(out.maxSpan, it.maxSpan);
        us.push_back(it.u - it.spanU * 0.5f);
        us.push_back(it.u + it.spanU * 0.5f);
        vs.push_back(it.v - it.spanV * 0.5f);
        vs.push_back(it.v + it.spanV * 0.5f);
    }

    out.uCoords = clusterUniqueCoords(std::move(us), tol);
    out.vCoords = clusterUniqueCoords(std::move(vs), tol);
    if (out.uCoords.size() < 2 || out.vCoords.size() < 2) return false;

    out.W = out.uCoords.size() - 1;
    out.H = out.vCoords.size() - 1;
    if (out.W > kMaxHananGridCells || out.H > kMaxHananGridCells ||
        out.W * out.H > kMaxHananGridCells) {
        return false;
    }

    out.occ.assign(out.W * out.H, 0);
    out.occupiedCount = 0;

    for (auto const& it : items) {
        int const c0 = findNearestCoord(out.uCoords, it.u - it.spanU * 0.5f, tol);
        int const c1 = findNearestCoord(out.uCoords, it.u + it.spanU * 0.5f, tol);
        int const r0 = findNearestCoord(out.vCoords, it.v - it.spanV * 0.5f, tol);
        int const r1 = findNearestCoord(out.vCoords, it.v + it.spanV * 0.5f, tol);
        if (c0 < 0 || c1 <= c0 || r0 < 0 || r1 <= r0) return false;

        for (int r = r0; r < r1; ++r) {
            for (int c = c0; c < c1; ++c) {
                std::size_t const pos = static_cast<std::size_t>(r) * out.W + static_cast<std::size_t>(c);
                if (out.occ[pos]) return false; // Overlapping cell
                out.occ[pos] = 1;
                ++out.occupiedCount;
            }
        }
    }
    return out.occupiedCount > 0;
}

std::vector<GridRect> postMergeGridRects(
    std::vector<GridRect> rects,
    HananGrid const& grid
) {
    bool changed = true;
    while (changed && rects.size() > 1) {
        changed = false;
        for (std::size_t i = 0; i < rects.size() && !changed; ++i) {
            for (std::size_t j = i + 1; j < rects.size(); ++j) {
                auto const& a = rects[i];
                auto const& b = rects[j];
                if (a.r0 == b.r0 && a.r1 == b.r1 && (a.c1 == b.c0 || b.c1 == a.c0)) {
                    std::size_t const nc0 = std::min(a.c0, b.c0);
                    std::size_t const nc1 = std::max(a.c1, b.c1);
                    if (grid.validDims(nc0, nc1, a.r0, a.r1)) {
                        rects[i] = {nc0, nc1, a.r0, a.r1};
                        rects.erase(rects.begin() + static_cast<std::ptrdiff_t>(j));
                        changed = true;
                        break;
                    }
                } else if (a.c0 == b.c0 && a.c1 == b.c1 && (a.r1 == b.r0 || b.r1 == a.r0)) {
                    std::size_t const nr0 = std::min(a.r0, b.r0);
                    std::size_t const nr1 = std::max(a.r1, b.r1);
                    if (grid.validDims(a.c0, a.c1, nr0, nr1)) {
                        rects[i] = {a.c0, a.c1, nr0, nr1};
                        rects.erase(rects.begin() + static_cast<std::ptrdiff_t>(j));
                        changed = true;
                        break;
                    }
                }
            }
        }
    }
    return rects;
}

std::vector<GridRect> sweepGrid2D(
    HananGrid const& grid,
    bool uFirst,
    bool revU,
    bool revV
) {
    std::vector<std::uint8_t> rem = grid.occ;
    std::vector<GridRect> out;
    std::size_t const W = grid.W;
    std::size_t const H = grid.H;

    auto orderIdx = [](std::size_t step, std::size_t n, bool rev) {
        return rev ? (n - 1 - step) : step;
    };

    for (std::size_t s1 = 0; s1 < (uFirst ? H : W); ++s1) {
        for (std::size_t s2 = 0; s2 < (uFirst ? W : H); ++s2) {
            std::size_t const r = uFirst ? orderIdx(s1, H, revV) : orderIdx(s2, H, revV);
            std::size_t const c = uFirst ? orderIdx(s2, W, revU) : orderIdx(s1, W, revU);
            if (!rem[r * W + c]) continue;

            if (uFirst) {
                std::size_t c0 = c;
                std::size_t c1 = c + 1;
                if (!revU) {
                    while (c1 < W && rem[r * W + c1] && grid.validDims(c0, c1 + 1, r, r + 1)) {
                        ++c1;
                    }
                } else {
                    while (c0 > 0 && rem[r * W + (c0 - 1)] && grid.validDims(c0 - 1, c1, r, r + 1)) {
                        --c0;
                    }
                }

                std::size_t r0 = r;
                std::size_t r1 = r + 1;
                if (!revV) {
                    while (r1 < H && grid.validDims(c0, c1, r0, r1 + 1)) {
                        bool ok = true;
                        for (std::size_t cc = c0; cc < c1; ++cc) {
                            if (!rem[r1 * W + cc]) {
                                ok = false;
                                break;
                            }
                        }
                        if (!ok) break;
                        ++r1;
                    }
                } else {
                    while (r0 > 0 && grid.validDims(c0, c1, r0 - 1, r1)) {
                        bool ok = true;
                        for (std::size_t cc = c0; cc < c1; ++cc) {
                            if (!rem[(r0 - 1) * W + cc]) {
                                ok = false;
                                break;
                            }
                        }
                        if (!ok) break;
                        --r0;
                    }
                }

                for (std::size_t rr = r0; rr < r1; ++rr) {
                    for (std::size_t cc = c0; cc < c1; ++cc) {
                        rem[rr * W + cc] = 0;
                    }
                }
                out.push_back({c0, c1, r0, r1});
            } else {
                std::size_t r0 = r;
                std::size_t r1 = r + 1;
                if (!revV) {
                    while (r1 < H && rem[r1 * W + c] && grid.validDims(c, c + 1, r0, r1 + 1)) {
                        ++r1;
                    }
                } else {
                    while (r0 > 0 && rem[(r0 - 1) * W + c] && grid.validDims(c, c + 1, r0 - 1, r1)) {
                        --r0;
                    }
                }

                std::size_t c0 = c;
                std::size_t c1 = c + 1;
                if (!revU) {
                    while (c1 < W && grid.validDims(c0, c1 + 1, r0, r1)) {
                        bool ok = true;
                        for (std::size_t rr = r0; rr < r1; ++rr) {
                            if (!rem[rr * W + c1]) {
                                ok = false;
                                break;
                            }
                        }
                        if (!ok) break;
                        ++c1;
                    }
                } else {
                    while (c0 > 0 && grid.validDims(c0 - 1, c1, r0, r1)) {
                        bool ok = true;
                        for (std::size_t rr = r0; rr < r1; ++rr) {
                            if (!rem[rr * W + (c0 - 1)]) {
                                ok = false;
                                break;
                            }
                        }
                        if (!ok) break;
                        --c0;
                    }
                }

                for (std::size_t rr = r0; rr < r1; ++rr) {
                    for (std::size_t cc = c0; cc < c1; ++cc) {
                        rem[rr * W + cc] = 0;
                    }
                }
                out.push_back({c0, c1, r0, r1});
            }
        }
    }
    return postMergeGridRects(std::move(out), grid);
}

// Histogram-based greedy maximal rectangle extractor (area-first or corner-hugging).
std::vector<GridRect> greedyHistogram2D(
    HananGrid const& grid,
    bool preferCornerFit
) {
    std::size_t const W = grid.W;
    std::size_t const H = grid.H;
    std::vector<std::uint8_t> rem = grid.occ;
    std::size_t remCount = grid.occupiedCount;
    std::vector<GridRect> out;
    std::vector<std::size_t> heights(W, 0);

    auto exposedCornerScore = [&](std::size_t c0, std::size_t c1, std::size_t r0, std::size_t r1) -> int {
        auto emptyAt = [&](int rr, int cc) -> bool {
            if (rr < 0 || cc < 0 || rr >= static_cast<int>(H) || cc >= static_cast<int>(W)) return true;
            return rem[static_cast<std::size_t>(rr) * W + static_cast<std::size_t>(cc)] == 0;
        };
        int score = 0;
        int const ir0 = static_cast<int>(r0);
        int const ir1 = static_cast<int>(r1) - 1;
        int const ic0 = static_cast<int>(c0);
        int const ic1 = static_cast<int>(c1) - 1;
        if (emptyAt(ir0 - 1, ic0) && emptyAt(ir0, ic0 - 1)) ++score;
        if (emptyAt(ir0 - 1, ic1) && emptyAt(ir0, ic1 + 1)) ++score;
        if (emptyAt(ir1 + 1, ic0) && emptyAt(ir1, ic0 - 1)) ++score;
        if (emptyAt(ir1 + 1, ic1) && emptyAt(ir1, ic1 + 1)) ++score;
        return score;
    };

    while (remCount > 0) {
        std::fill(heights.begin(), heights.end(), 0);
        GridRect bestRect{};
        double bestScore = -1.0;

        for (std::size_t r = 0; r < H; ++r) {
            for (std::size_t c = 0; c < W; ++c) {
                heights[c] = rem[r * W + c] ? (heights[c] + 1) : 0;
            }

            // Stack of (startCol, height)
            std::vector<std::pair<std::size_t, std::size_t>> stack;
            for (std::size_t c = 0; c <= W; ++c) {
                std::size_t const curH = (c < W) ? heights[c] : 0;
                std::size_t startCol = c;
                while (!stack.empty() && stack.back().second > curH) {
                    auto const [sc, h] = stack.back();
                    stack.pop_back();
                    startCol = sc;

                    // Clip height h to maxV from row r downward -> [r0 .. r+1)
                    std::size_t r0 = r + 1 - h;
                    while (r0 <= r && !grid.validDims(sc, sc + 1, r0, r + 1)) {
                        ++r0;
                    }
                    if (r0 <= r) {
                        if (!preferCornerFit && grid.validDims(sc, c, r0, r + 1)) {
                            std::size_t const cells = (c - sc) * (r + 1 - r0);
                            float const area = (grid.uCoords[c] - grid.uCoords[sc]) *
                                               (grid.vCoords[r + 1] - grid.vCoords[r0]);
                            double const score = static_cast<double>(cells) * 1000.0 + static_cast<double>(area) * 0.01;
                            if (score > bestScore) {
                                bestScore = score;
                                bestRect = {sc, c, r0, r + 1};
                            }
                        } else {
                            // Slide valid column sub-intervals within [sc, c) respecting maxU.
                            std::size_t right = sc;
                            for (std::size_t left = sc; left < c; ++left) {
                                if (right < left + 1) right = left + 1;
                                while (right < c && grid.validDims(left, right + 1, r0, r + 1)) {
                                    ++right;
                                }
                                if (!grid.validDims(left, right, r0, r + 1)) continue;

                                std::size_t const cells = (right - left) * (r + 1 - r0);
                                float const area = (grid.uCoords[right] - grid.uCoords[left]) *
                                                   (grid.vCoords[r + 1] - grid.vCoords[r0]);
                                double score = static_cast<double>(cells) * 1000.0 + static_cast<double>(area) * 0.01;
                                if (preferCornerFit) {
                                    score += static_cast<double>(exposedCornerScore(left, right, r0, r + 1)) * 200.0;
                                }
                                if (score > bestScore) {
                                    bestScore = score;
                                    bestRect = {left, right, r0, r + 1};
                                }
                                if (right == c) break;
                            }
                        }
                    }
                }
                if (curH > 0) {
                    stack.emplace_back(startCol, curH);
                }
            }
        }

        if (bestScore < 0.0) break;
        for (std::size_t rr = bestRect.r0; rr < bestRect.r1; ++rr) {
            for (std::size_t cc = bestRect.c0; cc < bestRect.c1; ++cc) {
                rem[rr * W + cc] = 0;
                --remCount;
            }
        }
        out.push_back(bestRect);
    }

    return postMergeGridRects(std::move(out), grid);
}

// Exact bounded branch-and-bound for small Hanan grids (<= 48 occupied cells).
std::vector<GridRect> exactBranchAndBound2D(
    HananGrid const& grid,
    std::vector<GridRect> best
) {
    if (grid.occupiedCount > 48 || best.size() <= 1) return best;

    std::size_t const W = grid.W;
    std::size_t const H = grid.H;

    // Max possible cells any single valid rectangle can cover in this grid (for tight LB).
    std::size_t maxSingleCells = 1;
    for (std::size_t r0 = 0; r0 < H; ++r0) {
        for (std::size_t r1 = r0 + 1; r1 <= H; ++r1) {
            if (!grid.validDims(0, 1, r0, r1)) break;
            for (std::size_t c0 = 0; c0 < W; ++c0) {
                for (std::size_t c1 = c0 + 1; c1 <= W; ++c1) {
                    if (!grid.validDims(c0, c1, r0, r1)) break;
                    bool allOcc = true;
                    for (std::size_t rr = r0; rr < r1 && allOcc; ++rr) {
                        for (std::size_t cc = c0; cc < c1; ++cc) {
                            if (!grid.occ[rr * W + cc]) {
                                allOcc = false;
                                break;
                            }
                        }
                    }
                    if (!allOcc) break;
                    maxSingleCells = std::max(maxSingleCells, (c1 - c0) * (r1 - r0));
                }
            }
        }
    }

    std::vector<std::uint8_t> rem = grid.occ;
    std::vector<GridRect> cur;
    std::size_t nodes = 0;

    auto search = [&](auto& self, std::size_t remCells) -> void {
        if (nodes >= kExactSearchNodeBudget) return;
        ++nodes;
        if (remCells == 0) {
            if (cur.size() < best.size()) best = cur;
            return;
        }
        std::size_t const lb = (remCells + maxSingleCells - 1) / maxSingleCells;
        if (cur.size() + lb >= best.size()) return;

        // Pick the uncovered cell with fewest maximal covering options (MRV) or first exposed cell.
        std::size_t pickR = 0, pickC = 0;
        std::vector<GridRect> pickCands;
        std::size_t minOpts = std::numeric_limits<std::size_t>::max();

        for (std::size_t r = 0; r < H; ++r) {
            for (std::size_t c = 0; c < W; ++c) {
                if (!rem[r * W + c]) continue;

                // Prefer top-left exposed corner or generate candidate rectangles covering (r, c).
                // Restrict to rectangles anchored with top/left or covering (r, c) maximally.
                bool const exposedLeft = (c == 0 || !rem[r * W + (c - 1)]);
                bool const exposedBottom = (r == 0 || !rem[(r - 1) * W + c]);
                if (!exposedLeft && !exposedBottom) continue;

                // Anchor at (r, c) or expand both sides? If (r,c) is exposedLeft & exposedBottom,
                // any rectangle covering (r,c) that can extend left/down is subsumed if we take
                // all maximal rectangles covering (r,c). Here generate maximal extensions from (c0, r0)
                // when (r, c) is leftmost+bottommost of some region, or all valid rectangles containing (r, c).
                std::vector<GridRect> cands;
                std::size_t cMin = c;
                while (cMin > 0 && rem[r * W + (cMin - 1)] && grid.validDims(cMin - 1, c + 1, r, r + 1)) --cMin;
                std::size_t rMin = r;
                while (rMin > 0 && rem[(rMin - 1) * W + c] && grid.validDims(c, c + 1, rMin - 1, r + 1)) --rMin;

                for (std::size_t r0 = rMin; r0 <= r; ++r0) {
                    for (std::size_t c0 = cMin; c0 <= c; ++c0) {
                        if (!rem[r0 * W + c0]) continue;
                        // Check [c0..c] x [r0..r] is full
                        bool baseOk = grid.validDims(c0, c + 1, r0, r + 1);
                        for (std::size_t rr = r0; rr <= r && baseOk; ++rr) {
                            for (std::size_t cc = c0; cc <= c; ++cc) {
                                if (!rem[rr * W + cc]) {
                                    baseOk = false;
                                    break;
                                }
                            }
                        }
                        if (!baseOk) continue;

                        // Maximal widths from c0 -> cMax, and for each c1 >= c+1 compute maximal r1 >= r+1
                        std::size_t rMax = r + 1;
                        while (rMax < H && grid.validDims(c0, c + 1, r0, rMax + 1)) {
                            bool ok = true;
                            for (std::size_t cc = c0; cc <= c; ++cc) {
                                if (!rem[rMax * W + cc]) {
                                    ok = false;
                                    break;
                                }
                            }
                            if (!ok) break;
                            ++rMax;
                        }

                        std::size_t prevR1 = 0;
                        std::size_t maxC1 = c + 1;
                        while (maxC1 < W && grid.validDims(c0, maxC1 + 1, r0, r + 1)) {
                            bool ok = true;
                            for (std::size_t rr = r0; rr <= r; ++rr) {
                                if (!rem[rr * W + maxC1]) {
                                    ok = false;
                                    break;
                                }
                            }
                            if (!ok) break;
                            ++maxC1;
                        }

                        for (std::size_t c1 = maxC1; c1 >= c + 1; --c1) {
                            std::size_t curR1 = r + 1;
                            while (curR1 < rMax && grid.validDims(c0, c1, r0, curR1 + 1)) {
                                bool ok = true;
                                for (std::size_t cc = c0; cc < c1; ++cc) {
                                    if (!rem[curR1 * W + cc]) {
                                        ok = false;
                                        break;
                                    }
                                }
                                if (!ok) break;
                                ++curR1;
                            }
                            if (curR1 > prevR1) {
                                cands.push_back({c0, c1, r0, curR1});
                                prevR1 = curR1;
                            }
                            if (c1 == c + 1) break;
                        }
                    }
                }

                if (cands.size() < minOpts) {
                    minOpts = cands.size();
                    pickR = r;
                    pickC = c;
                    pickCands = std::move(cands);
                    if (minOpts <= 1) goto found_mrv;
                }
            }
        }
    found_mrv:
        (void)pickR;
        (void)pickC;
        if (pickCands.empty()) return;

        std::sort(pickCands.begin(), pickCands.end(), [](GridRect const& a, GridRect const& b) {
            std::size_t const ca = (a.c1 - a.c0) * (a.r1 - a.r0);
            std::size_t const cb = (b.c1 - b.c0) * (b.r1 - b.r0);
            if (ca != cb) return ca > cb;
            if (a.r0 != b.r0) return a.r0 < b.r0;
            return a.c0 < b.c0;
        });

        for (auto const& rect : pickCands) {
            std::size_t const area = (rect.c1 - rect.c0) * (rect.r1 - rect.r0);
            if (cur.size() + 1 + ((remCells - area + maxSingleCells - 1) / maxSingleCells) >= best.size()) {
                continue;
            }
            for (std::size_t rr = rect.r0; rr < rect.r1; ++rr) {
                for (std::size_t cc = rect.c0; cc < rect.c1; ++cc) {
                    rem[rr * W + cc] = 0;
                }
            }
            cur.push_back(rect);
            self(self, remCells - area);
            cur.pop_back();
            for (std::size_t rr = rect.r0; rr < rect.r1; ++rr) {
                for (std::size_t cc = rect.c0; cc < rect.c1; ++cc) {
                    rem[rr * W + cc] = 1;
                }
            }
            if (nodes >= kExactSearchNodeBudget) break;
        }
    };

    search(search, grid.occupiedCount);
    return best;
}

std::vector<LocalItem> gridRectsToLocalItems(
    std::vector<GridRect> const& rects,
    HananGrid const& grid,
    std::vector<LocalItem> const& origItems,
    float tol
) {
    if (rects.size() >= origItems.size()) return origItems;

    // Map each GridRect to its geometric LocalItem.
    std::vector<LocalItem> out(rects.size(), origItems.front());
    for (std::size_t r = 0; r < rects.size(); ++r) {
        auto const& gr = rects[r];
        float const u0 = grid.uCoords[gr.c0];
        float const u1 = grid.uCoords[gr.c1];
        float const v0 = grid.vCoords[gr.r0];
        float const v1 = grid.vCoords[gr.r1];
        out[r].u = (u0 + u1) * 0.5f;
        out[r].v = (v0 + v1) * 0.5f;
        out[r].spanU = u1 - u0;
        out[r].spanV = v1 - v0;
        out[r].scaleX = out[r].spanU / std::max(out[r].baseWidth, 1.f);
        out[r].scaleY = out[r].spanV / std::max(out[r].baseHeight, 1.f);
        out[r].sources.clear();
    }

    // Check if every original item is cleanly contained within a single GridRect (no split).
    std::vector<int> owner(origItems.size(), -1);
    bool anySplit = false;
    for (std::size_t i = 0; i < origItems.size(); ++i) {
        auto const& it = origItems[i];
        float const iU0 = it.u - it.spanU * 0.5f;
        float const iU1 = it.u + it.spanU * 0.5f;
        float const iV0 = it.v - it.spanV * 0.5f;
        float const iV1 = it.v + it.spanV * 0.5f;
        int match = -1;
        for (std::size_t r = 0; r < out.size(); ++r) {
            float const rU0 = out[r].u - out[r].spanU * 0.5f;
            float const rU1 = out[r].u + out[r].spanU * 0.5f;
            float const rV0 = out[r].v - out[r].spanV * 0.5f;
            float const rV1 = out[r].v + out[r].spanV * 0.5f;
            if (iU0 >= rU0 - tol && iU1 <= rU1 + tol && iV0 >= rV0 - tol && iV1 <= rV1 + tol) {
                match = static_cast<int>(r);
                break;
            }
        }
        if (match < 0) {
            anySplit = true;
            break;
        }
        owner[i] = match;
    }

    if (!anySplit) {
        for (std::size_t i = 0; i < origItems.size(); ++i) {
            auto& dst = out[static_cast<std::size_t>(owner[i])];
            dst.sources.insert(dst.sources.end(), origItems[i].sources.begin(), origItems[i].sources.end());
        }
        return out;
    }

    // Re-tiling split at least one pre-stretched object while strictly reducing total count (rects.size() < origItems.size()).
    // Assign every input item to one output rect (giving every output rect >= 1 input item so applyOptimizationPlan
    // deletes all origItems cleanly and creates all changed rects without any double-delete or orphan).
    std::vector<bool> itemAssigned(origItems.size(), false);
    for (std::size_t r = 0; r < out.size(); ++r) {
        float const rU0 = out[r].u - out[r].spanU * 0.5f;
        float const rU1 = out[r].u + out[r].spanU * 0.5f;
        float const rV0 = out[r].v - out[r].spanV * 0.5f;
        float const rV1 = out[r].v + out[r].spanV * 0.5f;
        for (std::size_t i = 0; i < origItems.size(); ++i) {
            if (itemAssigned[i]) continue;
            auto const& it = origItems[i];
            float const iU0 = it.u - it.spanU * 0.5f;
            float const iU1 = it.u + it.spanU * 0.5f;
            float const iV0 = it.v - it.spanV * 0.5f;
            float const iV1 = it.v + it.spanV * 0.5f;
            if (std::max(iU0, rU0) + tol < std::min(iU1, rU1) &&
                std::max(iV0, rV0) + tol < std::min(iV1, rV1)) {
                itemAssigned[i] = true;
                out[r].sources.insert(out[r].sources.end(), it.sources.begin(), it.sources.end());
                break;
            }
        }
    }
    for (std::size_t r = 0; r < out.size(); ++r) {
        if (!out[r].sources.empty()) continue;
        for (std::size_t i = 0; i < origItems.size(); ++i) {
            if (!itemAssigned[i]) {
                itemAssigned[i] = true;
                out[r].sources.insert(out[r].sources.end(), origItems[i].sources.begin(), origItems[i].sources.end());
                break;
            }
        }
    }
    for (std::size_t i = 0; i < origItems.size(); ++i) {
        if (itemAssigned[i]) continue;
        auto const& it = origItems[i];
        float const iU0 = it.u - it.spanU * 0.5f;
        float const iU1 = it.u + it.spanU * 0.5f;
        float const iV0 = it.v - it.spanV * 0.5f;
        float const iV1 = it.v + it.spanV * 0.5f;
        std::size_t targetR = 0;
        for (std::size_t r = 0; r < out.size(); ++r) {
            float const rU0 = out[r].u - out[r].spanU * 0.5f;
            float const rU1 = out[r].u + out[r].spanU * 0.5f;
            float const rV0 = out[r].v - out[r].spanV * 0.5f;
            float const rV1 = out[r].v + out[r].spanV * 0.5f;
            if (std::max(iU0, rU0) + tol < std::min(iU1, rU1) &&
                std::max(iV0, rV0) + tol < std::min(iV1, rV1)) {
                targetR = r;
                break;
            }
        }
        out[targetR].sources.insert(out[targetR].sources.end(), it.sources.begin(), it.sources.end());
    }
    return out;
}

std::vector<LocalItem> solveUniform2DComponent(
    std::vector<LocalItem> const& items,
    OptimizerOptions const& opts
) {
    // Iterative 1D fixed-point + greedy 2D on atomic items (works even with overlaps or huge coords).
    auto best = mergeIterative1D(items, Axis::U, opts);
    auto candVH = mergeIterative1D(items, Axis::V, opts);
    if (candVH.size() < best.size()) best = std::move(candVH);

    auto rectU = mergeIterative1D(mergeGreedyRect2D(items, true, opts), Axis::U, opts);
    if (rectU.size() < best.size()) best = std::move(rectU);

    auto rectV = mergeIterative1D(mergeGreedyRect2D(items, false, opts), Axis::V, opts);
    if (rectV.size() < best.size()) best = std::move(rectV);

    HananGrid grid;
    if (!buildHananGrid(items, opts.posTolerance, grid)) {
        return best;
    }

    // Run unit-cell iterative 1D merges too if grid is small enough (handles pinwheel/mixed pre-stretched fills).
    if (grid.W * grid.H <= kMaxHistogramCells) {
        std::vector<LocalItem> unitCells;
        unitCells.reserve(grid.occupiedCount);
        LocalItem proto = items.front();
        proto.sources.clear();
        for (std::size_t r = 0; r < grid.H; ++r) {
            for (std::size_t c = 0; c < grid.W; ++c) {
                if (!grid.occ[r * grid.W + c]) continue;
                LocalItem cell = proto;
                cell.u = (grid.uCoords[c] + grid.uCoords[c + 1]) * 0.5f;
                cell.v = (grid.vCoords[r] + grid.vCoords[r + 1]) * 0.5f;
                cell.spanU = grid.uCoords[c + 1] - grid.uCoords[c];
                cell.spanV = grid.vCoords[r + 1] - grid.vCoords[r];
                cell.scaleX = cell.spanU / std::max(cell.baseWidth, 1.f);
                cell.scaleY = cell.spanV / std::max(cell.baseHeight, 1.f);
                unitCells.push_back(std::move(cell));
            }
        }
        auto u1 = mergeIterative1D(unitCells, Axis::U, opts);
        auto u2 = mergeIterative1D(unitCells, Axis::V, opts);
        auto toRects = [&](std::vector<LocalItem> const& li) {
            std::vector<GridRect> grs;
            grs.reserve(li.size());
            for (auto const& it : li) {
                int const c0 = findNearestCoord(grid.uCoords, it.u - it.spanU * 0.5f, opts.posTolerance);
                int const c1 = findNearestCoord(grid.uCoords, it.u + it.spanU * 0.5f, opts.posTolerance);
                int const r0 = findNearestCoord(grid.vCoords, it.v - it.spanV * 0.5f, opts.posTolerance);
                int const r1 = findNearestCoord(grid.vCoords, it.v + it.spanV * 0.5f, opts.posTolerance);
                if (c0 < 0 || c1 <= c0 || r0 < 0 || r1 <= r0) return std::vector<GridRect>{};
                grs.push_back({static_cast<std::size_t>(c0), static_cast<std::size_t>(c1),
                               static_cast<std::size_t>(r0), static_cast<std::size_t>(r1)});
            }
            return grs;
        };
        if (u1.size() < best.size()) {
            auto gr = toRects(u1);
            if (!gr.empty()) {
                auto mapped = gridRectsToLocalItems(gr, grid, items, opts.posTolerance);
                if (mapped.size() < best.size()) best = std::move(mapped);
            }
        }
        if (u2.size() < best.size()) {
            auto gr = toRects(u2);
            if (!gr.empty()) {
                auto mapped = gridRectsToLocalItems(gr, grid, items, opts.posTolerance);
                if (mapped.size() < best.size()) best = std::move(mapped);
            }
        }
    }

    std::vector<GridRect> bestGrid;
    bool hasGridBest = false;
    auto considerGrid = [&](std::vector<GridRect> cand) {
        if (cand.empty()) return;
        if (!hasGridBest || cand.size() < bestGrid.size()) {
            bestGrid = std::move(cand);
            hasGridBest = true;
        }
    };

    // 8-corner greedy sweeps
    for (int uFirst = 0; uFirst <= 1; ++uFirst) {
        for (int revU = 0; revU <= 1; ++revU) {
            for (int revV = 0; revV <= 1; ++revV) {
                considerGrid(sweepGrid2D(grid, uFirst != 0, revU != 0, revV != 0));
            }
        }
    }

    // Histogram maximal-rectangle greedy (for grids <= 2500 cells)
    if (grid.W * grid.H <= kMaxHistogramCells) {
        considerGrid(greedyHistogram2D(grid, false));
        considerGrid(greedyHistogram2D(grid, true));
    }

    // Exact bounded MRV branch-and-bound for small components
    if (hasGridBest && grid.occupiedCount <= 48 && bestGrid.size() > 1) {
        considerGrid(exactBranchAndBound2D(grid, bestGrid));
    }

    if (hasGridBest && bestGrid.size() < best.size()) {
        auto mapped = gridRectsToLocalItems(bestGrid, grid, items, opts.posTolerance);
        if (mapped.size() < best.size()) {
            best = std::move(mapped);
        }
    }

    return best;
}

// --- Axis1D Optimal 1D Strip Cover ---

struct StripCand {
    Axis axis = Axis::U;
    std::vector<std::size_t> indices;
    float span = 0.f;
    bool exactLine = false;
};

// Finds all maximal contiguous valid runs in active items along U and V.
std::vector<StripCand> findMaximalRuns1D(
    std::vector<LocalItem> const& active,
    OptimizerOptions const& opts
) {
    std::vector<StripCand> runs;
    std::size_t const n = active.size();
    if (n < 2) return runs;

    for (Axis axis : {Axis::U, Axis::V}) {
        std::vector<std::size_t> cands;
        cands.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            if (canStretchAlong(active[i], axis)) cands.push_back(i);
        }
        if (cands.size() < 2) continue;

        auto perpCenter = [&](std::size_t idx) { return axis == Axis::U ? active[idx].v : active[idx].u; };
        auto perpSpan = [&](std::size_t idx) { return axis == Axis::U ? active[idx].spanV : active[idx].spanU; };
        auto parCenter = [&](std::size_t idx) { return axis == Axis::U ? active[idx].u : active[idx].v; };
        auto parSpan = [&](std::size_t idx) { return axis == Axis::U ? active[idx].spanU : active[idx].spanV; };

        std::sort(cands.begin(), cands.end(), [&](std::size_t a, std::size_t b) {
            if (perpSpan(a) != perpSpan(b)) return perpSpan(a) < perpSpan(b);
            return perpCenter(a) < perpCenter(b);
        });

        std::size_t spanStart = 0;
        while (spanStart < cands.size()) {
            float const spanAnchor = perpSpan(cands[spanStart]);
            std::size_t spanEnd = spanStart + 1;
            while (spanEnd < cands.size() &&
                   approxEqual(perpSpan(cands[spanEnd]), spanAnchor, opts.posTolerance)) {
                ++spanEnd;
            }
            std::sort(cands.begin() + static_cast<std::ptrdiff_t>(spanStart),
                      cands.begin() + static_cast<std::ptrdiff_t>(spanEnd),
                      [&](std::size_t a, std::size_t b) {
                          if (perpCenter(a) != perpCenter(b)) return perpCenter(a) < perpCenter(b);
                          return parCenter(a) < parCenter(b);
                      });

            std::size_t i = spanStart;
            while (i < spanEnd) {
                float const perpAnchor = perpCenter(cands[i]);
                std::size_t bandEnd = i + 1;
                while (bandEnd < spanEnd &&
                       approxEqual(perpCenter(cands[bandEnd]), perpAnchor, opts.posTolerance)) {
                    ++bandEnd;
                }
                std::sort(cands.begin() + static_cast<std::ptrdiff_t>(i),
                          cands.begin() + static_cast<std::ptrdiff_t>(bandEnd),
                          [&](std::size_t a, std::size_t b) {
                              return parCenter(a) < parCenter(b);
                          });

                // Break band into contiguous flush segments first.
                std::size_t segStart = i;
                while (segStart < bandEnd) {
                    std::size_t segEnd = segStart + 1;
                    float segMax = parCenter(cands[segStart]) + parSpan(cands[segStart]) * 0.5f;
                    while (segEnd < bandEnd) {
                        float const cMin = parCenter(cands[segEnd]) - parSpan(cands[segEnd]) * 0.5f;
                        if (!approxEqual(cMin, segMax, opts.posTolerance)) break;
                        segMax = parCenter(cands[segEnd]) + parSpan(cands[segEnd]) * 0.5f;
                        ++segEnd;
                    }

                    // From both ends of the contiguous segment, compute the maximal valid run.
                    if (segEnd - segStart >= 2) {
                        auto emitFromStart = [&](std::size_t s) {
                            auto const& first = active[cands[s]];
                            float const runMin = parCenter(cands[s]) - parSpan(cands[s]) * 0.5f;
                            std::vector<std::size_t> seq{cands[s]};
                            float lastMax = parCenter(cands[s]) + parSpan(cands[s]) * 0.5f;
                            for (std::size_t k = s + 1; k < segEnd; ++k) {
                                float const cMax = parCenter(cands[k]) + parSpan(cands[k]) * 0.5f;
                                if (!canMergePairInRun(first, cMax - runMin, axis)) break;
                                lastMax = cMax;
                                seq.push_back(cands[k]);
                            }
                            if (seq.size() >= 2) {
                                runs.push_back({axis, std::move(seq), lastMax - runMin,
                                                (s == segStart && s + seq.size() == segEnd)});
                            }
                        };
                        emitFromStart(segStart);
                        // Also emit suffix run from end if segment exceeds maxSpan
                        std::size_t sBack = segEnd;
                        float const endMax = parCenter(cands[segEnd - 1]) + parSpan(cands[segEnd - 1]) * 0.5f;
                        auto const& lastIt = active[cands[segEnd - 1]];
                        while (sBack > segStart) {
                            float const cMin = parCenter(cands[sBack - 1]) - parSpan(cands[sBack - 1]) * 0.5f;
                            if (!canMergePairInRun(lastIt, endMax - cMin, axis)) break;
                            --sBack;
                        }
                        if (sBack > segStart && segEnd - sBack >= 2) {
                            std::vector<std::size_t> seq;
                            for (std::size_t k = sBack; k < segEnd; ++k) seq.push_back(cands[k]);
                            float const cMin = parCenter(cands[sBack]) - parSpan(cands[sBack]) * 0.5f;
                            runs.push_back({axis, std::move(seq), endMax - cMin, false});
                        }
                    }
                    segStart = segEnd;
                }
                i = bandEnd;
            }
            spanStart = spanEnd;
        }
    }
    return runs;
}

std::vector<LocalItem> greedyRuns1D(
    std::vector<LocalItem> active,
    bool preferExposedCorners,
    OptimizerOptions const& opts
) {
    std::vector<LocalItem> done;
    while (active.size() >= 2) {
        auto runs = findMaximalRuns1D(active, opts);
        if (runs.empty()) break;

        std::vector<bool> isExposed(active.size(), false);
        if (preferExposedCorners) {
            float const tol = opts.posTolerance;
            for (std::size_t i = 0; i < active.size(); ++i) {
                auto const& a = active[i];
                float const aU0 = a.u - a.spanU * 0.5f;
                float const aU1 = a.u + a.spanU * 0.5f;
                float const aV0 = a.v - a.spanV * 0.5f;
                float const aV1 = a.v + a.spanV * 0.5f;
                bool hasPosU = false, hasNegU = false, hasPosV = false, hasNegV = false;
                for (std::size_t j = 0; j < active.size(); ++j) {
                    if (i == j) continue;
                    auto const& b = active[j];
                    float const bU0 = b.u - b.spanU * 0.5f;
                    float const bU1 = b.u + b.spanU * 0.5f;
                    float const bV0 = b.v - b.spanV * 0.5f;
                    float const bV1 = b.v + b.spanV * 0.5f;
                    if (std::min(aV1, bV1) - std::max(aV0, bV0) > tol) {
                        if (approxEqual(bU0, aU1, tol)) hasPosU = true;
                        if (approxEqual(bU1, aU0, tol)) hasNegU = true;
                    }
                    if (std::min(aU1, bU1) - std::max(aU0, bU0) > tol) {
                        if (approxEqual(bV0, aV1, tol)) hasPosV = true;
                        if (approxEqual(bV1, aV0, tol)) hasNegV = true;
                    }
                }
                isExposed[i] = (!hasPosU || !hasNegU) && (!hasPosV || !hasNegV);
            }
        }

        std::size_t bestIdx = 0;
        double bestScore = -1.0;
        for (std::size_t r = 0; r < runs.size(); ++r) {
            auto const& run = runs[r];
            bool touchesCorner = false;
            if (preferExposedCorners) {
                touchesCorner = isExposed[run.indices.front()] || isExposed[run.indices.back()];
            }
            double score = static_cast<double>(run.indices.size() - 1) * 1000.0;
            if (touchesCorner) score += 400.0;
            if (run.exactLine) score += 150.0;
            score += static_cast<double>(run.span) * 0.01;
            if (score > bestScore) {
                bestScore = score;
                bestIdx = r;
            }
        }

        auto const& chosen = runs[bestIdx];
        std::vector<bool> removed(active.size(), false);
        LocalItem merged = active[chosen.indices.front()];
        removed[chosen.indices.front()] = true;
        for (std::size_t k = 1; k < chosen.indices.size(); ++k) {
            std::size_t const idx = chosen.indices[k];
            removed[idx] = true;
            merged = mergeTwoItems(std::move(merged), active[idx], chosen.axis);
        }

        std::vector<LocalItem> nextActive;
        nextActive.reserve(active.size() - chosen.indices.size() + 1);
        for (std::size_t i = 0; i < active.size(); ++i) {
            if (!removed[i]) nextActive.push_back(std::move(active[i]));
        }
        // Only put merged back into done (it cannot merge perpendicularly anyway for Axis1D),
        // or keep if it can still extend along the same axis? Runs are already maximal.
        done.push_back(std::move(merged));
        active = std::move(nextActive);
    }
    for (auto& it : active) done.push_back(std::move(it));
    auto cleanU = mergeIterative1D(done, Axis::U, opts);
    auto cleanV = mergeIterative1D(done, Axis::V, opts);
    return cleanU.size() <= cleanV.size() ? cleanU : cleanV;
}

// Exact 2-branch strip solver for small uniform-cell Axis1D / 1D-mode components (<= 48 items).
std::vector<LocalItem> exactStripCoverAxis1D(
    std::vector<LocalItem> const& items,
    std::vector<LocalItem> best,
    OptimizerOptions const& opts
) {
    std::size_t const n = items.size();
    if (n < 4 || n > 48 || best.size() <= 2) return best;

    float const cellU = items.front().spanU;
    float const cellV = items.front().spanV;
    for (auto const& it : items) {
        if (!canStretchAlong(it, Axis::U) || !canStretchAlong(it, Axis::V) ||
            !approxEqual(it.spanU, cellU, opts.posTolerance) ||
            !approxEqual(it.spanV, cellV, opts.posTolerance)) {
            return best;
        }
    }

    int const maxCellsU = std::max(1, static_cast<int>(std::floor(
        std::min(items.front().maxSpan + 0.5f, items.front().maxScale * items.front().baseWidth + 1e-4f) /
        std::max(cellU, 1.f))));
    int const maxCellsV = std::max(1, static_cast<int>(std::floor(
        std::min(items.front().maxSpan + 0.5f, items.front().maxScale * items.front().baseHeight + 1e-4f) /
        std::max(cellV, 1.f))));
    std::size_t const maxStripCells = static_cast<std::size_t>(std::max(maxCellsU, maxCellsV));
    if (maxStripCells <= 1) return best;

    // Integer grid coordinates relative to min U/V.
    float minU = items[0].u, minV = items[0].v;
    for (auto const& it : items) {
        minU = std::min(minU, it.u);
        minV = std::min(minV, it.v);
    }

    struct GridKey {
        int x = 0;
        int y = 0;
        std::size_t idx = 0;
    };
    std::vector<int> gx(n), gy(n);
    std::vector<GridKey> sortedKeys;
    sortedKeys.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        float const fx = (items[i].u - minU) / cellU;
        float const fy = (items[i].v - minV) / cellV;
        int const ix = static_cast<int>(std::lround(fx));
        int const iy = static_cast<int>(std::lround(fy));
        if (std::abs(fx - ix) * cellU > opts.posTolerance ||
            std::abs(fy - iy) * cellV > opts.posTolerance) {
            return best;
        }
        gx[i] = ix;
        gy[i] = iy;
        sortedKeys.push_back({ix, iy, i});
    }
    std::sort(sortedKeys.begin(), sortedKeys.end(), [](GridKey const& a, GridKey const& b) {
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    });
    for (std::size_t i = 1; i < sortedKeys.size(); ++i) {
        if (sortedKeys[i].x == sortedKeys[i - 1].x && sortedKeys[i].y == sortedKeys[i - 1].y) {
            return best; // Duplicate cell
        }
    }

    auto findCell = [&](int x, int y) -> int {
        auto it = std::lower_bound(sortedKeys.begin(), sortedKeys.end(), std::pair{x, y},
            [](GridKey const& k, std::pair<int, int> const& target) {
                if (k.x != target.first) return k.x < target.first;
                return k.y < target.second;
            });
        if (it != sortedKeys.end() && it->x == x && it->y == y) {
            return static_cast<int>(it->idx);
        }
        return -1;
    };

    std::vector<int> posX(n, -1), negX(n, -1), posY(n, -1), negY(n, -1);
    for (std::size_t i = 0; i < n; ++i) {
        posX[i] = findCell(gx[i] + 1, gy[i]);
        negX[i] = findCell(gx[i] - 1, gy[i]);
        posY[i] = findCell(gx[i], gy[i] + 1);
        negY[i] = findCell(gx[i], gy[i] - 1);
    }

    struct Strip {
        Axis axis;
        std::vector<std::size_t> members;
    };
    std::vector<bool> used(n, false);
    std::vector<Strip> cur;
    std::vector<Strip> bestStrips;
    bool improved = false;
    std::size_t nodes = 0;

    auto lineLength = [&](std::size_t seed, Axis axis) -> int {
        auto const& stepPos = (axis == Axis::U) ? posX : posY;
        auto const& stepNeg = (axis == Axis::U) ? negX : negY;
        int const maxC = (axis == Axis::U) ? maxCellsU : maxCellsV;
        int len = 1;
        int p = stepNeg[seed];
        while (p >= 0 && !used[p] && len < maxC) {
            ++len;
            p = stepNeg[p];
        }
        p = stepPos[seed];
        while (p >= 0 && !used[p] && len < maxC) {
            ++len;
            p = stepPos[p];
        }
        return len;
    };

    auto search = [&](auto& self, std::size_t remCount) -> void {
        if (nodes >= kExactSearchNodeBudget) return;
        ++nodes;
        if (remCount == 0) {
            if (cur.size() < best.size() && (!improved || cur.size() < bestStrips.size())) {
                bestStrips = cur;
                improved = true;
            }
            return;
        }
        std::size_t const bestLimit = improved ? bestStrips.size() : best.size();
        std::size_t const lb = (remCount + maxStripCells - 1) / maxStripCells;
        if (cur.size() + lb >= bestLimit) return;

        // Pick an exposed corner cell prioritizing minimum branching (MRV).
        int seed = -1;
        int bestMinLen = std::numeric_limits<int>::max();
        int bestSumLen = std::numeric_limits<int>::max();
        int fallbackSeed = -1;
        for (std::size_t i = 0; i < n; ++i) {
            if (used[i]) continue;
            if (fallbackSeed < 0) fallbackSeed = static_cast<int>(i);
            bool const openU = (posX[i] < 0 || used[posX[i]]) || (negX[i] < 0 || used[negX[i]]);
            bool const openV = (posY[i] < 0 || used[posY[i]]) || (negY[i] < 0 || used[negY[i]]);
            if (openU && openV) {
                int const lu = lineLength(i, Axis::U);
                int const lv = lineLength(i, Axis::V);
                int const minL = std::min(lu, lv);
                int const sumL = lu + lv;
                if (minL < bestMinLen || (minL == bestMinLen && sumL < bestSumLen)) {
                    bestMinLen = minL;
                    bestSumLen = sumL;
                    seed = static_cast<int>(i);
                    if (minL == 1 && sumL <= 2) break;
                }
            }
        }
        if (seed < 0) seed = fallbackSeed;
        if (seed < 0) return;

        auto buildLine = [&](Axis axis) -> std::vector<std::size_t> {
            auto const& stepPos = (axis == Axis::U) ? posX : posY;
            auto const& stepNeg = (axis == Axis::U) ? negX : negY;
            int const maxC = (axis == Axis::U) ? maxCellsU : maxCellsV;

            std::vector<std::size_t> negWalk;
            int p = stepNeg[seed];
            while (p >= 0 && !used[p] && static_cast<int>(negWalk.size()) + 1 < maxC) {
                negWalk.push_back(static_cast<std::size_t>(p));
                p = stepNeg[p];
            }
            std::vector<std::size_t> seq(negWalk.rbegin(), negWalk.rend());
            seq.push_back(static_cast<std::size_t>(seed));
            p = stepPos[seed];
            while (p >= 0 && !used[p] && static_cast<int>(seq.size()) < maxC) {
                seq.push_back(static_cast<std::size_t>(p));
                p = stepPos[p];
            }
            return seq;
        };

        auto runU = buildLine(Axis::U);
        auto runV = buildLine(Axis::V);

        std::array<std::pair<Axis, std::vector<std::size_t>>, 2> branches{{
            {Axis::U, std::move(runU)},
            {Axis::V, std::move(runV)},
        }};
        if (branches[1].second.size() > branches[0].second.size()) {
            std::swap(branches[0], branches[1]);
        }

        for (std::size_t b = 0; b < 2; ++b) {
            if (b == 1 && branches[0].second.size() == 1 && branches[1].second.size() == 1) break;
            auto const& members = branches[b].second;
            for (std::size_t m : members) used[m] = true;
            cur.push_back({branches[b].first, members});
            self(self, remCount - members.size());
            cur.pop_back();
            for (std::size_t m : members) used[m] = false;
            if (nodes >= kExactSearchNodeBudget) break;
        }
    };

    search(search, n);
    if (!improved) return best;

    std::vector<LocalItem> result;
    result.reserve(bestStrips.size());
    for (auto const& s : bestStrips) {
        LocalItem acc = items[s.members.front()];
        for (std::size_t k = 1; k < s.members.size(); ++k) {
            acc = mergeTwoItems(std::move(acc), items[s.members[k]], s.axis);
        }
        result.push_back(std::move(acc));
    }
    return result;
}

std::vector<LocalItem> solveAxis1DComponent(
    std::vector<LocalItem> const& items,
    bool hasOverlap,
    OptimizerOptions const& opts
) {
    auto best = mergeIterative1D(items, Axis::U, opts);
    auto vu = mergeIterative1D(items, Axis::V, opts);
    if (vu.size() < best.size()) best = std::move(vu);

    if (hasOverlap) return best;

    if (items.size() <= 320) {
        auto gLongest = greedyRuns1D(items, false, opts);
        if (gLongest.size() < best.size()) best = std::move(gLongest);

        auto gCorner = greedyRuns1D(items, true, opts);
        if (gCorner.size() < best.size()) best = std::move(gCorner);
    }

    if (items.size() <= 48) {
        best = exactStripCoverAxis1D(items, std::move(best), opts);
    }

    return best;
}

// Marks Axis1D objects that form a 90-degree L-elbow corner (one U neighbor XOR side,
// one V neighbor XOR side, and empty inner diagonal) so bordered corners remain 1x1.
// Runs in O(N log N) per compatibilityKey group.
std::vector<bool> detectElbowCorners(
    std::vector<ObjectRecord> const& records,
    OptimizerOptions const& opts
) {
    std::vector<bool> isCorner(records.size(), false);
    if (!opts.preserveCorners) return isCorner;

    std::map<std::string, std::vector<std::size_t>> byKey;
    for (std::size_t i = 0; i < records.size(); ++i) {
        auto const& rec = records[i];
        if (!rec.isEligible) continue;
        byKey[rec.compatibilityKey].push_back(i);
    }

    for (auto const& [key, group] : byKey) {
        if (group.size() < 3) continue;
        float const baseRot = records[group.front()].rotation;
        Axes const ax = localAxes(baseRot);

        struct Proj {
            std::size_t idx = 0;
            float u = 0.f;
            float v = 0.f;
            float spanU = 30.f;
            float spanV = 30.f;
        };
        std::vector<Proj> pts;
        pts.reserve(group.size());
        for (std::size_t idx : group) {
            auto const& r = records[idx];
            bool const swapped = std::abs(r.baseWidth - r.baseHeight) <= 0.2f &&
                                 isOrthogonalRotationDelta(r.rotation, baseRot) &&
                                 (quarterTurnOffset(r.rotation, baseRot) % 2 == 1);
            pts.push_back({
                idx,
                r.x * ax.ux + r.y * ax.uy,
                r.x * ax.vx + r.y * ax.vy,
                swapped ? (r.baseHeight * std::abs(r.scaleY)) : (r.baseWidth * std::abs(r.scaleX)),
                swapped ? (r.baseWidth * std::abs(r.scaleX)) : (r.baseHeight * std::abs(r.scaleY)),
            });
        }

        // Index sorted by V (for horizontal neighbor queries) and by U (for vertical & diagonal queries).
        std::vector<std::size_t> byV(pts.size());
        std::vector<std::size_t> byU(pts.size());
        std::iota(byV.begin(), byV.end(), 0);
        std::iota(byU.begin(), byU.end(), 0);
        std::sort(byV.begin(), byV.end(), [&](std::size_t a, std::size_t b) {
            if (pts[a].v != pts[b].v) return pts[a].v < pts[b].v;
            return pts[a].u < pts[b].u;
        });
        std::sort(byU.begin(), byU.end(), [&](std::size_t a, std::size_t b) {
            if (pts[a].u != pts[b].u) return pts[a].u < pts[b].u;
            return pts[a].v < pts[b].v;
        });

        float maxHalfU = 15.f;
        for (auto const& p : pts) maxHalfU = std::max(maxHalfU, p.spanU * 0.5f);

        auto pointCovered = [&](float targetU, float targetV) -> bool {
            float const minSearchU = targetU - maxHalfU - opts.posTolerance;
            auto it = std::lower_bound(byU.begin(), byU.end(), minSearchU, [&](std::size_t i, float val) {
                return pts[i].u < val;
            });
            for (; it != byU.end(); ++it) {
                auto const& q = pts[*it];
                if (q.u - q.spanU * 0.5f > targetU + opts.posTolerance) break;
                if (std::abs(q.u - targetU) <= q.spanU * 0.5f + opts.posTolerance &&
                    std::abs(q.v - targetV) <= q.spanV * 0.5f + opts.posTolerance) {
                    return true;
                }
            }
            return false;
        };

        float const tol = opts.posTolerance;
        for (std::size_t pi = 0; pi < pts.size(); ++pi) {
            auto const& p = pts[pi];
            auto const& rec = records[p.idx];
            if (rec.stretch != StretchCapability::Axis1D) continue;

            bool hasPosU = false, hasNegU = false;
            bool hasPosV = false, hasNegV = false;
            float posUSpan = 0.f, negUSpan = 0.f;
            float posVSpan = 0.f, negVSpan = 0.f;

            // Horizontal neighbors (dv ~= 0)
            auto itV = std::lower_bound(byV.begin(), byV.end(), p.v - tol, [&](std::size_t i, float val) {
                return pts[i].v < val;
            });
            for (; itV != byV.end(); ++itV) {
                auto const& q = pts[*itV];
                if (q.v > p.v + tol) break;
                if (q.idx == p.idx) continue;
                if (approxEqual(q.spanV, p.spanV, tol)) {
                    float const du = q.u - p.u;
                    float const expected = (p.spanU + q.spanU) * 0.5f;
                    if (approxEqual(du, expected, tol)) {
                        hasPosU = true;
                        posUSpan = q.spanU;
                    } else if (approxEqual(du, -expected, tol)) {
                        hasNegU = true;
                        negUSpan = q.spanU;
                    }
                }
            }

            // Vertical neighbors (du ~= 0)
            auto itU = std::lower_bound(byU.begin(), byU.end(), p.u - tol, [&](std::size_t i, float val) {
                return pts[i].u < val;
            });
            for (; itU != byU.end(); ++itU) {
                auto const& q = pts[*itU];
                if (q.u > p.u + tol) break;
                if (q.idx == p.idx) continue;
                if (approxEqual(q.spanU, p.spanU, tol)) {
                    float const dv = q.v - p.v;
                    float const expected = (p.spanV + q.spanV) * 0.5f;
                    if (approxEqual(dv, expected, tol)) {
                        hasPosV = true;
                        posVSpan = q.spanV;
                    } else if (approxEqual(dv, -expected, tol)) {
                        hasNegV = true;
                        negVSpan = q.spanV;
                    }
                }
            }

            bool const singleU = (hasPosU != hasNegU);
            bool const singleV = (hasPosV != hasNegV);
            if (!singleU || !singleV) continue;

            // Check whether the inner diagonal corner just beyond p's adjacent edges is occupied.
            float const probeStepU = std::min(p.spanU, hasPosU ? posUSpan : negUSpan) * 0.5f;
            float const probeStepV = std::min(p.spanV, hasPosV ? posVSpan : negVSpan) * 0.5f;
            float const diagU = p.u + (hasPosU ? (p.spanU * 0.5f + probeStepU) : -(p.spanU * 0.5f + probeStepU));
            float const diagV = p.v + (hasPosV ? (p.spanV * 0.5f + probeStepV) : -(p.spanV * 0.5f + probeStepV));
            if (!pointCovered(diagU, diagV)) {
                isCorner[p.idx] = true;
            }
        }
    }

    return isCorner;
}

std::string formatNumber(float value) {
    std::array<char, 64> buf{};
    auto const res = std::to_chars(buf.data(), buf.data() + buf.size(), value,
                                   std::chars_format::fixed, 4);
    if (res.ec != std::errc{}) return "0";
    std::string out(buf.data(), res.ptr);
    if (auto dot = out.find('.'); dot != std::string::npos) {
        while (!out.empty() && out.back() == '0') out.pop_back();
        if (!out.empty() && out.back() == '.') out.pop_back();
    }
    if (out == "-0") out = "0";
    return out;
}

} // namespace

bool extractSaveXY(std::string_view saveString, float& outX, float& outY) {
    while (!saveString.empty() && (saveString.back() == ';' || saveString.back() == ',')) {
        saveString.remove_suffix(1);
    }
    bool hasX = false;
    bool hasY = false;
    std::size_t start = 0;
    std::vector<std::string_view> tokens;
    while (start <= saveString.size()) {
        auto const comma = saveString.find(',', start);
        if (comma == std::string_view::npos) {
            tokens.push_back(saveString.substr(start));
            break;
        }
        tokens.push_back(saveString.substr(start, comma - start));
        start = comma + 1;
    }
    for (std::size_t i = 0; i + 1 < tokens.size(); i += 2) {
        if (tokens[i] == "2" || tokens[i] == "3") {
            std::string tmp(tokens[i + 1]);
            char* end = nullptr;
            float const parsed = std::strtof(tmp.c_str(), &end);
            if (end != tmp.c_str() && std::isfinite(parsed)) {
                if (tokens[i] == "2") {
                    outX = parsed;
                    hasX = true;
                } else {
                    outY = parsed;
                    hasY = true;
                }
            }
        }
    }
    return hasX && hasY;
}

std::string rewriteObjectTransform(
    std::string_view saveString,
    float x,
    float y,
    float scaleX,
    float scaleY
) {
    while (!saveString.empty() && (saveString.back() == ';' || saveString.back() == ',')) {
        saveString.remove_suffix(1);
    }
    std::vector<std::string_view> tokens;
    std::size_t start = 0;
    while (start <= saveString.size()) {
        auto const comma = saveString.find(',', start);
        if (comma == std::string_view::npos) {
            tokens.push_back(saveString.substr(start));
            break;
        }
        tokens.push_back(saveString.substr(start, comma - start));
        start = comma + 1;
    }

    std::string out;
    out.reserve(saveString.size() + 48);
    for (std::size_t i = 0; i + 1 < tokens.size(); i += 2) {
        auto const key = tokens[i];
        if (key == "2" || key == "3" || key == "32" || key == "128" || key == "129") {
            continue;
        }
        if (!out.empty()) out += ',';
        out += key;
        out += ',';
        out += tokens[i + 1];
    }

    auto appendKV = [&](char const* k, float v) {
        if (!out.empty()) out += ',';
        out += k;
        out += ',';
        out += formatNumber(v);
    };
    appendKV("2", x);
    appendKV("3", y);
    appendKV("128", scaleX);
    appendKV("129", scaleY);
    return out;
}

OptimizationPlan planStructureOptimization(
    std::vector<ObjectRecord> const& records,
    OptimizerOptions const& options
) {
    OptimizationPlan plan;
    plan.inputCount = records.size();

    auto const elbowCorners = detectElbowCorners(records, options);

    struct Bucket {
        float baseRot = 0.f;
        bool hasBaseRot = false;
        std::vector<LocalItem> items;
    };
    std::map<std::string, Bucket> buckets;

    for (std::size_t i = 0; i < records.size(); ++i) {
        auto const& rec = records[i];
        if (rec.isSolid) ++plan.solidInputCount;
        else ++plan.decoInputCount;

        if (!rec.isEligible) {
            ++plan.excludedCount;
            continue;
        }
        if (rec.stretch == StretchCapability::None || (options.preserveCorners && elbowCorners[i])) {
            ++plan.preservedCornerCount;
            plan.preservedCorners.push_back(i);
            continue;
        }

        ++plan.eligibleCount;
        auto& b = buckets[rec.compatibilityKey];
        if (!b.hasBaseRot) {
            b.baseRot = rec.rotation;
            b.hasBaseRot = true;
        }

        Axes const ax = localAxes(b.baseRot);
        bool const swappedAxes = std::abs(rec.baseWidth - rec.baseHeight) <= 0.2f &&
                                 isOrthogonalRotationDelta(rec.rotation, b.baseRot) &&
                                 (quarterTurnOffset(rec.rotation, b.baseRot) % 2 == 1);

        float const maxScale = effectiveMaxScale(rec, options);
        float const maxSpan = effectiveMaxSpan(rec, options, maxScale);
        float const localBaseU = swappedAxes ? rec.baseHeight : rec.baseWidth;
        float const localBaseV = swappedAxes ? rec.baseWidth : rec.baseHeight;
        float const spanU = localBaseU * std::abs(swappedAxes ? rec.scaleY : rec.scaleX);
        float const spanV = localBaseV * std::abs(swappedAxes ? rec.scaleX : rec.scaleY);

        LocalItem item;
        item.recordIdx = i;
        item.sources = {i};
        item.u = rec.x * ax.ux + rec.y * ax.uy;
        item.v = rec.x * ax.vx + rec.y * ax.vy;
        item.spanU = spanU;
        item.spanV = spanV;
        item.scaleX = spanU / std::max(localBaseU, 1.f);
        item.scaleY = spanV / std::max(localBaseV, 1.f);
        item.baseWidth = localBaseU;
        item.baseHeight = localBaseV;
        item.stretch = (!options.merge2DFills && rec.stretch == StretchCapability::Uniform2D)
            ? StretchCapability::Axis1D
            : rec.stretch;
        item.isSolid = rec.isSolid;
        item.maxScale = maxScale;
        item.maxSpan = maxSpan;

        b.items.push_back(std::move(item));
    }

    std::size_t solidReduced = 0;
    std::size_t decoReduced = 0;

    for (auto& [key, bucket] : buckets) {
        if (bucket.items.size() < 2) continue;
        Axes const ax = localAxes(bucket.baseRot);

        auto components = splitConnectedComponents(bucket.items, options.posTolerance);

        for (auto& comp : components) {
            if (comp.items.size() < 2) continue;

            bool allUniform2D = options.merge2DFills;
            bool allBiAxis1D = true;
            for (auto const& item : comp.items) {
                if (item.stretch != StretchCapability::Uniform2D) allUniform2D = false;
                bool const biAxis = (item.stretch == StretchCapability::Axis1D) ||
                                    (!options.merge2DFills && item.stretch == StretchCapability::Uniform2D);
                if (!biAxis) allBiAxis1D = false;
            }

            std::vector<LocalItem> best;
            if (allUniform2D && !comp.hasOverlap) {
                best = solveUniform2DComponent(comp.items, options);
            } else if (allUniform2D) {
                auto uv = mergeIterative1D(comp.items, Axis::U, options);
                auto vu = mergeIterative1D(comp.items, Axis::V, options);
                best = uv.size() <= vu.size() ? std::move(uv) : std::move(vu);
            } else if (allBiAxis1D) {
                best = solveAxis1DComponent(comp.items, comp.hasOverlap, options);
            } else {
                auto uv = mergeIterative1D(comp.items, Axis::U, options);
                auto vu = mergeIterative1D(comp.items, Axis::V, options);
                best = uv.size() <= vu.size() ? std::move(uv) : std::move(vu);
            }

            if (best.size() >= comp.items.size()) continue;

            for (auto& out : best) {
                if (out.sources.empty()) continue;
                if (out.sources.size() == 1) {
                    // Only emit a 1-source group if a Hanan re-tiling genuinely reshaped it.
                    auto const& orig = records[out.sources.front()];
                    float const origU = orig.x * ax.ux + orig.y * ax.uy;
                    float const origV = orig.x * ax.vx + orig.y * ax.vy;
                    bool const origSwapped = std::abs(orig.baseWidth - orig.baseHeight) <= 0.2f &&
                                             isOrthogonalRotationDelta(orig.rotation, bucket.baseRot) &&
                                             (quarterTurnOffset(orig.rotation, bucket.baseRot) % 2 == 1);
                    float const origSpanU = origSwapped ? (orig.baseHeight * std::abs(orig.scaleY))
                                                        : (orig.baseWidth * std::abs(orig.scaleX));
                    float const origSpanV = origSwapped ? (orig.baseWidth * std::abs(orig.scaleX))
                                                        : (orig.baseHeight * std::abs(orig.scaleY));
                    if (approxEqual(out.u, origU, options.posTolerance) &&
                        approxEqual(out.v, origV, options.posTolerance) &&
                        approxEqual(out.spanU, origSpanU, options.posTolerance) &&
                        approxEqual(out.spanV, origSpanV, options.posTolerance)) {
                        continue;
                    }
                }

                std::sort(out.sources.begin(), out.sources.end());
                std::size_t keeperIdx = out.sources.front();
                int bestKeeperScore = -1;
                for (std::size_t sIdx : out.sources) {
                    auto const& candRec = records[sIdx];
                    bool const alignedRot = isOrthogonalRotationDelta(candRec.rotation, bucket.baseRot) &&
                                            quarterTurnOffset(candRec.rotation, bucket.baseRot) == 0;
                    int score = (alignedRot ? 4 : 0) +
                                (!candRec.flipX ? 1 : 0) +
                                (!candRec.flipY ? 1 : 0);
                    if (score > bestKeeperScore) {
                        bestKeeperScore = score;
                        keeperIdx = sIdx;
                        if (score == 6) break;
                    }
                }

                auto const& keeperRec = records[keeperIdx];
                bool const keeperSwapped = std::abs(keeperRec.baseWidth - keeperRec.baseHeight) <= 0.2f &&
                                           isOrthogonalRotationDelta(keeperRec.rotation, bucket.baseRot) &&
                                           (quarterTurnOffset(keeperRec.rotation, bucket.baseRot) % 2 == 1);

                MergedGroup group;
                group.keeperIndex = keeperIdx;
                group.sourceIndices = std::move(out.sources);
                group.newX = out.u * ax.ux + out.v * ax.vx;
                group.newY = out.u * ax.uy + out.v * ax.vy;
                if (keeperSwapped) {
                    group.newScaleX = out.spanV / std::max(keeperRec.baseWidth, 1.f);
                    group.newScaleY = out.spanU / std::max(keeperRec.baseHeight, 1.f);
                } else {
                    group.newScaleX = out.spanU / std::max(keeperRec.baseWidth, 1.f);
                    group.newScaleY = out.spanV / std::max(keeperRec.baseHeight, 1.f);
                }

                plan.mergedObjectCount += group.sourceIndices.size();
                if (out.isSolid) solidReduced += group.sourceIndices.size() - 1;
                else decoReduced += group.sourceIndices.size() - 1;

                plan.merges.push_back(std::move(group));
            }
        }
    }

    plan.outputCount = plan.inputCount - plan.mergedObjectCount + plan.merges.size();
    plan.solidOutputCount = plan.solidInputCount - solidReduced;
    plan.decoOutputCount = plan.decoInputCount - decoReduced;
    return plan;
}

} // namespace paimon::optimizer
