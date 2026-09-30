// bucle de bordes del modo pintura: mide fidelidad solo en bordes
// (celdas junto a otro color + anillo de 1px) y parecido cromatico en ellos,
// barriendo dimensiones para la curva calidad vs objetos.
// uso: g++ -std=c++23 -o2 -o /tmp/edge_sweep tests/edge_sweep.cpp && /tmp/edge_sweep [dims...]
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

// mascara de borde a escala de render: pixel cuyo indice de celda difiere de
// algun 4-vecino, dilatada con un anillo de 1px para pillar derrames.
std::vector<char> edgeMask(ImportPlan const& plan, int scale) {
    int const W = plan.width * scale, H = plan.height * scale;
    auto const& cells = plan.frames.front().cells;
    auto idx = [&](int x, int y) {
        return cells[static_cast<std::size_t>(y / scale) * plan.width + x / scale];
    };
    std::vector<char> edge(static_cast<std::size_t>(W) * H, 0);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            int const c = idx(x, y);
            if ((x + 1 < W && idx(x + 1, y) != c) ||
                (x > 0 && idx(x - 1, y) != c) ||
                (y + 1 < H && idx(x, y + 1) != c) ||
                (y > 0 && idx(x, y - 1) != c))
                edge[static_cast<std::size_t>(y) * W + x] = 1;
        }
    // dilatar 1px.
    std::vector<char> out = edge;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            if (!edge[static_cast<std::size_t>(y) * W + x]) continue;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < W && ny < H)
                        out[static_cast<std::size_t>(ny) * W + nx] = 1;
                }
        }
    return out;
}

struct EdgeScore {
    double exact = 100.0;  // % pixeles de borde con color exacto
    double delta = 0.0;    // distancia rgb media en borde (0-441)
    double cover = 0.0;    // % pixeles de borde que mide
    double spill = 0.0;    // % borde con color equivocado (derrame vecino)
    double gap = 0.0;      // % borde sin cubrir (hueco/transparente)
    double parecido = 0.0;  // distancia rgb media vs original en borde
    double cerca = 0.0;    // % borde: derrame con su color al lado (suavizado)
    double lejos = 0.0;    // % borde: derrame sin su color al lado (invasion)
};

EdgeScore edgeFidelity(
    ImportPlan const& plan,
    std::vector<std::uint8_t> const& srcRgba,
    int srcW,
    int srcH
) {
    constexpr int scale = 2;
    auto const preview = renderPlanFrame(plan, 0, scale);
    int const W = plan.width * scale, H = plan.height * scale;
    auto const mask = edgeMask(plan, scale);
    auto const& cells = plan.frames.front().cells;
    long hit = 0, total = 0, spill = 0, gap = 0, nearCount = 0, farCount = 0;
    double acc = 0.0, accSrc = 0.0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            if (!mask[static_cast<std::size_t>(y) * W + x]) continue;
            int const index =
                cells[static_cast<std::size_t>(y / scale) * plan.width + x / scale];
            std::size_t const p = (static_cast<std::size_t>(y) * W + x) * 4;
            bool const visible = preview[p + 3] != 0;
            if (index < 0 && !visible) continue;
            ++total;
            if (index < 0 || !visible) {
                acc += 441.0;
                ++gap;
                continue;
            }
            auto const& col = plan.palette[static_cast<std::size_t>(index)];
            int dr = (int)preview[p] - col.r;
            int dg = (int)preview[p + 1] - col.g;
            int db = (int)preview[p + 2] - col.b;
            acc += std::sqrt(double(dr * dr + dg * dg + db * db));
            if (dr == 0 && dg == 0 && db == 0) ++hit;
            else {
                ++spill;
                // ¿el color pintado tiene alguna celda vecina (8) con ese
                // mismo color? si no, es invasion lejos del borde, no
                // suavizado de la orla.
                int painted = -1;
                for (std::size_t k = 0; k < plan.palette.size(); ++k) {
                    auto const& pc = plan.palette[k];
                    if (preview[p] == pc.r && preview[p + 1] == pc.g &&
                        preview[p + 2] == pc.b) {
                        painted = static_cast<int>(k);
                        break;
                    }
                }
                bool nearColor = false;
                if (painted >= 0) {
                    int ccx = x / scale, ccy = y / scale;
                    for (int ddy = -1; ddy <= 1 && !nearColor; ++ddy)
                        for (int ddx = -1; ddx <= 1 && !nearColor; ++ddx) {
                            int nx = ccx + ddx, ny = ccy + ddy;
                            if (nx < 0 || ny < 0 || nx >= plan.width ||
                                ny >= plan.height)
                                continue;
                            if (cells[static_cast<std::size_t>(ny) * plan.width +
                                      nx] == painted)
                                nearColor = true;
                        }
                }
                if (nearColor) ++nearCount;
                else ++farCount;
            }
            // parecido al original: muestra la fuente a tamaño de render.
            int sx = std::min(srcW - 1, x * srcW / W);
            int sy = std::min(srcH - 1, y * srcH / H);
            std::size_t const sp =
                (static_cast<std::size_t>(sy) * srcW + sx) * 4;
            int sr = (int)preview[p] - srcRgba[sp];
            int sg = (int)preview[p + 1] - srcRgba[sp + 1];
            int sb = (int)preview[p + 2] - srcRgba[sp + 2];
            // pixel sin cubrir: se veria el fondo, penaliza como negro.
            if (index < 0 || !visible) {
                sr = 0 - (int)srcRgba[sp];
                sg = 0 - (int)srcRgba[sp + 1];
                sb = 0 - (int)srcRgba[sp + 2];
            }
            accSrc += std::sqrt(double(sr * sr + sg * sg + sb * sb));
        }
    EdgeScore s;
    if (total > 0) {
        s.exact = 100.0 * hit / total;
        s.delta = acc / total;
        s.cover = 100.0 * total / (W * H);
        s.spill = 100.0 * spill / total;
        s.gap = 100.0 * gap / total;
        s.parecido = accSrc / total;
        s.cerca = 100.0 * nearCount / total;
        s.lejos = 100.0 * farCount / total;
    }
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<int> dims{64, 96, 128, 192, 256, 320};
    int colors = 16;
    bool pixel = false;
    bool dither = false;
    bool customDims = false;
    if (argc > 1) {
        dims.clear();
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg.rfind("--colors=", 0) == 0) {
                colors = std::stoi(arg.substr(9));
                continue;
            }
            if (arg == "--pixel") {
                pixel = true;
                continue;
            }
            if (arg == "--dither") {
                dither = true;
                continue;
            }
            if (!customDims) {
                customDims = true;
            }
            dims.push_back(std::stoi(arg));
        }
        if (!customDims) dims = {64, 96, 128, 192, 256, 320};
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
    std::cout << "imagen,dim,rejilla,objetos,bordeExact,bordeDelta,bordeCover,derrame,hueco,parecido,cerca,lejos\n";
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
            std::vector<std::uint8_t> srcRgba(
                pixels, pixels + static_cast<std::size_t>(width) * height * 4);
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
            options.maxColors = colors;
            options.objectBudget = 12000;
            options.background = BackgroundMode::Keep;
            options.sampling =
                pixel ? SamplingMode::Pixel : SamplingMode::Smooth;
            options.dither = dither;
            auto result = buildPlan(source, options);
            if (!result) {
                std::cout << file << "," << dim << ",ERROR:" << result.error << "\n";
                continue;
            }
            EdgeScore s = edgeFidelity(result.plan, srcRgba, width, height);
            std::cout << file << "," << dim << "," << result.plan.width << "x"
                      << result.plan.height << "," << result.plan.staticObjects.size()
                      << "," << std::fixed << std::setprecision(2) << s.exact << ","
                      << std::setprecision(3) << s.delta << "," << std::setprecision(1)
                      << s.cover << "," << s.spill << "," << s.gap << ","
                      << std::setprecision(2) << s.parecido << ","
                      << std::setprecision(1) << s.cerca << "," << s.lejos
                      << "\n";
        }
    }
    return 0;
}
