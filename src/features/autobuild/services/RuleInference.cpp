#include "RuleInference.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace paimon::autobuild {

namespace {

int edgeRole(Links const& links, int direction) {
    if (direction == kUpDirection || direction == kDownDirection) {
        return static_cast<int>(links.open[kRightDirection]) |
               (static_cast<int>(links.open[kLeftDirection]) << 1);
    }
    return static_cast<int>(links.open[kUpDirection]) |
           (static_cast<int>(links.open[kDownDirection]) << 1);
}

std::uint64_t packCell(int x, int y) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
           static_cast<std::uint32_t>(y);
}

int wrapped(int value, int size) {
    int result = value % size;
    return result < 0 ? result + size : result;
}

void addOne(std::vector<Links>& links, int piece, int direction, int neighbour) {
    if (piece < 0 || piece >= static_cast<int>(links.size()) ||
        neighbour < 0 || neighbour >= static_cast<int>(links.size())) {
        return;
    }
    links[piece].side[direction].push_back(neighbour);
}

void connect(std::vector<Links>& links, int piece, int direction, int neighbour) {
    addOne(links, piece, direction, neighbour);
    addOne(links, neighbour, kOppositeDirection[direction], piece);
}

void inferLegacySeams(std::vector<Links>& links, std::vector<bool> const& skip) {
    constexpr size_t kMaxSeams = 32;
    for (int direction = 0; direction < kCardinalDirections; ++direction) {
        std::vector<int> candidates[4];
        std::vector<int> allCandidates;
        for (int piece = 0; piece < static_cast<int>(links.size()); ++piece) {
            if (skip[piece]) continue;
            if (!links[piece].open[kOppositeDirection[direction]]) continue;
            candidates[edgeRole(links[piece], direction)].push_back(piece);
            allCandidates.push_back(piece);
        }

        for (int piece = 0; piece < static_cast<int>(links.size()); ++piece) {
            if (skip[piece]) continue;
            if (!links[piece].open[direction]) continue;

            auto const& sameRole = candidates[edgeRole(links[piece], direction)];
            auto const& choices = sameRole.empty() ? allCandidates : sameRole;
            if (choices.size() <= kMaxSeams) {
                for (int other : choices) connect(links, piece, direction, other);
                continue;
            }

            auto middle = std::lower_bound(choices.begin(), choices.end(), piece);
            size_t center = static_cast<size_t>(middle - choices.begin());
            size_t start = center > kMaxSeams / 2 ? center - kMaxSeams / 2 : 0;
            start = std::min(start, choices.size() - kMaxSeams);
            for (size_t i = start; i < start + kMaxSeams; ++i) {
                connect(links, piece, direction, choices[i]);
            }
        }
    }
}

} // namespace

RuleSet inferRules(Template const& tpl) {
    RuleSet rules;
    if (tpl.pieces.size() > static_cast<size_t>(kMaxTemplateGridCells) ||
        tpl.grids.size() > static_cast<size_t>(kMaxTemplateGrids)) {
        return rules;
    }

    auto& links = rules.pieces;
    std::vector<bool> covered(tpl.pieces.size(), false);
    std::vector<bool> gridState;
    long long totalArea = 0;
    for (size_t sampleIndex = 0; sampleIndex < tpl.grids.size(); ++sampleIndex) {
        auto const& grid = tpl.grids[sampleIndex];
        if (grid.width <= 0 || grid.height <= 0 || grid.cells.empty()) continue;
        long long const area = static_cast<long long>(grid.width) * grid.height;
        if (grid.cells.size() > static_cast<size_t>(area) ||
            area > kMaxTemplateGridCells - totalArea) {
            return {};
        }
        totalArea += area;

        struct CellState {
            int x;
            int y;
            int state;
        };
        std::vector<CellState> cells;
        cells.reserve(static_cast<size_t>(area));
        std::unordered_map<std::uint64_t, int> pieceByCell;
        pieceByCell.reserve(grid.cells.size());
        for (auto const& cell : grid.cells) {
            if (cell.x < 0 || cell.x >= grid.width || cell.y < 0 || cell.y >= grid.height ||
                cell.piece < 0 || cell.piece >= static_cast<int>(tpl.pieces.size())) {
                continue;
            }
            pieceByCell.emplace(packCell(cell.x, cell.y), cell.piece);
        }
        if (pieceByCell.empty()) continue;

        std::unordered_map<std::uint64_t, int> byCell;
        byCell.reserve(static_cast<size_t>(area));
        for (int y = 0; y < grid.height; ++y) {
            for (int x = 0; x < grid.width; ++x) {
                auto const key = packCell(x, y);
                auto piece = pieceByCell.find(key);
                int const pieceId = piece == pieceByCell.end() ? -1 : piece->second;
                int const state = static_cast<int>(links.size());
                links.emplace_back();
                rules.pieceOf.push_back(pieceId);
                rules.sampleOf.push_back(static_cast<int>(sampleIndex));
                rules.borders.push_back(0);
                gridState.push_back(true);
                cells.push_back({x, y, state});
                byCell.emplace(key, state);
                if (pieceId >= 0) {
                    covered[pieceId] = true;
                    std::uint8_t context = 0;
                    for (int direction = 0; direction < kNeighbourDirections; ++direction) {
                        auto const neighbour = pieceByCell.find(packCell(
                            x + kDirectionX[direction], y + kDirectionY[direction]));
                        if (neighbour != pieceByCell.end()) context |= 1 << direction;
                    }
                    rules.observations.push_back(
                        {pieceId, static_cast<int>(sampleIndex), context});
                }
            }
        }

        for (auto const& cell : cells) {
            for (int direction = 0; direction < kNeighbourDirections; ++direction) {
                int x = cell.x + kDirectionX[direction];
                int y = cell.y + kDirectionY[direction];
                if (direction < kCardinalDirections &&
                    (x < 0 || x >= grid.width || y < 0 || y >= grid.height)) {
                    rules.borders[cell.state] |= 1 << direction;
                }
                auto const neighbour = byCell.find(packCell(wrapped(x, grid.width),
                                                            wrapped(y, grid.height)));
                connect(links, cell.state, direction, neighbour->second);
            }
        }
    }

    std::vector<int> stateOfPiece(tpl.pieces.size(), -1);
    for (size_t piece = 0; piece < tpl.pieces.size(); ++piece) {
        if (covered[piece]) continue;
        int state = static_cast<int>(links.size());
        stateOfPiece[piece] = state;
        links.emplace_back();
        rules.pieceOf.push_back(static_cast<int>(piece));
        rules.sampleOf.push_back(-1);
        rules.borders.push_back(0);
        gridState.push_back(false);
    }

    for (size_t piece = 0; piece < tpl.links.size() && piece < stateOfPiece.size(); ++piece) {
        int state = stateOfPiece[piece];
        if (state < 0) continue;
        for (int direction = 0; direction < kNeighbourDirections; ++direction) {
            links[state].open[direction] = tpl.links[piece].open[direction];
            if (direction < kCardinalDirections && tpl.links[piece].open[direction]) {
                rules.borders[state] |= 1 << direction;
            }
            for (int neighbour : tpl.links[piece].side[direction]) {
                if (neighbour < 0 || neighbour >= static_cast<int>(stateOfPiece.size())) continue;
                int neighbourState = stateOfPiece[neighbour];
                if (neighbourState >= 0) connect(links, state, direction, neighbourState);
            }
        }
    }

    if (std::find(gridState.begin(), gridState.end(), false) != gridState.end()) {
        std::fill_n(rules.emptySide, kNeighbourDirections, true);
    }
    inferLegacySeams(links, gridState);

    for (auto& link : links) {
        for (auto& side : link.side) {
            std::sort(side.begin(), side.end());
            side.erase(std::unique(side.begin(), side.end()), side.end());
        }
    }
    return rules;
}

} // namespace paimon::autobuild
