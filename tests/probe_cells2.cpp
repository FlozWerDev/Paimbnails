#include <filesystem>
#include <iostream>
#include <string>
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
int main(int argc, char** argv) {
    for (int a = 1; a < argc; ++a) {
        fs::path path = fs::path("resources") / argv[a];
        int w = 0, h = 0, ch = 0;
        std::uint8_t* px = stbi_load(path.string().c_str(), &w, &h, &ch, 4);
        if (!px) continue;
        SourceAnimation src; src.width = w; src.height = h;
        src.frames.resize(1); src.frames.front().delayMs = 100;
        src.frames.front().rgba.assign(px, px + (std::size_t)w * h * 4);
        stbi_image_free(px);
        Options op; op.mode = ImportMode::Paint; op.maxDimension = 64;
        op.maxColors = 16; op.objectBudget = 12000; op.background = BackgroundMode::Keep;
        auto r = buildPlan(src, op);
        if (!r) continue;
        auto const& plan = r.plan;
        auto const pv = renderPlanFrame(plan, 0, 1);
        auto const& cells = plan.frames.front().cells;
        long F = 0, H = 0, ok = 0;
        for (int y = 0; y < plan.height; ++y)
            for (int x = 0; x < plan.width; ++x) {
                int ref = cells[(std::size_t)y * plan.width + x];
                std::size_t p = ((std::size_t)y * plan.width + x) * 4;
                bool vis = pv[p + 3] != 0;
                int painted = -1;
                if (vis) {
                    for (std::size_t k = 0; k < plan.palette.size(); ++k) {
                        auto const& pc = plan.palette[k];
                        if (pv[p] == pc.r && pv[p+1] == pc.g && pv[p+2] == pc.b) { painted = (int)k; break; }
                    }
                    if (painted < 0) painted = -2;
                }
                if (ref < 0 && vis && painted >= 0) ++F;
                else if (ref >= 0 && painted != ref) { if (!vis || painted < 0) ++H; }
                else ++ok;
            }
        std::cout << argv[a] << " objs=" << plan.staticObjects.size() << " F=" << F << " H=" << H << "\n";
    }
    return 0;
}
