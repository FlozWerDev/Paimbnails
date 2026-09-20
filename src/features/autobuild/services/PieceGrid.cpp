#include "PieceGrid.hpp"

#include "SaveString.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <set>
#include <unordered_map>

namespace paimon::autobuild {

namespace {

std::uint64_t packCell(int gx, int gy) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(gx)) << 32) |
           static_cast<std::uint32_t>(gy);
}

int gridIndex(float value, float cell) {
    return static_cast<int>(std::floor(value / cell + 0.5f));
}

float quant(float value) {
    float rounded = std::round(value * 100.f) / 100.f;
    return rounded == 0.f ? 0.f : rounded;
}

struct RawCluster {
    int gx = 0;
    int gy = 0;
    Piece piece;
};

} // namespace

std::string pieceSignature(Piece const& piece) {
    std::vector<std::string> parts;
    parts.reserve(piece.objects.size());
    char buffer[64];
    for (auto const& obj : piece.objects) {
        std::snprintf(buffer, sizeof(buffer), "%.2f|%.2f|", quant(obj.dx), quant(obj.dy));
        parts.push_back(std::string(buffer) + shapeKey(obj.save));
    }
    std::sort(parts.begin(), parts.end());
    std::string out;
    out.reserve(parts.size() * 48);
    for (auto const& part : parts) {
        out += part;
        out += '\n';
    }
    return out;
}

void centerPiece(Piece& piece) {
    if (piece.objects.empty()) return;
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    for (auto const& obj : piece.objects) {
        minX = std::min(minX, obj.dx);
        minY = std::min(minY, obj.dy);
        maxX = std::max(maxX, obj.dx);
        maxY = std::max(maxY, obj.dy);
    }
    float cx = (minX + maxX) / 2.f;
    float cy = (minY + maxY) / 2.f;
    for (auto& obj : piece.objects) {
        obj.dx -= cx;
        obj.dy -= cy;
    }
    piece.width = maxX - minX;
    piece.height = maxY - minY;
}

Template waveFromObjects(std::vector<CapturedObject> objects, float cell) {
    Template tpl;
    tpl.mode = Mode::Wave;
    tpl.cell = cell > 0.f ? cell : 30.f;
    if (objects.empty()) return tpl;

    // Anchor grid to sample min corner to avoid phase shift.
    float originX = objects.front().dx;
    float originY = objects.front().dy;
    for (auto const& object : objects) {
        originX = std::min(originX, object.dx);
        originY = std::min(originY, object.dy);
    }

    std::unordered_map<std::uint64_t, int> byCell;
    std::vector<RawCluster> cells;
    for (auto& object : objects) {
        int gx = gridIndex(object.dx - originX, tpl.cell);
        int gy = gridIndex(object.dy - originY, tpl.cell);
        auto key = packCell(gx, gy);

        auto found = byCell.find(key);
        if (found == byCell.end()) {
            found = byCell.emplace(key, static_cast<int>(cells.size())).first;
            cells.push_back({gx, gy, {}});
        }
        object.dx -= originX + gx * tpl.cell;
        object.dy -= originY + gy * tpl.cell;
        cells[found->second].piece.objects.push_back(std::move(object));
    }

    std::unordered_map<std::string, int> bySignature;
    std::vector<int> cellPiece(cells.size(), 0);
    for (size_t i = 0; i < cells.size(); ++i) {
        measurePiece(cells[i].piece);
        auto signature = pieceSignature(cells[i].piece);
        auto found = bySignature.find(signature);
        if (found == bySignature.end()) {
            bySignature.emplace(std::move(signature), static_cast<int>(tpl.pieces.size()));
            cellPiece[i] = static_cast<int>(tpl.pieces.size());
            tpl.pieces.push_back(std::move(cells[i].piece));
        } else {
            cellPiece[i] = found->second;
            tpl.pieces[found->second].weight++;
        }
    }

    tpl.links.resize(tpl.pieces.size());
    std::vector<std::array<std::set<int>, kNeighbourDirections>> sides(tpl.pieces.size());
    for (size_t i = 0; i < cells.size(); ++i) {
        int piece = cellPiece[i];
        for (int d = 0; d < kNeighbourDirections; ++d) {
            auto neighbour = byCell.find(packCell(cells[i].gx + kDirectionX[d],
                                                  cells[i].gy + kDirectionY[d]));
            if (neighbour == byCell.end()) {
                tpl.links[piece].open[d] = true;
            } else {
                sides[piece][d].insert(cellPiece[neighbour->second]);
            }
        }
    }
    for (size_t p = 0; p < tpl.pieces.size(); ++p) {
        for (int d = 0; d < kNeighbourDirections; ++d) {
            tpl.links[p].side[d].assign(sides[p][d].begin(), sides[p][d].end());
        }
    }

    int minX = cells.front().gx;
    int minY = cells.front().gy;
    int maxX = minX;
    int maxY = minY;
    for (auto const& entry : cells) {
        minX = std::min(minX, entry.gx);
        minY = std::min(minY, entry.gy);
        maxX = std::max(maxX, entry.gx);
        maxY = std::max(maxY, entry.gy);
    }

    SampleGrid grid;
    grid.width = maxX - minX + 1;
    grid.height = maxY - minY + 1;
    long long const area = static_cast<long long>(grid.width) * grid.height;
    if (area > kMaxTemplateGridCells) return tpl;
    grid.cells.reserve(cells.size());
    for (size_t i = 0; i < cells.size(); ++i) {
        grid.cells.push_back({cells[i].gx - minX, cells[i].gy - minY, cellPiece[i]});
    }
    tpl.grids.push_back(std::move(grid));
    return tpl;
}

} // namespace paimon::autobuild
