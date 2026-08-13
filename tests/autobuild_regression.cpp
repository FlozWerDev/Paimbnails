#include <climits>
#include <cmath>
#include <iostream>
#include <vector>

#include "../src/features/autobuild/services/RuleInference.hpp"
#include "../src/features/autobuild/services/Solver.hpp"
#include "../src/features/autobuild/services/SmartTemplateEngine.hpp"

using namespace paimon::autobuild;

namespace {

Template gridTemplate(int width, int height) {
    Template tpl;
    tpl.mode = Mode::Wave;
    tpl.cell = 30.f;
    tpl.pieces.resize(static_cast<size_t>(width * height));
    tpl.links.resize(tpl.pieces.size());

    SampleGrid grid;
    grid.width = width;
    grid.height = height;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            grid.cells.push_back({x, y, y * width + x});
        }
    }
    tpl.grids.push_back(std::move(grid));
    return tpl;
}

std::vector<Target> rectangle(int width, int height) {
    std::vector<Target> targets;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            targets.push_back({{x * 30.f, y * 30.f}});
        }
    }
    return targets;
}

std::vector<int> placedGrid(std::vector<Placement> const& placements, int width, int height) {
    std::vector<int> out(static_cast<size_t>(width * height), -1);
    for (auto const& placement : placements) {
        int x = static_cast<int>(std::round(placement.pos.x / 30.f));
        int y = static_cast<int>(std::round(placement.pos.y / 30.f));
        if (x >= 0 && x < width && y >= 0 && y < height) {
            out[static_cast<size_t>(y * width + x)] = placement.piece;
        }
    }
    return out;
}

Options strictOptions() {
    Options opts;
    opts.strictRules = true;
    opts.allowGaps = false;
    opts.smartTemplates = false;
    opts.backtracks = 2000;
    return opts;
}

bool singleCellRepeats() {
    auto tpl = gridTemplate(1, 1);
    SolveStats stats;
    auto placements = solveWave(tpl, strictOptions(), rectangle(18, 12), 7, stats);
    bool pass = placements.size() == 216 && stats.forced == 0 && stats.gaps == 0;
    std::cout << "single-cell: placed=" << placements.size()
              << " forced=" << stats.forced << '\n';
    return pass;
}

bool complexGridKeepsPhase() {
    constexpr int sampleWidth = 3;
    constexpr int sampleHeight = 2;
    constexpr int outputWidth = 13;
    constexpr int outputHeight = 9;

    auto tpl = gridTemplate(sampleWidth, sampleHeight);
    SolveStats stats;
    auto placements = solveWave(tpl, strictOptions(), rectangle(outputWidth, outputHeight),
                                12345, stats);
    auto grid = placedGrid(placements, outputWidth, outputHeight);
    if (placements.size() != outputWidth * outputHeight || stats.forced != 0) return false;

    int phaseX = grid[0] % sampleWidth;
    int phaseY = grid[0] / sampleWidth;
    for (int y = 0; y < outputHeight; ++y) {
        for (int x = 0; x < outputWidth; ++x) {
            int expected = ((phaseY + y) % sampleHeight) * sampleWidth +
                           ((phaseX + x) % sampleWidth);
            if (grid[static_cast<size_t>(y * outputWidth + x)] != expected) return false;
        }
    }
    std::cout << "complex-grid: phase=" << phaseX << ',' << phaseY
              << " placed=" << placements.size() << '\n';
    return true;
}

bool repeatedPiecesKeepTheirRoles() {
    constexpr int sampleWidth = 3;
    constexpr int sampleHeight = 2;
    constexpr int outputWidth = 19;
    constexpr int outputHeight = 13;
    constexpr int pattern[sampleHeight][sampleWidth] = {{0, 1, 0}, {2, 0, 3}};

    Template tpl;
    tpl.mode = Mode::Wave;
    tpl.cell = 30.f;
    tpl.pieces.resize(4);
    tpl.links.resize(4);
    SampleGrid sample;
    sample.width = sampleWidth;
    sample.height = sampleHeight;
    for (int y = 0; y < sampleHeight; ++y) {
        for (int x = 0; x < sampleWidth; ++x) {
            sample.cells.push_back({x, y, pattern[y][x]});
        }
    }
    tpl.grids.push_back(std::move(sample));

    for (unsigned seed = 1; seed <= 12; ++seed) {
        SolveStats stats;
        auto placements = solveWave(tpl, strictOptions(), rectangle(outputWidth, outputHeight),
                                    seed, stats);
        if (placements.size() != outputWidth * outputHeight || stats.forced != 0) return false;
        auto grid = placedGrid(placements, outputWidth, outputHeight);

        bool matchesPhase = false;
        for (int phaseY = 0; phaseY < sampleHeight && !matchesPhase; ++phaseY) {
            for (int phaseX = 0; phaseX < sampleWidth && !matchesPhase; ++phaseX) {
                bool matches = true;
                for (int y = 0; y < outputHeight && matches; ++y) {
                    for (int x = 0; x < outputWidth; ++x) {
                        int expected = pattern[(phaseY + y) % sampleHeight]
                                              [(phaseX + x) % sampleWidth];
                        if (grid[static_cast<size_t>(y * outputWidth + x)] != expected) {
                            matches = false;
                            break;
                        }
                    }
                }
                matchesPhase = matches;
            }
        }
        if (!matchesPhase) return false;
    }

    std::cout << "repeated-roles: seeds=12 stable\n";
    return true;
}

bool arbitraryCropStaysSolvable() {
    auto tpl = gridTemplate(3, 2);
    SolveStats stats;
    auto placements = solveWave(tpl, strictOptions(), rectangle(2, 7), 91, stats);
    bool pass = placements.size() == 14 && stats.forced == 0;
    std::cout << "crop: placed=" << placements.size() << " forced=" << stats.forced << '\n';
    return pass;
}

bool fullAreaFinishes() {
    auto tpl = gridTemplate(5, 4);
    SolveStats stats;
    auto placements = solveWave(tpl, strictOptions(), rectangle(100, 60), 481, stats);
    bool pass = placements.size() == 6000 && stats.forced == 0 && !stats.budgetExceeded;
    std::cout << "full-area: placed=" << placements.size() << " forced=" << stats.forced
              << " ms=" << stats.ms << '\n';
    return pass;
}

bool largeVocabularyStaysFast() {
    auto tpl = gridTemplate(120, 100);
    SolveStats stats;
    auto placements = solveWave(tpl, strictOptions(), rectangle(100, 60), 1841, stats);
    bool pass = placements.size() == 6000 && stats.forced == 0 && !stats.budgetExceeded;
    std::cout << "large-vocabulary: pieces=" << tpl.pieces.size()
              << " placed=" << placements.size() << " ms=" << stats.ms << '\n';
    return pass;
}

bool sparsePatternKeepsItsSpace() {
    Template tpl;
    tpl.mode = Mode::Wave;
    tpl.cell = 30.f;
    tpl.pieces.resize(3);
    tpl.links.resize(3);
    SampleGrid grid;
    grid.width = 2;
    grid.height = 2;
    grid.cells = {{0, 0, 0}, {1, 0, 1}, {0, 1, 2}};
    tpl.grids.push_back(std::move(grid));

    auto opts = strictOptions();
    opts.allowGaps = true;
    SolveStats stats;
    auto placements = solveWave(tpl, opts, rectangle(20, 20), 918, stats);
    bool pass = stats.filled == 300 && stats.gaps == 100 && stats.forced == 0;
    std::cout << "sparse: placed=" << placements.size() << " gaps=" << stats.gaps
              << " forced=" << stats.forced << '\n';
    return pass;
}

bool sparseSamplesDoNotSplice() {
    Template tpl;
    tpl.mode = Mode::Wave;
    tpl.cell = 30.f;
    tpl.pieces.resize(2);
    tpl.links.resize(2);
    SampleGrid first;
    first.width = 2;
    first.height = 1;
    first.cells = {{0, 0, 0}};
    SampleGrid second;
    second.width = 2;
    second.height = 1;
    second.cells = {{0, 0, 1}};
    tpl.grids.push_back(std::move(first));
    tpl.grids.push_back(std::move(second));

    auto opts = strictOptions();
    opts.allowGaps = true;
    for (unsigned seed = 1; seed <= 24; ++seed) {
        SolveStats stats;
        auto placements = solveWave(tpl, opts, rectangle(40, 1), seed, stats);
        if (placements.size() != 20 || stats.gaps != 20 || stats.forced != 0) return false;
        int const family = placements.front().piece;
        for (auto const& placement : placements) {
            if (placement.piece != family) return false;
        }
    }
    std::cout << "sparse-families: seeds=24 coherent\n";
    return true;
}

bool fullPatternDoesNotInventGaps() {
    auto tpl = gridTemplate(2, 2);
    auto opts = strictOptions();
    opts.allowGaps = true;
    SolveStats stats;
    auto placements = solveWave(tpl, opts, rectangle(12, 8), 412, stats);
    bool pass = placements.size() == 96 && stats.gaps == 0 && stats.forced == 0;
    std::cout << "full-with-gaps: placed=" << placements.size()
              << " gaps=" << stats.gaps << " forced=" << stats.forced << '\n';
    return pass;
}

bool multipleSamplesStayCoherent() {
    Template tpl;
    tpl.mode = Mode::Wave;
    tpl.cell = 30.f;
    tpl.pieces.resize(8);
    tpl.links.resize(8);
    SampleGrid first;
    first.width = 2;
    first.height = 2;
    first.cells = {{0, 0, 0}, {1, 0, 1}, {0, 1, 2}, {1, 1, 3}};
    SampleGrid second;
    second.width = 2;
    second.height = 2;
    second.cells = {{0, 0, 4}, {1, 0, 5}, {0, 1, 6}, {1, 1, 7}};
    tpl.grids.push_back(std::move(first));
    tpl.grids.push_back(std::move(second));

    SolveStats stats;
    auto placements = solveWave(tpl, strictOptions(), rectangle(20, 12), 711, stats);
    bool const upperFamily = !placements.empty() && placements.front().piece >= 4;
    bool pass = placements.size() == 240 && stats.forced == 0;
    for (auto const& placement : placements) {
        pass = pass && (placement.piece >= 4) == upperFamily;
    }
    std::cout << "multi-sample: family=" << (upperFamily ? 2 : 1)
              << " forced=" << stats.forced << '\n';
    return pass;
}

bool disconnectedTargetsKeepOneSample() {
    Template tpl;
    tpl.mode = Mode::Wave;
    tpl.cell = 30.f;
    tpl.pieces.resize(2);
    tpl.links.resize(2);
    SampleGrid first;
    first.width = 1;
    first.height = 1;
    first.cells = {{0, 0, 0}};
    SampleGrid second;
    second.width = 1;
    second.height = 1;
    second.cells = {{0, 0, 1}};
    tpl.grids.push_back(std::move(first));
    tpl.grids.push_back(std::move(second));
    std::vector<Target> targets = {{{0.f, 0.f}}, {{300.f, 0.f}}, {{600.f, 0.f}}};

    for (unsigned seed = 1; seed <= 24; ++seed) {
        SolveStats stats;
        auto placements = solveWave(tpl, strictOptions(), targets, seed, stats);
        if (placements.size() != targets.size()) return false;
        for (auto const& placement : placements) {
            if (placement.piece != placements.front().piece) return false;
        }
    }
    std::cout << "disconnected-family: seeds=24 coherent\n";
    return true;
}

bool inferenceRejectsOversizedSamples() {
    Template tpl;
    tpl.mode = Mode::Wave;
    tpl.pieces.resize(1);
    tpl.links.resize(1);
    SampleGrid first;
    first.width = 60001;
    first.height = 1;
    first.cells = {{0, 0, 0}};
    SampleGrid second;
    second.width = 60000;
    second.height = 1;
    second.cells = {{0, 0, 0}};
    tpl.grids.push_back(std::move(first));
    tpl.grids.push_back(std::move(second));

    auto rules = inferRules(tpl);
    bool pass = rules.pieces.empty();
    std::cout << "sample-budget: " << (pass ? "rejected" : "accepted") << '\n';
    return pass;
}

bool inferenceRejectsMalformedCatalogs() {
    Template tooManyPieces;
    tooManyPieces.pieces.resize(static_cast<size_t>(kMaxTemplateGridCells) + 1);
    bool pass = inferRules(tooManyPieces).pieces.empty();

    Template duplicateCells;
    duplicateCells.pieces.resize(1);
    duplicateCells.links.resize(1);
    SampleGrid grid;
    grid.width = 1;
    grid.height = 1;
    grid.cells = {{0, 0, 0}, {0, 0, 0}};
    duplicateCells.grids.push_back(std::move(grid));
    pass = pass && inferRules(duplicateCells).pieces.empty();
    std::cout << "malformed-budget: " << (pass ? "rejected" : "accepted") << '\n';
    return pass;
}

bool solverRejectsOversizedDomain() {
    auto tpl = gridTemplate(18000, 1);
    SolveStats stats;
    auto placements = solveWave(tpl, strictOptions(), rectangle(120000, 1), 7, stats);
    bool pass = placements.empty() && !stats.budgetExceeded;
    std::cout << "domain-budget: " << (pass ? "rejected" : "accepted") << '\n';
    return pass;
}

bool propagationBudgetIsReported() {
    constexpr int pieces = 500;
    constexpr int familySize = pieces / 2;
    Template tpl;
    tpl.mode = Mode::Wave;
    tpl.cell = 30.f;
    tpl.pieces.resize(pieces);
    tpl.links.resize(pieces);
    for (int piece = 0; piece < pieces; ++piece) {
        int const begin = piece < familySize ? 0 : familySize;
        for (int direction = 0; direction < kNeighbourDirections; ++direction) {
            auto& side = tpl.links[piece].side[direction];
            side.reserve(familySize);
            for (int other = begin; other < begin + familySize; ++other) {
                side.push_back(other);
            }
        }
    }

    auto opts = strictOptions();
    opts.backtracks = 0;
    SolveStats stats;
    auto placements = solveWave(tpl, opts, rectangle(3, 3), 902, stats);
    bool pass = placements.empty() && stats.budgetExceeded;
    std::cout << "propagation-budget: " << (pass ? "reported" : "missed") << '\n';
    return pass;
}

bool legacyTemplateGetsSeams() {
    Template tpl;
    tpl.mode = Mode::Wave;
    tpl.pieces.resize(1);
    tpl.links.resize(1);
    for (int direction = 0; direction < kCardinalDirections; ++direction) {
        tpl.links[0].open[direction] = true;
    }

    SolveStats stats;
    auto placements = solveWave(tpl, strictOptions(), rectangle(10, 5), 33, stats);
    bool pass = placements.size() == 50 && stats.forced == 0;
    std::cout << "legacy: placed=" << placements.size() << " forced=" << stats.forced << '\n';
    return pass;
}

bool strictRulesNeverForceInvalidLinks() {
    Template tpl;
    tpl.mode = Mode::Wave;
    tpl.pieces.resize(1);
    tpl.links.resize(1);
    tpl.links[0].open[kRightDirection] = true;

    auto targets = rectangle(2, 1);
    SolveStats strictStats;
    auto strict = solveWave(tpl, strictOptions(), targets, 44, strictStats);

    auto relaxedOptions = strictOptions();
    relaxedOptions.strictRules = false;
    SolveStats relaxedStats;
    auto relaxed = solveWave(tpl, relaxedOptions, targets, 44, relaxedStats);
    bool pass = strict.empty() && strictStats.forced == 0 && relaxed.size() == 2;
    std::cout << "strict-invalid: rejected=" << strict.empty()
              << " relaxed=" << relaxed.size() << '\n';
    return pass;
}

bool legacyInferenceStaysBounded() {
    Template tpl;
    tpl.mode = Mode::Wave;
    tpl.pieces.resize(12000);
    tpl.links.resize(tpl.pieces.size());
    for (auto& link : tpl.links) {
        for (int direction = 0; direction < kCardinalDirections; ++direction) {
            link.open[direction] = true;
        }
    }

    auto rules = inferRules(tpl);
    size_t seams = 0;
    for (auto const& link : rules.pieces) {
        for (int direction = 0; direction < kCardinalDirections; ++direction) {
            seams += link.side[direction].size();
        }
    }
    bool pass = seams > 0 && seams <= tpl.pieces.size() * kCardinalDirections * 64;
    std::cout << "legacy-bounded: pieces=" << tpl.pieces.size() << " seams=" << seams << '\n';
    return pass;
}

bool fixedSeedIsDeterministic() {
    auto tpl = gridTemplate(4, 3);
    SolveStats firstStats;
    SolveStats secondStats;
    auto targets = rectangle(9, 8);
    auto first = solveWave(tpl, strictOptions(), targets, 8821, firstStats);
    auto second = solveWave(tpl, strictOptions(), targets, 8821, secondStats);
    bool pass = first.size() == second.size();
    for (size_t i = 0; pass && i < first.size(); ++i) {
        pass = first[i].piece == second[i].piece && first[i].pos.x == second[i].pos.x &&
               first[i].pos.y == second[i].pos.y;
    }
    std::cout << "determinism: " << (pass ? "stable" : "changed") << '\n';
    return pass;
}

bool variedGridSizesStaySolvable() {
    int cases = 0;
    for (int sampleWidth = 1; sampleWidth <= 6; ++sampleWidth) {
        for (int sampleHeight = 1; sampleHeight <= 5; ++sampleHeight) {
            int outputWidth = sampleHeight * 2 + 1;
            int outputHeight = sampleWidth + 3;
            auto tpl = gridTemplate(sampleWidth, sampleHeight);
            SolveStats stats;
            auto placements = solveWave(tpl, strictOptions(),
                                        rectangle(outputWidth, outputHeight),
                                        static_cast<unsigned>(sampleWidth * 100 + sampleHeight),
                                        stats);
            if (placements.size() != static_cast<size_t>(outputWidth * outputHeight) ||
                stats.forced != 0 || stats.gaps != 0) {
                return false;
            }
            ++cases;
        }
    }
    std::cout << "varied-grids: cases=" << cases << '\n';
    return true;
}

bool contextTransformsCoverD4() {
    auto const up = static_cast<std::uint8_t>(1 << kUpDirection);
    auto const down = static_cast<std::uint8_t>(1 << kDownDirection);
    auto const right = static_cast<std::uint8_t>(1 << kRightDirection);
    auto const left = static_cast<std::uint8_t>(1 << kLeftDirection);
    auto const topRight = static_cast<std::uint8_t>(1 << 4);
    auto const bottomRight = static_cast<std::uint8_t>(1 << 6);

    bool pass = transformContext(right, {1, false}) == down &&
                transformContext(right, {3, false}) == up &&
                transformContext(right, {0, true}) == left &&
                transformContext(topRight, {1, false}) == bottomRight &&
                contextDistance(up | right, up | right | topRight) == 1 &&
                contextDistance(up, down) == 8;
    std::cout << "smart-d4: " << (pass ? "mapped" : "invalid") << '\n';
    return pass;
}

bool smartCatalogUsesNativeFallbackOrder() {
    Template tpl;
    tpl.pieces.resize(3);
    RuleSet rules;
    auto const up = static_cast<std::uint8_t>(1 << kUpDirection);
    auto const right = static_cast<std::uint8_t>(1 << kRightDirection);
    auto const topRight = static_cast<std::uint8_t>(1 << 4);
    rules.observations = {{0, 0, right}, {1, 1, up}};

    std::mt19937 rng(81);
    SmartTemplateEngine rotated(tpl, rules, 0, true, false);
    auto direct = rotated.choose(right, 2, -1, rng);
    auto remapped = rotated.choose(static_cast<std::uint8_t>(1 << kDownDirection),
                                   2, -1, rng);
    auto familyFirst = rotated.choose(up, 2, -1, rng);

    RuleSet cornerRules;
    cornerRules.observations = {{0, 0, static_cast<std::uint8_t>(up | right | topRight)}};
    SmartTemplateEngine simplified(tpl, cornerRules, 0, false, false);
    auto noCorner = simplified.choose(static_cast<std::uint8_t>(up | right), 2, -1, rng);

    bool pass = direct.piece == 0 && direct.match == SmartMatch::Exact &&
                direct.transform.identity() && remapped.piece == 0 &&
                remapped.match == SmartMatch::Remapped &&
                transformContext(right, remapped.transform) ==
                    static_cast<std::uint8_t>(1 << kDownDirection) &&
                familyFirst.piece == 0 && familyFirst.match == SmartMatch::Remapped &&
                noCorner.piece == 0 &&
                noCorner.match == SmartMatch::Simplified;
    std::cout << "smart-fallbacks: direct=" << static_cast<int>(direct.match)
              << " remap=" << static_cast<int>(remapped.match)
              << " simplified=" << static_cast<int>(noCorner.match) << '\n';
    return pass;
}

bool smartVariantsAvoidImmediateRepeats() {
    Template tpl;
    tpl.pieces.resize(2);
    RuleSet rules;
    auto const context = static_cast<std::uint8_t>(1 << kRightDirection);
    rules.observations = {{0, 0, context}, {1, 0, context}};
    SmartTemplateEngine smart(tpl, rules, 0, false, false);
    std::mt19937 rng(5);
    auto choice = smart.choose(context, 0, 0, rng);
    bool pass = choice.piece == 1 && choice.match == SmartMatch::Exact;
    std::cout << "smart-repeat: piece=" << choice.piece << '\n';
    return pass;
}

bool smartFixedSeedIsDeterministic() {
    auto tpl = gridTemplate(4, 3);
    auto opts = strictOptions();
    opts.smartTemplates = true;
    opts.rotateVariants = true;
    opts.flipVariants = true;
    auto targets = rectangle(24, 16);
    SolveStats firstStats;
    SolveStats secondStats;
    auto first = solveWave(tpl, opts, targets, 7103, firstStats);
    auto second = solveWave(tpl, opts, targets, 7103, secondStats);

    bool pass = first.size() == second.size();
    for (size_t i = 0; pass && i < first.size(); ++i) {
        pass = first[i].piece == second[i].piece &&
               first[i].pos.x == second[i].pos.x && first[i].pos.y == second[i].pos.y &&
               first[i].transform.quarterTurns == second[i].transform.quarterTurns &&
               first[i].transform.flipX == second[i].transform.flipX;
    }
    std::cout << "smart-determinism: " << (pass ? "stable" : "changed") << '\n';
    return pass;
}

bool smartLargeVocabularyStaysFast() {
    auto tpl = gridTemplate(120, 100);
    auto opts = strictOptions();
    opts.smartTemplates = true;
    opts.rotateVariants = true;
    opts.flipVariants = true;
    SolveStats stats;
    auto placements = solveWave(tpl, opts, rectangle(100, 60), 992, stats);
    bool pass = placements.size() == 6000 && !stats.budgetExceeded && stats.forced == 0;
    std::cout << "smart-large: placed=" << placements.size() << " ms=" << stats.ms << '\n';
    return pass;
}

bool smartWaveRotatesSparseReferences() {
    Template tpl;
    tpl.mode = Mode::Wave;
    tpl.cell = 30.f;
    tpl.pieces.resize(2);
    tpl.links.resize(2);
    SampleGrid sample;
    sample.width = 2;
    sample.height = 1;
    sample.cells = {{0, 0, 0}, {1, 0, 1}};
    tpl.grids.push_back(std::move(sample));

    auto opts = strictOptions();
    opts.smartTemplates = true;
    opts.rotateVariants = true;
    opts.flipVariants = false;
    opts.avoidRepeats = false;
    SolveStats stats;
    auto placements = solveWave(tpl, opts, rectangle(1, 2), 129, stats);
    bool pass = placements.size() == 2 && stats.smartRemapped == 2 &&
                stats.smartExact == 0 && stats.smartSimplified == 0;
    for (auto const& placement : placements) {
        pass = pass && !placement.transform.identity();
    }
    std::cout << "smart-wave: placed=" << placements.size()
              << " remapped=" << stats.smartRemapped << '\n';
    return pass;
}

bool stampRepeatIsSoft() {
    Template tpl;
    tpl.mode = Mode::Stamp;
    tpl.pieces.resize(2);
    tpl.pieces[0].weight = 1;
    tpl.pieces[0].width = 100.f;
    tpl.pieces[0].height = 100.f;
    tpl.pieces[1].weight = INT_MAX;

    Options opts;
    opts.avoidOverlap = true;
    opts.avoidRepeats = true;
    std::vector<Target> targets = {{{0.f, 0.f}}, {{10.f, 0.f}}};
    SolveStats stats;
    auto placements = solveStamps(tpl, opts, targets, 5, stats);
    bool pass = placements.size() == 2 && placements[0].piece == 1 &&
                placements[1].piece == 1 && stats.gaps == 0;
    std::cout << "stamp-soft-repeat: placed=" << placements.size()
              << " gaps=" << stats.gaps << '\n';
    return pass;
}

} // namespace

int main() {
    bool pass = singleCellRepeats();
    pass = complexGridKeepsPhase() && pass;
    pass = repeatedPiecesKeepTheirRoles() && pass;
    pass = arbitraryCropStaysSolvable() && pass;
    pass = fullAreaFinishes() && pass;
    pass = largeVocabularyStaysFast() && pass;
    pass = sparsePatternKeepsItsSpace() && pass;
    pass = sparseSamplesDoNotSplice() && pass;
    pass = fullPatternDoesNotInventGaps() && pass;
    pass = multipleSamplesStayCoherent() && pass;
    pass = disconnectedTargetsKeepOneSample() && pass;
    pass = inferenceRejectsOversizedSamples() && pass;
    pass = inferenceRejectsMalformedCatalogs() && pass;
    pass = solverRejectsOversizedDomain() && pass;
    pass = propagationBudgetIsReported() && pass;
    pass = legacyTemplateGetsSeams() && pass;
    pass = strictRulesNeverForceInvalidLinks() && pass;
    pass = legacyInferenceStaysBounded() && pass;
    pass = fixedSeedIsDeterministic() && pass;
    pass = variedGridSizesStaySolvable() && pass;
    pass = contextTransformsCoverD4() && pass;
    pass = smartCatalogUsesNativeFallbackOrder() && pass;
    pass = smartVariantsAvoidImmediateRepeats() && pass;
    pass = smartFixedSeedIsDeterministic() && pass;
    pass = smartLargeVocabularyStaysFast() && pass;
    pass = smartWaveRotatesSparseReferences() && pass;
    pass = stampRepeatIsSoft() && pass;
    return pass ? 0 : 1;
}
