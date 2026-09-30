// uso: /tmp/prev <img> [dim] [colors] [pixel] [scale]
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "../src/utils/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../src/utils/stb_image_write.h"

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

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::string const name = argv[1];
    int const dim = argc > 2 ? std::stoi(argv[2]) : 64;
    int const colors = argc > 3 ? std::stoi(argv[3]) : 16;
    bool const pixelSampling = argc > 4 && std::string(argv[4]) == "pixel";
    int const displayScale = argc > 5 ? std::clamp(std::stoi(argv[5]), 1, 8) : 1;
    fs::path path = fs::path(name);
    if (path.is_relative()) path = fs::path("resources") / name;
    int w = 0, h = 0, ch = 0;
    std::uint8_t* px = stbi_load(path.string().c_str(), &w, &h, &ch, 4);
    if (!px) return 1;
    SourceAnimation src;
    src.width = w;
    src.height = h;
    src.frames.resize(1);
    src.frames.front().delayMs = 100;
    src.frames.front().rgba.assign(px, px + static_cast<std::size_t>(w) * h * 4);
    stbi_image_free(px);
    Options op;
    op.mode = ImportMode::Paint;
    op.maxDimension = dim;
    op.maxColors = colors;
    op.sampling = pixelSampling ? SamplingMode::Pixel : SamplingMode::Smooth;
    op.objectBudget = 12000;
    op.background = BackgroundMode::Keep;
    auto r = buildPlan(src, op);
    if (!r) return 1;
    auto const& plan = r.plan;
    // escala solo la vista, no la rejilla.
    int const scale = displayScale;
    int const W = plan.width * scale, H = plan.height * scale;
    auto const rgba = renderPlanFrame(plan, 0, scale, true);
    int const oH = H, oW = static_cast<int>(std::lround(
        static_cast<double>(w) * oH / h));
    std::vector<std::uint8_t> orig(static_cast<std::size_t>(oW) * oH * 4);
    for (int y = 0; y < oH; ++y)
        for (int x = 0; x < oW; ++x) {
            int const sx = std::min(w - 1, x * w / oW);
            int const sy = std::min(h - 1, y * h / oH);
            auto const* s =
                src.frames.front().rgba.data() +
                (static_cast<std::size_t>(sy) * w + sx) * 4;
            auto* d = orig.data() + (static_cast<std::size_t>(y) * oW + x) * 4;
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = s[3];
        }
    int const gap = 6, outW = oW + gap + W, outH = std::max(oH, H);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(outW) * outH * 4, 0);
    auto blit = [&](std::vector<std::uint8_t> const& img, int iw, int ih, int ox) {
        for (int y = 0; y < ih; ++y)
            for (int x = 0; x < iw; ++x) {
                auto const* s =
                    img.data() + (static_cast<std::size_t>(y) * iw + x) * 4;
                float const a = s[3] / 255.f;
                auto* d = out.data() +
                    (static_cast<std::size_t>(y) * outW + ox + x) * 4;
                d[0] = static_cast<std::uint8_t>(s[0] * a + 128 * (1 - a));
                d[1] = static_cast<std::uint8_t>(s[1] * a + 128 * (1 - a));
                d[2] = static_cast<std::uint8_t>(s[2] * a + 128 * (1 - a));
                d[3] = 255;
            }
    };
    blit(orig, oW, oH, 0);
    blit(rgba, W, H, oW + gap);
    fs::create_directories("/tmp/prev320");
    std::string const dst =
        "/tmp/prev320/" + path.stem().string() + "-320.png";
    stbi_write_png(dst.c_str(), outW, outH, 4, out.data(), outW * 4);
    std::cout << name << " rejilla=" << plan.width << "x" << plan.height
              << " objetos=" << plan.totalObjects
              << " (estaticos=" << plan.staticObjects.size() << ")"
              << " escala=" << scale << " -> " << dst << " (" << outW << "x"
              << outH << ")\n";
    return 0;
}
