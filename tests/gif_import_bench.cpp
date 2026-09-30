// banco de pruebas del importador de imagenes contra dibujos de verdad, no
// contra figuras sinteticas. carga cada imagen de una carpeta, la pasa por el
// mismo buildplan que usa el editor y mide lo que se ve mal en el nivel: cuantos
// objetos cuesta, cuanto se parece de cerca, cuanto se parece de lejos, cuantas
// entradas de la paleta son el mismo color a la vista y cuantos trazos finos hay
// apilados donde tendria que haber uno gordo.
//   g++ -std=c++23 -o2 -o bench tests/gif_import_bench.cpp
//   ./bench <carpeta-o-imagen> [--mode paint|render|art|blocks|free|blur|vert|vertx] [--dim 64]
//           [--colors 16] [--budget 12000] [--glow-scale 4] [--dump <carpeta>]

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
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

namespace {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;

    bool valid() const { return width > 0 && height > 0; }
};

Image loadImage(fs::path const& path) {
    Image image;
    int channels = 0;
    std::uint8_t* pixels = stbi_load(
        path.string().c_str(), &image.width, &image.height, &channels, 4);
    if (!pixels) return {};
    image.rgba.assign(
        pixels, pixels + static_cast<std::size_t>(image.width) * image.height * 4);
    stbi_image_free(pixels);
    return image;
}

SourceAnimation toAnimation(Image const& image) {
    SourceAnimation source;
    source.width = image.width;
    source.height = image.height;
    source.frames.resize(1);
    source.frames.front().delayMs = 100;
    source.frames.front().rgba = image.rgba;
    return source;
}

// media de la imagen sobre la caja de una celda del plan. es lo que la celda
// tendria que valer, y sirve de referencia tanto de cerca como de lejos.
Image resample(Image const& image, int width, int height) {
    Image output;
    output.width = width;
    output.height = height;
    output.rgba.assign(static_cast<std::size_t>(width) * height * 4, 0);
    for (int y = 0; y < height; ++y) {
        int const fromY = y * image.height / height;
        int const toY = std::max(fromY + 1, (y + 1) * image.height / height);
        for (int x = 0; x < width; ++x) {
            int const fromX = x * image.width / width;
            int const toX = std::max(fromX + 1, (x + 1) * image.width / width);
            double sum[4]{};
            double count = 0.0;
            for (int sampleY = fromY; sampleY < toY; ++sampleY) {
                for (int sampleX = fromX; sampleX < toX; ++sampleX) {
                    std::size_t const index =
                        (static_cast<std::size_t>(sampleY) * image.width + sampleX) * 4;
                    double const alpha = image.rgba[index + 3] / 255.0;
                    sum[0] += image.rgba[index] * alpha;
                    sum[1] += image.rgba[index + 1] * alpha;
                    sum[2] += image.rgba[index + 2] * alpha;
                    sum[3] += image.rgba[index + 3];
                    count += alpha;
                }
            }
            double const total = static_cast<double>(toY - fromY) * (toX - fromX);
            std::size_t const index = (static_cast<std::size_t>(y) * width + x) * 4;
            output.rgba[index + 3] = static_cast<std::uint8_t>(
                std::lround(std::clamp(sum[3] / total, 0.0, 255.0)));
            if (count <= 0.0) continue;
            for (int channel = 0; channel < 3; ++channel) {
                output.rgba[index + channel] = static_cast<std::uint8_t>(
                    std::lround(std::clamp(sum[channel] / count, 0.0, 255.0)));
            }
        }
    }
    return output;
}

Image renderPlan(ImportPlan const& plan, int scale) {
    Image image;
    image.width = plan.width * scale;
    image.height = plan.height * scale;
    image.rgba = renderPlanFrame(plan, 0, scale);
    return image;
}

// distancia perceptual media entre dos imagenes del mismo tamano, contando el
// hueco como un color mas: dejar transparente lo que tenia color es un fallo tan
// grande como pintarlo de otro tono.
double meanDelta(Image const& first, Image const& second) {
    double total = 0.0;
    std::size_t count = 0;
    for (std::size_t index = 0; index + 3 < first.rgba.size(); index += 4) {
        bool const firstVisible = first.rgba[index + 3] >= 128;
        bool const secondVisible = second.rgba[index + 3] >= 128;
        ++count;
        if (!firstVisible && !secondVisible) continue;
        if (firstVisible != secondVisible) {
            total += 1.0;
            continue;
        }
        auto const left = rgbToOkLab({
            first.rgba[index], first.rgba[index + 1], first.rgba[index + 2]});
        auto const right = rgbToOkLab({
            second.rgba[index], second.rgba[index + 1], second.rgba[index + 2]});
        total += oklabDistance(left, right);
    }
    return count > 0 ? total / static_cast<double>(count) : 0.0;
}

// de lejos el ojo promedia: las dos se reducen hasta que cada muestra vale por
// cuatro celdas del plan, que es como se ve el nivel desde la distancia a la que
// se juega. un tono de mas que no se distingue apenas mueve este numero; una
// silueta mal puesta lo hunde. `cells` es el lado de la rejilla del plan, no el
// de la imagen, porque lo que se difumina son celdas y no pixeles del render.
double farDelta(Image const& first, Image const& second, int cells) {
    int const width = std::max(1, cells / 4);
    int const height = std::max(1, width * first.height / std::max(first.width, 1));
    return meanDelta(resample(first, width, height), resample(second, width, height));
}

// lo que la geometria acierta de la rejilla que le mandaron pintar, mirando
// dentro de la celda y no solo su centro. el plan se puntua a si mismo con una
// muestra por celda, y ahi cualquier tira girada que pase por el centro cuenta
// como acierto aunque deje las esquinas del color de debajo: en el juego el
// dibujo es continuo, asi que lo que se ve es este numero.
double gridFidelity(ImportPlan const& plan, int scale) {
    auto const preview = renderPlanFrame(plan, 0, scale);
    auto const& cells = plan.frames.front().cells;
    std::size_t correct = 0;
    std::size_t compared = 0;
    for (int y = 0; y < plan.height * scale; ++y) {
        for (int x = 0; x < plan.width * scale; ++x) {
            int const index = cells[
                static_cast<std::size_t>(y / scale) * plan.width + x / scale];
            std::size_t const pixel =
                (static_cast<std::size_t>(y) * plan.width * scale + x) * 4;
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

// entradas de la paleta que a la vista son la misma: cada una arrastra su propia
// familia de objetos sin anadir nada al dibujo.
int microColors(std::vector<Color> const& palette) {
    std::vector<OkLab> labs;
    labs.reserve(palette.size());
    for (auto const& color : palette) labs.push_back(rgbToOkLab(color));
    int count = 0;
    for (std::size_t i = 0; i < labs.size(); ++i) {
        for (std::size_t j = i + 1; j < labs.size(); ++j) {
            if (oklabDistance(labs[i], labs[j]) < kPaletteMinDistance) ++count;
        }
    }
    return count;
}

struct StrokeStats {
    int mergeable = 0;
    int strokes = 0;
};

// dos objetos que se podrian cambiar por uno solo sin tocar ni un pixel: mismo
// color, mismo giro y pegados por un lado entero, de forma que su union es otra
// vez un rectangulo. cada pareja asi es un objeto tirado, y es exactamente el
// defecto de trazar una linea gruesa a base de tiras finas apiladas.
StrokeStats mergeablePairs(std::vector<Primitive> const& objects) {
    StrokeStats stats;
    std::vector<Primitive const*> rects;
    for (auto const& object : objects) {
        if (object.kind != PrimitiveKind::Stroke && object.kind != PrimitiveKind::Block) {
            continue;
        }
        rects.push_back(&object);
    }
    stats.strokes = static_cast<int>(rects.size());

    for (std::size_t i = 0; i < rects.size(); ++i) {
        for (std::size_t j = i + 1; j < rects.size(); ++j) {
            auto const& first = *rects[i];
            auto const& second = *rects[j];
            if (first.color != second.color) continue;
            float difference = std::fmod(std::abs(first.rotation - second.rotation), 180.f);
            difference = std::min(difference, 180.f - difference);
            if (difference > 0.05f) continue;

            float const angle = first.rotation * paimon::gifimport::kPi / 180.f;
            float const cosine = std::cos(angle);
            float const sine = std::sin(angle);
            auto project = [&](Primitive const& object) {
                float const major = object.x * cosine + object.y * sine;
                float const minor = -object.x * sine + object.y * cosine;
                return std::array<float, 4>{
                    major - object.width * 0.5f, major + object.width * 0.5f,
                    minor - object.height * 0.5f, minor + object.height * 0.5f
                };
            };
            auto const a = project(first);
            auto const b = project(second);
            // pegados por un lado y con ese lado del mismo largo: la union es un
            // rectangulo justo y no se lleva por delante nada de alrededor.
            bool const alongMajor = std::abs(a[2] - b[2]) < 0.01f &&
                std::abs(a[3] - b[3]) < 0.01f &&
                (std::abs(a[1] - b[0]) < 0.01f || std::abs(b[1] - a[0]) < 0.01f);
            bool const alongMinor = std::abs(a[0] - b[0]) < 0.01f &&
                std::abs(a[1] - b[1]) < 0.01f &&
                (std::abs(a[3] - b[2]) < 0.01f || std::abs(b[3] - a[2]) < 0.01f);
            if (alongMajor || alongMinor) ++stats.mergeable;
        }
    }
    return stats;
}

ImportMode parseMode(std::string const& name) {
    if (name == "art") return ImportMode::Art;
    if (name == "blocks") return ImportMode::Blocks;
    if (name == "render") return ImportMode::Render;
    if (name == "free") return ImportMode::Free;
    if (name == "circles") return ImportMode::Circles;
    if (name == "blur") return ImportMode::Blur;
    if (name == "vert") return ImportMode::Vert;
    if (name == "vertx") return ImportMode::VertX;
    return ImportMode::Paint;
}

char const* modeName(ImportMode mode) {
    switch (mode) {
        case ImportMode::Blocks: return "blocks";
        case ImportMode::Art: return "art";
        case ImportMode::Paint: return "paint";
        case ImportMode::Render: return "render";
        case ImportMode::Free: return "free";
        case ImportMode::Circles: return "circles";
        case ImportMode::Blur: return "blur";
        case ImportMode::Vert: return "vert";
        case ImportMode::VertX: return "vertx";
    }
    return "?";
}

// en el juego la biblioteca del modo libre sale de los objetos de decoracion de
// gd. aqui no hay gd, asi que para medir el algoritmo se le da una tanda de
// siluetas analiticas —cunas, cuartos de circulo, medias lunas, chaflanes— con
// id 0: el banco no emite nada, solo mide cuanto cubren y a que precio.
std::vector<CatalogEntry> syntheticCatalog() {
    constexpr int side = 32;
    std::vector<CatalogEntry> entries;
    auto add = [&](auto&& inside) {
        CatalogEntry entry;
        entry.baseWidth = 30.f;
        entry.baseHeight = 30.f;
        entry.mask.width = side;
        entry.mask.height = side;
        entry.mask.coverage.assign(static_cast<std::size_t>(side) * side, 0);
        for (int y = 0; y < side; ++y) {
            for (int x = 0; x < side; ++x) {
                float const u = (x + 0.5f) / side;
                float const v = (y + 0.5f) / side;
                entry.mask.coverage[static_cast<std::size_t>(y) * side + x] =
                    inside(u, v) ? 255 : 0;
            }
        }
        entries.push_back(std::move(entry));
    };

    add([](float, float) { return true; });
    add([](float u, float v) { float const dx = u - 0.5f, dy = v - 0.5f;
                              return dx * dx + dy * dy <= 0.25f; });
    add([](float u, float v) { return u + v <= 1.f; });
    add([](float u, float v) { return u + v <= 1.f && u + v >= 0.5f; });
    add([](float u, float v) { return u * u + v * v <= 1.f; });
    add([](float u, float v) { return u * u + v * v >= 1.f; });
    add([](float u, float v) { float const dx = u - 0.5f;
                               return dx * dx + (v - 1.f) * (v - 1.f) <= 0.25f; });
    add([](float u, float v) { return v >= u * 0.5f; });
    add([](float u, float v) { return v >= 1.f - u * u; });
    add([](float u, float v) { return v <= 1.f - u * u; });
    add([](float u, float v) { return std::abs(v - 0.5f) <= 0.5f * (1.f - u); });
    add([](float u, float v) { return u >= 0.25f || v >= 0.25f; });
    return entries;
}

// los modos suaves necesitan sus 7 moldes: aqui son las mascaras analiticas de
// verdad (gaussiana/rampa), asi que el banco mide el trazado, no la emision.
std::vector<PlanStamp> syntheticSoftStamps() {
    std::vector<PlanStamp> stamps(7);
    auto fill = [&](std::size_t slot, StampMask mask, float rotation) {
        PlanStamp stamp;
        stamp.objectId = static_cast<int>(9100 + slot);
        stamp.baseWidth = 30.f;
        stamp.baseHeight = 30.f;
        stamp.rotation = rotation;
        stamp.mask = std::move(mask);
        stamps[slot] = std::move(stamp);
    };
    fill(0, analyticRadialGlowMask(), 0.f);
    auto ramp = analyticVerticalGradientMask();
    auto mirrored = ramp;
    std::reverse(mirrored.coverage.begin(), mirrored.coverage.end());
    fill(1, std::move(ramp), 0.f);
    fill(2, std::move(mirrored), 180.f);
    auto quarter = analyticQuarterGlowMask();
    int const side = quarter.width;
    for (int q = 0; q < 4; ++q) {
        StampMask rotated{side, side, std::vector<std::uint8_t>(
            static_cast<std::size_t>(side) * side)};
        for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
            int sx = x, sy = y;
            if (q == 1) { sx = y; sy = side - 1 - x; }
            if (q == 2) { sx = side - 1 - x; sy = side - 1 - y; }
            if (q == 3) { sx = side - 1 - y; sy = x; }
            rotated.coverage[static_cast<std::size_t>(y) * side + x] =
                quarter.coverage[static_cast<std::size_t>(sy) * side + sx];
        }
        fill(static_cast<std::size_t>(3 + q), std::move(rotated), q * 90.f);
    }
    return stamps;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "uso: bench <carpeta-o-imagen> [--mode paint] [--dim 64]"
                     " [--colors 16] [--budget 12000] [--glow-scale 4] [--dump carpeta]\n";
        return 2;
    }

    fs::path const target = argv[1];
    Options options;
    options.mode = ImportMode::Paint;
    options.maxDimension = 64;
    options.maxColors = 16;
    options.objectBudget = 12000;
    options.background = BackgroundMode::Keep;
    options.sampling = SamplingMode::Smooth;
    fs::path dump;

    for (int i = 2; i + 1 < argc; i += 2) {
        std::string const key = argv[i];
        std::string const value = argv[i + 1];
        if (key == "--mode") options.mode = parseMode(value);
        else if (key == "--dim") options.maxDimension = std::stoi(value);
        else if (key == "--colors") options.maxColors = std::stoi(value);
        else if (key == "--budget") options.objectBudget = std::stoi(value);
        else if (key == "--glow-scale") options.blurGlowDiameter = std::stof(value);
        else if (key == "--dump") dump = value;
        else if (key == "--stamps" && value == "sinteticos") {
            setStampCatalog(syntheticCatalog());
        }
    }
    if (!dump.empty()) fs::create_directories(dump);
    if (usesSoftGeometry(options.mode)) options.softStamps = syntheticSoftStamps();

    std::vector<fs::path> inputs;
    if (fs::is_directory(target)) {
        for (auto const& entry : fs::directory_iterator(target)) {
            auto const extension = entry.path().extension().string();
            if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
                extension == ".bmp" || extension == ".tga") {
                inputs.push_back(entry.path());
            }
        }
        std::sort(inputs.begin(), inputs.end());
    } else {
        inputs.push_back(target);
    }
    if (inputs.empty()) {
        std::cerr << "no hay imagenes en " << target << '\n';
        return 2;
    }

    std::cout << "modo=" << modeName(options.mode) << " dim=" << options.maxDimension
              << " colores=" << options.maxColors << " presupuesto="
              << options.objectBudget << "\n\n";
    std::cout << std::left << std::setw(26) << "imagen"
              << std::right << std::setw(8) << "rejilla"
              << std::setw(9) << "objetos"
              << std::setw(8) << "paleta"
              << std::setw(7) << "micro"
              << std::setw(10) << "geometria"
              << std::setw(9) << "cerca"
              << std::setw(9) << "lejos"
              << std::setw(9) << "revision"
              << std::setw(11) << "fusionab."
              << std::setw(8) << "tiras"
              << std::setw(8) << "moldes" << '\n';

    double totalNear = 0.0;
    double totalFar = 0.0;
    double totalGrid = 0.0;
    double totalObjects = 0.0;
    int totalStacked = 0;
    int totalMicro = 0;
    int counted = 0;

    for (auto const& input : inputs) {
        auto const image = loadImage(input);
        if (!image.valid()) {
            std::cout << std::left << std::setw(26) << input.filename().string()
                      << "  no se pudo leer\n";
            continue;
        }
        auto const result = buildPlan(toAnimation(image), options);
        if (!result) {
            std::cout << std::left << std::setw(26) << input.filename().string()
                      << "  " << result.error << '\n';
            continue;
        }
        auto const& plan = result.plan;

        constexpr int scale = 4;
        auto const rendered = renderPlan(plan, scale);
        auto const reference = resample(image, plan.width * scale, plan.height * scale);
        double const near = meanDelta(rendered, reference);
        double const far = farDelta(rendered, reference, plan.width);
        auto const strokes = mergeablePairs(plan.staticObjects);
        double const grid = gridFidelity(plan, scale);

        std::vector<Primitive> everything = plan.staticObjects;
        for (auto const& track : plan.tracks) {
            everything.insert(everything.end(), track.objects.begin(), track.objects.end());
        }

        std::cout << std::left << std::setw(26) << input.filename().string()
                  << std::right << std::setw(8)
                  << (std::to_string(plan.width) + "x" + std::to_string(plan.height))
                  << std::setw(9) << plan.totalObjects
                  << std::setw(8) << plan.palette.size()
                  << std::setw(7) << microColors(plan.palette)
                  << std::setw(9) << std::fixed << std::setprecision(2) << grid << "%"
                  << std::setw(9) << std::setprecision(4) << near
                  << std::setw(9) << far
                  << std::setw(8) << std::setprecision(1) << plan.similarity << "%"
                  << std::setw(9) << strokes.mergeable
                  << std::setw(8) << strokes.strokes
                  << std::setw(8) << plan.stampObjects << '\n';
        if (plan.gradientWash) {
            std::cout << std::string(26, ' ') << "  wash 2903 arriba="
                      << plan.washTop << " abajo=" << plan.washBottom << '\n';
        }

        totalNear += near;
        totalFar += far;
        totalGrid += grid;
        totalObjects += static_cast<double>(plan.totalObjects);
        totalStacked += strokes.mergeable;
        totalMicro += microColors(plan.palette);
        ++counted;

        if (!dump.empty()) {
            auto const stem = input.stem().string();
            auto const out = (dump / (stem + "-plan.png")).string();
            stbi_write_png(
                out.c_str(), rendered.width, rendered.height, 4,
                rendered.rgba.data(), rendered.width * 4);
            auto const source = (dump / (stem + "-ref.png")).string();
            stbi_write_png(
                source.c_str(), reference.width, reference.height, 4,
                reference.rgba.data(), reference.width * 4);
            // la rejilla cuantizada es lo que la geometria tiene que reproducir:
            // separarla del plan dice si lo que falla es el color o el dibujo.
            Image grid;
            grid.width = plan.width;
            grid.height = plan.height;
            grid.rgba.assign(static_cast<std::size_t>(grid.width) * grid.height * 4, 0);
            for (std::size_t cell = 0; cell < plan.frames.front().cells.size(); ++cell) {
                int const index = plan.frames.front().cells[cell];
                if (index < 0) continue;
                auto const& color = plan.palette[static_cast<std::size_t>(index)];
                grid.rgba[cell * 4] = color.r;
                grid.rgba[cell * 4 + 1] = color.g;
                grid.rgba[cell * 4 + 2] = color.b;
                grid.rgba[cell * 4 + 3] = 255;
            }
            auto const gridPath = (dump / (stem + "-grid.png")).string();
            stbi_write_png(
                gridPath.c_str(), grid.width, grid.height, 4,
                grid.rgba.data(), grid.width * 4);
        }
    }

    if (counted > 0) {
        std::cout << "\nmedia: objetos=" << std::setprecision(0)
                  << totalObjects / counted
                  << " geometria=" << std::setprecision(2) << totalGrid / counted << "%"
                  << " cerca=" << std::setprecision(4) << totalNear / counted
                  << " lejos=" << totalFar / counted
                  << " fusionables=" << totalStacked
                  << " micro=" << totalMicro << '\n';
    }
    return 0;
}
