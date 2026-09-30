// sonda de fusion: ¿cuantos objetos se ahorran uniendo tiras/bloques
// colineales del mismo color sin cambiar ni un pixel cubierto?
// uso: g++ -std=c++23 -o2 -o /tmp/merge_probe tests/merge_probe.cpp && /tmp/merge_probe [dims...]
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

// solo rectangulos rectos del mismo color y capa cuyo envolvente es tambien
// rectangulo y no tapa centros de otro color: fusion sin cambiar un pixel.
bool tryMergeBlocks(
    Primitive const& a,
    Primitive const& b,
    std::vector<int> const& cells,
    int width,
    int height,
    Primitive& out
) {
    if (a.kind != PrimitiveKind::Block || b.kind != PrimitiveKind::Block) return false;
    if (a.color != b.color || a.layer != b.layer) return false;
    if (a.rotation != 0.f || b.rotation != 0.f) return false;
    float ax0 = a.x - a.width / 2, ax1 = a.x + a.width / 2;
    float ay0 = a.y - a.height / 2, ay1 = a.y + a.height / 2;
    float bx0 = b.x - b.width / 2, bx1 = b.x + b.width / 2;
    float by0 = b.y - b.height / 2, by1 = b.y + b.height / 2;
    // adyacentes o solapados en ambos ejes (tolerancia de costura).
    if (ax1 < bx0 - 0.05f || bx1 < ax0 - 0.05f) return false;
    if (ay1 < by0 - 0.05f || by1 < ay0 - 0.05f) return false;
    float x0 = std::min(ax0, bx0), x1 = std::max(ax1, bx1);
    float y0 = std::min(ay0, by0), y1 = std::max(ay1, by1);
    float areaBox = (x1 - x0) * (y1 - y0);
    float areaSum =
        (ax1 - ax0) * (ay1 - ay0) + (bx1 - bx0) * (by1 - by0);
    // el envolvente no puede crecer mas de un 5%: nada de pintar de mas.
    if (areaBox > areaSum * 1.05f) return false;
    Primitive m{
        (x0 + x1) / 2, (y0 + y1) / 2, x1 - x0, y1 - y0, 0.f, a.color,
        PrimitiveKind::Block, a.layer};
    // ...y ni siquiera ese 5%: solo celdas del propio color.
    auto placed = xformOf(m);
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

// tiras giradas iguales (mismo angulo y grosor), colineales y contiguas: una
// sola tira las cubre a las dos si el hueco entre ellas es del mismo color.
bool tryMergeStrips(
    Primitive const& a,
    Primitive const& b,
    std::vector<int> const& cells,
    int width,
    int height,
    Primitive& out
) {
    if (a.kind != PrimitiveKind::Stroke || b.kind != PrimitiveKind::Stroke) return false;
    if (a.color != b.color || a.layer != b.layer) return false;
    if (a.rotation != b.rotation) return false;
    float const thickA = std::min(a.width, a.height);
    float const thickB = std::min(b.width, b.height);
    if (std::abs(thickA - thickB) > 0.05f) return false;
    bool const alongX = a.width >= a.height;
    float lenA = alongX ? a.width : a.height;
    float lenB = alongX ? b.width : b.height;
    float ang = a.rotation * 3.14159265f / 180.f;
    float dx = std::abs(std::cos(ang)), dy = std::abs(std::sin(ang));
    float ux = alongX ? dx : dy;  // eje largo unitario (1er cuadrante)
    float uy = alongX ? dy : dx;
    // eje corto: distancia entre centros proyectada; debe ser ~0.
    float nx = -uy, ny = ux;
    float relX = b.x - a.x, relY = b.y - a.y;
    if (std::abs(relX * nx + relY * ny) > thickA * 0.5f + 0.05f) return false;
    float t = relX * ux + relY * uy;  // separacion a lo largo del eje
    if (t < 0) return tryMergeStrips(b, a, cells, width, height, out);
    if (t > (lenA + lenB) / 2 + 1.05f) return false;  // hueco > 1 celda
    float total = t + (lenA + lenB) / 2;
    float cx = a.x + ux * (t / 2), cy = a.y + uy * (t / 2);
    Primitive m = a;
    m.x = cx;
    m.y = cy;
    if (alongX) m.width = total;
    else m.height = total;
    auto placed = xformOf(m);
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

std::vector<Primitive> mergePass(
    std::vector<Primitive> objects,
    std::vector<int> const& cells,
    int width,
    int height
) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < objects.size() && !changed; ++i) {
            for (std::size_t j = i + 1; j < objects.size(); ++j) {
                Primitive m;
                if (tryMergeBlocks(objects[i], objects[j], cells, width, height, m) ||
                    tryMergeStrips(objects[i], objects[j], cells, width, height, m)) {
                    objects[i] = m;
                    objects.erase(objects.begin() + j);
                    changed = true;
                    break;
                }
            }
        }
    }
    return objects;
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
    std::vector<int> dims{64, 128};
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
    std::cout << "imagen,dim,objs0,objs1,grid0,grid1\n";
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
            options.maxColors = 16;
            options.objectBudget = 12000;
            options.background = BackgroundMode::Keep;
            options.sampling = SamplingMode::Smooth;
            auto result = buildPlan(source, options);
            if (!result) {
                std::cout << file << "," << dim << ",ERROR\n";
                continue;
            }
            auto const& cells = result.plan.frames.front().cells;
            auto merged = mergePass(
                result.plan.staticObjects, cells, result.plan.width,
                result.plan.height);
            double g0 = gridFidelity(result.plan, result.plan.staticObjects);
            double g1 = gridFidelity(result.plan, merged);
            std::cout << file << "," << dim << ","
                      << result.plan.staticObjects.size() << "," << merged.size()
                      << "," << std::fixed << std::setprecision(2) << g0 << ","
                      << g1 << "\n";
        }
    }
    return 0;
}
