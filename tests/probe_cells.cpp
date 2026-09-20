// Confusion por color del modo pintura.
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
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
        "paim_progPlate6.png", "paim_progPlate2.png", "paim_vsFrame.png",
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
        std::map<int, std::array<int, 3>> refColor;
        for (int y = 0; y < plan.height; ++y)
            for (int x = 0; x < plan.width; ++x) {
                int const c =
                    cells[static_cast<std::size_t>(y) * plan.width + x];
                if (c < 0 || refColor.count(c)) continue;
                auto const& col = plan.palette[static_cast<std::size_t>(c)];
                refColor[c] = {col.r, col.g, col.b};
            }
        struct Acc {
            long total = 0, ok = 0, hueco = 0, derrame = 0, cambio = 0;
            std::map<int, long> pintadoComo;
        };
        std::map<int, Acc> acc;
        for (int y = 0; y < plan.height; ++y)
            for (int x = 0; x < plan.width; ++x) {
                int const ref =
                    cells[static_cast<std::size_t>(y) * plan.width + x];
                std::size_t const p =
                    (static_cast<std::size_t>(y) * plan.width + x) * 4;
                bool const visible = preview[p + 3] != 0;
                int painted = -1;
                if (visible) {
                    for (std::size_t k = 0; k < plan.palette.size(); ++k) {
                        auto const& pc = plan.palette[k];
                        if (preview[p] == pc.r && preview[p + 1] == pc.g &&
                            preview[p + 2] == pc.b) {
                            painted = static_cast<int>(k);
                            break;
                        }
                    }
                    if (painted < 0) painted = -2;  // mezcla no exacta
                }
                if (ref < 0 && !visible) continue;
                if (ref >= 0) {
                    auto& a = acc[ref];
                    ++a.total;
                    if (painted == ref) ++a.ok;
                    else if (!visible || painted < 0) ++a.hueco;
                    else {
                        ++a.derrame;
                        ++a.pintadoComo[painted];
                    }
                } else {
                    auto& a = acc[ref];
                    ++a.total;
                    if (!visible || painted < 0) ++a.ok;
                    else {
                        ++a.cambio;
                        ++a.pintadoComo[painted];
                    }
                }
            }
        std::cout << "== " << file << " (" << plan.width << "x" << plan.height
                  << ", objs=" << plan.staticObjects.size() << ")\n";
        if (plan.width == 64 && plan.height == 64) {
            for (int y = 0; y < plan.height; ++y) {
                for (int x = 0; x < plan.width; ++x) {
                    int const ref =
                        cells[static_cast<std::size_t>(y) * plan.width + x];
                    std::size_t const p =
                        (static_cast<std::size_t>(y) * plan.width + x) * 4;
                    bool const visible = preview[p + 3] != 0;
                    int painted = -1;
                    if (visible) {
                        for (std::size_t k = 0; k < plan.palette.size(); ++k) {
                            auto const& pc = plan.palette[k];
                            if (preview[p] == pc.r &&
                                preview[p + 1] == pc.g &&
                                preview[p + 2] == pc.b) {
                                painted = static_cast<int>(k);
                                break;
                            }
                        }
                        if (painted < 0) painted = -2;
                    }
                    char ch = '.';
                    if (ref < 0 && (visible && painted >= 0)) ch = 'F';
                    else if (ref >= 0 && painted != ref) {
                        ch = (!visible || painted < 0) ? 'H' : 'A' + (painted % 26);
                    }
                    std::cout << ch;
                }
                std::cout << "\n";
            }
        }
        for (auto const& [c, a] : acc) {
            double const bad =
                a.total ? 100.0 * (a.total - a.ok) / a.total : 0.0;
            std::cout << "  ref c" << c;
            if (c >= 0) {
                auto const& rgb = refColor[c];
                std::cout << " rgb(" << rgb[0] << "," << rgb[1] << ","
                          << rgb[2] << ")";
            } else {
                std::cout << " fondo";
            }
            std::cout << " n=" << a.total << " mal=" << bad << "% (hueco="
                      << a.hueco << " derrame=" << a.derrame
                      << " cambio=" << a.cambio << ")";
            for (auto const& [p, n] : a.pintadoComo)
                std::cout << " ->c" << p << ":" << n;
            std::cout << "\n";
        }
    }
    return 0;
}
