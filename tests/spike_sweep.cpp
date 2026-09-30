// barrido de picos del modo pintura: construye el plan una vez por imagen y
// evalua n variantes runtime del normalizador de astillas sobre copias de los
// objetos, puntuando picos restantes vs fidelidad de rejilla vs nº objetos.
// uso: g++ -std=c++23 -o2 -o /tmp/spike_sweep tests/spike_sweep.cpp && /tmp/spike_sweep
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

float foldedAngle(float rotation) {
    float folded = std::fmod(std::abs(rotation), 90.f);
    return std::min(folded, 90.f - folded);
}

// picos: tiras/triangulos girados con algun lado <= sidecap (la punta asoma por
// las esquinas del cuadrado que los representa). definicion base del test
// paint-spikes: ambos lados <= 1.6; aqui se parametriza para ver la cola.
int countSpikes(std::vector<Primitive> const& objects, float sideCap, float angleLo = 5.f) {
    int spikes = 0;
    for (auto const& object : objects) {
        bool const shaped = object.kind == PrimitiveKind::Stroke ||
            object.kind == PrimitiveKind::Triangle ||
            object.kind == PrimitiveKind::WideTriangle;
        if (!shaped) continue;
        float const folded = foldedAngle(object.rotation);
        if (folded <= angleLo || folded >= 85.f) continue;
        if (std::min(object.width, object.height) <= sideCap) ++spikes;
    }
    return spikes;
}

double gridFidelity(ImportPlan const& plan, std::vector<Primitive> const& objects) {
    constexpr int scale = 2;
    ImportPlan probe = plan;
    probe.staticObjects = objects;
    auto const preview = renderPlanFrame(probe, 0, scale);
    auto const& cells = plan.frames.front().cells;
    std::size_t correct = 0, compared = 0;
    for (int y = 0; y < plan.height * scale; ++y) {
        for (int x = 0; x < plan.width * scale; ++x) {
            int const index = cells[static_cast<std::size_t>(y / scale) * plan.width + x / scale];
            std::size_t const pixel = (static_cast<std::size_t>(y) * plan.width * scale + x) * 4;
            bool const visible = preview[pixel + 3] != 0;
            if (index < 0 && !visible) continue;
            ++compared;
            if (index < 0 || !visible) continue;
            auto const& color = plan.palette[static_cast<std::size_t>(index)];
            if (preview[pixel] == color.r && preview[pixel + 1] == color.g &&
                preview[pixel + 2] == color.b) {
                ++correct;
            }
        }
    }
    return compared > 0 ? 100.0 * static_cast<double>(correct) / compared : 100.0;
}

struct SweepParams {
    float sideGate;   // max(w,h) maximo para normalizar
    float angleGate;  // folded minimo para normalizar
    float diaMin;     // diametro minimo para intentar disco
};

std::vector<Primitive> normalizeVariant(
    std::vector<Primitive> const& input,
    std::vector<int> const& cells,
    int width,
    int height,
    SweepParams const& params
) {
    std::vector<Primitive> output;
    output.reserve(input.size());
    for (auto const& object : input) {
        bool const shaped = object.kind == PrimitiveKind::Stroke ||
            object.kind == PrimitiveKind::Triangle ||
            object.kind == PrimitiveKind::WideTriangle;
        float const folded = foldedAngle(object.rotation);
        float const maxSide = std::max(object.width, object.height);
        float const minSide = std::min(object.width, object.height);
        if (!shaped || folded <= params.angleGate || maxSide > params.sideGate) {
            output.push_back(object);
            continue;
        }
        // centros de su color que cubria el original: hay que conservarlos.
        std::vector<std::pair<float, float>> centers;
        {
            auto const placed = xformOf(object);
            auto const box = xformBox(placed, width, height);
            for (int y = box[1]; y <= box[3]; ++y) {
                for (int x = box[0]; x <= box[2]; ++x) {
                    int const position = y * width + x;
                    if (position < 0 || position >= static_cast<int>(cells.size())) continue;
                    if (cells[static_cast<std::size_t>(position)] != object.color) continue;
                    if (placed.contains(x + 0.5f, y + 0.5f)) {
                        centers.emplace_back(x + 0.5f, y + 0.5f);
                    }
                }
            }
        }
        auto preserves = [&](std::vector<Primitive> const& shapes) {
            for (auto const& center : centers) {
                bool covered = false;
                for (auto const& shape : shapes) {
                    if (xformOf(shape).contains(center.first, center.second)) {
                        covered = true;
                        break;
                    }
                }
                if (!covered) return false;
            }
            return true;
        };
        bool done = false;
        if (minSide >= params.diaMin) {
            Primitive circle{object.x, object.y, minSide, minSide, 0.f, object.color,
                             PrimitiveKind::Circle, object.layer};
            if (preserves({circle})) {
                output.push_back(circle);
                done = true;
            }
        }
        if (!done) {
            for (float scale : {1.f, 0.9f, 0.75f, 0.6f, 0.45f}) {
                Primitive block{object.x, object.y, object.width * scale,
                                object.height * scale, 0.f, object.color,
                                PrimitiveKind::Block, object.layer};
                if (preserves({block})) {
                    output.push_back(block);
                    done = true;
                    break;
                }
            }
        }
        if (!done) output.push_back(object);  // no se pudo sin perder cobertura
    }
    return output;
}

}  // namespace

int main(int argc, char** argv) {
    bool const census = argc > 1 && std::string(argv[1]) == "census";
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
    Options options;
    options.mode = ImportMode::Paint;
    options.maxDimension = 64;
    options.maxColors = 16;
    options.objectBudget = 12000;
    options.background = BackgroundMode::Keep;
    options.sampling = SamplingMode::Smooth;

    struct Case {
        std::string name;
        ImportPlan plan;
    };
    std::vector<Case> cases;
    for (auto const& file : images) {
        fs::path path = fs::path("/home/fernando/Descargas") / file;
        int width = 0, height = 0, channels = 0;
        std::uint8_t* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
        if (!pixels) {
            std::cout << file << ": no se pudo leer\n";
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
        auto result = buildPlan(source, options);
        if (!result) {
            std::cout << file << ": " << result.error << "\n";
            continue;
        }
        cases.push_back({file, std::move(result.plan)});
    }

    std::vector<float> sideGates{1.6f, 2.0f, 2.5f, 3.0f, 4.0f, 1e9f};
    std::vector<float> angleGates{3.f, 5.f, 7.f, 10.f, 15.f};
    std::vector<float> diaMins{0.5f, 0.65f, 0.75f, 0.9f};
    if (census) {
        // censo de picos restantes: una fila por tira/triangulo girado con
        // lado fino <= 2.5 (los <=3.0 ya pasaron por normalizepaintspikes).
        std::cout << "imagen,kind,maxSide,minSide,folded\n";
        for (auto const& c : cases) {
            for (auto const& object : c.plan.staticObjects) {
                bool const shaped = object.kind == PrimitiveKind::Stroke ||
                    object.kind == PrimitiveKind::Triangle ||
                    object.kind == PrimitiveKind::WideTriangle;
                if (!shaped) continue;
                float const folded = foldedAngle(object.rotation);
                if (folded <= 5.f || folded >= 85.f) continue;
                if (std::min(object.width, object.height) > 2.5f) continue;
                std::cout << c.name << ","
                          << (object.kind == PrimitiveKind::Stroke ? "stroke"
                              : object.kind == PrimitiveKind::Triangle ? "tri" : "wide")
                          << "," << object.width << "," << object.height << ","
                          << folded << "\n";
            }
        }
        return 0;
    }
    // 6*5*4 = 120 combos x 8 imagenes = 960 evaluaciones (>350 pedidas).
    std::cout << "imagen,sideGate,angleGate,diaMin,picos0,picos1,grid0,grid1,objs0,objs1\n";
    long total = 0;
    double bestScore = -1e9;
    SweepParams best{1.6f, 5.f, 0.75f};
    for (auto const& c : cases) {
        auto const& cells = c.plan.frames.front().cells;
        int const width = c.plan.width, height = c.plan.height;
        int const spikes0 = countSpikes(c.plan.staticObjects, 2.5f);
        double const grid0 = gridFidelity(c.plan, c.plan.staticObjects);
        for (float side : sideGates) {
            for (float angle : angleGates) {
                for (float dia : diaMins) {
                    SweepParams params{side, angle, dia};
                    auto objects = normalizeVariant(
                        c.plan.staticObjects, cells, width, height, params);
                    int const spikes1 = countSpikes(objects, 2.5f);
                    double const grid1 = gridFidelity(c.plan, objects);
                    std::cout << c.name << "," << side << "," << angle << "," << dia
                              << "," << spikes0 << "," << spikes1 << "," << std::fixed
                              << std::setprecision(2) << grid0 << "," << grid1
                              << "," << c.plan.staticObjects.size() << "," << objects.size()
                              << "\n";
                    ++total;
                    // puntuacion: cada pico quitado vale 1, cada 0.1 de rejilla
                    // perdida resta 2, cada objeto de mas resta 0.05.
                    double const score = (spikes0 - spikes1) -
                        20.0 * std::max(0.0, grid0 - grid1) -
                        0.05 * static_cast<double>(
                                      static_cast<long>(objects.size()) -
                                      static_cast<long>(c.plan.staticObjects.size()));
                    if (score > bestScore) {
                        bestScore = score;
                        best = params;
                    }
                }
            }
        }
    }
    std::cout << "EVALS=" << total << " BEST side=" << best.sideGate
              << " angle=" << best.angleGate << " diaMin=" << best.diaMin
              << " score=" << bestScore << "\n";
    return 0;
}
