#include "Solver.hpp"

#include <Geode/loader/Log.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <queue>
#include <random>
#include <unordered_map>

namespace paimon::autobuild {

namespace {

constexpr int DX[4] = {0, 0, 1, -1};
constexpr int DY[4] = {1, -1, 0, 0};
constexpr int OPP[4] = {1, 0, 3, 2};

// Tile 0 means "leave this cell empty"; tile n+1 is piece n.
constexpr int kEmpty = 0;
constexpr long long kTimeBudgetMs = 2500;

using Word = std::uint64_t;

std::uint64_t packCell(int gx, int gy) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(gx)) << 32) |
           static_cast<std::uint32_t>(gy);
}

int gridIndex(float value, float cell) {
    return static_cast<int>(std::floor(value / cell + 0.5f));
}

// The wave keeps one bitset per cell with the tiles still possible there, plus
// the running weight sums the entropy heuristic needs.
struct Wave {
    int cellCount = 0;
    int tiles = 0;
    int words = 0;

    std::vector<Word> domain;      // cellCount * words
    std::vector<int> count;
    std::vector<double> sumWeight;
    std::vector<double> sumWeightLog;
    std::vector<int> collapsed;
    std::vector<double> weight;    // per tile
    std::vector<Word> allowed[4];  // tiles * words
    std::vector<std::array<int, 4>> neighbour;
    std::vector<std::pair<int, int>> trail;

    Word* bitsOf(int cell) { return domain.data() + static_cast<size_t>(cell) * words; }
    Word const* allowedOf(int dir, int tile) const {
        return allowed[dir].data() + static_cast<size_t>(tile) * words;
    }

    bool has(int cell, int tile) const {
        return (domain[static_cast<size_t>(cell) * words + (tile >> 6)] >> (tile & 63)) & 1ULL;
    }

    bool remove(int cell, int tile) {
        Word mask = Word{1} << (tile & 63);
        Word& word = domain[static_cast<size_t>(cell) * words + (tile >> 6)];
        if (!(word & mask)) return false;
        word &= ~mask;
        count[cell]--;
        sumWeight[cell] -= weight[tile];
        sumWeightLog[cell] -= weight[tile] * std::log(weight[tile]);
        trail.emplace_back(cell, tile);
        return true;
    }

    void restoreTo(size_t mark) {
        while (trail.size() > mark) {
            auto [cell, tile] = trail.back();
            trail.pop_back();
            domain[static_cast<size_t>(cell) * words + (tile >> 6)] |= Word{1} << (tile & 63);
            count[cell]++;
            sumWeight[cell] += weight[tile];
            sumWeightLog[cell] += weight[tile] * std::log(weight[tile]);
        }
    }

    // Pin a cell to one tile outside the trail: used when nothing fits and the
    // build has to move on instead of unwinding forever.
    void pin(int cell, int tile) {
        Word* bits = bitsOf(cell);
        for (int w = 0; w < words; ++w) bits[w] = 0;
        bits[tile >> 6] = Word{1} << (tile & 63);
        count[cell] = 1;
        sumWeight[cell] = weight[tile];
        sumWeightLog[cell] = weight[tile] * std::log(weight[tile]);
        collapsed[cell] = tile;
    }

    template <class Fn>
    void forEachTile(int cell, Fn&& fn) const {
        for (int w = 0; w < words; ++w) {
            Word bits = domain[static_cast<size_t>(cell) * words + w];
            while (bits) {
                int tile = w * 64 + std::countr_zero(bits);
                bits &= bits - 1;
                fn(tile);
            }
        }
    }

    double entropy(int cell) const {
        if (sumWeight[cell] <= 0.0 || count[cell] <= 1) return 0.0;
        return std::log(sumWeight[cell]) - sumWeightLog[cell] / sumWeight[cell];
    }
};

void buildAllowed(Wave& wave, Template const& tpl, bool relaxed) {
    for (int d = 0; d < 4; ++d) {
        wave.allowed[d].assign(static_cast<size_t>(wave.tiles) * wave.words, 0);
    }
    auto set = [&](int dir, int tile, int neighbourTile) {
        wave.allowed[dir][static_cast<size_t>(tile) * wave.words + (neighbourTile >> 6)] |=
            Word{1} << (neighbourTile & 63);
    };

    static Links const kNoLinks{};
    for (size_t p = 0; p < tpl.pieces.size(); ++p) {
        int tile = static_cast<int>(p) + 1;
        auto const& links = p < tpl.links.size() ? tpl.links[p] : kNoLinks;
        for (int d = 0; d < 4; ++d) {
            for (int other : links.side[d]) {
                if (other < 0 || other >= static_cast<int>(tpl.pieces.size())) continue;
                set(d, tile, other + 1);
            }
            if (links.open[d]) {
                set(d, tile, kEmpty);
                // Mirror: an empty cell accepts every piece that may border it.
                set(OPP[d], kEmpty, tile);
            }
            // Free mode (or a template with no data for this side at all): the
            // side stops constraining, so a fill larger than the sample keeps
            // going instead of falling back on every inner cell.
            bool noData = links.side[d].empty() && !links.open[d];
            if (noData || (relaxed && links.side[d].empty())) {
                for (int t = 0; t < wave.tiles; ++t) {
                    set(d, tile, t);
                    set(OPP[d], t, tile);
                }
            }
        }
    }
    for (int d = 0; d < 4; ++d) set(d, kEmpty, kEmpty);
}

} // namespace

std::vector<Placement> solveWave(Template const& tpl, Options const& opts,
                                 std::vector<Target> const& targets,
                                 unsigned seed, SolveStats& stats) {
    std::vector<Placement> out;
    if (tpl.pieces.empty() || targets.empty()) return out;

    auto const started = std::chrono::steady_clock::now();
    float const cell = tpl.cell > 0.f ? tpl.cell : 30.f;

    struct Cell {
        int gx = 0;
        int gy = 0;
    };
    std::unordered_map<std::uint64_t, int> byCell;
    std::vector<Cell> cells;
    cells.reserve(targets.size());
    for (auto const& target : targets) {
        int gx = gridIndex(target.pos.x, cell);
        int gy = gridIndex(target.pos.y, cell);
        auto key = packCell(gx, gy);
        if (byCell.find(key) != byCell.end()) continue;
        byCell.emplace(key, static_cast<int>(cells.size()));
        cells.push_back({gx, gy});
    }

    Wave wave;
    wave.cellCount = static_cast<int>(cells.size());
    wave.tiles = static_cast<int>(tpl.pieces.size()) + 1;
    wave.words = (wave.tiles + 63) / 64;

    wave.weight.assign(wave.tiles, 1.0);
    double totalWeight = 0.0;
    for (size_t p = 0; p < tpl.pieces.size(); ++p) {
        double value = std::max(1, tpl.pieces[p].weight);
        wave.weight[p + 1] = value;
        totalWeight += value;
    }
    wave.weight[kEmpty] = std::max(1.0, totalWeight / std::max<size_t>(4, tpl.pieces.size() * 4));

    buildAllowed(wave, tpl, !opts.strictRules);

    wave.domain.assign(static_cast<size_t>(wave.cellCount) * wave.words, 0);
    wave.count.assign(wave.cellCount, 0);
    wave.sumWeight.assign(wave.cellCount, 0.0);
    wave.sumWeightLog.assign(wave.cellCount, 0.0);
    wave.collapsed.assign(wave.cellCount, -1);
    wave.neighbour.assign(wave.cellCount, std::array<int, 4>{{-1, -1, -1, -1}});

    int const firstTile = opts.allowGaps ? kEmpty : 1;
    for (int c = 0; c < wave.cellCount; ++c) {
        for (int t = firstTile; t < wave.tiles; ++t) {
            wave.bitsOf(c)[t >> 6] |= Word{1} << (t & 63);
            wave.count[c]++;
            wave.sumWeight[c] += wave.weight[t];
            wave.sumWeightLog[c] += wave.weight[t] * std::log(wave.weight[t]);
        }
        for (int d = 0; d < 4; ++d) {
            auto found = byCell.find(packCell(cells[c].gx + DX[d], cells[c].gy + DY[d]));
            wave.neighbour[c][d] = found == byCell.end() ? -1 : found->second;
        }
    }

    // Cells on the border of the fill prefer pieces that were captured with a
    // free side there, so edges look like the sample's edges. Skipped when that
    // would leave the cell with nothing to place.
    if (tpl.links.size() == tpl.pieces.size()) {
        std::array<std::vector<Word>, 4> openMask;
        for (int d = 0; d < 4; ++d) {
            openMask[d].assign(wave.words, 0);
            openMask[d][kEmpty >> 6] |= Word{1} << (kEmpty & 63);
            for (size_t p = 0; p < tpl.pieces.size(); ++p) {
                if (!tpl.links[p].open[d]) continue;
                int tile = static_cast<int>(p) + 1;
                openMask[d][tile >> 6] |= Word{1} << (tile & 63);
            }
        }
        for (int c = 0; c < wave.cellCount; ++c) {
            for (int d = 0; d < 4; ++d) {
                if (wave.neighbour[c][d] >= 0) continue;
                bool fits = false;
                for (int w = 0; w < wave.words && !fits; ++w) {
                    fits = (wave.bitsOf(c)[w] & openMask[d][w]) != 0;
                }
                if (!fits) continue;
                for (int w = 0; w < wave.words; ++w) {
                    Word drop = wave.bitsOf(c)[w] & ~openMask[d][w];
                    while (drop) {
                        int tile = w * 64 + std::countr_zero(drop);
                        drop &= drop - 1;
                        wave.remove(c, tile);
                    }
                }
            }
        }
    }
    wave.trail.clear();

    std::mt19937 rng(seed);
    std::vector<Word> support(wave.words, 0);
    std::vector<int> queue;

    auto timeUp = [&] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - started).count() > kTimeBudgetMs;
    };

    auto propagate = [&](int from) {
        queue.clear();
        queue.push_back(from);
        while (!queue.empty()) {
            int current = queue.back();
            queue.pop_back();
            for (int d = 0; d < 4; ++d) {
                int other = wave.neighbour[current][d];
                if (other < 0) continue;
                std::fill(support.begin(), support.end(), 0);
                wave.forEachTile(current, [&](int tile) {
                    Word const* row = wave.allowedOf(d, tile);
                    for (int w = 0; w < wave.words; ++w) support[w] |= row[w];
                });
                bool changed = false;
                for (int w = 0; w < wave.words; ++w) {
                    Word drop = wave.bitsOf(other)[w] & ~support[w];
                    while (drop) {
                        int tile = w * 64 + std::countr_zero(drop);
                        drop &= drop - 1;
                        if (wave.remove(other, tile)) changed = true;
                    }
                }
                if (wave.count[other] == 0) return false;
                if (changed) queue.push_back(other);
            }
        }
        return true;
    };

    // Lowest entropy first, kept in a lazy heap: stale entries are cheaper to
    // re-push than rescanning every cell on each collapse.
    struct Candidate {
        double entropy;
        int cell;
        bool operator>(Candidate const& other) const { return entropy > other.entropy; }
    };
    std::priority_queue<Candidate, std::vector<Candidate>, std::greater<Candidate>> heap;
    std::uniform_real_distribution<double> noise(0.0, 1e-4);

    auto pushCell = [&](int c) {
        if (wave.collapsed[c] < 0) heap.push({wave.entropy(c) + noise(rng), c});
    };
    for (int c = 0; c < wave.cellCount; ++c) pushCell(c);

    // Cells whose domain changed since the last push go back in the heap.
    auto pushTouched = [&](size_t mark) {
        for (size_t i = mark; i < wave.trail.size(); ++i) pushCell(wave.trail[i].first);
    };

    auto pickCell = [&] {
        while (!heap.empty()) {
            auto top = heap.top();
            heap.pop();
            if (wave.collapsed[top.cell] >= 0) continue;
            double current = wave.entropy(top.cell);
            if (current > top.entropy + 1e-6) {
                heap.push({current + noise(rng), top.cell});
                continue;
            }
            return top.cell;
        }
        for (int c = 0; c < wave.cellCount; ++c) {
            if (wave.collapsed[c] < 0) return c;
        }
        return -1;
    };

    auto pickTile = [&](int c) {
        double total = wave.sumWeight[c];
        int chosen = -1;
        if (total <= 0.0) {
            wave.forEachTile(c, [&](int tile) { if (chosen < 0) chosen = tile; });
            return chosen;
        }
        std::uniform_real_distribution<double> dist(0.0, total);
        double roll = dist(rng);
        double accumulated = 0.0;
        wave.forEachTile(c, [&](int tile) {
            if (chosen >= 0) return;
            accumulated += wave.weight[tile];
            if (roll <= accumulated) chosen = tile;
        });
        if (chosen < 0) wave.forEachTile(c, [&](int tile) { chosen = tile; });
        return chosen;
    };

    // Nothing fits here: take the tile that agrees with the most already placed
    // neighbours, heaviest first on a tie.
    auto bestFit = [&](int c) {
        int best = firstTile;
        double bestScore = -1.0;
        for (int tile = firstTile; tile < wave.tiles; ++tile) {
            double score = wave.weight[tile];
            for (int d = 0; d < 4; ++d) {
                int other = wave.neighbour[c][d];
                if (other < 0) continue;
                int otherTile = wave.collapsed[other];
                if (otherTile < 0) continue;
                Word const* row = wave.allowedOf(OPP[d], otherTile);
                if ((row[tile >> 6] >> (tile & 63)) & 1ULL) score += 1000.0;
            }
            if (score > bestScore) {
                bestScore = score;
                best = tile;
            }
        }
        return best;
    };

    struct Decision {
        int cell;
        int tile;
        size_t mark;
    };
    std::vector<Decision> decisions;
    int remaining = wave.cellCount;

    while (remaining > 0) {
        if (timeUp()) {
            stats.timedOut = true;
            break;
        }
        int c = pickCell();
        if (c < 0) break;

        if (wave.count[c] == 0) {
            wave.pin(c, bestFit(c));
            decisions.clear();
            wave.trail.clear();
            stats.forced++;
            remaining--;
            continue;
        }

        size_t mark = wave.trail.size();
        int tile = pickTile(c);
        if (tile < 0) {
            pushCell(c);
            continue;
        }

        std::vector<int> others;
        wave.forEachTile(c, [&](int candidate) {
            if (candidate != tile) others.push_back(candidate);
        });
        for (int candidate : others) wave.remove(c, candidate);

        if (propagate(c)) {
            wave.collapsed[c] = tile;
            decisions.push_back({c, tile, mark});
            pushTouched(mark);
            remaining--;
            continue;
        }

        pushTouched(mark);
        wave.restoreTo(mark);
        wave.remove(c, tile);
        pushCell(c);
        stats.backtracks++;

        if (wave.count[c] > 0) continue;
        if (decisions.empty() || stats.backtracks > opts.backtracks) continue;  // pinned next loop

        auto decision = decisions.back();
        decisions.pop_back();
        pushTouched(decision.mark);
        wave.restoreTo(decision.mark);
        wave.collapsed[decision.cell] = -1;
        remaining++;
        wave.remove(decision.cell, decision.tile);
        pushCell(decision.cell);
    }

    out.reserve(cells.size());
    for (int c = 0; c < wave.cellCount; ++c) {
        int tile = wave.collapsed[c];
        if (tile < 0) {
            tile = wave.count[c] > 0 ? pickTile(c) : bestFit(c);
            stats.forced++;
        }
        if (tile <= kEmpty) {
            stats.gaps++;
            continue;
        }
        Placement placement;
        placement.piece = tile - 1;
        placement.pos = cocos2d::CCPoint{cells[c].gx * cell, cells[c].gy * cell};
        out.push_back(placement);
    }

    stats.cells = wave.cellCount;
    stats.filled = static_cast<int>(out.size());
    stats.ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - started).count();
    geode::log::info("[Autobuild] onda: {} celdas, {} llenas, {} huecos, {} forzadas, "
                     "{} retrocesos, {} ms{}",
                     stats.cells, stats.filled, stats.gaps, stats.forced, stats.backtracks,
                     stats.ms, stats.timedOut ? " (tiempo agotado)" : "");
    return out;
}

std::vector<Placement> solveStamps(Template const& tpl, Options const& opts,
                                   std::vector<Target> const& targets,
                                   unsigned seed, SolveStats& stats) {
    std::vector<Placement> out;
    if (tpl.pieces.empty() || targets.empty()) return out;

    auto const started = std::chrono::steady_clock::now();
    std::mt19937 rng(seed);

    double totalWeight = 0.0;
    for (auto const& piece : tpl.pieces) totalWeight += std::max(1, piece.weight);

    struct Box {
        float minX, minY, maxX, maxY;
    };
    std::unordered_map<std::uint64_t, std::vector<Box>> placedBoxes;
    constexpr float kBucket = 240.f;

    auto overlaps = [&](Box const& box) {
        int x0 = static_cast<int>(std::floor(box.minX / kBucket));
        int x1 = static_cast<int>(std::floor(box.maxX / kBucket));
        int y0 = static_cast<int>(std::floor(box.minY / kBucket));
        int y1 = static_cast<int>(std::floor(box.maxY / kBucket));
        for (int x = x0; x <= x1; ++x) {
            for (int y = y0; y <= y1; ++y) {
                auto bucket = placedBoxes.find(packCell(x, y));
                if (bucket == placedBoxes.end()) continue;
                for (auto const& other : bucket->second) {
                    if (box.minX < other.maxX && box.maxX > other.minX &&
                        box.minY < other.maxY && box.maxY > other.minY) {
                        return true;
                    }
                }
            }
        }
        return false;
    };

    auto remember = [&](Box const& box) {
        int x0 = static_cast<int>(std::floor(box.minX / kBucket));
        int x1 = static_cast<int>(std::floor(box.maxX / kBucket));
        int y0 = static_cast<int>(std::floor(box.minY / kBucket));
        int y1 = static_cast<int>(std::floor(box.maxY / kBucket));
        for (int x = x0; x <= x1; ++x) {
            for (int y = y0; y <= y1; ++y) placedBoxes[packCell(x, y)].push_back(box);
        }
    };

    int previous = -1;
    for (auto const& target : targets) {
        int chosen = -1;
        for (int attempt = 0; attempt < 8 && chosen < 0; ++attempt) {
            std::uniform_real_distribution<double> dist(0.0, totalWeight);
            double roll = dist(rng);
            double accumulated = 0.0;
            int candidate = static_cast<int>(tpl.pieces.size()) - 1;
            for (size_t p = 0; p < tpl.pieces.size(); ++p) {
                accumulated += std::max(1, tpl.pieces[p].weight);
                if (roll <= accumulated) {
                    candidate = static_cast<int>(p);
                    break;
                }
            }
            if (opts.avoidRepeats && candidate == previous && tpl.pieces.size() > 1) continue;

            if (opts.avoidOverlap) {
                auto const& piece = tpl.pieces[candidate];
                Box box{target.pos.x - piece.width / 2.f - 1.f,
                        target.pos.y - piece.height / 2.f - 1.f,
                        target.pos.x + piece.width / 2.f + 1.f,
                        target.pos.y + piece.height / 2.f + 1.f};
                if (overlaps(box)) continue;
                remember(box);
            }
            chosen = candidate;
        }
        if (chosen < 0) {
            stats.gaps++;
            continue;
        }
        previous = chosen;

        Placement placement;
        placement.piece = chosen;
        placement.pos = target.pos;
        out.push_back(placement);
    }

    stats.cells = static_cast<int>(targets.size());
    stats.filled = static_cast<int>(out.size());
    stats.ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - started).count();
    geode::log::info("[Autobuild] sellos: {} destinos, {} colocados, {} saltados, {} ms",
                     stats.cells, stats.filled, stats.gaps, stats.ms);
    return out;
}

} // namespace paimon::autobuild
