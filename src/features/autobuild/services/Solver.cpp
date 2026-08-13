#include "Solver.hpp"
#include "RuleInference.hpp"
#include "SmartTemplateEngine.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <queue>
#include <random>
#include <unordered_map>

namespace paimon::autobuild {

namespace {

// Tile 0 means "leave this cell empty"; the rest are learned pattern states.
constexpr int kEmpty = 0;
constexpr size_t kMaxWaveDomainBytes = 256u * 1024u * 1024u;
constexpr std::uint64_t kMaxPropagationWork = 250'000'000;

using Word = std::uint64_t;

struct AllowedRow {
    bool all = false;
    std::vector<int> tiles;
};

struct TrailEntry {
    int cell = 0;
    int word = 0;
    Word removed = 0;
    int count = 0;
    double weight = 0.0;
    double weightLog = 0.0;
};

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
    std::vector<Word> baseDomain;
    std::vector<int> baseCount;
    std::vector<double> baseWeight;
    std::vector<double> baseWeightLog;
    std::vector<AllowedRow> allowed[kNeighbourDirections];
    std::vector<std::array<int, kNeighbourDirections>> neighbour;
    std::vector<std::uint8_t> boundary;
    std::vector<std::uint8_t> openBoundary;
    std::vector<TrailEntry> trail;

    Word* bitsOf(int cell) { return domain.data() + static_cast<size_t>(cell) * words; }
    bool allows(int direction, int tile, int neighbourTile) const {
        auto const& row = allowed[direction][tile];
        return row.all || std::binary_search(row.tiles.begin(), row.tiles.end(), neighbourTile);
    }

    double biasOf(int cell, int tile) const {
        if (tile == kEmpty) return 1.0;
        auto borders = boundary[cell];
        auto open = openBoundary[tile];
        int matches = std::popcount(static_cast<unsigned>(borders & open));
        int misses = std::popcount(static_cast<unsigned>(borders)) - matches;
        constexpr double matchBias[5] = {1.0, 2.0, 4.0, 8.0, 16.0};
        constexpr double missBias[5] = {1.0, 0.55, 0.3025, 0.166375, 0.1};
        return matchBias[matches] * missBias[misses];
    }

    bool removeMask(int cell, int wordIndex, Word removed) {
        Word& word = domain[static_cast<size_t>(cell) * words + wordIndex];
        removed &= word;
        if (!removed) return false;
        Word const before = word;
        word &= ~removed;

        TrailEntry entry{cell, wordIndex, removed};
        if (before == baseDomain[wordIndex]) {
            entry.count = baseCount[wordIndex];
            entry.weight = baseWeight[wordIndex];
            entry.weightLog = baseWeightLog[wordIndex];
            Word kept = before & ~removed;
            while (kept) {
                int tile = wordIndex * 64 + std::countr_zero(kept);
                kept &= kept - 1;
                entry.count--;
                entry.weight -= weight[tile];
                entry.weightLog -= weight[tile] * std::log(weight[tile]);
            }
        } else {
            Word bits = removed;
            while (bits) {
                int tile = wordIndex * 64 + std::countr_zero(bits);
                bits &= bits - 1;
                entry.count++;
                entry.weight += weight[tile];
                entry.weightLog += weight[tile] * std::log(weight[tile]);
            }
        }
        count[cell] -= entry.count;
        sumWeight[cell] -= entry.weight;
        sumWeightLog[cell] -= entry.weightLog;
        trail.push_back(entry);
        return true;
    }

    bool remove(int cell, int tile) {
        return removeMask(cell, tile >> 6, Word{1} << (tile & 63));
    }

    void restoreTo(size_t mark) {
        while (trail.size() > mark) {
            auto const entry = trail.back();
            trail.pop_back();
            domain[static_cast<size_t>(entry.cell) * words + entry.word] |= entry.removed;
            count[entry.cell] += entry.count;
            sumWeight[entry.cell] += entry.weight;
            sumWeightLog[entry.cell] += entry.weightLog;
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

void buildAllowed(Wave& wave, RuleSet const& rules, bool relaxed) {
    auto const& links = rules.pieces;
    for (int d = 0; d < kNeighbourDirections; ++d) {
        wave.allowed[d].resize(wave.tiles);
    }
    auto add = [&](int direction, int tile, int neighbourTile) {
        wave.allowed[direction][tile].tiles.push_back(neighbourTile);
    };

    for (size_t p = 0; p < links.size(); ++p) {
        int tile = static_cast<int>(p) + 1;
        auto const& pieceLinks = links[p];
        for (int d = 0; d < kNeighbourDirections; ++d) {
            for (int other : pieceLinks.side[d]) {
                if (other < 0 || other >= static_cast<int>(links.size())) continue;
                add(d, tile, other + 1);
            }
            if (pieceLinks.open[d]) {
                add(d, tile, kEmpty);
                // Mirror: an empty cell accepts every piece that may border it.
                add(kOppositeDirection[d], kEmpty, tile);
            }

            bool noData = pieceLinks.side[d].empty() && !pieceLinks.open[d];
            if (noData) {
                wave.allowed[d][tile].all = true;
            } else if (relaxed) {
                wave.allowed[d][tile].all = true;
                add(kOppositeDirection[d], kEmpty, tile);
            }
        }
    }
    for (int d = 0; d < kNeighbourDirections; ++d) {
        if (rules.emptySide[d] || relaxed) add(d, kEmpty, kEmpty);
        for (auto& row : wave.allowed[d]) {
            if (row.all) {
                row.tiles.clear();
                continue;
            }
            std::sort(row.tiles.begin(), row.tiles.end());
            row.tiles.erase(std::unique(row.tiles.begin(), row.tiles.end()), row.tiles.end());
        }
    }
}

} // namespace

std::vector<Placement> solveWave(Template const& tpl, Options const& opts,
                                 std::vector<Target> const& targets,
                                 unsigned seed, SolveStats& stats) {
    std::vector<Placement> out;
    if (tpl.pieces.empty() || targets.empty()) return out;
    if (targets.size() > static_cast<size_t>(kMaxTemplateGridCells)) return out;

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

    auto const rules = inferRules(tpl);
    if (rules.pieces.empty() || rules.pieceOf.size() != rules.pieces.size() ||
        rules.sampleOf.size() != rules.pieces.size() ||
        rules.borders.size() != rules.pieces.size() ||
        rules.pieces.size() > static_cast<size_t>(kMaxTemplateGridCells)) {
        return out;
    }

    std::vector<int> samples;
    for (int sample : rules.sampleOf) {
        if (sample >= 0) samples.push_back(sample);
    }
    std::sort(samples.begin(), samples.end());
    samples.erase(std::unique(samples.begin(), samples.end()), samples.end());

    int preferredSample = -1;
    if (!samples.empty()) {
        std::mt19937 familyRng(seed ^ 0x9e3779b9u);
        std::uniform_int_distribution<size_t> chooseSample(0, samples.size() - 1);
        preferredSample = samples[chooseSample(familyRng)];
    }

    auto stateEligible = [&](size_t state) {
        return preferredSample < 0 || rules.sampleOf[state] == preferredSample;
    };

    Wave wave;
    wave.cellCount = static_cast<int>(cells.size());
    wave.tiles = static_cast<int>(rules.pieces.size()) + 1;
    wave.words = (wave.tiles + 63) / 64;
    size_t const cellCount = static_cast<size_t>(wave.cellCount);
    size_t const wordCount = static_cast<size_t>(wave.words);
    if (cellCount > std::numeric_limits<size_t>::max() / wordCount) return out;
    size_t const domainWords = cellCount * wordCount;
    if (domainWords > kMaxWaveDomainBytes / sizeof(Word)) return out;

    wave.weight.assign(wave.tiles, 1.0);
    std::vector<int> statesPerPiece(tpl.pieces.size(), 0);
    for (size_t state = 0; state < rules.pieceOf.size(); ++state) {
        if (!stateEligible(state)) continue;
        int const piece = rules.pieceOf[state];
        if (piece >= 0 && piece < static_cast<int>(statesPerPiece.size())) {
            statesPerPiece[piece]++;
        }
    }
    double totalWeight = 0.0;
    for (size_t state = 0; state < rules.pieceOf.size(); ++state) {
        int piece = rules.pieceOf[state];
        if (piece < -1 || piece >= static_cast<int>(tpl.pieces.size())) return out;
        double value = piece < 0 ? 1.0
                                 : static_cast<double>(std::max(1, tpl.pieces[piece].weight)) /
                                       std::max(1, statesPerPiece[piece]);
        wave.weight[state + 1] = value;
        if (stateEligible(state)) totalWeight += value;
    }
    wave.weight[kEmpty] = std::max(1.0, totalWeight /
        std::max<size_t>(4, rules.pieces.size() * 4));

    buildAllowed(wave, rules, !opts.strictRules);

    wave.domain.assign(domainWords, 0);
    wave.count.assign(wave.cellCount, 0);
    wave.sumWeight.assign(wave.cellCount, 0.0);
    wave.sumWeightLog.assign(wave.cellCount, 0.0);
    wave.collapsed.assign(wave.cellCount, -1);
    wave.neighbour.assign(wave.cellCount, {});
    for (auto& neighbours : wave.neighbour) neighbours.fill(-1);
    wave.boundary.assign(wave.cellCount, 0);
    wave.openBoundary.assign(wave.tiles, 0);
    for (int tile = 1; tile < wave.tiles; ++tile) {
        wave.openBoundary[tile] = rules.borders[tile - 1];
    }

    wave.baseDomain.assign(wave.words, 0);
    wave.baseCount.assign(wave.words, 0);
    wave.baseWeight.assign(wave.words, 0.0);
    wave.baseWeightLog.assign(wave.words, 0.0);
    std::vector<int> selectableTiles;
    bool freeEmpty = false;
    for (int direction = 0; direction < kNeighbourDirections; ++direction) {
        freeEmpty = freeEmpty || rules.emptySide[direction];
    }
    for (size_t state = 0; state < rules.pieces.size(); ++state) {
        if (!stateEligible(state)) continue;
        auto const& link = rules.pieces[state];
        for (int direction = 0; direction < kNeighbourDirections; ++direction) {
            freeEmpty = freeEmpty || link.open[direction];
        }
    }
    double initialWeight = 0.0;
    double initialWeightLog = 0.0;
    for (int tile = kEmpty; tile < wave.tiles; ++tile) {
        if (tile > kEmpty && !stateEligible(static_cast<size_t>(tile - 1))) continue;
        bool const gap = tile == kEmpty || rules.pieceOf[tile - 1] < 0;
        if (gap && !opts.allowGaps) continue;
        if (tile == kEmpty && !freeEmpty) continue;
        int word = tile >> 6;
        wave.baseDomain[word] |= Word{1} << (tile & 63);
        wave.baseCount[word]++;
        wave.baseWeight[word] += wave.weight[tile];
        wave.baseWeightLog[word] += wave.weight[tile] * std::log(wave.weight[tile]);
        initialWeight += wave.weight[tile];
        initialWeightLog += wave.weight[tile] * std::log(wave.weight[tile]);
        selectableTiles.push_back(tile);
    }
    if (selectableTiles.empty()) return out;
    int initialCount = static_cast<int>(selectableTiles.size());
    for (int c = 0; c < wave.cellCount; ++c) {
        std::copy(wave.baseDomain.begin(), wave.baseDomain.end(), wave.bitsOf(c));
        wave.count[c] = initialCount;
        wave.sumWeight[c] = initialWeight;
        wave.sumWeightLog[c] = initialWeightLog;
        for (int d = 0; d < kNeighbourDirections; ++d) {
            auto found = byCell.find(packCell(cells[c].gx + kDirectionX[d],
                                              cells[c].gy + kDirectionY[d]));
            wave.neighbour[c][d] = found == byCell.end() ? -1 : found->second;
        }
        for (int d = 0; d < kCardinalDirections; ++d) {
            if (wave.neighbour[c][d] < 0) wave.boundary[c] |= 1 << d;
        }
    }
    wave.trail.clear();

    std::mt19937 rng(seed);
    std::vector<Word> support(wave.words, 0);
    std::vector<int> queue;
    std::uint64_t const propagationBudget = std::min(
        kMaxPropagationWork,
        std::max<std::uint64_t>(
            1'000'000,
            static_cast<std::uint64_t>(domainWords) * 12 +
                static_cast<std::uint64_t>(wave.cellCount) * 256 +
                static_cast<std::uint64_t>(std::max(0, opts.backtracks)) * 4096));
    std::uint64_t propagationWork = 0;
    bool workExceeded = false;
    auto spendWork = [&](std::uint64_t amount = 1) {
        if (amount > propagationBudget - propagationWork) {
            workExceeded = true;
            return false;
        }
        propagationWork += amount;
        return true;
    };

    auto propagate = [&](int from) {
        queue.clear();
        queue.push_back(from);
        while (!queue.empty()) {
            int current = queue.back();
            queue.pop_back();
            for (int d = 0; d < kNeighbourDirections; ++d) {
                int other = wave.neighbour[current][d];
                if (other < 0) continue;
                if (!spendWork()) return false;
                std::fill(support.begin(), support.end(), 0);
                bool supportsAll = false;
                wave.forEachTile(current, [&](int tile) {
                    if (supportsAll || !spendWork()) return;
                    auto const& row = wave.allowed[d][tile];
                    if (row.all) {
                        std::fill(support.begin(), support.end(), ~Word{0});
                        supportsAll = true;
                        return;
                    }
                    if (!spendWork(row.tiles.size())) return;
                    for (int neighbourTile : row.tiles) {
                        support[neighbourTile >> 6] |= Word{1} << (neighbourTile & 63);
                    }
                });
                if (workExceeded) return false;
                bool changed = false;
                for (int w = 0; w < wave.words; ++w) {
                    if (!spendWork()) return false;
                    Word drop = wave.bitsOf(other)[w] & ~support[w];
                    if (wave.removeMask(other, w, drop)) changed = true;
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
    std::vector<int> touchedAt(wave.cellCount, 0);
    int touchGeneration = 0;
    auto pushTouched = [&](size_t mark) {
        if (++touchGeneration == std::numeric_limits<int>::max()) {
            std::fill(touchedAt.begin(), touchedAt.end(), 0);
            touchGeneration = 1;
        }
        for (size_t i = mark; i < wave.trail.size(); ++i) {
            int cell = wave.trail[i].cell;
            if (touchedAt[cell] == touchGeneration) continue;
            touchedAt[cell] = touchGeneration;
            pushCell(cell);
        }
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
        double total = 0.0;
        int chosen = -1;
        wave.forEachTile(c, [&](int tile) {
            total += wave.weight[tile] * wave.biasOf(c, tile);
        });
        if (total <= 0.0) {
            wave.forEachTile(c, [&](int tile) { if (chosen < 0) chosen = tile; });
            return chosen;
        }
        std::uniform_real_distribution<double> dist(0.0, total);
        double roll = dist(rng);
        double accumulated = 0.0;
        wave.forEachTile(c, [&](int tile) {
            if (chosen >= 0) return;
            accumulated += wave.weight[tile] * wave.biasOf(c, tile);
            if (roll <= accumulated) chosen = tile;
        });
        if (chosen < 0) wave.forEachTile(c, [&](int tile) { chosen = tile; });
        return chosen;
    };

    // Nothing fits here: take the tile that agrees with the most already placed
    // neighbours, heaviest first on a tie.
    auto bestFit = [&](int c) {
        int best = selectableTiles.front();
        double bestScore = -1.0;
        for (int tile : selectableTiles) {
            double score = wave.weight[tile] * wave.biasOf(c, tile);
            for (int d = 0; d < kNeighbourDirections; ++d) {
                int other = wave.neighbour[c][d];
                if (other < 0) continue;
                int otherTile = wave.collapsed[other];
                if (otherTile < 0) continue;
                if (wave.allows(kOppositeDirection[d], otherTile, tile) &&
                    wave.allows(d, tile, otherTile)) {
                    score += 1000.0;
                }
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
    auto markBudgetExceeded = [&] {
        stats.budgetExceeded = true;
        stats.cells = wave.cellCount;
        stats.ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - started).count();
    };

    while (remaining > 0) {
        int c = pickCell();
        if (c < 0) break;

        if (wave.count[c] == 0) {
            if (opts.strictRules) {
                stats.cells = wave.cellCount;
                stats.ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - started).count();
                return out;
            }
            wave.pin(c, bestFit(c));
            decisions.clear();
            wave.trail.clear();
            size_t const mark = wave.trail.size();
            bool const valid = propagate(c);
            if (workExceeded) {
                markBudgetExceeded();
                return out;
            }
            if (!valid) wave.restoreTo(mark);
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

        for (int w = 0; w < wave.words; ++w) {
            Word keep = w == (tile >> 6) ? Word{1} << (tile & 63) : 0;
            wave.removeMask(c, w, wave.bitsOf(c)[w] & ~keep);
        }

        bool const valid = propagate(c);
        if (workExceeded) {
            markBudgetExceeded();
            return out;
        }
        if (valid) {
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
        if (decisions.empty() || stats.backtracks >= opts.backtracks) continue;

        auto decision = decisions.back();
        decisions.pop_back();
        pushTouched(decision.mark);
        wave.restoreTo(decision.mark);
        wave.collapsed[decision.cell] = -1;
        remaining++;
        wave.remove(decision.cell, decision.tile);
        pushCell(decision.cell);
    }

    for (int c = 0; c < wave.cellCount; ++c) {
        if (wave.collapsed[c] >= 0) continue;
        if (opts.strictRules && wave.count[c] == 0) {
            stats.cells = wave.cellCount;
            stats.ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - started).count();
            return {};
        }
        int tile = wave.count[c] > 0 ? pickTile(c) : bestFit(c);
        wave.pin(c, tile);
        size_t const mark = wave.trail.size();
        bool const valid = propagate(c);
        if (workExceeded) {
            markBudgetExceeded();
            return {};
        }
        if (!valid) {
            wave.restoreTo(mark);
            if (opts.strictRules) {
                stats.cells = wave.cellCount;
                stats.ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - started).count();
                return {};
            }
        }
        stats.forced++;
    }

    std::vector<int> structuralPiece(wave.cellCount, -1);
    for (int c = 0; c < wave.cellCount; ++c) {
        int tile = wave.collapsed[c];
        if (tile <= kEmpty || rules.pieceOf[tile - 1] < 0) continue;
        structuralPiece[c] = rules.pieceOf[tile - 1];
    }

    std::optional<SmartTemplateEngine> smart;
    if (opts.smartTemplates && !rules.observations.empty()) {
        smart.emplace(tpl, rules, preferredSample, opts.rotateVariants, opts.flipVariants);
    }

    out.reserve(cells.size());
    int previousPiece = -1;
    for (int c = 0; c < wave.cellCount; ++c) {
        if (structuralPiece[c] < 0) {
            stats.gaps++;
            continue;
        }

        SmartChoice choice{structuralPiece[c], {}, SmartMatch::Original};
        if (smart && !smart->empty()) {
            std::uint8_t context = 0;
            for (int direction = 0; direction < kNeighbourDirections; ++direction) {
                int const neighbour = wave.neighbour[c][direction];
                if (neighbour >= 0 && structuralPiece[neighbour] >= 0) {
                    context |= static_cast<std::uint8_t>(1 << direction);
                }
            }
            choice = smart->choose(context, structuralPiece[c],
                                   opts.avoidRepeats ? previousPiece : -1, rng);
        }

        switch (choice.match) {
            case SmartMatch::Exact: stats.smartExact++; break;
            case SmartMatch::Remapped: stats.smartRemapped++; break;
            case SmartMatch::Simplified: stats.smartSimplified++; break;
            case SmartMatch::Original: break;
        }

        Placement placement;
        placement.piece = choice.piece;
        placement.pos = Point{cells[c].gx * cell, cells[c].gy * cell};
        placement.transform = choice.transform;
        out.push_back(placement);
        previousPiece = choice.piece;
    }

    stats.cells = wave.cellCount;
    stats.filled = static_cast<int>(out.size());
    stats.ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - started).count();
    return out;
}

std::vector<Placement> solveStamps(Template const& tpl, Options const& opts,
                                   std::vector<Target> const& targets,
                                   unsigned seed, SolveStats& stats) {
    std::vector<Placement> out;
    if (tpl.pieces.empty() || targets.empty()) return out;

    auto const started = std::chrono::steady_clock::now();
    std::mt19937 rng(seed);

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

    auto boxFor = [](Piece const& piece, Target const& target) {
        return Box{target.pos.x - piece.width / 2.f - 1.f,
                   target.pos.y - piece.height / 2.f - 1.f,
                   target.pos.x + piece.width / 2.f + 1.f,
                   target.pos.y + piece.height / 2.f + 1.f};
    };

    struct Candidate {
        int piece = -1;
        Box box{};
    };

    auto choose = [&](std::vector<Candidate> const& candidates) {
        double total = 0.0;
        for (auto const& candidate : candidates) {
            total += std::max(1, tpl.pieces[candidate.piece].weight);
        }
        std::uniform_real_distribution<double> dist(0.0, total);
        double roll = dist(rng);
        double accumulated = 0.0;
        for (auto const& candidate : candidates) {
            accumulated += std::max(1, tpl.pieces[candidate.piece].weight);
            if (roll <= accumulated) return candidate;
        }
        return candidates.back();
    };

    int previous = -1;
    std::vector<Candidate> viable;
    std::vector<Candidate> alternatives;
    viable.reserve(tpl.pieces.size());
    alternatives.reserve(tpl.pieces.size());
    for (auto const& target : targets) {
        viable.clear();
        for (int piece = 0; piece < static_cast<int>(tpl.pieces.size()); ++piece) {
            auto box = boxFor(tpl.pieces[piece], target);
            if (opts.avoidOverlap && overlaps(box)) continue;
            viable.push_back({piece, box});
        }
        if (viable.empty()) {
            stats.gaps++;
            continue;
        }

        alternatives.clear();
        if (opts.avoidRepeats && previous >= 0) {
            for (auto const& candidate : viable) {
                if (candidate.piece != previous) alternatives.push_back(candidate);
            }
        }
        auto const chosen = choose(alternatives.empty() ? viable : alternatives);
        if (opts.avoidOverlap) remember(chosen.box);
        previous = chosen.piece;

        Placement placement;
        placement.piece = chosen.piece;
        placement.pos = target.pos;
        out.push_back(placement);
    }

    stats.cells = static_cast<int>(targets.size());
    stats.filled = static_cast<int>(out.size());
    stats.ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - started).count();
    return out;
}

} // namespace paimon::autobuild
