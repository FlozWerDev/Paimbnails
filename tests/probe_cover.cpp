// probe: lista primitivas que cubren cada celda f, de mayor a menor capa.
#include <algorithm>
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
        "paim_progPlate6.png",
    };
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
        auto const& plan = result.plan;
        auto const preview = renderPlanFrame(plan, 0, 1);
        auto const& cells = plan.frames.front().cells;
        auto const& objs = plan.staticObjects;
        // ascendente: la visible es la ultima que cubre.
        std::vector<std::size_t> order(objs.size());
        for (std::size_t i = 0; i < objs.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return objs[a].layer < objs[b].layer;
        });
        long shown = 0;
        for (int y = 0; y < plan.height && shown < 25; ++y) {
            for (int x = 0; x < plan.width && shown < 25; ++x) {
                int const ref =
                    cells[static_cast<std::size_t>(y) * plan.width + x];
                if (ref >= 0) continue;
                std::size_t const p =
                    (static_cast<std::size_t>(y) * plan.width + x) * 4;
                if (preview[p + 3] == 0) continue;
                std::cout << "F(" << x << "," << y << ") rgb("
                          << (int)preview[p] << "," << (int)preview[p + 1]
                          << "," << (int)preview[p + 2] << "):";
                int found = 0;
                for (auto it = order.rbegin(); it != order.rend() && found < 4; ++it) {
                    auto const& o = objs[*it];
                    auto const placed = xformOf(o);
                    if (!placed.contains(x + 0.5f, y + 0.5f)) continue;
                    std::cout << " [#" << *it << " k" << (int)o.kind << " c"
                              << o.color << " L" << o.layer << " "
                              << o.x << "," << o.y << " " << o.width << "x"
                              << o.height << " r" << o.rotation << "]";
                    ++found;
                }
                std::cout << "\n";
                ++shown;
            }
        }
    }
    return 0;
}
