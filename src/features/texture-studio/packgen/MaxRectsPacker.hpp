#pragma once
// BSSF rect packer; (y, x, index) tie-breaks keep output thread-schedule independent. No rotation keeps shelf A/B.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace paimon::texture_studio::packgen {

struct PackRect {
    int w = 0;
    int h = 0;
    int id = -1;  // caller tag, preserved in PackPlacement
};

struct PackPlacement {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int id = -1;
    bool rotated = false;
};

class MaxRectsPacker {
public:
    struct Options {
        int maxSize = 4096;
        int padding = 2;
        bool allowRotate = true;
        bool potOutput = false;  // round atlas up to power-of-two
    };

    explicit MaxRectsPacker() : m_opts() {}
    explicit MaxRectsPacker(Options opts) : m_opts(opts) {}

    struct Result {
        std::vector<PackPlacement> placements;
        int atlasW = 0;
        int atlasH = 0;
        bool fits = false;
    };

    Result pack(std::vector<PackRect> const& rects) {
        Result out;
        out.placements.reserve(rects.size());
        if (rects.empty()) {
            out.atlasW = 0;
            out.atlasH = 0;
            out.fits = true;
            return out;
        }

        // Deterministic input order: taller first, then wider, then id.
        std::vector<std::size_t> order(rects.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            if (rects[a].h != rects[b].h) return rects[a].h > rects[b].h;
            if (rects[a].w != rects[b].w) return rects[a].w > rects[b].w;
            return rects[a].id < rects[b].id;
        });

        // Growing atlas: expand only when the next rect stops fitting; squarer wins, less UV waste.
        int curW = 0, curH = 0;
        for (std::size_t oi : order) {
            int rw = rects[oi].w + m_opts.padding;
            int rh = rects[oi].h + m_opts.padding;
            if (rw <= 0 || rh <= 0) continue;
            if (curW == 0) {
                curW = std::min(m_opts.maxSize, rw);
                curH = std::min(m_opts.maxSize, rh);
                m_free.clear();
                m_free.push_back({0, 0, curW, curH});
            }
            Placement best = findBest(rw, rh);
            while (best.scoreShort == kNoFit) {
                if (!grow(curW, curH, rw, rh)) {
                    out.fits = false;
                    out.atlasW = curW;
                    out.atlasH = curH;
                    return out;
                }
                best = findBest(rw, rh);
            }
            PackPlacement p;
            p.x = best.x;
            p.y = best.y;
            p.w = rects[oi].w;
            p.h = rects[oi].h;
            p.id = rects[oi].id;
            p.rotated = best.rotated;
            out.placements.push_back(p);
            int pw = rw, ph = rh;
            if (best.rotated) std::swap(pw, ph);
            placeRect(best.x, best.y, pw, ph);
        }

        int usedW = 0, usedH = 0;
        for (auto const& p : out.placements) {
            int pw = p.rotated ? p.h : p.w;
            int ph = p.rotated ? p.w : p.h;
            usedW = std::max(usedW, p.x + pw + m_opts.padding);
            usedH = std::max(usedH, p.y + ph + m_opts.padding);
        }
        if (m_opts.potOutput) {
            usedW = potUp(usedW);
            usedH = potUp(usedH);
        }
        out.atlasW = std::max(1, usedW);
        out.atlasH = std::max(1, usedH);
        out.fits = true;
        // Deterministic output order: by id.
        std::sort(out.placements.begin(), out.placements.end(),
                  [](PackPlacement const& a, PackPlacement const& b) { return a.id < b.id; });
        return out;
    }

private:
    struct FreeRect {
        int x, y, w, h;
    };
    struct Placement {
        int x = 0, y = 0;
        bool rotated = false;
        int scoreShort = std::numeric_limits<int>::max();
        int scoreLong = std::numeric_limits<int>::max();
    };
    static constexpr int kNoFit = std::numeric_limits<int>::max();

    Options m_opts;
    std::vector<FreeRect> m_free;

    Placement findBest(int rw, int rh) const {
        Placement best;
        for (auto const& f : m_free) {
            // Without rotation.
            if (rw <= f.w && rh <= f.h) {
                int leftoverH = f.w - rw;
                int leftoverV = f.h - rh;
                int shortFit = std::min(leftoverH, leftoverV);
                int longFit = std::max(leftoverH, leftoverV);
                if (betterScore(shortFit, longFit, f.y, f.x, best)) {
                    best = {f.x, f.y, false, shortFit, longFit};
                }
            }
            // With rotation.
            if (m_opts.allowRotate && rh <= f.w && rw <= f.h) {
                int leftoverH = f.w - rh;
                int leftoverV = f.h - rw;
                int shortFit = std::min(leftoverH, leftoverV);
                int longFit = std::max(leftoverH, leftoverV);
                if (betterScore(shortFit, longFit, f.y, f.x, best)) {
                    best = {f.x, f.y, true, shortFit, longFit};
                }
            }
        }
        return best;
    }

    static bool betterScore(int shortFit, int longFit, int fy, int fx, Placement const& best) {
        if (shortFit != best.scoreShort) return shortFit < best.scoreShort;
        if (longFit != best.scoreLong) return longFit < best.scoreLong;
        if (fy != best.y) return fy < best.y;
        return fx < best.x;
    }

    bool grow(int& curW, int& curH, int rw, int rh) {
        // Right then down; squarer growth wins, either may hit maxSize.
        bool canRight = curW < m_opts.maxSize;
        bool canDown = curH < m_opts.maxSize;
        int growRightW = std::min(m_opts.maxSize, curW * 2);
        int growDownH = std::min(m_opts.maxSize, curH * 2);
        // Score squareness of each candidate.
        auto squareness = [](int w, int h) {
            return w > h ? (w - h) : (h - w);
        };
        bool tryRightFirst = squareness(growRightW, curH) <= squareness(curW, growDownH);
        if (tryRightFirst) {
            if (canRight && expandRight(curW, curH)) return true;
            if (canDown && expandDown(curW, curH)) return true;
        } else {
            if (canDown && expandDown(curW, curH)) return true;
            if (canRight && expandRight(curW, curH)) return true;
        }
        (void)rw;
        (void)rh;
        return false;
    }

    bool expandRight(int& curW, int curH) {
        int newW = std::min(m_opts.maxSize, curW * 2);
        if (newW <= curW) return false;
        m_free.push_back({curW, 0, newW - curW, curH});
        curW = newW;
        return true;
    }

    bool expandDown(int curW, int& curH) {
        int newH = std::min(m_opts.maxSize, curH * 2);
        if (newH <= curH) return false;
        m_free.push_back({0, curH, curW, newH - curH});
        curH = newH;
        return true;
    }

    void placeRect(int x, int y, int w, int h) {
        // Guillotine-split intersecting free rects (shorter axis first), then prune contained.
        std::vector<FreeRect> next;
        next.reserve(m_free.size() + 2);
        for (auto const& f : m_free) {
            if (!intersects(f, x, y, w, h)) {
                next.push_back(f);
                continue;
            }
            // Left slab.
            if (x > f.x) next.push_back({f.x, f.y, x - f.x, f.h});
            // Right slab.
            if (x + w < f.x + f.w) next.push_back({x + w, f.y, f.x + f.w - (x + w), f.h});
            // Top slab (within the horizontal overlap).
            int ox0 = std::max(f.x, x);
            int ox1 = std::min(f.x + f.w, x + w);
            if (y > f.y && ox1 > ox0) next.push_back({ox0, f.y, ox1 - ox0, y - f.y});
            // Bottom slab.
            if (y + h < f.y + f.h && ox1 > ox0)
                next.push_back({ox0, y + h, ox1 - ox0, f.y + f.h - (y + h)});
        }
        m_free.clear();
        for (auto const& r : next) {
            if (r.w > 0 && r.h > 0) m_free.push_back(r);
        }
        pruneFree();
    }

    static bool intersects(FreeRect const& f, int x, int y, int w, int h) {
        return x < f.x + f.w && x + w > f.x && y < f.y + f.h && y + h > f.y;
    }

    static bool containedIn(FreeRect const& a, FreeRect const& b) {
        return a.x >= b.x && a.y >= b.y && a.x + a.w <= b.x + b.w && a.y + a.h <= b.y + b.h;
    }

    void pruneFree() {
        for (std::size_t i = 0; i < m_free.size(); ++i) {
            for (std::size_t j = i + 1; j < m_free.size();) {
                if (containedIn(m_free[i], m_free[j])) {
                    m_free[i] = m_free.back();
                    m_free.pop_back();
                    if (i > 0) {
                        --i;
                    }
                    break;
                } else if (containedIn(m_free[j], m_free[i])) {
                    m_free[j] = m_free.back();
                    m_free.pop_back();
                } else {
                    ++j;
                }
            }
        }
    }

    static int potUp(int v) {
        int p = 1;
        while (p < v) p *= 2;
        return p;
    }
};

}  // namespace paimon::texture_studio::packgen
