// Bucle de confiabilidad por semilla en muestras sanas.

#include <cmath>
#include <iostream>
#include <vector>

#include "../src/features/autobuild/services/DesignCritic.hpp"
#include "../src/features/autobuild/services/Invention.hpp"
#include "../src/features/autobuild/services/PieceGrid.hpp"
#include "../src/features/autobuild/services/Solver.hpp"

using namespace paimon::autobuild;

namespace {

constexpr int kSeeds = 40;

Options strictNoSmart() {
    Options opts;
    opts.strictRules = true;
    opts.allowGaps = false;
    opts.smartTemplates = false;
    opts.backtracks = 2000;
    return opts;
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

// Muestra sana 4x3: en estricto exige cobertura total.
Template checkerTemplate() {
    std::vector<CapturedObject> objects;
    char const* saves[2] = {"1,1888,2,0,3,0", "1,1889,2,15,3,15"};
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 4; ++x) {
            CapturedObject object;
            object.objectId = 1888 + (x + y) % 2;
            object.dx = x * 30.f;
            object.dy = y * 30.f;
            object.save = saves[(x + y) % 2];
            objects.push_back(std::move(object));
        }
    }
    return waveFromObjects(std::move(objects), 30.f);
}

std::vector<int> pieceSequence(std::vector<Placement> const& placements) {
    std::vector<int> out;
    for (auto const& placement : placements) out.push_back(placement.piece);
    return out;
}

bool waveLoopIsReliable() {
    auto tpl = checkerTemplate();
    if (tpl.pieces.size() != 2) {
        std::cout << "wave-loop: muestra mal capturada\n";
        return false;
    }
    auto opts = strictNoSmart();
    auto targets = rectangle(6, 4);
    bool pass = true;
    double min = 100.0, sum = 0.0, max = 0.0;
    DesignScore best;
    bool haveBest = false;
    for (int seed = 1; seed <= kSeeds; ++seed) {
        SolveStats a, b;
        auto first = solveWave(tpl, opts, targets, seed, a);
        auto second = solveWave(tpl, opts, targets, seed, b);
        pass = pass && pieceSequence(first) == pieceSequence(second);
        pass = pass && !a.budgetExceeded && a.forced == 0 && a.gaps == 0 &&
               static_cast<int>(first.size()) == 24;
        auto score = scoreDesign(tpl, first, a, opts);
        for (auto const& placement : first) {
            for (auto const& object : tpl.pieces[placement.piece].objects) {
                pass = pass && !isGameplayLocked(object);
            }
        }
        min = std::min(min, score.total);
        sum += score.total;
        max = std::max(max, score.total);
        if (!haveBest || isBetter(score, best)) {
            best = score;
            haveBest = true;
        }
        auto firstScore = scoreDesign(tpl, first, a, opts);
        pass = pass && !isBetter(firstScore, best);
    }
    std::cout << "wave-loop: seeds=" << kSeeds << " score min=" << min
              << " media=" << sum / kSeeds << " max=" << max << '\n';
    return pass && min > 50.0;
}

bool smartLoopIsReliable() {
    auto tpl = checkerTemplate();
    auto opts = strictNoSmart();
    opts.smartTemplates = true;
    auto targets = rectangle(5, 5);
    bool pass = true;
    double min = 100.0, sum = 0.0;
    for (int seed = 1; seed <= kSeeds; ++seed) {
        SolveStats a;
        auto placements = solveWave(tpl, opts, targets, seed, a);
        pass = pass && !a.budgetExceeded && a.forced == 0 &&
               static_cast<int>(placements.size()) == 25;
        auto score = scoreDesign(tpl, placements, a, opts);
        min = std::min(min, score.total);
        sum += score.total;
    }
    std::cout << "smart-loop: seeds=" << kSeeds << " min=" << min
              << " media=" << sum / kSeeds << '\n';
    return pass && min > 50.0;
}

bool stampLoopIsReliable() {
    Template tpl;
    tpl.mode = Mode::Stamp;
    tpl.pieces.resize(2);
    tpl.pieces[0].weight = 1;
    tpl.pieces[0].width = 60.f;
    tpl.pieces[0].height = 60.f;
    tpl.pieces[1].weight = 3;
    tpl.pieces[1].width = 60.f;
    tpl.pieces[1].height = 60.f;
    Options opts;
    std::vector<Target> targets = {{{0.f, 0.f}}, {{200.f, 0.f}}, {{400.f, 50.f}}};
    bool pass = true;
    for (int seed = 1; seed <= kSeeds; ++seed) {
        SolveStats a, b;
        auto first = solveStamps(tpl, opts, targets, seed, a);
        auto second = solveStamps(tpl, opts, targets, seed, b);
        pass = pass && pieceSequence(first) == pieceSequence(second);
        pass = pass && first.size() == 3 && !a.budgetExceeded;
        auto score = scoreDesign(tpl, first, a, opts);
        pass = pass && score.total > 0.0 && score.coverage == 1.0;
    }
    std::cout << "stamp-loop: seeds=" << kSeeds << " estable\n";
    return pass;
}

bool criticGradesFailures() {
    auto tpl = checkerTemplate();
    auto opts = strictNoSmart();
    SolveStats stats;
    stats.budgetExceeded = true;
    // Vacio o sin presupuesto puntua cero.
    DesignScore empty = scoreDesign(tpl, {}, stats, opts);
    stats.budgetExceeded = false;
    std::vector<Placement> none;
    DesignScore noBudget = scoreDesign(tpl, none, stats, opts);
    // A igual nota ganan menos forzadas y luego menos huecos.
    DesignScore a, b;
    a.total = b.total = 80.0;
    a.forced = 0;
    b.forced = 2;
    bool pass = empty.total == 0.0 && noBudget.total == 0.0 &&
                isBetter(a, b) && !isBetter(b, a);
    std::cout << "critic-failures: pass=" << pass << '\n';
    return pass;
}

} // namespace

int main() {
    bool pass = waveLoopIsReliable();
    pass = smartLoopIsReliable() && pass;
    pass = stampLoopIsReliable() && pass;
    pass = criticGradesFailures() && pass;
    std::cout << (pass ? "CONFIABLE" : "FALLO") << '\n';
    return pass ? 0 : 1;
}
