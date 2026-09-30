// probe: clasifica pares fusionables de pintura por causa de rechazo.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
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

int main() {
    std::vector<std::string> images{
        "paim_Daily.png", "paim_progTierHex.png", "paim_progTierRound.png",
        "paim_progTierShield.png", "paim_vsFrame.png", "paim_capturadora.png",
    };
    long totalPairs = 0, crossBg = 0, rotDiff = 0, counted = 0;
    for (auto const& file : images) {
        fs::path path = fs::path("resources") / file;
        int width = 0, height = 0, channels = 0;
        std::uint8_t* pixels =
            stbi_load(path.string().c_str(), &width, &height, &channels, 4);
        if (!pixels) continue;
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
        options.maxDimension = 64;
        options.maxColors = 16;
        options.objectBudget = 12000;
        options.background = BackgroundMode::Keep;
        auto result = buildPlan(source, options);
        if (!result) continue;
        auto const& objs = result.plan.staticObjects;
        std::vector<Primitive const*> rects;
        for (auto const& o : objs) {
            if (o.kind == PrimitiveKind::Stroke || o.kind == PrimitiveKind::Block)
                rects.push_back(&o);
        }
        long pairs = 0, bg = 0, rot = 0;
        for (std::size_t i = 0; i < rects.size(); ++i) {
            for (std::size_t j = i + 1; j < rects.size(); ++j) {
                auto const& a = *rects[i];
                auto const& b = *rects[j];
                if (a.color != b.color) continue;
                float diff =
                    std::fmod(std::abs(a.rotation - b.rotation), 180.f);
                diff = std::min(diff, 180.f - diff);
                if (diff > 0.05f) {
                    ++rot;
                    continue;
                }
                float const ang =
                    a.rotation * paimon::gifimport::kPi / 180.f;
                float const c = std::cos(ang), s = std::sin(ang);
                auto proj = [&](Primitive const& o) {
                    float maj = o.x * c + o.y * s, mnr = -o.x * s + o.y * c;
                    return std::array<float, 4>{
                        maj - o.width * 0.5f, maj + o.width * 0.5f,
                        mnr - o.height * 0.5f, mnr + o.height * 0.5f};
                };
                auto const A = proj(a), B = proj(b);
                bool major = std::abs(A[2] - B[2]) < 0.01f &&
                    std::abs(A[3] - B[3]) < 0.01f &&
                    (std::abs(A[1] - B[0]) < 0.01f ||
                     std::abs(B[1] - A[0]) < 0.01f);
                bool minor = std::abs(A[0] - B[0]) < 0.01f &&
                    std::abs(A[1] - B[1]) < 0.01f &&
                    (std::abs(A[3] - B[2]) < 0.01f ||
                     std::abs(B[3] - A[2]) < 0.01f);
                if (!major && !minor) continue;
                ++pairs;
                if ((a.layer < 0) != (b.layer < 0)) ++bg;
                if (pairs <= 40) {
                    std::cout.precision(17);
                    std::cout << "  par i=" << i << " j=" << j
                              << " (" << (int)a.kind << ",L" << a.layer
                              << ",c" << a.color << ") [" << a.x << "," << a.y
                              << " " << a.width << "x" << a.height << " r"
                              << a.rotation << "] <-> (" << (int)b.kind << ",L"
                              << b.layer << ",c" << b.color << ") [" << b.x
                              << "," << b.y << " " << b.width << "x"
                              << b.height << " r" << b.rotation << "] "
                              << (major ? "MAYOR" : "minor") << "\n";
                }
            }
        }
        std::cout << file << " rects=" << rects.size() << " pares=" << pairs
                  << " cruzaFondo=" << bg << " giroDescartado=" << rot << "\n";
        totalPairs += pairs;
        crossBg += bg;
        counted += 1;
    }
    std::cout << "TOTAL pares=" << totalPairs << " cruzaFondo=" << crossBg
              << " (" << (totalPairs ? 100 * crossBg / totalPairs : 0) << "%)\n";
    return 0;
}
