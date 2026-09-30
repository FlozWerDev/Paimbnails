// renderiza el plan de pintura de una imagen a png (escala 8) para comparar.
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

int main(int argc, char** argv) {
    if (argc < 4 || argc > 7) {
        std::cerr << "uso: render_compare <entrada> <salida.png> <escala> [dimension] [pixel|suave] [colores]\n";
        return 1;
    }
    int width = 0, height = 0, channels = 0;
    std::uint8_t* pixels = stbi_load(argv[1], &width, &height, &channels, 4);
    if (!pixels) {
        std::cerr << "no se pudo leer " << argv[1] << "\n";
        return 1;
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
    options.maxDimension = argc >= 5 ? std::stoi(argv[4]) : 64;
    options.maxColors = argc >= 7 ? std::stoi(argv[6]) : 16;
    options.objectBudget = 12000;
    options.background = BackgroundMode::Keep;
    options.sampling =
        (argc >= 6 && std::string(argv[5]) == "pixel") ? SamplingMode::Pixel
                                                      : SamplingMode::Smooth;
    auto result = buildPlan(source, options);
    if (!result) {
        std::cerr << result.error << "\n";
        return 1;
    }
    int const scale = std::stoi(argv[3]);
    auto const preview = renderPlanFrame(result.plan, 0, scale);
    int const outWidth = result.plan.width * scale;
    int const outHeight = result.plan.height * scale;
    std::cout << "objetos=" << result.plan.staticObjects.size() << "\n";
    if (!stbi_write_png(argv[2], outWidth, outHeight, 4, preview.data(),
                        outWidth * 4)) {
        std::cerr << "no se pudo escribir " << argv[2] << "\n";
        return 1;
    }
    // ideal de celdas: cada celda pintada plana con su color de paleta. el
    // render contra este ideal aisla el error de ajuste geometrico (la forma
    // no sigue sus celdas); el ideal contra el original aisla el de
    // cuantizacion (la celda no puede decir dos colores a la vez).
    {
        auto const& cells = result.plan.frames.front().cells;
        std::vector<std::uint8_t> ideal(
            static_cast<std::size_t>(outWidth) * outHeight * 4, 0);
        for (int y = 0; y < outHeight; ++y)
            for (int x = 0; x < outWidth; ++x) {
                int const index =
                    cells[static_cast<std::size_t>(y / scale) * result.plan.width +
                          x / scale];
                if (index < 0) continue;
                auto const& col =
                    result.plan.palette[static_cast<std::size_t>(index)];
                std::size_t const p =
                    (static_cast<std::size_t>(y) * outWidth + x) * 4;
                ideal[p] = col.r;
                ideal[p + 1] = col.g;
                ideal[p + 2] = col.b;
                ideal[p + 3] = 255;
            }
        std::string idealPath = std::string(argv[2]) + ".ideal.png";
        if (!stbi_write_png(idealPath.c_str(), outWidth, outHeight, 4,
                            ideal.data(), outWidth * 4)) {
            std::cerr << "no se pudo escribir " << idealPath << "\n";
            return 1;
        }
        // % pixeles donde el render no es su propia celda (fallo geometrico).
        long compared = 0, missed = 0;
        for (std::size_t p = 0; p < ideal.size(); p += 4) {
            if (ideal[p + 3] == 0 && preview[p + 3] == 0) continue;
            ++compared;
            if (preview[p] != ideal[p] || preview[p + 1] != ideal[p + 1] ||
                preview[p + 2] != ideal[p + 2] ||
                (preview[p + 3] == 0) != (ideal[p + 3] == 0))
                ++missed;
        }
        std::cout << "falloGeometrico="
                  << (compared > 0 ? 100.0 * missed / compared : 0.0) << "%\n";
    }
    return 0;
}
