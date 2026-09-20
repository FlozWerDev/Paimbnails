#include "DesignCritic.hpp"

#include <algorithm>
#include <cmath>
#include <set>

#include "LevelParse.hpp"
#include "SaveString.hpp"

namespace paimon::autobuild {

namespace {

double clamp01(double value) {
    return std::clamp(value, 0.0, 1.0);
}

} // namespace

DesignScore scoreDesign(Template const& tpl, std::vector<Placement> const& placements,
                        SolveStats const& stats, Options const& opts) {
    DesignScore score;
    score.forced = stats.forced;
    score.gaps = stats.gaps;
    if (stats.budgetExceeded || placements.empty()) return score;

    int const cells = std::max(1, stats.cells);
    score.coverage = clamp01(static_cast<double>(stats.filled) / cells);

    std::set<int> used;
    std::set<int> usedColors;
    for (auto const& placement : placements) {
        if (placement.piece < 0 ||
            placement.piece >= static_cast<int>(tpl.pieces.size())) {
            continue;
        }
        used.insert(placement.piece);
        for (auto const& object : tpl.pieces[placement.piece].objects) {
            collectColorIds(object.save, usedColors);
        }
    }
    int const expected =
        std::max(1, std::min<int>(static_cast<int>(tpl.pieces.size()), cells));
    score.variety = clamp01(static_cast<double>(used.size()) / expected);

    score.palette = 1.0;
    if (!usedColors.empty() && !tpl.colors.empty()) {
        std::set<int> palette;
        for (auto const& channel : parseColorChannels(tpl.colors)) {
            palette.insert(channel.id);
        }
        if (!palette.empty()) {
            int inside = 0;
            for (int id : usedColors) {
                if (palette.count(id)) ++inside;
            }
            score.palette =
                static_cast<double>(inside) / usedColors.size();
        }
    }

    double const forcedRate = static_cast<double>(stats.forced) / cells;
    double gapRate = static_cast<double>(stats.gaps) / cells;
    if (opts.allowGaps) gapRate *= 0.25;  // pedidos: pesan menos, no son fallo
    score.clean = clamp01(1.0 - forcedRate - gapRate);

    score.total = 100.0 * (0.55 * score.coverage + 0.20 * score.variety +
                           0.15 * score.palette + 0.10 * score.clean);
    return score;
}

bool isBetter(DesignScore const& a, DesignScore const& b) {
    if (std::abs(a.total - b.total) > 1e-9) return a.total > b.total;
    if (a.forced != b.forced) return a.forced < b.forced;
    return a.gaps < b.gaps;
}

} // namespace paimon::autobuild
