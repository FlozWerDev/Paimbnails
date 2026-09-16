// Sonda escalera->tira: ¿cuantos bloques/discos pequenos forman carreras en
// diagonal que una sola tira girada cubriria sin cambiar ningun centro de celda?
// La sonda anterior (merge_probe) solo unia tira-tira y bloque-bloque rectos;
// esta prueba la conversion que falta: N peldanos -> 1 diagonal.
// Uso: g++ -std=c++23 -O2 -o /tmp/stair_probe tests/stair_probe.cpp && /tmp/stair_probe [dims...]
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "../src/utils/stb_image.h"

#include "../src/features/gif-import/services/GifImportPipeline.hpp"
#include "../src/features/gif-import/services/ColorSpace.cpp"
#include "../src/features/gif-import/services/GifParallel.cpp"
#include "../src/features/gif-import/services/GifShapeRaster.cpp"
#include "../src/features/gif-import/services/GifVectorMath.cpp"
#include "../src/features/gif-import/services/GifArtVectorizer.cpp"
#include "../src/features/gif-import/services/GifCircleVectorizer.cpp"
#include "../src/features/gif-import/services/GifFreeVectorizer.cpp"
#include "../src/features/gif-import/services/GifStampCatalog.cpp"
#include "../src/features/gif-import/services/GifGlowPass.cpp"
#include "../src/features/gif-import/services/GifMotionPlanner.cpp"
#include "../src/features/gif-import/services/GifPaintVectorizer.cpp"
#include "../src/features/gif-import/services/ImageWatermark.cpp"
#include "../src/features/gif-import/services/GifImportPipeline.cpp"

using namespace paimon::gifimport;
namespace fs = std::filesystem;

namespace {

constexpr float kPi = 3.14159265f;

struct Step {
    std::size_t objIndex;
    float x, y;      // centro
    float thick;     // grosor (min lado / diametro)
};

bool stepCandidate(Primitive const& o) {
    if (o.kind == PrimitiveKind::Block && o.rotation == 0.f &&
        o.width <= 2.5f && o.height <= 2.5f)
        return true;
    if (o.kind == PrimitiveKind::Circle && o.width <= 2.5f) return true;
    return false;
}

// Tira por los centros extremos; valida: cubre los centros de la carrera y no
// cubre ningun centro de otro color (estricto: sin orla en la sonda).
bool fitRunStrip(
    std::vector<Step> const& run,
    std::vector<Primitive> const& objects,
    std::vector<int> const& cells,
    int width,
    int height,
    Primitive& out
) {
    if (run.size() < 3) return false;
    float x0 = run.front().x, y0 = run.front().y;
    float x1 = run.back().x, y1 = run.back().y;
    float dx = x1 - x0, dy = y1 - y0;
    float len = std::hypot(dx, dy);
    if (len < 1.5f) return false;
    float thick = 0.f;
    for (auto const& s : run) thick = std::max(thick, s.thick);
    thick = std::max(thick, 1.f);
    Primitive const& seed = objects[run.front().objIndex];
    Primitive m{
        (x0 + x1) / 2, (y0 + y1) / 2, len + thick, thick,
        std::atan2(dy, dx) * 180.f / kPi, seed.color, PrimitiveKind::Stroke,
        seed.layer};
    auto placed = xformOf(m);
    // Todos los centros de la carrera dentro (holgura de costura).
    for (auto const& s : run) {
        // contiene con margen: prueba el centro y 4 puntos a 0.2 celdas.
        bool inside = placed.contains(s.x, s.y);
        if (!inside) return false;
    }
    // Ningun centro ajeno dentro.
    auto box = xformBox(placed, width, height);
    for (int y = box[1]; y <= box[3]; ++y)
        for (int x = box[0]; x <= box[2]; ++x) {
            if (!placed.contains(x + 0.5f, y + 0.5f)) continue;
            int const pos = y * width + x;
            if (pos < 0 || pos >= static_cast<int>(cells.size())) continue;
            if (cells[static_cast<std::size_t>(pos)] != m.color) return false;
        }
    out = m;
    return true;
}

struct StairResult {
    int runs = 0;
    int stepsInRuns = 0;
    int saved = 0;  // objetos ahorrados (steps - runs convertidas)
    std::vector<Primitive> merged;
};

StairResult stairPass(
    std::vector<Primitive> const& objects,
    std::vector<int> const& cells,
    int width,
    int height
) {
    std::vector<Step> steps;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        if (!stepCandidate(objects[i])) continue;
        steps.push_back({i, objects[i].x, objects[i].y,
                         objects[i].kind == PrimitiveKind::Circle
                             ? objects[i].width
                             : std::min(objects[i].width, objects[i].height)});
    }
    std::vector<char> used(objects.size(), 0);
    StairResult result;
    result.merged = objects;
    std::vector<char> drop(objects.size(), 0);
    std::vector<Primitive> add;
    // Direcciones diagonales unitarias.
    constexpr float kDirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    for (auto const& seed : steps) {
        if (used[seed.objIndex]) continue;
        // Mejor carrera de las 4 direcciones (solo 2 ejes unicos, pero barato).
        std::vector<Step> best{seed};
        for (auto const& d : kDirs) {
            std::vector<Step> run{seed};
            float cx = seed.x, cy = seed.y;
            for (int hop = 0; hop < 12; ++hop) {
                float ex = cx + d[0], ey = cy + d[1];
                Step const* found = nullptr;
                for (auto const& s : steps) {
                    if (used[s.objIndex]) continue;
                    if (s.objIndex == run.back().objIndex) continue;
                    bool inRun = false;
                    for (auto const& r : run)
                        if (r.objIndex == s.objIndex) {
                            inRun = true;
                            break;
                        }
                    if (inRun) continue;
                    if (std::hypot(s.x - ex, s.y - ey) > 0.75f) continue;
                    // Mismo color y capa que la semilla.
                    if (objects[s.objIndex].color != objects[seed.objIndex].color ||
                        objects[s.objIndex].layer != objects[seed.objIndex].layer)
                        continue;
                    found = &s;
                    break;
                }
                if (!found) break;
                run.push_back(*found);
                cx = found->x;
                cy = found->y;
            }
            if (run.size() > best.size()) best = std::move(run);
        }
        if (best.size() < 3) continue;
        Primitive strip;
        if (!fitRunStrip(best, objects, cells, width, height, strip)) continue;
        for (auto const& s : best) used[s.objIndex] = 1;
        for (auto const& s : best) drop[s.objIndex] = 1;
        add.push_back(strip);
        result.runs++;
        result.stepsInRuns += static_cast<int>(best.size());
        result.saved += static_cast<int>(best.size()) - 1;
    }
    std::vector<Primitive> out;
    for (std::size_t i = 0; i < objects.size(); ++i)
        if (!drop[i]) out.push_back(objects[i]);
    out.insert(out.end(), add.begin(), add.end());
    result.merged = std::move(out);
    return result;
}

double gridFidelity(ImportPlan const& plan, std::vector<Primitive> const& objects) {
    constexpr int scale = 2;
    ImportPlan probe = plan;
    probe.staticObjects = objects;
    auto const preview = renderPlanFrame(probe, 0, scale);
    auto const& cells = plan.frames.front().cells;
    std::size_t correct = 0, compared = 0;
    for (int y = 0; y < plan.height * scale; ++y)
        for (int x = 0; x < plan.width * scale; ++x) {
            int const index =
                cells[static_cast<std::size_t>(y / scale) * plan.width + x / scale];
            std::size_t const pixel =
                (static_cast<std::size_t>(y) * plan.width * scale + x) * 4;
            bool const visible = preview[pixel + 3] != 0;
            if (index < 0 && !visible) continue;
            ++compared;
            if (index < 0 || !visible) continue;
            auto const& color = plan.palette[static_cast<std::size_t>(index)];
            if (preview[pixel] == color.r && preview[pixel + 1] == color.g &&
                preview[pixel + 2] == color.b)
                ++correct;
        }
    return compared > 0 ? 100.0 * static_cast<double>(correct) / compared : 100.0;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<int> dims{128};
    if (argc > 1) {
        dims.clear();
        for (int i = 1; i < argc; ++i) dims.push_back(std::stoi(argv[i]));
    }
    std::vector<std::string> images{
        "muestra_pintura.png",
        "muestra_pintura_sin_escalera.png",
        "muestra_lado_a_lado.png",
        "paimon-paint-preview-128-final-current.png",
        "paimon-paint-comparison-128-final-current.png",
        "4d3109fee0f39a47873b4ada69b9aa10.jpg",
        "9dc0fc3231c31b069fa0e70a874bdf5b.jpg",
        "19b783638a5ac1eac49d6bd1845d1039.jpg",
    };
    std::cout << "imagen,dim,objs0,objs1,runs,steps,grid0,grid1\n";
    for (int dim : dims) {
        for (auto const& file : images) {
            fs::path path = fs::path("/home/fernando/Descargas") / file;
            int width = 0, height = 0, channels = 0;
            std::uint8_t* pixels =
                stbi_load(path.string().c_str(), &width, &height, &channels, 4);
            if (!pixels) {
                std::cout << file << "," << dim << ",ERROR\n";
                continue;
            }
            SourceAnimation source;
            source.width = width;
            source.height = height;
            source.frames.resize(1);
            source.frames.front().delayMs = 100;
            source.frames.front().rgba.assign(
                pixels, pixels + static_cast<std::size_t>(width) * height * 4);
            stbi_image_free(pixels);
            Options options;
            options.mode = ImportMode::Paint;
            options.maxDimension = dim;
            options.maxColors = 24;
            options.objectBudget = 12000;
            options.background = BackgroundMode::Keep;
            options.sampling = SamplingMode::Smooth;
            auto result = buildPlan(source, options);
            if (!result) {
                std::cout << file << "," << dim << ",ERROR\n";
                continue;
            }
            auto const& cells = result.plan.frames.front().cells;
            auto stair = stairPass(
                result.plan.staticObjects, cells, result.plan.width,
                result.plan.height);
            double g0 = gridFidelity(result.plan, result.plan.staticObjects);
            double g1 = gridFidelity(result.plan, stair.merged);
            std::cout << file << "," << dim << ","
                      << result.plan.staticObjects.size() << ","
                      << stair.merged.size() << "," << stair.runs << ","
                      << stair.stepsInRuns << "," << std::fixed
                      << std::setprecision(2) << g0 << "," << g1 << "\n";
        }
    }
    return 0;
}
