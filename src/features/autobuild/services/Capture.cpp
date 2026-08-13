#include "Capture.hpp"

#include "SaveString.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <unordered_map>

using namespace geode::prelude;

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

// kS38 holds the colour channels as "chan|chan|chan"; the settings string keeps
// going with other kA keys afterwards, so the value ends at the next separator.
std::string extractColors(std::string const& settings) {
    auto start = settings.find("kS38,");
    if (start == std::string::npos) return {};
    start += 5;
    auto end = settings.find_first_of(",;", start);
    auto body = end == std::string::npos ? settings.substr(start)
                                         : settings.substr(start, end - start);
    return body.size() > 4000000 ? std::string{} : body;
}

} // namespace

int markerLayerOf(int objectId) {
    switch (objectId) {
        case kMarkerLayer1: return 1;
        case kMarkerLayer2: return 2;
        case kMarkerLayer3: return 3;
        default:            return 0;
    }
}

std::vector<GameObject*> selectionOf(EditorUI* ui) {
    std::vector<GameObject*> out;
    if (!ui) return out;
    if (auto* array = ui->getSelectedObjects()) {
        for (unsigned i = 0; i < array->count(); ++i) {
            if (auto* obj = typeinfo_cast<GameObject*>(array->objectAtIndex(i))) {
                out.push_back(obj);
            }
        }
    }
    if (out.empty() && ui->m_selectedObject) out.push_back(ui->m_selectedObject);
    return out;
}

// kS38 body of the level the editor currently has open.
static std::string levelColors(EditorUI* ui) {
    if (!ui || !ui->m_editorLayer) return {};
    auto* settings = ui->m_editorLayer->m_levelSettings;
    if (!settings) return {};
    return extractColors(std::string(settings->getSaveString()));
}

// Two pieces with the same signature are the same shape with the same object
// state, so they collapse into one tile with a higher weight.
static std::string pieceSignature(Piece const& piece) {
    std::vector<std::string> parts;
    parts.reserve(piece.objects.size());
    for (auto const& obj : piece.objects) {
        parts.push_back(fmt::format("{:.2f}|{:.2f}|{}", quant(obj.dx), quant(obj.dy),
                                    shapeKey(obj.save)));
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

Template waveFromObjects(std::vector<CapturedObject> objects, float cell) {
    Template tpl;
    tpl.mode = Mode::Wave;
    tpl.cell = cell > 0.f ? cell : 30.f;

    std::unordered_map<std::uint64_t, int> byCell;
    std::vector<RawCluster> cells;
    for (auto& object : objects) {
        int gx = gridIndex(object.dx, tpl.cell);
        int gy = gridIndex(object.dy, tpl.cell);
        auto key = packCell(gx, gy);

        auto found = byCell.find(key);
        if (found == byCell.end()) {
            found = byCell.emplace(key, static_cast<int>(cells.size())).first;
            cells.push_back({gx, gy, {}});
        }
        object.dx -= gx * tpl.cell;
        object.dy -= gy * tpl.cell;
        cells[found->second].piece.objects.push_back(std::move(object));
    }
    if (cells.empty()) return tpl;

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

    log::info("[Autobuild] onda: {} celdas -> {} piezas (celda {:.0f})",
              cells.size(), tpl.pieces.size(), tpl.cell);
    return tpl;
}

namespace {

Result<std::vector<GameObject*>> decorationFrom(EditorUI* ui, Options const& opts) {
    auto objects = selectionOf(ui);
    if (objects.empty()) return Err("Selecciona los objetos que quieres capturar.");

    std::vector<GameObject*> decoration;
    decoration.reserve(objects.size());
    for (auto* obj : objects) {
        if (!obj || isMarkerId(obj->m_objectID)) continue;
        decoration.push_back(obj);
    }
    if (decoration.empty()) {
        return Err("La seleccion solo tiene marcadores. Selecciona la decoracion.");
    }
    if (static_cast<int>(decoration.size()) > opts.maxObjects) {
        return Err(fmt::format("Demasiados objetos ({}). Sube el limite en Ajustes "
                               "o selecciona menos.", decoration.size()));
    }
    return Ok(std::move(decoration));
}

Result<Template> captureWave(EditorUI* ui, Options const& opts) {
    auto decoration = decorationFrom(ui, opts);
    if (decoration.isErr()) return Err(decoration.unwrapErr());
    auto* lel = ui->m_editorLayer;

    std::vector<CapturedObject> objects;
    objects.reserve(decoration.unwrap().size());
    for (auto* obj : decoration.unwrap()) {
        CapturedObject captured;
        captured.objectId = obj->m_objectID;
        auto pos = obj->getPosition();
        captured.dx = pos.x;
        captured.dy = pos.y;
        captured.save = std::string(obj->getSaveString(lel));
        if (captured.save.empty()) continue;
        objects.push_back(std::move(captured));
    }
    if (objects.empty()) return Err("No se pudo leer ningun objeto de la seleccion.");

    auto tpl = waveFromObjects(std::move(objects), std::clamp(opts.captureCell, 5.f, 300.f));
    if (tpl.grids.empty()) {
        return Err("La muestra abarca demasiadas celdas. Usa una celda mayor o "
                   "reduce la distancia entre sus extremos.");
    }
    tpl.colors = levelColors(ui);
    return Ok(std::move(tpl));
}

// Group objects that sit within `radius` of each other on both axes. The bucket
// grid keeps this near linear instead of comparing every pair.
std::vector<std::vector<GameObject*>> clusterObjects(std::vector<GameObject*> const& objects,
                                                     float radius) {
    std::unordered_map<std::uint64_t, std::vector<int>> buckets;
    std::vector<cocos2d::CCPoint> positions;
    positions.reserve(objects.size());
    for (size_t i = 0; i < objects.size(); ++i) {
        auto pos = objects[i]->getPosition();
        positions.push_back(pos);
        buckets[packCell(static_cast<int>(std::floor(pos.x / radius)),
                         static_cast<int>(std::floor(pos.y / radius)))]
            .push_back(static_cast<int>(i));
    }

    std::vector<std::vector<GameObject*>> clusters;
    std::vector<bool> taken(objects.size(), false);
    std::vector<int> queue;
    for (size_t start = 0; start < objects.size(); ++start) {
        if (taken[start]) continue;
        taken[start] = true;
        queue.clear();
        queue.push_back(static_cast<int>(start));

        std::vector<GameObject*> cluster;
        while (!queue.empty()) {
            int index = queue.back();
            queue.pop_back();
            cluster.push_back(objects[index]);

            auto const& origin = positions[index];
            int bx = static_cast<int>(std::floor(origin.x / radius));
            int by = static_cast<int>(std::floor(origin.y / radius));
            for (int ox = -1; ox <= 1; ++ox) {
                for (int oy = -1; oy <= 1; ++oy) {
                    auto bucket = buckets.find(packCell(bx + ox, by + oy));
                    if (bucket == buckets.end()) continue;
                    for (int candidate : bucket->second) {
                        if (taken[candidate]) continue;
                        auto const& other = positions[candidate];
                        if (std::abs(other.x - origin.x) > radius) continue;
                        if (std::abs(other.y - origin.y) > radius) continue;
                        taken[candidate] = true;
                        queue.push_back(candidate);
                    }
                }
            }
        }
        clusters.push_back(std::move(cluster));
    }
    return clusters;
}

Result<Template> captureStamps(EditorUI* ui, Options const& opts) {
    auto decoration = decorationFrom(ui, opts);
    if (decoration.isErr()) return Err(decoration.unwrapErr());
    auto* lel = ui->m_editorLayer;

    float const radius = std::clamp(opts.clusterRadius, 15.f, 480.f);
    auto clusters = clusterObjects(decoration.unwrap(), radius);

    Template tpl;
    tpl.mode = Mode::Stamp;
    tpl.cell = std::clamp(opts.captureCell, 5.f, 300.f);
    tpl.colors = levelColors(ui);

    std::unordered_map<std::string, int> bySignature;
    for (auto const& cluster : clusters) {
        if (cluster.empty()) continue;

        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();
        for (auto* obj : cluster) {
            auto pos = obj->getPosition();
            minX = std::min(minX, pos.x);
            minY = std::min(minY, pos.y);
            maxX = std::max(maxX, pos.x);
            maxY = std::max(maxY, pos.y);
        }
        cocos2d::CCPoint anchor = {(minX + maxX) / 2.f, (minY + maxY) / 2.f};

        Piece piece;
        piece.objects.reserve(cluster.size());
        for (auto* obj : cluster) {
            auto pos = obj->getPosition();
            CapturedObject captured;
            captured.objectId = obj->m_objectID;
            captured.dx = pos.x - anchor.x;
            captured.dy = pos.y - anchor.y;
            captured.save = std::string(obj->getSaveString(lel));
            if (captured.save.empty()) continue;
            piece.objects.push_back(std::move(captured));
        }
        if (piece.objects.empty()) continue;
        measurePiece(piece);

        auto signature = pieceSignature(piece);
        auto found = bySignature.find(signature);
        if (found == bySignature.end()) {
            bySignature.emplace(std::move(signature), static_cast<int>(tpl.pieces.size()));
            tpl.pieces.push_back(std::move(piece));
        } else {
            tpl.pieces[found->second].weight++;
        }
    }
    if (tpl.pieces.empty()) return Err("No se pudo leer ningun objeto de la seleccion.");

    log::info("[Autobuild] captura sellos: {} grupos -> {} piezas (radio {:.0f})",
              clusters.size(), tpl.pieces.size(), radius);
    return Ok(std::move(tpl));
}

} // namespace

Result<Template> capture(EditorUI* ui, Mode mode, Options const& opts) {
    if (!ui || !ui->m_editorLayer) return Err("El editor no esta disponible.");
    return mode == Mode::Wave ? captureWave(ui, opts) : captureStamps(ui, opts);
}

Result<> accumulate(Template& target, Template const& sample) {
    if (!sample.valid()) return Err("La muestra esta vacia.");
    if (target.mode != sample.mode) {
        return Err("La muestra usa otro modo que la plantilla seleccionada.");
    }
    if (target.mode == Mode::Wave && std::abs(target.cell - sample.cell) > 0.01f) {
        return Err(fmt::format("La plantilla usa celdas de {:.0f} y la muestra de {:.0f}.",
                               target.cell, sample.cell));
    }
    if (target.mode == Mode::Wave) {
        if (target.grids.size() + sample.grids.size() >
            static_cast<size_t>(kMaxTemplateGrids)) {
            return Err("La plantilla alcanzo el limite de muestras guardadas.");
        }
        long long cells = 0;
        for (auto const& grid : target.grids) {
            cells += static_cast<long long>(grid.width) * grid.height;
        }
        for (auto const& grid : sample.grids) {
            cells += static_cast<long long>(grid.width) * grid.height;
        }
        if (cells > kMaxTemplateGridCells) {
            return Err("La plantilla alcanzo el limite de celdas de muestra guardadas.");
        }
    }

    std::unordered_map<std::string, int> bySignature;
    for (size_t i = 0; i < target.pieces.size(); ++i) {
        bySignature.emplace(pieceSignature(target.pieces[i]), static_cast<int>(i));
    }
    target.links.resize(target.pieces.size());

    std::vector<int> remap(sample.pieces.size(), -1);
    for (size_t i = 0; i < sample.pieces.size(); ++i) {
        auto signature = pieceSignature(sample.pieces[i]);
        auto found = bySignature.find(signature);
        if (found == bySignature.end()) {
            int index = static_cast<int>(target.pieces.size());
            bySignature.emplace(std::move(signature), index);
            target.pieces.push_back(sample.pieces[i]);
            target.links.emplace_back();
            remap[i] = index;
        } else {
            target.pieces[found->second].weight += sample.pieces[i].weight;
            remap[i] = found->second;
        }
    }

    if (target.mode == Mode::Wave) {
        for (size_t i = 0; i < sample.links.size() && i < remap.size(); ++i) {
            auto& into = target.links[remap[i]];
            for (int d = 0; d < kNeighbourDirections; ++d) {
                if (sample.links[i].open[d]) into.open[d] = true;
                for (int neighbour : sample.links[i].side[d]) {
                    if (neighbour < 0 || neighbour >= static_cast<int>(remap.size())) continue;
                    int mapped = remap[neighbour];
                    auto& list = into.side[d];
                    if (std::find(list.begin(), list.end(), mapped) == list.end()) {
                        list.push_back(mapped);
                    }
                }
            }
        }
    }

    for (auto const& grid : sample.grids) {
        SampleGrid mapped;
        mapped.width = grid.width;
        mapped.height = grid.height;
        mapped.cells.reserve(grid.cells.size());
        for (auto const& cell : grid.cells) {
            if (cell.piece < 0 || cell.piece >= static_cast<int>(remap.size())) continue;
            mapped.cells.push_back({cell.x, cell.y, remap[cell.piece]});
        }
        if (!mapped.cells.empty()) target.grids.push_back(std::move(mapped));
    }

    if (target.colors.empty()) target.colors = sample.colors;
    target.samples += std::max(1, sample.samples);
    return Ok();
}

} // namespace paimon::autobuild
