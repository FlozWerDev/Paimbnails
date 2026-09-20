#include "Builder.hpp"

#include "Capture.hpp"
#include "DesignCritic.hpp"
#include "Invention.hpp"
#include "SaveString.hpp"

#include <Geode/binding/ColorAction.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GJEffectManager.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <set>

using namespace geode::prelude;

namespace paimon::autobuild {

namespace {

constexpr int kMaxAreaCells = 6000;

// Ultimo build para deshacer o regenerar sin reseleccionar.
struct Session {
    LevelEditorLayer* editor = nullptr;
    std::vector<Target> targets;
    std::vector<std::string> markerSaves;
    std::vector<Ref<GameObject>> created;
    unsigned seed = 0;

    void clear() {
        editor = nullptr;
        targets.clear();
        markerSaves.clear();
        created.clear();
        seed = 0;
    }
};

Session& session() {
    static Session instance;
    return instance;
}

unsigned randomSeed() {
    std::random_device device;
    return device();
}

int removeCreated(EditorUI* ui) {
    auto& state = session();
    auto* lel = ui->m_editorLayer;
    if (!lel) return 0;

    ui->deselectAll();
    int removed = 0;
    for (auto& object : state.created) {
        if (!object || !object->getParent()) continue;
        lel->removeObject(object, true);
        ++removed;
    }
    state.created.clear();
    return removed;
}

void importColors(LevelEditorLayer* lel, std::string const& colors, std::set<int> const& wanted) {
    if (!lel || colors.empty() || wanted.empty()) return;
    auto* settings = lel->m_levelSettings;
    if (!settings || !settings->m_effectManager) return;

    int imported = 0;
    size_t start = 0;
    while (start <= colors.size()) {
        auto end = colors.find('|', start);
        auto segment = colors.substr(start, end == std::string::npos ? std::string::npos
                                                                     : end - start);
        start = end == std::string::npos ? colors.size() + 1 : end + 1;
        if (segment.empty()) continue;

        auto* action = ColorAction::create();
        if (!action) continue;
        action->setupFromString(segment);
        if (!wanted.count(action->m_colorID)) continue;
        settings->m_effectManager->setColorAction(action, action->m_colorID);
        ++imported;
    }
    if (imported > 0) log::info("[Autobuild] {} canales de color importados", imported);
}

std::vector<GameObject*> markersIn(std::vector<GameObject*> const& objects, Options const& opts) {
    std::vector<GameObject*> markers;
    for (auto* obj : objects) {
        int layer = markerLayerOf(obj->m_objectID);
        if (layer == 0) continue;
        if (layer == 2 && !opts.layer2Markers) continue;
        if (layer == 3 && !opts.layer3Markers) continue;
        markers.push_back(obj);
    }
    return markers;
}

Target targetFrom(GameObject* obj) {
    auto const pos = obj->getPosition();
    return Target{{pos.x, pos.y}};
}

Result<BuildReport> paste(EditorUI* ui, Template const& tpl, Options const& opts,
                          std::vector<Target> targets, std::vector<GameObject*> markers,
                          unsigned seed) {
    auto* lel = ui->m_editorLayer;

    // Prueba varias semillas y conserva el mejor diseno.
    SolveStats stats;
    std::vector<Placement> placements;
    DesignScore design;
    int const tries = std::clamp(opts.refineTries, 1, 12);
    for (int i = 0; i < tries; ++i) {
        unsigned attempt = i == 0 ? seed : deriveSeed(seed, 101u + static_cast<unsigned>(i));
        SolveStats attemptStats;
        auto attemptPlacements =
            tpl.mode == Mode::Wave ? solveWave(tpl, opts, targets, attempt, attemptStats)
                                   : solveStamps(tpl, opts, targets, attempt, attemptStats);
        if (i == 0) {
            stats = attemptStats;
            placements = std::move(attemptPlacements);
            design = scoreDesign(tpl, placements, stats, opts);
            continue;
        }
        if (attemptPlacements.empty() || attemptStats.budgetExceeded) continue;
        auto attemptScore = scoreDesign(tpl, attemptPlacements, attemptStats, opts);
        if (placements.empty() || stats.budgetExceeded || isBetter(attemptScore, design)) {
            stats = attemptStats;
            placements = std::move(attemptPlacements);
            design = attemptScore;
        }
    }
    if (tpl.mode == Mode::Wave) {
        log::info("[Autobuild] onda: {} celdas, {} llenas, {} huecos, {} forzadas, "
                  "{} retrocesos, {} ms{}",
                  stats.cells, stats.filled, stats.gaps, stats.forced, stats.backtracks,
                  stats.ms, stats.budgetExceeded ? " (presupuesto agotado)" : "");
        if (stats.smartExact + stats.smartRemapped + stats.smartSimplified > 0) {
            log::info("[Autobuild] plantilla adaptable: {} exactas, {} remapeadas, "
                      "{} aproximadas",
                      stats.smartExact, stats.smartRemapped, stats.smartSimplified);
        }
    } else {
        log::info("[Autobuild] sellos: {} destinos, {} colocados, {} saltados, {} ms",
                  stats.cells, stats.filled, stats.gaps, stats.ms);
    }
    if (tries > 1) {
        log::info("[Autobuild] refinado: mejor de {} intentos, diseno {:.0f}/100",
                  tries, design.total);
    }
    if (stats.budgetExceeded) {
        return Err("Autobuild agoto el presupuesto de calculo sin encontrar una solucion. "
                   "Prueba con un area menor, otra semilla o reglas flexibles.");
    }
    if (placements.empty()) {
        return Err("La plantilla no pudo colocar nada aqui. Prueba con otra semilla "
                   "o permite huecos o reglas flexibles.");
    }

    IdShift shift{opts.shiftColors, opts.shiftGroups, opts.shiftLayers, opts.shiftZOrder,
                  opts.addGroup};
    std::set<int> usedColors;
    std::string payload;
    payload.reserve(static_cast<size_t>(placements.size()) * 128);

    struct TransformBatch {
        unsigned first = 0;
        unsigned count = 0;
        Point pivot;
        PieceTransform transform;
    };
    std::vector<TransformBatch> transforms;

    BuildReport report;
    for (auto const& placement : placements) {
        if (placement.piece < 0 || placement.piece >= static_cast<int>(tpl.pieces.size())) continue;
        TransformBatch batch{static_cast<unsigned>(report.objects), 0, placement.pos,
                             placement.transform};
        for (auto const& object : tpl.pieces[placement.piece].objects) {
            if (isMarkerId(object.objectId)) continue;
            if (report.objects >= opts.maxObjects) {
                report.truncated = true;
                break;
            }
            payload += retarget(object.save, placement.pos.x + object.dx,
                                placement.pos.y + object.dy, shift);
            if (opts.copyColors) collectColorIds(object.save, usedColors);
            report.objects++;
            batch.count++;
        }
        if (batch.count > 0 && !batch.transform.identity()) transforms.push_back(batch);
        if (report.truncated) break;
    }
    if (payload.empty()) return Err("Las piezas de la plantilla no tienen objetos validos.");

    // Guarda marcadores para restaurarlos al deshacer.
    std::vector<std::string> markerSaves;
    ui->deselectAll();
    if (opts.removeMarkers) {
        for (auto* marker : markers) {
            if (!marker || !marker->getParent()) continue;
            markerSaves.push_back(std::string(marker->getSaveString(lel)));
            lel->removeObject(marker, true);
        }
    }

    CCArray* created = lel->createObjectsFromString(payload, false, true);
    if (created) {
        for (auto const& batch : transforms) {
            if (batch.first >= created->count()) continue;
            auto* objects = CCArray::createWithCapacity(batch.count);
            GameObject* firstObject = nullptr;
            unsigned const end = std::min(created->count(), batch.first + batch.count);
            for (unsigned index = batch.first; index < end; ++index) {
                if (auto* object = typeinfo_cast<GameObject*>(created->objectAtIndex(index))) {
                    if (!firstObject) firstObject = object;
                    objects->addObject(object);
                }
            }
            if (!firstObject) continue;

            CCPoint const pivot{batch.pivot.x, batch.pivot.y};
            if (batch.transform.quarterTurns != 0) {
                ui->rotateObjects(objects, 90.f * (batch.transform.quarterTurns % 4), pivot);
            }
            if (batch.transform.flipX) {
                float const before = firstObject->getPositionX();
                ui->flipObjectsX(objects);
                float const correction = 2.f * pivot.x - before - firstObject->getPositionX();
                if (std::abs(correction) > 0.001f) {
                    CCPoint const offset{correction, 0.f};
                    for (unsigned index = 0; index < objects->count(); ++index) {
                        if (auto* object =
                                typeinfo_cast<GameObject*>(objects->objectAtIndex(index))) {
                            ui->moveObject(object, offset);
                        }
                    }
                }
            }
        }
    }
    if (created) lel->updateObjectColors(created);

    if (opts.copyColors) {
        if (opts.shiftColors != 0) {
            std::set<int> shifted;
            for (int id : usedColors) {
                shifted.insert(id >= 1 && id <= 999 ? id + opts.shiftColors : id);
            }
            usedColors = std::move(shifted);
        }
        importColors(lel, tpl.colors, usedColors);
    }

    auto& state = session();
    state.editor = lel;
    state.targets = std::move(targets);
    state.markerSaves = std::move(markerSaves);
    state.seed = seed;
    state.created.clear();
    if (created) {
        state.created.reserve(created->count());
        for (unsigned i = 0; i < created->count(); ++i) {
            if (auto* obj = typeinfo_cast<GameObject*>(created->objectAtIndex(i))) {
                state.created.push_back(obj);
            }
        }
    }

    report.targets = stats.cells;
    report.gaps = stats.gaps;
    report.forced = stats.forced;
    report.seed = seed;
    report.ms = stats.ms;
    log::info("[Autobuild] construido: {} objetos en {} destinos (semilla {})",
              report.objects, report.targets, seed);
    return Ok(report);
}

} // namespace

std::string BuildReport::describe() const {
    std::string text = fmt::format("{} objetos en {} sitios", objects, targets);
    if (gaps > 0) text += fmt::format(", {} huecos", gaps);
    if (forced > 0) text += fmt::format(", {} forzadas", forced);
    text += fmt::format(" - semilla {}", seed);
    if (truncated) text += " (limite de objetos alcanzado)";
    return text;
}

Result<BuildPlan> planBuild(EditorUI* ui, Options const& opts, float cell) {
    if (!ui || !ui->m_editorLayer) return Err("El editor no esta disponible.");
    auto objects = selectionOf(ui);
    if (objects.empty()) return Err("Selecciona donde quieres construir.");

    BuildPlan plan;
    plan.markers = markersIn(objects, opts);
    auto& targets = plan.targets;
    switch (opts.target) {
        case TargetMode::Markers: {
            if (plan.markers.empty()) {
                return Err("No hay marcadores en la seleccion. Coloca bloques 467 "
                           "(o 143 / 146) donde quieras construir.");
            }
            targets.reserve(plan.markers.size());
            for (auto* marker : plan.markers) targets.push_back(targetFrom(marker));
            break;
        }
        case TargetMode::Selection: {
            targets.reserve(objects.size());
            for (auto* obj : objects) targets.push_back(targetFrom(obj));
            break;
        }
        case TargetMode::Area: {
            float minX = std::numeric_limits<float>::max();
            float minY = std::numeric_limits<float>::max();
            float maxX = std::numeric_limits<float>::lowest();
            float maxY = std::numeric_limits<float>::lowest();
            for (auto* obj : objects) {
                auto pos = obj->getPosition();
                minX = std::min(minX, pos.x);
                minY = std::min(minY, pos.y);
                maxX = std::max(maxX, pos.x);
                maxY = std::max(maxY, pos.y);
            }
            // Ancla la rejilla al minimo de la seleccion, no al origen.
            float step = cell > 0.f ? cell : 30.f;
            int nx = static_cast<int>(std::floor((maxX - minX) / step + 0.5f)) + 1;
            int ny = static_cast<int>(std::floor((maxY - minY) / step + 0.5f)) + 1;
            nx = std::max(1, nx);
            ny = std::max(1, ny);
            long long total = static_cast<long long>(nx) * ny;
            if (total > kMaxAreaCells) {
                return Err(fmt::format("El area pedida son {} celdas. Reduce la seleccion "
                                       "o usa una celda mas grande.", total));
            }
            targets.reserve(static_cast<size_t>(total));
            for (int ix = 0; ix < nx; ++ix) {
                for (int iy = 0; iy < ny; ++iy) {
                    Target target{{minX + ix * step, minY + iy * step}};
                    targets.push_back(target);
                }
            }
            break;
        }
    }

    if (targets.empty()) return Err("No hay ningun destino en la seleccion.");
    return Ok(std::move(plan));
}

Result<BuildReport> generate(EditorUI* ui, Template const& tpl, Options const& opts) {
    if (!ui || !ui->m_editorLayer) return Err("El editor no esta disponible.");
    if (!tpl.valid()) return Err("La plantilla esta vacia.");

    auto plan = planBuild(ui, opts, tpl.cell);
    if (plan.isErr()) return Err(plan.unwrapErr());

    unsigned seed = opts.seed != 0 ? static_cast<unsigned>(opts.seed) : randomSeed();
    auto& resolved = plan.unwrap();
    return paste(ui, tpl, opts, std::move(resolved.targets), std::move(resolved.markers), seed);
}

Result<BuildReport> regenerate(EditorUI* ui, Template const& tpl, Options const& opts) {
    if (!canUndo(ui)) return generate(ui, tpl, opts);

    // Mismas celdas con nueva semilla; conserva marcadores para undo.
    auto targets = session().targets;
    auto markerSaves = session().markerSaves;
    removeCreated(ui);

    auto report = paste(ui, tpl, opts, std::move(targets), {}, randomSeed());
    if (report.isErr()) return report;

    auto& state = session();
    if (state.markerSaves.empty()) state.markerSaves = std::move(markerSaves);
    return report;
}

bool canUndo(EditorUI* ui) {
    if (!ui || !ui->m_editorLayer) return false;
    auto& state = session();
    return state.editor == ui->m_editorLayer && !state.created.empty();
}

Result<> undoLast(EditorUI* ui) {
    if (!canUndo(ui)) return Err("No hay nada que deshacer.");
    auto* lel = ui->m_editorLayer;
    auto& state = session();

    int removed = removeCreated(ui);

    if (!state.markerSaves.empty()) {
        std::string payload;
        for (auto const& save : state.markerSaves) {
            payload += save;
            if (payload.empty() || payload.back() != ';') payload += ';';
        }
        lel->createObjectsFromString(payload, true, true);
        state.markerSaves.clear();
    }

    log::info("[Autobuild] deshecho: {} objetos borrados", removed);
    return Ok();
}

void forgetSession() {
    session().clear();
}

} // namespace paimon::autobuild
