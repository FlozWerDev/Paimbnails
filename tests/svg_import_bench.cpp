// Banco de pruebas y auditoria del conversor directo SVG -> objetos de GD.
// Mide fidelidad (PSNR, delta cerca/lejos, bordes Sobel, huecos), coste en objetos,
// auditoria de objetos ocultos/microscopicos, y exporta el .level real de GD + PNGs.
//   g++ -std=c++23 -O2 -o svg_bench tests/svg_import_bench.cpp -lpthread
//   ./svg_bench <archivo.svg> <referencia.png> [--dump <carpeta>] [--colors 24] [--dim 256]

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "../src/utils/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../src/utils/stb_image_write.h"

#include "../src/features/gif-import/services/GifSvgImporter.hpp"
#include "../src/features/gif-import/services/GifPrimitiveSerializer.hpp"
#include "../src/features/gif-import/services/ColorSpace.cpp"
#include "../src/features/gif-import/services/GifParallel.cpp"
#include "../src/features/gif-import/services/GifShapeRaster.cpp"
#include "../src/features/gif-import/services/GifVectorMath.cpp"
#include "../src/features/gif-import/services/GifMotionPlanner.cpp"
#include "../src/features/gif-import/services/GifStampCatalog.cpp"
#include "../src/features/gif-import/services/GifArtVectorizer.cpp"
#include "../src/features/gif-import/services/GifSvgImporter.cpp"

using namespace paimon::gifimport;
namespace fs = std::filesystem;

namespace {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    bool valid() const { return width > 0 && height > 0; }
};

Image loadImage(fs::path const& path) {
    Image img;
    int ch = 0;
    auto* px = stbi_load(path.string().c_str(), &img.width, &img.height, &ch, 4);
    if (!px) return {};
    img.rgba.assign(px, px + static_cast<std::size_t>(img.width) * img.height * 4);
    stbi_image_free(px);
    return img;
}

Image resampleBilinear(Image const& src, int w, int h) {
    Image out{w, h, std::vector<std::uint8_t>(static_cast<std::size_t>(w) * h * 4, 0)};
    for (int y = 0; y < h; ++y) {
        double fy = (y + 0.5) * src.height / h - 0.5;
        int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, src.height - 1);
        int y1 = std::min(y0 + 1, src.height - 1);
        double wy = std::clamp(fy - y0, 0.0, 1.0);
        for (int x = 0; x < w; ++x) {
            double fx = (x + 0.5) * src.width / w - 0.5;
            int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, src.width - 1);
            int x1 = std::min(x0 + 1, src.width - 1);
            double wx = std::clamp(fx - x0, 0.0, 1.0);
            auto idx = [&](int ix, int iy) {
                return (static_cast<std::size_t>(iy) * src.width + ix) * 4;
            };
            std::size_t i00 = idx(x0, y0), i10 = idx(x1, y0), i01 = idx(x0, y1), i11 = idx(x1, y1);
            std::size_t dst = (static_cast<std::size_t>(y) * w + x) * 4;
            for (int c = 0; c < 4; ++c) {
                double v = (src.rgba[i00 + c] * (1 - wx) + src.rgba[i10 + c] * wx) * (1 - wy) +
                           (src.rgba[i01 + c] * (1 - wx) + src.rgba[i11 + c] * wx) * wy;
                out.rgba[dst + c] = static_cast<std::uint8_t>(std::clamp(std::lround(v), 0l, 255l));
            }
        }
    }
    return out;
}

double meanDelta(Image const& a, Image const& b) {
    double sum = 0.0;
    std::size_t count = static_cast<std::size_t>(a.width) * a.height;
    for (std::size_t i = 0; i < count; ++i) {
        std::size_t p = i * 4;
        for (int c = 0; c < 3; ++c) {
            sum += std::abs(static_cast<int>(a.rgba[p + c]) - static_cast<int>(b.rgba[p + c])) / 255.0;
        }
    }
    return sum / (static_cast<double>(count) * 3.0);
}

double psnrDb(Image const& ref, Image const& ren) {
    double mse = 0.0;
    std::size_t count = static_cast<std::size_t>(ref.width) * ref.height;
    for (std::size_t i = 0; i < count; ++i) {
        std::size_t p = i * 4;
        for (int c = 0; c < 3; ++c) {
            double d = static_cast<double>(ref.rgba[p + c]) - static_cast<double>(ren.rgba[p + c]);
            mse += d * d;
        }
    }
    mse /= static_cast<double>(count) * 3.0;
    return mse <= 1e-9 ? 99.0 : 10.0 * std::log10((255.0 * 255.0) / mse);
}

double edgeMae(Image const& ref, Image const& ren) {
    auto lum = [](Image const& img, int x, int y) {
        std::size_t p = (static_cast<std::size_t>(y) * img.width + x) * 4;
        return 0.299 * img.rgba[p] + 0.587 * img.rgba[p + 1] + 0.114 * img.rgba[p + 2];
    };
    double sum = 0.0;
    std::size_t n = 0;
    for (int y = 1; y + 1 < ref.height; ++y) {
        for (int x = 1; x + 1 < ref.width; ++x) {
            auto sobel = [&](Image const& img) {
                double gx = -lum(img, x - 1, y - 1) + lum(img, x + 1, y - 1)
                            - 2 * lum(img, x - 1, y) + 2 * lum(img, x + 1, y)
                            - lum(img, x - 1, y + 1) + lum(img, x + 1, y + 1);
                double gy = -lum(img, x - 1, y - 1) - 2 * lum(img, x, y - 1) - lum(img, x + 1, y - 1)
                            + lum(img, x - 1, y + 1) + 2 * lum(img, x, y + 1) + lum(img, x + 1, y + 1);
                return std::hypot(gx, gy) / 4.0;
            };
            sum += std::abs(sobel(ref) - sobel(ren));
            ++n;
        }
    }
    return n > 0 ? sum / static_cast<double>(n) : 0.0;
}

double farDelta(Image const& a, Image const& b) {
    Image sa = resampleBilinear(a, 64, 64);
    Image sb = resampleBilinear(b, 64, 64);
    return meanDelta(sa, sb);
}

struct AuditSummary {
    int occluded = 0;
    int microLt4 = 0;
    int microLt9 = 0;
    int transparentHoles = 0;
    float maxSkew = 0.f;
    double minVisArea1000 = 1e9;
};

AuditSummary auditPlan(ImportPlan const& plan, int scale) {
    AuditSummary s;
    int outW = plan.width * scale;
    int outH = plan.height * scale;
    double pxTo1000 = (1000.0 / std::max(outW, outH)) * (1000.0 / std::max(outW, outH));

    std::vector<Primitive const*> sorted;
    sorted.reserve(plan.staticObjects.size());
    for (auto const& o : plan.staticObjects) {
        sorted.push_back(&o);
        s.maxSkew = std::max(s.maxSkew, std::abs(o.skew));
    }
    std::stable_sort(sorted.begin(), sorted.end(), [](auto* a, auto* b) { return a->layer < b->layer; });

    std::vector<std::int32_t> owner(static_cast<std::size_t>(outW) * outH, -1);
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        auto shape = xformOf(*sorted[i]);
        forEachSample(shape, plan.width, plan.height, scale, [&](int x, int y) {
            owner[static_cast<std::size_t>(y) * outW + x] = static_cast<std::int32_t>(i);
            return false;
        });
    }
    std::vector<std::size_t> vis(sorted.size(), 0);
    for (int o : owner) {
        if (o < 0) ++s.transparentHoles;
        else ++vis[static_cast<std::size_t>(o)];
    }
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        if (vis[i] == 0) {
            ++s.occluded;
        } else {
            double area1000 = vis[i] * pxTo1000;
            s.minVisArea1000 = std::min(s.minVisArea1000, area1000);
            if (area1000 < 4.0) ++s.microLt4;
            if (area1000 < 9.0) ++s.microLt9;
        }
    }
    if (sorted.empty()) s.minVisArea1000 = 0.0;
    return s;
}

void writeLevelFile(fs::path const& path, ImportPlan const& plan) {
    std::string level = "kA6,1,kA7,1,kS38,";
    for (std::size_t i = 0; i < plan.palette.size(); ++i) {
        auto c = plan.palette[i];
        level += "1_" + std::to_string(c.r) + "_2_" + std::to_string(c.g) +
                 "_3_" + std::to_string(c.b) + "_6_" + std::to_string(i + 1) + "_7_1|";
    }
    level += ';';
    auto shapes = defaultShapes();
    PrimitivePlacement placement;
    placement.imageHeight = plan.height;
    for (auto const& obj : plan.staticObjects) {
        PrimitiveStyle style{shapes[shapeIndex(obj.kind)], obj.color + 1, nullptr};
        appendSerializedPrimitive(level, obj, style, placement);
    }
    std::ofstream(path) << level;
}

void writeCropZoom(Image const& img, int x0, int y0, int cw, int ch, int zoom, fs::path const& outPath) {
    int ow = cw * zoom, oh = ch * zoom;
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(ow) * oh * 4, 0);
    for (int y = 0; y < oh; ++y) {
        int sy = std::clamp(y0 + y / zoom, 0, img.height - 1);
        for (int x = 0; x < ow; ++x) {
            int sx = std::clamp(x0 + x / zoom, 0, img.width - 1);
            std::size_t src = (static_cast<std::size_t>(sy) * img.width + sx) * 4;
            std::size_t dst = (static_cast<std::size_t>(y) * ow + x) * 4;
            for (int c = 0; c < 4; ++c) buf[dst + c] = img.rgba[src + c];
        }
    }
    stbi_write_png(outPath.string().c_str(), ow, oh, 4, buf.data(), ow * 4);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "uso: svg_bench <archivo.svg> <referencia.png> [--dump <carpeta>]\n";
        return 1;
    }
    fs::path svgPath = argv[1];
    fs::path refPath = argv[2];
    fs::path dumpDir;
    for (int i = 3; i < argc; ++i) {
        if (std::string(argv[i]) == "--dump" && i + 1 < argc) dumpDir = argv[++i];
    }
    if (!dumpDir.empty()) fs::create_directories(dumpDir);

    Image refRaw = loadImage(refPath);
    if (!refRaw.valid()) {
        std::cerr << "No se pudo leer la imagen de referencia: " << refPath << '\n';
        return 1;
    }

    struct Profile {
        std::string name;
        SvgOptions opt;
    };

    SvgOptions budgetMatch;
    budgetMatch.dimension = 256;
    budgetMatch.maxColors = 24;
    budgetMatch.objectBudget = 1870;
    budgetMatch.curveTolerance = 1.15f;
    budgetMatch.simplifyTolerance = 1.75f;
    budgetMatch.minTriangleArea = 22.0f;
    budgetMatch.quadTolerance = 0.72f;
    budgetMatch.ellipseTolerance = 1.5f;
    budgetMatch.minRegionArea = 35.f;
    budgetMatch.minVisibleArea = 8.0f;
    budgetMatch.minVisualImpact = 2.6f;

    SvgOptions compact;
    compact.dimension = 256;
    compact.maxColors = 24;
    compact.curveTolerance = 0.90f;
    compact.simplifyTolerance = 1.45f;
    compact.minTriangleArea = 15.0f;
    compact.quadTolerance = 0.65f;
    compact.ellipseTolerance = 1.4f;
    compact.minRegionArea = 26.f;
    compact.minVisibleArea = 6.5f;
    compact.minVisualImpact = 2.2f;

    SvgOptions balanced;
    balanced.dimension = 256;
    balanced.maxColors = 24;
    balanced.curveTolerance = 0.55f;
    balanced.simplifyTolerance = 0.95f;
    balanced.minTriangleArea = 9.0f;
    balanced.quadTolerance = 0.55f;
    balanced.ellipseTolerance = 1.25f;
    balanced.minRegionArea = 20.f;
    balanced.minVisibleArea = 5.0f;
    balanced.minVisualImpact = 1.8f;

    SvgOptions ultra48 = compact;
    ultra48.maxColors = 48;
    ultra48.minColorDistance = 0.015f;

    std::vector<Profile> profiles{
        {"match-1870", budgetMatch},
        {"compact-24c", compact},
        {"balanced-24c", balanced},
        {"compact-48c", ultra48},
    };

    std::cout << std::left << std::setw(15) << "perfil"
              << std::right << std::setw(8) << "objs"
              << std::setw(7) << "blk"
              << std::setw(7) << "tri"
              << std::setw(6) << "cir"
              << std::setw(6) << "col"
              << std::setw(8) << "PSNR"
              << std::setw(8) << "PSNRaa"
              << std::setw(9) << "cerca"
              << std::setw(9) << "lejos"
              << std::setw(9) << "bordes"
              << std::setw(6) << "huec"
              << std::setw(6) << "occl"
              << std::setw(7) << "mic<4"
              << std::setw(7) << "skewM"
              << std::setw(7) << "ms" << '\n';

    for (auto const& prof : profiles) {
        auto res = buildSvgPlanFromFile(svgPath, prof.opt);
        if (!res) {
            std::cerr << prof.name << " ERROR: " << res.error << '\n';
            continue;
        }
        auto const& plan = res.plan;
        constexpr int scale = 4; // 256*4 = 1024x1024
        Image rendered{plan.width * scale, plan.height * scale, renderPlanFrame(plan, 0, scale, false)};
        Image renderedAA{plan.width * scale, plan.height * scale, renderPlanFrame(plan, 0, scale, true)};
        Image ref = resampleBilinear(refRaw, rendered.width, rendered.height);

        double near = meanDelta(renderedAA, ref);
        double far = farDelta(renderedAA, ref);
        double psnr = psnrDb(ref, rendered);
        double psnrAA = psnrDb(ref, renderedAA);
        double edge = edgeMae(ref, renderedAA);
        auto audit = auditPlan(plan, scale);

        std::cout << std::left << std::setw(15) << prof.name
                  << std::right << std::setw(8) << plan.totalObjects
                  << std::setw(7) << plan.blockObjects
                  << std::setw(7) << plan.triangleObjects
                  << std::setw(6) << plan.circleObjects
                  << std::setw(6) << plan.palette.size()
                  << std::setw(8) << std::fixed << std::setprecision(2) << psnr
                  << std::setw(8) << std::setprecision(2) << psnrAA
                  << std::setw(9) << std::setprecision(4) << near
                  << std::setw(9) << far
                  << std::setw(9) << std::setprecision(3) << edge
                  << std::setw(6) << audit.transparentHoles
                  << std::setw(6) << audit.occluded
                  << std::setw(7) << audit.microLt4
                  << std::setw(7) << std::setprecision(1) << audit.maxSkew
                  << std::setw(7) << std::setprecision(1) << res.stats.elapsedMs << '\n';

        std::cout << "  stats: planar=" << res.stats.planarPartition
                  << " shapes=" << res.stats.parsedShapes
                  << " -> mergedRegions=" << res.stats.mergedRegions
                  << " absorbedMicro=" << res.stats.absorbedMicroRegions
                  << " elimHoles=" << res.stats.eliminatedHoles
                  << " skipBackdrop=" << res.stats.skippedBackdropRegions
                  << " ellipses=" << res.stats.ellipsesFitted
                  << " quadsMerged=" << res.stats.parallelogramsMerged
                  << " pruned(occl=" << res.stats.occludedPruned
                  << ",micro=" << res.stats.microPruned << ")\n";

        if (!dumpDir.empty()) {
            auto outPng = dumpDir / (prof.name + ".png");
            stbi_write_png(outPng.string().c_str(), renderedAA.width, renderedAA.height, 4,
                           renderedAA.rgba.data(), renderedAA.width * 4);
            writeLevelFile(dumpDir / (prof.name + ".level"), plan);
            if (prof.name == "compact-24c") {
                writeCropZoom(renderedAA, 380, 320, 260, 260, 2, dumpDir / "crop_eye.png");
                writeCropZoom(renderedAA, 650, 40, 320, 320, 2, dumpDir / "crop_constellation.png");
            }
        }
    }
    return 0;
}
