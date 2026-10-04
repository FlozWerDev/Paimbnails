// tests de regresion para el modo pintura y el modo circulos del convertidor de
// imagen a objetos de gd.
// pintura combina bloques, diagonales y circulos cuando respetan las fronteras
// de color. los parches deben mantener los bordes suaves sin dejar huecos ni
// tapar capas superiores con circulos de otra hoja de sprites.
// cada prueba es una funcion bool que devuelve true si pasa. main() las ejecuta
// todas y devuelve 0 si pasan o 1 si alguna falla.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

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

namespace {

// utilidades de construccion de imagenes sinteticas

SourceAnimation animation(int width, int height, int frames, std::uint8_t r = 0,
                          std::uint8_t g = 0, std::uint8_t b = 0, std::uint8_t a = 255) {
    SourceAnimation source;
    source.width = width;
    source.height = height;
    source.frames.resize(static_cast<std::size_t>(frames));
    for (auto& frame : source.frames) {
        frame.delayMs = 100;
        frame.rgba.resize(static_cast<std::size_t>(width) * height * 4);
        for (std::size_t i = 0; i < frame.rgba.size(); i += 4) {
            frame.rgba[i] = r;
            frame.rgba[i + 1] = g;
            frame.rgba[i + 2] = b;
            frame.rgba[i + 3] = a;
        }
    }
    return source;
}

void setPixel(SourceAnimation& source, int frame, int x, int y,
              std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
    auto& rgba = source.frames[static_cast<std::size_t>(frame)].rgba;
    std::size_t const index = (static_cast<std::size_t>(y) * source.width + x) * 4;
    rgba[index] = r;
    rgba[index + 1] = g;
    rgba[index + 2] = b;
    rgba[index + 3] = a;
}

Options exactOptions(int dimension) {
    Options options;
    options.maxDimension = dimension;
    options.minDimension = 4;
    options.maxColors = 8;
    options.maxFrames = 60;
    options.objectBudget = 50000;
    options.background = BackgroundMode::Keep;
    options.sampling = SamplingMode::Pixel;
    options.dither = false;
    options.loop = true;
    return options;
}

Options paintOptions(int dimension) {
    auto options = exactOptions(dimension);
    options.mode = ImportMode::Paint;
    options.sampling = SamplingMode::Smooth;
    return options;
}

Options circleOptions(int dimension) {
    auto options = paintOptions(dimension);
    options.mode = ImportMode::Circles;
    return options;
}

// deshace la marca de agua para poder contar objetos y medir geometria limpia.
std::vector<Primitive> unmarked(ImportPlan const& plan) {
    auto objects = plan.staticObjects;
    for (auto& object : objects) {
        object.rotation = std::fmod(object.rotation, 360.f);
    }
    prunePaintObjects(objects, std::max(plan.width, 1), std::max(plan.height, 1));
    return objects;
}

// mascara de celdas vacias: 1 donde no hay nada pintado, 0 donde si.
std::vector<std::uint8_t> emptyOutside(std::vector<int> const& positions, int cells) {
    std::vector<std::uint8_t> empty(static_cast<std::size_t>(cells), 1);
    for (int position : positions) empty[static_cast<std::size_t>(position)] = 0;
    return empty;
}

// cuenta cuantos objetos de cada tipo hay.
struct PrimitiveStats {
    int blocks = 0;
    int strokes = 0;
    int circles = 0;
    int triangles = 0;
    int glows = 0;
    int stamps = 0;
    int total = 0;
};

PrimitiveStats countPrimitives(std::vector<Primitive> const& objects) {
    PrimitiveStats stats;
    for (auto const& object : objects) {
        switch (object.kind) {
            case PrimitiveKind::Block: ++stats.blocks; break;
            case PrimitiveKind::Stroke: ++stats.strokes; break;
            case PrimitiveKind::Circle: ++stats.circles; break;
            case PrimitiveKind::Triangle: ++stats.triangles; break;
            case PrimitiveKind::WideTriangle: ++stats.triangles; break;
            case PrimitiveKind::Glow: ++stats.glows; break;
            case PrimitiveKind::Stamp: ++stats.stamps; break;
        }
    }
    stats.total = static_cast<int>(objects.size());
    return stats;
}

// verifica si cada celda pintada tiene cobertura subpixel suficiente.
struct CoverageResult {
    int missing = 0;
    int minimumCoverage = 0;
    int interiorHoles = 0;
};

CoverageResult measureCoverage(ImportPlan const& plan, int scale) {
    auto const preview = renderPlanFrame(plan, 0, scale);
    auto const& cells = plan.frames.front().cells;
    CoverageResult result;
    result.minimumCoverage = scale * scale;
    for (std::size_t position = 0; position < cells.size(); ++position) {
        if (cells[position] < 0) continue;
        int const cellX = static_cast<int>(position % plan.width);
        int const cellY = static_cast<int>(position / plan.width);
        int visible = 0;
        for (int y = 0; y < scale; ++y) {
            for (int x = 0; x < scale; ++x) {
                std::size_t const pixel =
                    (static_cast<std::size_t>(cellY * scale + y) * plan.width * scale +
                     cellX * scale + x) * 4;
                visible += preview[pixel + 3] != 0;
            }
        }
        result.minimumCoverage = std::min(result.minimumCoverage, visible);
        if (visible == 0) ++result.missing;
        // huecos interiores: solo en celdas rodeadas del mismo color.
        if (cellX == 0 || cellY == 0 || cellX + 1 == plan.width ||
            cellY + 1 == plan.height) continue;
        int const color = cells[position];
        if (cells[position - 1] != color || cells[position + 1] != color ||
            cells[position - plan.width] != color ||
            cells[position + plan.width] != color) continue;
        result.interiorHoles += scale * scale - visible;
    }
    return result;
}

//  tests de modo circulos

// 1. el modo circulos solo produce primitivekind::circle. nada de cuadrados,
//    trazos ni triangulos, que estarian en otra hoja de sprites y gd los
//    pondria debajo de todos los circulos sin importar la capa.
bool circleModeNeverEmitsSquares() {
    auto source = animation(48, 48, 1, 0, 0, 0, 0);
    // un circulo grande y un rectangulo: los dos deben acabar hechos de circulos.
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 48; ++x) {
            float const dx = x + 0.5f - 24.f;
            float const dy = y + 0.5f - 18.f;
            if (dx * dx + dy * dy <= 144.f) setPixel(source, 0, x, y, 90, 200, 240);
            if (y >= 34 && y < 42 && x >= 6 && x < 42) {
                setPixel(source, 0, x, y, 235, 120, 90);
            }
        }
    }
    auto result = buildPlan(source, circleOptions(48));
    if (!result) {
        std::cout << "circle-no-squares: " << result.error << '\n';
        return false;
    }
    auto const& objects = result.plan.staticObjects;
    bool const allCircles = std::all_of(
        objects.begin(), objects.end(), [](Primitive const& object) {
            return object.kind == PrimitiveKind::Circle;
        });
    auto stats = countPrimitives(objects);
    std::cout << "circle-no-squares: total=" << stats.total
              << " circles=" << stats.circles
              << " blocks=" << stats.blocks
              << " strokes=" << stats.strokes << '\n';
    if (!allCircles) std::cerr << "FAIL: circle mode emitted non-circle primitives\n";
    return allCircles;
}

// 2. el modo circulos cubre todas las celdas sin dejar huecos. cada celda
//    pintada debe tener al menos un circulo/elipse que la toque.
bool circleModeCoversAllCells() {
    auto source = animation(40, 40, 1, 0, 0, 0, 0);
    for (int y = 4; y < 36; ++y) {
        for (int x = 4; x < 36; ++x) {
            float const dx = x + 0.5f - 20.f;
            float const dy = y + 0.5f - 20.f;
            if (dx * dx + dy * dy <= 196.f) setPixel(source, 0, x, y, 90, 200, 240);
        }
    }
    auto result = buildPlan(source, circleOptions(40));
    if (!result) {
        std::cout << "circle-coverage: " << result.error << '\n';
        return false;
    }
    auto const preview = renderPlanFrame(result.plan, 0, 1);
    int missing = 0;
    for (int position = 0; position < result.plan.width * result.plan.height; ++position) {
        if (result.plan.frames.front().cells[static_cast<std::size_t>(position)] < 0) continue;
        missing += preview[static_cast<std::size_t>(position) * 4 + 3] == 0;
    }
    std::cout << "circle-coverage: huecos=" << missing
              << " objects=" << result.plan.staticObjects.size() << '\n';
    if (missing != 0) std::cerr << "FAIL: circle mode left " << missing << " cells uncovered\n";
    return missing == 0;
}

// 3. el modo circulos produce tanto discos gordos (w y h > 4) como husos
//    estirados (w > 2*h) para cubrir zonas macizas y lineas finas.
bool circleModeProducesDiscsAndSpindles() {
    auto source = animation(48, 48, 1, 0, 0, 0, 0);
    // un disco macizo.
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 48; ++x) {
            float const dx = x + 0.5f - 24.f;
            float const dy = y + 0.5f - 16.f;
            if (dx * dx + dy * dy <= 100.f) setPixel(source, 0, x, y, 90, 200, 240);
        }
    }
    // una barra horizontal fina.
    for (int y = 36; y < 40; ++y) {
        for (int x = 4; x < 44; ++x) setPixel(source, 0, x, y, 235, 120, 90);
    }
    auto result = buildPlan(source, circleOptions(48));
    if (!result) {
        std::cout << "circle-shapes: " << result.error << '\n';
        return false;
    }
    auto const& objects = result.plan.staticObjects;
    bool const plump = std::any_of(
        objects.begin(), objects.end(), [](Primitive const& object) {
            return object.width > 4.f && object.height > 4.f;
        });
    bool const stretched = std::any_of(
        objects.begin(), objects.end(), [](Primitive const& object) {
            return object.width > object.height * 2.f ||
                   object.height > object.width * 2.f;
        });
    std::cout << "circle-shapes: discos=" << plump << " husos=" << stretched
              << " objects=" << objects.size() << '\n';
    if (!plump) std::cerr << "FAIL: circle mode did not produce any plump discs\n";
    if (!stretched) std::cerr << "FAIL: circle mode did not produce any stretched spindles\n";
    return plump && stretched;
}

// 4. el modo circulos no deja que un circulo invada celdas de otro color
//    (blocked). cada circulo solo puede crecer sobre sus propias celdas o
//    las que quedan tapadas por capas superiores.
bool circleModeRespectsColorBoundaries() {
    auto source = animation(40, 40, 1, 0, 0, 0, 0);
    // dos bloques de distinto color, adyacentes.
    for (int y = 10; y < 30; ++y) {
        for (int x = 4; x < 20; ++x) setPixel(source, 0, x, y, 200, 60, 60);
        for (int x = 20; x < 36; ++x) setPixel(source, 0, x, y, 60, 60, 200);
    }
    auto result = buildPlan(source, circleOptions(40));
    if (!result) {
        std::cout << "circle-boundaries: " << result.error << '\n';
        return false;
    }
    // verificar que ningun pixel central de una celda bien interior (al menos 2
    // celdas desde la frontera) tenga el color del otro lado. las celdas justo en
    // la frontera pueden tener un pico de spill legitimo (kspill = 14%).
    auto const preview = renderPlanFrame(result.plan, 0, 4);
    int const scale = 4;
    int bleeds = 0;
    for (int y = 12; y < 28; ++y) {
        for (int x = 4; x < 36; ++x) {
            // saltar las 2 columnas junto a la frontera (x=18,19,20,21).
            if (x >= 18 && x <= 21) continue;
            std::size_t const pixel =
                (static_cast<std::size_t>(y * scale + scale / 2) * result.plan.width * scale +
                 x * scale + scale / 2) * 4;
            if (preview[pixel + 3] == 0) continue;
            bool const wasRed = x < 20;
            bool const isRed = preview[pixel] > 150 && preview[pixel + 2] < 100;
            bool const isBlue = preview[pixel + 2] > 150 && preview[pixel] < 100;
            if (wasRed && isBlue) ++bleeds;
            if (!wasRed && isRed) ++bleeds;
        }
    }
    std::cout << "circle-boundaries: bleeds=" << bleeds
              << " objects=" << result.plan.staticObjects.size() << '\n';
    if (bleeds > 0) std::cerr << "FAIL: circle mode bled " << bleeds << " cells across colors\n";
    return bleeds == 0;
}

// 5. los circulos en modo circulos usan cuadrados (bloques axis-aligned o
//    circulos sin giro) en las curvas, no rectangulos girados. es decir,
//    los circulos salen con rotation==0 o girados, pero siempre como
//    primitivekind::circle. si son stroke girados, algo esta mal.
bool circleModeCurvesUseCirclesNotRotatedRects() {
    auto source = animation(48, 48, 1, 0, 0, 0, 0);
    // un anillo: circulo exterior menos circulo interior.
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 48; ++x) {
            float const dx = x + 0.5f - 24.f;
            float const dy = y + 0.5f - 24.f;
            float const dist = dx * dx + dy * dy;
            if (dist <= 400.f && dist >= 100.f) setPixel(source, 0, x, y, 160, 80, 220);
        }
    }
    auto result = buildPlan(source, circleOptions(48));
    if (!result) {
        std::cout << "circle-curves: " << result.error << '\n';
        return false;
    }
    auto const& objects = result.plan.staticObjects;
    bool const allCircles = std::all_of(
        objects.begin(), objects.end(), [](Primitive const& object) {
            return object.kind == PrimitiveKind::Circle;
        });
    int rotatedRects = 0;
    for (auto const& object : objects) {
        if (object.kind == PrimitiveKind::Stroke) {
            float const angle = std::fmod(std::abs(object.rotation), 90.f);
            if (angle > 5.f && angle < 85.f) ++rotatedRects;
        }
    }
    std::cout << "circle-curves: allCircles=" << allCircles
              << " rotatedRects=" << rotatedRects
              << " objects=" << objects.size() << '\n';
    if (!allCircles) std::cerr << "FAIL: circle mode used non-circle primitives on a ring\n";
    return allCircles && rotatedRects == 0;
}

// 6. un solo pixel debe cubrirse con un circulo minimo, no con un bloque.
bool circleModeHandlesSinglePixel() {
    auto source = animation(12, 12, 1, 0, 0, 0, 0);
    setPixel(source, 0, 6, 6, 200, 120, 80);
    auto result = buildPlan(source, circleOptions(12));
    if (!result) {
        std::cout << "circle-single-pixel: " << result.error << '\n';
        return false;
    }
    auto const& objects = result.plan.staticObjects;
    bool const allCircles = std::all_of(
        objects.begin(), objects.end(), [](Primitive const& object) {
            return object.kind == PrimitiveKind::Circle;
        });
    // debe haber al menos 1 circulo y debe cubrir el pixel.
    auto const preview = renderPlanFrame(result.plan, 0, 1);
    std::size_t const center = (6 * static_cast<std::size_t>(result.plan.width) + 6) * 4;
    bool const covered = preview[center + 3] != 0;
    std::cout << "circle-single-pixel: objects=" << objects.size()
              << " allCircles=" << allCircles << " covered=" << covered << '\n';
    if (!allCircles || !covered) {
        std::cerr << "FAIL: circle mode did not properly handle a single pixel\n";
    }
    return allCircles && covered && !objects.empty();
}

// 7. una linea diagonal fina en modo circulos debe cubrirse enteramente
//    con circulos/elipses estiradas, sin dejar huecos.
bool circleModeCoversDiagonalLine() {
    auto source = animation(32, 32, 1, 0, 0, 0, 0);
    for (int i = 2; i < 30; ++i) {
        setPixel(source, 0, i, i, 90, 200, 120);
    }
    auto result = buildPlan(source, circleOptions(32));
    if (!result) {
        std::cout << "circle-diagonal: " << result.error << '\n';
        return false;
    }
    auto const& objects = result.plan.staticObjects;
    bool const allCircles = std::all_of(
        objects.begin(), objects.end(), [](Primitive const& object) {
            return object.kind == PrimitiveKind::Circle;
        });
    auto const preview = renderPlanFrame(result.plan, 0, 1);
    int missing = 0;
    for (int i = 2; i < 30; ++i) {
        std::size_t const pixel =
            (static_cast<std::size_t>(i) * result.plan.width + i) * 4;
        if (preview[pixel + 3] == 0) ++missing;
    }
    std::cout << "circle-diagonal: objects=" << objects.size()
              << " allCircles=" << allCircles << " missing=" << missing << '\n';
    if (!allCircles || missing > 0) {
        std::cerr << "FAIL: circle mode left gaps on a diagonal line\n";
    }
    return allCircles && missing == 0;
}

// 8. en modo circulos, una forma en l (no convexa) debe cubrirse sin que
//    ningun circulo se salga demasiado sobre el fondo.
bool circleModeHandlesLShape() {
    auto source = animation(32, 32, 1, 0, 0, 0, 0);
    // pata vertical de la l.
    for (int y = 4; y < 28; ++y) {
        for (int x = 4; x < 10; ++x) setPixel(source, 0, x, y, 60, 180, 220);
    }
    // pata horizontal de la l.
    for (int y = 22; y < 28; ++y) {
        for (int x = 10; x < 28; ++x) setPixel(source, 0, x, y, 60, 180, 220);
    }
    auto result = buildPlan(source, circleOptions(32));
    if (!result) {
        std::cout << "circle-L-shape: " << result.error << '\n';
        return false;
    }
    auto const& objects = result.plan.staticObjects;
    bool const allCircles = std::all_of(
        objects.begin(), objects.end(), [](Primitive const& object) {
            return object.kind == PrimitiveKind::Circle;
        });
    // cobertura.
    auto const preview = renderPlanFrame(result.plan, 0, 1);
    int missing = 0;
    for (int position = 0; position < result.plan.width * result.plan.height; ++position) {
        if (result.plan.frames.front().cells[static_cast<std::size_t>(position)] < 0) continue;
        missing += preview[static_cast<std::size_t>(position) * 4 + 3] == 0;
    }
    std::cout << "circle-L-shape: objects=" << objects.size()
              << " allCircles=" << allCircles << " missing=" << missing << '\n';
    if (!allCircles || missing > 0) {
        std::cerr << "FAIL: circle mode failed on L-shaped region\n";
    }
    return allCircles && missing == 0;
}

// 9. en modo circulos con dos colores, el de arriba no puede quedar debajo
//    del de abajo. dado que todos son circulos del mismo sprite, el z si
//    manda y el orden de emision importa.
bool circleModePreservesLayerOrder() {
    auto source = animation(32, 32, 1, 0, 0, 0, 0);
    // color de fondo: verde grande.
    for (int y = 4; y < 28; ++y) {
        for (int x = 4; x < 28; ++x) setPixel(source, 0, x, y, 60, 200, 80);
    }
    // color de arriba: punto rojo centrado.
    for (int y = 12; y < 20; ++y) {
        for (int x = 12; x < 20; ++x) setPixel(source, 0, x, y, 230, 60, 60);
    }
    auto result = buildPlan(source, circleOptions(32));
    if (!result) {
        std::cout << "circle-layers: " << result.error << '\n';
        return false;
    }
    // el rojo tiene que ser visible en el centro.
    auto const preview = renderPlanFrame(result.plan, 0, 4);
    int const scale = 4;
    int const cx = 16 * scale + scale / 2;
    int const cy = 16 * scale + scale / 2;
    std::size_t const pixel =
        (static_cast<std::size_t>(cy) * result.plan.width * scale + cx) * 4;
    bool const redVisible = preview[pixel] > 150 && preview[pixel + 1] < 100;
    std::cout << "circle-layers: redVisible=" << redVisible
              << " objects=" << result.plan.staticObjects.size() << '\n';
    if (!redVisible) std::cerr << "FAIL: circle mode buried the top color\n";
    return redVisible;
}

// 10. vectorizecircles directamente: una fila horizontal de 20 celdas debe
//     cubrirse con unas pocas elipses estiradas, no con 20 circulos individuales.
bool circleVectorizerMergesHorizontalRow() {
    constexpr int width = 24;
    constexpr int height = 6;
    std::vector<int> positions;
    for (int x = 2; x < 22; ++x) positions.push_back(3 * width + x);
    auto objects = vectorizeCircles(
        positions, width, height, 0, 0, {},
        emptyOutside(positions, width * height));
    // con 20 celdas en fila, deberian ser pocas elipses (no una por celda).
    std::cout << "circle-merge-row: objects=" << objects.size() << '\n';
    bool const merged = objects.size() <= 6;
    bool const allCircles = std::all_of(
        objects.begin(), objects.end(), [](Primitive const& object) {
            return object.kind == PrimitiveKind::Circle;
        });
    if (!merged) std::cerr << "FAIL: vectorizeCircles did not merge a row (" << objects.size() << " objects)\n";
    if (!allCircles) std::cerr << "FAIL: vectorizeCircles emitted non-circle primitives\n";
    return merged && allCircles;
}

// 11. vectorizecircles: un cuadrado macizo de 10x10 debe cubrirse sin huecos.
bool circleVectorizerCoversSolidSquare() {
    constexpr int width = 16;
    constexpr int height = 16;
    std::vector<int> positions;
    for (int y = 3; y < 13; ++y) {
        for (int x = 3; x < 13; ++x) positions.push_back(y * width + x);
    }
    auto objects = vectorizeCircles(
        positions, width, height, 0, 0, {},
        emptyOutside(positions, width * height));
    // verificar cobertura.
    int missing = 0;
    for (int position : positions) {
        int const px = position % width;
        int const py = position / width;
        bool const covered = std::any_of(
            objects.begin(), objects.end(), [&](Primitive const& object) {
                return xformOf(object).contains(px + 0.5f, py + 0.5f);
            });
        if (!covered) ++missing;
    }
    std::cout << "circle-solid-square: objects=" << objects.size()
              << " missing=" << missing << '\n';
    if (missing > 0) std::cerr << "FAIL: vectorizeCircles left " << missing << " cells uncovered in a square\n";
    return missing == 0;
}

// 12. spill en circulos con dos colores: los circulos de un color no deben
//     asomar sobre las celdas de otro color vecino. se verifica que cuando
//     la region vecina no es hueco (empty=0) ni esta tapada por capas
//     superiores (blocked=0), los circulos respetan el limite y no invaden
//     las celdas interiores del otro color.
bool circleVectorizerControlsSpillWithBlocked() {
    constexpr int width = 24;
    constexpr int height = 24;
    constexpr std::size_t cells = static_cast<std::size_t>(width) * height;
    // color a: mitad izquierda [2, 12).
    std::vector<int> positionsA;
    for (int y = 4; y < 20; ++y) {
        for (int x = 2; x < 12; ++x) positionsA.push_back(y * width + x);
    }
    // color b: mitad derecha [12, 22). no es hueco (empty=0) ni capa superior (blocked=0).
    std::vector<std::uint8_t> empty(cells, 1);
    for (int y = 4; y < 20; ++y) {
        for (int x = 2; x < 22; ++x) {
            empty[static_cast<std::size_t>(y * width + x)] = 0;
        }
    }
    std::vector<std::uint8_t> const blocked(cells, 0);

    auto objects = vectorizeCircles(
        positionsA, width, height, 0, 0, blocked, empty);

    // verificar que ningun circulo cubre el centro de celdas interiores de b (x >= 14).
    int invasions = 0;
    for (auto const& object : objects) {
        auto const placed = xformOf(object);
        for (int y = 4; y < 20; ++y) {
            for (int x = 14; x < 22; ++x) {
                if (placed.contains(x + 0.5f, y + 0.5f)) ++invasions;
            }
        }
    }
    bool const allCircles = std::all_of(
        objects.begin(), objects.end(), [](Primitive const& object) {
            return object.kind == PrimitiveKind::Circle;
        });
    std::cout << "circle-spill-blocked: invasions=" << invasions
              << " objects=" << objects.size()
              << " allCircles=" << allCircles << '\n';
    bool const pass = invasions == 0 && allCircles;
    if (!pass) std::cerr << "FAIL: circle spill invaded " << invasions << " protected interior cells\n";
    return pass;
}

//  tests de modo pintura - enfoque en curvas y circulos

// 13. en modo pintura, un circulo grande debe usar primitivekind::circle,
//     no una pila de cuadraditos.
bool paintModeDetectsCircle() {
    auto source = animation(40, 40, 1, 0, 0, 0, 0);
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 40; ++x) {
            float const dx = x + 0.5f - 20.f;
            float const dy = y + 0.5f - 20.f;
            if (dx * dx + dy * dy <= 225.f) setPixel(source, 0, x, y, 80, 190, 240);
        }
    }
    auto result = buildPlan(source, paintOptions(40));
    if (!result) return false;
    auto const objects = unmarked(result.plan);
    bool const hasCircle = std::any_of(
        objects.begin(), objects.end(), [](Primitive const& object) {
            return object.kind == PrimitiveKind::Circle;
        });
    std::cout << "paint-circle-detect: hasCircle=" << hasCircle
              << " objects=" << objects.size() << '\n';
    if (!hasCircle) std::cerr << "FAIL: paint mode did not detect a circular region\n";
    return hasCircle;
}

// 14. en modo pintura, un circulo pequeno (4-6 celdas de diametro) aun
//     debe caber como circle y no se descompone en bloques.
bool paintModeDetectsSmallCircle() {
    auto source = animation(20, 20, 1, 0, 0, 0, 0);
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 20; ++x) {
            float const dx = x + 0.5f - 10.f;
            float const dy = y + 0.5f - 10.f;
            if (dx * dx + dy * dy <= 9.f) setPixel(source, 0, x, y, 80, 190, 240);
        }
    }
    auto result = buildPlan(source, paintOptions(20));
    if (!result) return false;
    auto const objects = unmarked(result.plan);
    bool const hasCircle = std::any_of(
        objects.begin(), objects.end(), [](Primitive const& object) {
            return object.kind == PrimitiveKind::Circle && object.width >= 4.f;
        });
    std::cout << "paint-small-circle: hasCircle=" << hasCircle
              << " objects=" << objects.size() << '\n';
    return hasCircle;
}

// 15. las curvas en modo pintura usan cuadrados/trazos (bloques y strokes)
//     y no rectangulos girados pequenos que dejan picos. un arco suave
//     debe cubrirse con pocos objetos sin picos subpixel.
bool paintModeCurvesUseBlocksNotRotatedSlivers() {
    auto source = animation(48, 48, 1, 0, 0, 0, 0);
    // un arco grueso: semicirculo exterior - semicirculo interior.
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 48; ++x) {
            float const dx = x + 0.5f - 24.f;
            float const dy = y + 0.5f - 32.f;
            float const dist = dx * dx + dy * dy;
            if (dist <= 576.f && dist >= 196.f && y <= 32) {
                setPixel(source, 0, x, y, 200, 60, 120);
            }
        }
    }
    auto result = buildPlan(source, paintOptions(48));
    if (!result) return false;
    auto countSpikes = [](std::vector<Primitive> const& objects) {
        int spikes = 0;
        for (auto const& object : objects) {
            float angle = std::fmod(std::abs(object.rotation), 90.f);
            angle = std::min(angle, 90.f - angle);
            if (angle > 5.f && angle < 85.f &&
                object.width <= 1.6f && object.height <= 1.6f) {
                ++spikes;
            }
        }
        return spikes;
    };
    int const rawSpikes = countSpikes(result.plan.staticObjects);
    auto const objects = unmarked(result.plan);
    // contar picos: objetos girados mas pequeños que 1.6 celdas.
    int const spikes = countSpikes(objects);
    std::cout << "paint-curve-blocks: raw=" << rawSpikes
              << " spikes=" << spikes
              << " objects=" << objects.size()
              << " review=" << result.plan.similarity << "%\n";
    if (rawSpikes > 0 || spikes > 0) {
        std::cerr << "FAIL: paint mode left spikes on a curve\n";
    }
    return rawSpikes == 0 && spikes == 0 && result.plan.similarity >= 93.f;
}

// 16. un circulo en pintura no puede invadir celdas de otro color.
//     gd pinta los circulos en otra hoja de sprites, asi que un circulo
//     que asoma sobre otro color se ve porque el cuadrado de debajo siempre
//     queda abajo.
bool paintModeCircleDoesNotBleedOverForeground() {
    auto source = animation(32, 32, 1, 0, 0, 0, 0);
    // circulo rojo.
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            float const dx = x + 0.5f - 12.f;
            float const dy = y + 0.5f - 16.f;
            if (dx * dx + dy * dy <= 64.f) setPixel(source, 0, x, y, 230, 60, 60);
        }
    }
    // banda azul superpuesta.
    for (int y = 12; y < 20; ++y) {
        for (int x = 16; x < 30; ++x) setPixel(source, 0, x, y, 60, 60, 230);
    }
    auto result = buildPlan(source, paintOptions(32));
    if (!result) return false;
    // verificar que en las celdas azules no se vea rojo.
    auto const preview = renderPlanFrame(result.plan, 0, 4);
    int const scale = 4;
    int bleeds = 0;
    for (int y = 12; y < 20; ++y) {
        for (int x = 18; x < 28; ++x) {
            std::size_t const pixel =
                (static_cast<std::size_t>(y * scale + scale / 2) * result.plan.width * scale +
                 x * scale + scale / 2) * 4;
            if (preview[pixel + 3] == 0) continue;
            if (preview[pixel] > 150 && preview[pixel + 2] < 100) ++bleeds;
        }
    }
    std::cout << "paint-circle-bleed: bleeds=" << bleeds
              << " objects=" << result.plan.visualObjects << '\n';
    if (bleeds > 0) std::cerr << "FAIL: paint circle bled into foreground color\n";
    return bleeds == 0;
}

// 17. una elipse (no circulo perfecto) en pintura debe encajar como circle
//     si el aspecto es <= 1.8.
bool paintModeDetectsEllipse() {
    auto source = animation(48, 48, 1, 0, 0, 0, 0);
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 48; ++x) {
            float const dx = (x + 0.5f - 24.f) / 17.f;
            float const dy = (y + 0.5f - 24.f) / 11.f;
            if (dx * dx + dy * dy <= 1.f) setPixel(source, 0, x, y, 200, 150, 60);
        }
    }
    auto result = buildPlan(source, paintOptions(48));
    if (!result) return false;
    auto const objects = unmarked(result.plan);
    bool const hasCircle = std::any_of(
        objects.begin(), objects.end(), [](Primitive const& object) {
            return object.kind == PrimitiveKind::Circle && object.width > 8.f;
        });
    std::cout << "paint-ellipse: hasCircle=" << hasCircle
              << " objects=" << objects.size() << '\n';
    if (!hasCircle) std::cerr << "FAIL: paint mode did not detect an ellipse\n";
    return hasCircle;
}

// 18. en modo pintura un semicirculo debe cubrirse con cobertura >= 93%.
bool paintModeSemicircleCoverage() {
    auto source = animation(40, 40, 1, 0, 0, 0, 0);
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 40; ++x) {
            float const dx = x + 0.5f - 20.f;
            float const dy = y + 0.5f - 30.f;
            if (dx * dx + dy * dy <= 289.f && y <= 30) {
                setPixel(source, 0, x, y, 120, 80, 200);
            }
        }
    }
    auto result = buildPlan(source, paintOptions(40));
    if (!result) return false;
    auto const coverage = measureCoverage(result.plan, 8);
    std::cout << "paint-semicircle: missing=" << coverage.missing
              << " minimum=" << coverage.minimumCoverage << "/64"
              << " review=" << result.plan.similarity << "%\n";
    return coverage.missing == 0 && result.plan.similarity >= 93.f;
}

// 19. las costuras entre dos colores en una frontera curva no deben dejar
//     huecos. esto prueba la reparacion de costuras del modo pintura en
//     bordes curvos.
bool paintModeClosesSeamsOnCurvedBoundary() {
    auto source = animation(40, 40, 1, 238, 231, 218);
    // frontera curva sinusoidal.
    for (int y = 0; y < 40; ++y) {
        int const edge = 20 + static_cast<int>(std::lround(std::sin(y * 0.45f) * 6.f));
        for (int x = edge; x < 40; ++x) {
            setPixel(source, 0, x, y, 90, 60, 140);
        }
    }
    auto result = buildPlan(source, paintOptions(40));
    if (!result) return false;
    constexpr int scale = 8;
    auto const preview = renderPlanFrame(result.plan, 0, scale);
    int holes = 0;
    for (int y = scale; y < (result.plan.height - 1) * scale; ++y) {
        for (int x = scale; x < (result.plan.width - 1) * scale; ++x) {
            auto const sample =
                (static_cast<std::size_t>(y) * result.plan.width * scale + x) * 4;
            holes += preview[sample + 3] == 0;
        }
    }
    std::cout << "paint-curve-seams: holes=" << holes
              << " objects=" << result.plan.visualObjects << '\n';
    if (holes > 0) std::cerr << "FAIL: paint mode left " << holes << " seam holes on curved boundary\n";
    return holes == 0 && result.plan.similarity >= 95.f;
}

// 20. el modo circulos es estrictamente circulos: incluso una imagen que
//     seria perfecta como un bloque (cuadrado macizo) debe convertirse en
//     circulos, no bloques.
bool circleModeConvertsSquareToCircles() {
    auto source = animation(20, 20, 1, 0, 0, 0, 0);
    for (int y = 4; y < 16; ++y) {
        for (int x = 4; x < 16; ++x) setPixel(source, 0, x, y, 200, 100, 60);
    }
    auto result = buildPlan(source, circleOptions(20));
    if (!result) {
        std::cout << "circle-square: " << result.error << '\n';
        return false;
    }
    auto const& objects = result.plan.staticObjects;
    bool const allCircles = std::all_of(
        objects.begin(), objects.end(), [](Primitive const& object) {
            return object.kind == PrimitiveKind::Circle;
        });
    auto const preview = renderPlanFrame(result.plan, 0, 1);
    int missing = 0;
    for (int position = 0; position < result.plan.width * result.plan.height; ++position) {
        if (result.plan.frames.front().cells[static_cast<std::size_t>(position)] < 0) continue;
        missing += preview[static_cast<std::size_t>(position) * 4 + 3] == 0;
    }
    std::cout << "circle-square: allCircles=" << allCircles
              << " missing=" << missing << " objects=" << objects.size() << '\n';
    if (!allCircles) std::cerr << "FAIL: circle mode used non-circle on a square\n";
    if (missing > 0) std::cerr << "FAIL: circle mode left gaps covering a square\n";
    return allCircles && missing == 0;
}

// 21. modo pintura con un disco sobre fondo pintado: el disco debe usar
//     circle y usar menos objetos que el modo bloques.
bool paintModeCircleBeatsBlocks() {
    auto source = animation(40, 40, 1, 0, 0, 0, 0);
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 40; ++x) {
            float const dx = x + 0.5f - 20.f;
            float const dy = y + 0.5f - 20.f;
            if (dx * dx + dy * dy <= 225.f) setPixel(source, 0, x, y, 60, 200, 120);
        }
    }
    auto blocks = buildPlan(source, exactOptions(40));
    auto paint = buildPlan(source, paintOptions(40));
    bool const pass = blocks && paint &&
        paint.plan.visualObjects < blocks.plan.visualObjects;
    std::cout << "paint-circle-vs-blocks: blocks=" << (blocks ? blocks.plan.visualObjects : 0)
              << " paint=" << (paint ? paint.plan.visualObjects : 0) << '\n';
    if (!pass) std::cerr << "FAIL: paint mode did not beat blocks on a circle over background\n";
    return pass;
}

// 22. modo circulos con animacion: los objetos no deben exceder el presupuesto.
bool circleModeAnimationStaysInBudget() {
    auto source = animation(20, 16, 4, 0, 0, 0, 0);
    for (int frame = 0; frame < 4; ++frame) {
        float const centerX = 6.f + frame * 2.5f;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 20; ++x) {
                float const dx = x + 0.5f - centerX;
                float const dy = y + 0.5f - 8.f;
                if (dx * dx + dy * dy <= 16.f) setPixel(source, frame, x, y, 200, 90, 240);
            }
        }
    }
    auto options = circleOptions(20);
    options.objectBudget = 3000;
    auto result = buildPlan(source, options);
    bool const pass = result && result.plan.totalObjects <= 3000;
    // verificar que todos los objetos visuales sean circulos.
    bool allCircles = true;
    if (result) {
        for (auto const& object : result.plan.staticObjects) {
            if (object.kind != PrimitiveKind::Circle) allCircles = false;
        }
        for (auto const& track : result.plan.tracks) {
            for (auto const& object : track.objects) {
                if (object.kind != PrimitiveKind::Circle) allCircles = false;
            }
        }
    }
    std::cout << "circle-animation-budget: objects=" << (result ? result.plan.totalObjects : 0)
              << " allCircles=" << allCircles << '\n';
    if (!pass) std::cerr << "FAIL: circle animation exceeded budget\n";
    if (!allCircles) std::cerr << "FAIL: circle animation emitted non-circle primitives\n";
    return pass && allCircles;
}

// 23. dos circulos de distinto color en modo circulos: cada uno mantiene
//     su color, y los dos estan cubiertos.
bool circleModeTwoColorCircles() {
    auto source = animation(40, 40, 1, 0, 0, 0, 0);
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 40; ++x) {
            float const d1 = (x + 0.5f - 14.f) * (x + 0.5f - 14.f) +
                             (y + 0.5f - 20.f) * (y + 0.5f - 20.f);
            float const d2 = (x + 0.5f - 28.f) * (x + 0.5f - 28.f) +
                             (y + 0.5f - 20.f) * (y + 0.5f - 20.f);
            if (d1 <= 64.f) setPixel(source, 0, x, y, 230, 80, 80);
            if (d2 <= 64.f) setPixel(source, 0, x, y, 80, 80, 230);
        }
    }
    auto result = buildPlan(source, circleOptions(40));
    if (!result) {
        std::cout << "circle-two-colors: " << result.error << '\n';
        return false;
    }
    bool const allCircles = std::all_of(
        result.plan.staticObjects.begin(), result.plan.staticObjects.end(),
        [](Primitive const& object) { return object.kind == PrimitiveKind::Circle; });
    // cobertura.
    auto const preview = renderPlanFrame(result.plan, 0, 1);
    int missing = 0;
    for (int position = 0; position < result.plan.width * result.plan.height; ++position) {
        if (result.plan.frames.front().cells[static_cast<std::size_t>(position)] < 0) continue;
        missing += preview[static_cast<std::size_t>(position) * 4 + 3] == 0;
    }
    std::cout << "circle-two-colors: allCircles=" << allCircles
              << " missing=" << missing
              << " objects=" << result.plan.staticObjects.size() << '\n';
    if (!allCircles) std::cerr << "FAIL: two-color circle mode emitted non-circles\n";
    if (missing > 0) std::cerr << "FAIL: two-color circle mode left gaps\n";
    return allCircles && missing == 0;
}

// 24. una onda sinusoidal gruesa en modo pintura: la cobertura y la fidelidad
//     deben ser buenas, y no debe haber picos diminutos.
bool paintModeSineWaveCoverage() {
    auto source = animation(40, 24, 1, 0, 0, 0, 0);
    for (int y = 0; y < 24; ++y) {
        for (int x = 0; x < 40; ++x) {
            float const wave = 12.f + std::sin(x * 0.25f) * 5.f;
            if (std::abs(y + 0.5f - wave) <= 4.5f) {
                setPixel(source, 0, x, y, 160, 60, 200);
            }
        }
    }
    auto result = buildPlan(source, paintOptions(40));
    if (!result) return false;
    auto const objects = unmarked(result.plan);
    int spikes = 0;
    for (auto const& object : objects) {
        float angle = std::fmod(std::abs(object.rotation), 90.f);
        angle = std::min(angle, 90.f - angle);
        if (angle > 5.f && angle < 85.f &&
            object.width <= 1.6f && object.height <= 1.6f) {
            ++spikes;
        }
    }
    auto const coverage = measureCoverage(result.plan, 8);
    std::cout << "paint-sine-wave: objects=" << objects.size()
              << " spikes=" << spikes
              << " missing=" << coverage.missing
              << " review=" << result.plan.similarity << "%\n";
    if (spikes > 2) std::cerr << "FAIL: sine wave left " << spikes << " spikes\n";
    if (coverage.missing > 0) std::cerr << "FAIL: sine wave left cells uncovered\n";
    return spikes <= 2 && coverage.missing == 0 && result.plan.similarity >= 78.f;
}

// 25. en modo circulos, el contorno de un circulo grande no debe tener
//     huecos visibles entre elipses: al nivel de 1 muestra/celda, cada
//     celda debe estar cubierta.
bool circleModeNoPerimeterGaps() {
    auto source = animation(48, 48, 1, 0, 0, 0, 0);
    // solo el anillo perimetral (sin interior macizo).
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 48; ++x) {
            float const dx = x + 0.5f - 24.f;
            float const dy = y + 0.5f - 24.f;
            float const dist = std::sqrt(dx * dx + dy * dy);
            if (dist >= 15.f && dist <= 20.f) {
                setPixel(source, 0, x, y, 200, 120, 60);
            }
        }
    }
    auto result = buildPlan(source, circleOptions(48));
    if (!result) {
        std::cout << "circle-perimeter: " << result.error << '\n';
        return false;
    }
    bool const allCircles = std::all_of(
        result.plan.staticObjects.begin(), result.plan.staticObjects.end(),
        [](Primitive const& object) { return object.kind == PrimitiveKind::Circle; });
    auto const preview = renderPlanFrame(result.plan, 0, 1);
    int missing = 0;
    for (int position = 0; position < result.plan.width * result.plan.height; ++position) {
        if (result.plan.frames.front().cells[static_cast<std::size_t>(position)] < 0) continue;
        missing += preview[static_cast<std::size_t>(position) * 4 + 3] == 0;
    }
    std::cout << "circle-perimeter: allCircles=" << allCircles
              << " missing=" << missing
              << " objects=" << result.plan.staticObjects.size() << '\n';
    if (!allCircles) std::cerr << "FAIL: perimeter ring used non-circles\n";
    if (missing > 0) std::cerr << "FAIL: perimeter ring has " << missing << " gaps\n";
    return allCircles && missing == 0;
}

// 26. vectorizecircles: componentes desconectados deben procesarse todos.
//     dos manchas separadas, las dos cubiertas.
bool circleVectorizerHandlesDisconnectedComponents() {
    constexpr int width = 20;
    constexpr int height = 10;
    std::vector<int> positions;
    // mancha 1: izquierda.
    for (int y = 2; y < 5; ++y) {
        for (int x = 2; x < 6; ++x) positions.push_back(y * width + x);
    }
    // mancha 2: derecha.
    for (int y = 5; y < 8; ++y) {
        for (int x = 14; x < 18; ++x) positions.push_back(y * width + x);
    }
    auto objects = vectorizeCircles(
        positions, width, height, 0, 0, {},
        emptyOutside(positions, width * height));
    int missing = 0;
    for (int position : positions) {
        int const px = position % width;
        int const py = position / width;
        bool const covered = std::any_of(
            objects.begin(), objects.end(), [&](Primitive const& object) {
                return xformOf(object).contains(px + 0.5f, py + 0.5f);
            });
        if (!covered) ++missing;
    }
    std::cout << "circle-disconnected: objects=" << objects.size()
              << " missing=" << missing << '\n';
    if (missing > 0) std::cerr << "FAIL: disconnected circles left " << missing << " cells uncovered\n";
    return missing == 0;
}

// 27. en modo pintura, un rombo (cuadrado girado 45 grados) debe encajar
//     como una pieza girada, no como 50 bloques axis-aligned. el modo
//     pintura debe detectar el angulo principal por la envolvente convexa.
// un fondo opaco tambien debe permitir lados diagonales, sin reconstruir la
// escalera con parches. se mide el resultado del pipeline, incluidas costuras.
bool paintPrunesBoundaryTeeth() {
    std::vector<Primitive> objects{
        {16.f, 16.f, 20.f, 20.f, 0.f, 0, PrimitiveKind::Circle, 0},
        {23.5f, 22.5f, 1.f, 1.f, 0.f, 0, PrimitiveKind::Block, 1}
    };
    prunePaintObjects(objects, 32, 32);
    std::cout << "paint-boundary-teeth: objects=" << objects.size() << '\n';
    if (objects.size() != 1 || objects.front().kind != PrimitiveKind::Circle) return false;
    // un detalle de otro color en el mismo punto debe conservarse.
    objects.push_back({23.5f, 22.5f, 1.f, 1.f, 0.f, 1, PrimitiveKind::Block, 2});
    prunePaintObjects(objects, 32, 32);
    return objects.size() == 2;
}

bool paintSeparateDiscsStayRound() {
    auto source = animation(64, 40, 1, 20, 40, 70);
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 64; ++x) {
            float const dy = y + 0.5f - 20.f;
            float const dx = x + 0.5f - (x < 32 ? 16.f : 48.f);
            if (dx * dx + dy * dy <= 121.f) {
                setPixel(source, 0, x, y, 220, 80, 60);
            }
        }
    }
    auto result = buildPlan(source, paintOptions(64));
    if (!result) return false;
    auto const objects = unmarked(result.plan);
    auto const stats = countPrimitives(objects);
    auto const coverage = measureCoverage(result.plan, 8);
    std::cout << "paint-separate-discs: circles=" << stats.circles
              << " objects=" << objects.size() << '\n';
    return stats.circles == 2 && objects.size() <= 3 &&
        coverage.missing == 0 && coverage.interiorHoles == 0;
}

bool paintRepairsKeepLongDiagonal() {
    constexpr int size = 32;
    std::vector<int> positions;
    std::vector<int> allowed;
    std::vector<std::uint8_t> permitted(size * size, 0);
    for (int i = 5; i < 27; ++i) {
        positions.push_back(i * size + i);
        for (int dx = -1; dx <= 1; ++dx) {
            allowed.push_back(i * size + i + dx);
            permitted[i * size + i + dx] = 1;
        }
    }
    std::vector<Primitive> objects;
    appendRepairs(objects, positions, size, size, 0, 0, {}, allowed);
    int missing = 0;
    for (int position : positions) {
        bool covered = false;
        for (auto const& object : objects) {
            covered |= xformOf(object).contains(position % size + 0.5f,
                                                position / size + 0.5f);
        }
        missing += !covered;
    }
    int invaded = 0;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (permitted[y * size + x]) continue;
            for (auto const& object : objects) {
                invaded += xformOf(object).contains(x + 0.5f, y + 0.5f);
            }
        }
    }
    std::cout << "paint-diagonal-repairs: objects=" << objects.size()
              << " missing=" << missing << " invaded=" << invaded << '\n';
    return objects.size() <= 4 && missing == 0 && invaded == 0;
}

bool paintModeSmoothDiamondOverBackground() {
    auto source = animation(64, 64, 1, 30, 60, 110);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            if (std::abs(x - 32) + std::abs(y - 32) <= 22) {
                setPixel(source, 0, x, y, 230, 50, 90);
            }
        }
    }
    auto result = buildPlan(source, paintOptions(64));
    if (!result) return false;
    auto const objects = unmarked(result.plan);
    int diagonals = 0;
    for (auto const& object : objects) {
        float const angle = std::fmod(std::abs(object.rotation), 90.f);
        diagonals += object.kind == PrimitiveKind::Stroke &&
            angle > 10.f && angle < 80.f && object.width > 20.f;
    }
    auto const coverage = measureCoverage(result.plan, 8);
    std::cout << "paint-diamond-background: objects=" << objects.size()
              << " diagonals=" << diagonals << " holes=" << coverage.interiorHoles << '\n';
    return diagonals > 0 && objects.size() <= 8 &&
        coverage.missing == 0 && coverage.interiorHoles == 0 &&
        result.plan.similarity >= 97.f;
}

bool paintModeFitsDiamondAsTiltedBox() {
    auto source = animation(40, 40, 1, 0, 0, 0, 0);
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 40; ++x) {
            if (std::abs(x - 20) + std::abs(y - 20) <= 12) {
                setPixel(source, 0, x, y, 120, 200, 80);
            }
        }
    }
    auto result = buildPlan(source, paintOptions(40));
    if (!result) return false;
    auto const objects = unmarked(result.plan);
    bool const tilted = std::any_of(
        objects.begin(), objects.end(), [](Primitive const& object) {
            float const quarter = std::fmod(std::abs(object.rotation), 90.f);
            return std::min(quarter, 90.f - quarter) > 5.f;
        });
    std::cout << "paint-diamond: objects=" << objects.size()
              << " tilted=" << tilted << '\n';
    if (!tilted) std::cerr << "FAIL: paint mode did not tilt any object for a diamond\n";
    // con un buen ajuste, no deberian ser mas de 4 objetos.
    return tilted && objects.size() <= 6;
}

// 28. en modo circulos un rombo tambien debe quedar cubierto solo con
//     circulos, nunca con bloques.
bool circleModeCoversDiamond() {
    auto source = animation(32, 32, 1, 0, 0, 0, 0);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            if (std::abs(x - 16) + std::abs(y - 16) <= 10) {
                setPixel(source, 0, x, y, 200, 100, 60);
            }
        }
    }
    auto result = buildPlan(source, circleOptions(32));
    if (!result) {
        std::cout << "circle-diamond: " << result.error << '\n';
        return false;
    }
    bool const allCircles = std::all_of(
        result.plan.staticObjects.begin(), result.plan.staticObjects.end(),
        [](Primitive const& object) { return object.kind == PrimitiveKind::Circle; });
    auto const preview = renderPlanFrame(result.plan, 0, 1);
    int missing = 0;
    for (int position = 0; position < result.plan.width * result.plan.height; ++position) {
        if (result.plan.frames.front().cells[static_cast<std::size_t>(position)] < 0) continue;
        missing += preview[static_cast<std::size_t>(position) * 4 + 3] == 0;
    }
    std::cout << "circle-diamond: allCircles=" << allCircles
              << " missing=" << missing
              << " objects=" << result.plan.staticObjects.size() << '\n';
    if (!allCircles) std::cerr << "FAIL: diamond circle mode used non-circles\n";
    if (missing > 0) std::cerr << "FAIL: diamond circle mode left " << missing << " gaps\n";
    return allCircles && missing == 0;
}

// 29. modo pintura con multiples colores en curvas: la frontera entre los
//     colores no debe dejar huecos ni sangrado cruzado.
bool paintModeMulticolorCurvesNoGaps() {
    auto source = animation(48, 48, 1, 0, 0, 0, 0);
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 48; ++x) {
            float const dx = x + 0.5f - 24.f;
            float const dy = y + 0.5f - 24.f;
            float const dist = std::sqrt(dx * dx + dy * dy);
            if (dist <= 20.f) {
                // sector por angulo.
                float const angle = std::atan2(dy, dx);
                if (angle < -1.f) setPixel(source, 0, x, y, 230, 60, 60);
                else if (angle < 1.f) setPixel(source, 0, x, y, 60, 200, 60);
                else setPixel(source, 0, x, y, 60, 60, 230);
            }
        }
    }
    auto result = buildPlan(source, paintOptions(48));
    if (!result) return false;
    constexpr int scale = 8;
    auto const preview = renderPlanFrame(result.plan, 0, scale);
    int holes = 0;
    std::vector<int> holeCells;
    auto const& cells = result.plan.frames.front().cells;
    for (std::size_t position = 0; position < cells.size(); ++position) {
        if (cells[position] < 0) continue;
        int const cx = static_cast<int>(position % result.plan.width);
        int const cy = static_cast<int>(position / result.plan.width);
        // revisar el centro de la celda.
        std::size_t const pixel =
            (static_cast<std::size_t>(cy * scale + scale / 2) * result.plan.width * scale +
             cx * scale + scale / 2) * 4;
        if (preview[pixel + 3] == 0) {
            ++holes;
            holeCells.push_back(cy * result.plan.width + cx);
        }
    }
    std::cout << "paint-multicolor-curves: holes=" << holes
              << " objects=" << result.plan.visualObjects
              << " review=" << result.plan.similarity << "%\n";
    if (!holeCells.empty()) {
        std::cerr << "FAIL: multicolor curves left " << holes << " center-pixel holes at";
        for (int position : holeCells) {
            std::cerr << ' ' << (position % result.plan.width)
                      << ',' << (position / result.plan.width);
        }
        std::cerr << '\n';
    }
    return holes == 0 && result.plan.similarity >= 90.f;
}

//  tests de codos en redondo (round joints del modo pintura)
// las tiras que giran con angulo se cortan a tope en el vertice y un disco
// del grosor del trazo tapa el pico de fuera. sin el disco, la esquina del
// bisel asoma (hasta medio grosor sobre el vertice); en un giro suave, en
// cambio, el disco sobresaldria mas que el pico y el codo sigue con bisel.

// esquina exterior maxima de tiras y discos fuera de la mancha, en celdas.
float maxPaintSpike(
    std::vector<Primitive> const& objects,
    std::vector<std::uint8_t> const& mask,
    int width,
    int height
) {
    constexpr float pi = 3.14159265358979323846f;
    auto outside = [&](float x, float y) {
        int const ix = static_cast<int>(std::floor(x));
        int const iy = static_cast<int>(std::floor(y));
        if (ix >= 0 && iy >= 0 && ix < width && iy < height &&
            mask[static_cast<std::size_t>(iy) * width + ix] != 0) {
            return 0.f;
        }
        float best = 1e9f;
        for (int cy = 0; cy < height; ++cy) {
            for (int cx = 0; cx < width; ++cx) {
                if (mask[static_cast<std::size_t>(cy) * width + cx] == 0) continue;
                float const dx = x < cx ? cx - x : (x > cx + 1 ? x - (cx + 1) : 0.f);
                float const dy = y < cy ? cy - y : (y > cy + 1 ? y - (cy + 1) : 0.f);
                best = std::min(best, std::hypot(dx, dy));
            }
        }
        return best;
    };
    float worst = 0.f;
    for (auto const& object : objects) {
        if (object.kind == PrimitiveKind::Circle) {
            for (int s = 0; s < 32; ++s) {
                float const a = s * 2.f * pi / 32.f;
                worst = std::max(worst, outside(
                    object.x + std::cos(a) * object.width * 0.5f,
                    object.y + std::sin(a) * object.height * 0.5f));
            }
            continue;
        }
        if (object.kind != PrimitiveKind::Stroke) continue;
        float const a = object.rotation * pi / 180.f;
        float const ca = std::cos(a);
        float const sa = std::sin(a);
        for (float sx : {-0.5f, 0.5f}) {
            for (float sy : {-0.5f, 0.5f}) {
                worst = std::max(worst, outside(
                    object.x + (ca * object.width * sx - sa * object.height * sy),
                    object.y + (sa * object.width * sx + ca * object.height * sy)));
            }
        }
    }
    return worst;
}

// celdas de la mancha cuyo centro no tapa ningun objeto.
int paintMissingCells(
    std::vector<Primitive> const& objects,
    std::vector<int> const& positions,
    int width
) {
    int missing = 0;
    for (int position : positions) {
        float const x = static_cast<float>(position % width) + 0.5f;
        float const y = static_cast<float>(position / width) + 0.5f;
        bool covered = false;
        for (auto const& object : objects) {
            if (xformOf(object).contains(x, y)) {
                covered = true;
                break;
            }
        }
        missing += !covered;
    }
    return missing;
}

std::vector<Primitive> paintCells(std::vector<int> const& cells, int size) {
    std::vector<std::uint8_t> empty(
        static_cast<std::size_t>(size) * size, 1);
    for (int position : cells) empty[static_cast<std::size_t>(position)] = 0;
    return vectorizePaint(cells, size, size, 0, 0, {}, empty);
}

std::vector<std::uint8_t> paintMask(std::vector<int> const& cells, int size) {
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(size) * size, 0);
    for (int position : cells) mask[static_cast<std::size_t>(position)] = 1;
    return mask;
}

// 31. un codo de 90 grados lleva disco y el pico exterior baja de 1 celda
//     (sin disco pasa de 1.4), sin dejar huecos ni disparar los objetos.
bool paintRoundJointCapsElbow() {
    constexpr int size = 40;
    std::vector<int> cells;
    for (int y = 17; y <= 20; ++y) {
        for (int x = 6; x <= 33; ++x) cells.push_back(y * size + x);
    }
    for (int y = 6; y <= 33; ++y) {
        for (int x = 6; x <= 9; ++x) cells.push_back(y * size + x);
    }
    auto const objects = paintCells(cells, size);
    auto const stats = countPrimitives(objects);
    float const spike = maxPaintSpike(objects, paintMask(cells, size), size, size);
    int const missing = paintMissingCells(objects, cells, size);
    std::cout << "paint-elbow-joint: objects=" << objects.size()
              << " circles=" << stats.circles
              << " spike=" << spike << " missing=" << missing << '\n';
    bool const pass = stats.circles >= 1 && spike < 1.f && missing == 0 &&
        objects.size() <= 15;
    if (!pass) std::cerr << "FAIL: elbow did not get a round joint cap\n";
    return pass;
}

// 32. una curva suave (anillo) no lleva ningun disco: los empalmes de
//     trazado son continuaciones, no horquillas, y el remate cuadrado las
//     tapa. cualquier disco aqui seria bulto sobre la curva.
bool paintRoundJointLeavesRingClean() {
    constexpr int size = 40;
    std::vector<int> cells;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float const d = std::hypot(x + 0.5f - 20.f, y + 0.5f - 20.f);
            if (d >= 10.f && d <= 14.f) cells.push_back(y * size + x);
        }
    }
    auto const objects = paintCells(cells, size);
    auto const stats = countPrimitives(objects);
    float const spike = maxPaintSpike(objects, paintMask(cells, size), size, size);
    int const missing = paintMissingCells(objects, cells, size);
    std::cout << "paint-ring-joint: objects=" << objects.size()
              << " circles=" << stats.circles
              << " spike=" << spike << " missing=" << missing << '\n';
    bool const pass = stats.circles == 0 && spike < 0.6f && missing == 0;
    if (!pass) std::cerr << "FAIL: smooth ring grew joint discs\n";
    return pass;
}

// 33. un zigzag de codos de 90 pone discos en los codos y el pico maximo
//     baja, sin dejar huecos. los empalmes con angulo conocido tambien
//     licitan aunque el punto sea muy agudo.
bool paintRoundJointCoversZigzag() {
    constexpr int size = 40;
    std::vector<int> cells;
    auto bar = [&](int x0, int x1, int y0, int y1) {
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) cells.push_back(y * size + x);
        }
    };
    bar(6, 9, 22, 33);
    bar(6, 17, 19, 22);
    bar(14, 17, 11, 22);
    bar(14, 25, 11, 14);
    bar(22, 25, 6, 14);
    auto const objects = paintCells(cells, size);
    auto const stats = countPrimitives(objects);
    float const spike = maxPaintSpike(objects, paintMask(cells, size), size, size);
    int const missing = paintMissingCells(objects, cells, size);
    std::cout << "paint-zigzag-joint: objects=" << objects.size()
              << " circles=" << stats.circles
              << " spike=" << spike << " missing=" << missing << '\n';
    bool const pass = stats.circles >= 3 && spike < 1.6f && missing == 0 &&
        objects.size() <= 30;
    if (!pass) std::cerr << "FAIL: zigzag elbows did not get round joints\n";
    return pass;
}

// 30. modo circulos: el tamaño total de objetos debe ser razonable. un
//     circulo simple de ~14 celdas de diametro no necesita cientos de
//     ellipses diminutas.
bool circleModeObjectCountIsReasonable() {
    auto source = animation(32, 32, 1, 0, 0, 0, 0);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            float const dx = x + 0.5f - 16.f;
            float const dy = y + 0.5f - 16.f;
            if (dx * dx + dy * dy <= 49.f) setPixel(source, 0, x, y, 90, 200, 240);
        }
    }
    auto result = buildPlan(source, circleOptions(32));
    if (!result) {
        std::cout << "circle-object-count: " << result.error << '\n';
        return false;
    }
    // un circulo de ~14px diametro tiene ~150 celdas; deberian ser muchas menos
    // elipses que celdas (el punto del vectorizador es justamente reducir).
    int const targetCells = static_cast<int>(std::count_if(
        result.plan.frames.front().cells.begin(),
        result.plan.frames.front().cells.end(),
        [](int c) { return c >= 0; }));
    bool const reasonable = result.plan.staticObjects.size() <=
        static_cast<std::size_t>(targetCells / 2);
    std::cout << "circle-object-count: objects=" << result.plan.staticObjects.size()
              << " cells=" << targetCells << '\n';
    if (!reasonable) {
        std::cerr << "FAIL: circle mode used too many objects ("
                  << result.plan.staticObjects.size() << " for " << targetCells << " cells)\n";
    }
    return reasonable;
}

bool fragmentCentersMatch(
    std::vector<Primitive> const& objects,
    std::vector<std::int32_t> const& cells,
    int width,
    int height
) {
    auto const forms = xformsOf(objects);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int color = -1;
            for (std::size_t index = 0; index < objects.size(); ++index) {
                if (forms[index].contains(x + 0.5f, y + 0.5f)) color = objects[index].color;
            }
            if (color != cells[static_cast<std::size_t>(y) * width + x]) return false;
        }
    }
    return true;
}

bool paintFragmentsJoinShallowSteps() {
    constexpr int width = 48, height = 32;
    std::vector<std::int32_t> cells(width * height, -1);
    std::vector<Primitive> objects;
    for (int x = 4; x < 36; ++x) {
        int const y = 4 + x / 2;
        cells[y * width + x] = 0;
        objects.push_back({x + 0.5f, y + 0.5f, 1.f, 1.f, 0.f, 0, PrimitiveKind::Block, 2});
    }
    smoothPaintFragments(objects, cells, {0}, width, height);
    int const strokes = static_cast<int>(std::count_if(objects.begin(), objects.end(), [](auto const& object) {
        return object.kind == PrimitiveKind::Stroke && std::abs(object.rotation) > 10.f;
    }));
    std::cout << "paint-fragment-shallow: objects=" << objects.size() << " strokes=" << strokes << '\n';
    return objects.size() <= 8 && strokes > 0 && fragmentCentersMatch(objects, cells, width, height);
}

bool paintFragmentsJoinSteepSteps() {
    constexpr int size = 48;
    std::vector<std::int32_t> cells(size * size, -1);
    std::vector<int> positions;
    for (int y = 4; y < 36; ++y) {
        int const position = y * size + 4 + y / 3;
        cells[position] = 0;
        positions.push_back(position);
    }
    auto objects = packBlocks(positions, size, size, 0);
    std::size_t const before = objects.size();
    smoothPaintFragments(objects, cells, {0}, size, size);
    bool const rotated = std::any_of(objects.begin(), objects.end(), [](auto const& object) {
        return object.kind == PrimitiveKind::Stroke &&
            std::abs(std::sin(object.rotation * kPi / 90.f)) > 0.1f;
    });
    std::cout << "paint-fragment-steep: before=" << before << " after=" << objects.size() << '\n';
    return objects.size() < before && rotated && fragmentCentersMatch(objects, cells, size, size);
}

bool paintFragmentsKeepCurveContacts() {
    constexpr int size = 48;
    std::vector<std::int32_t> cells(size * size, -1);
    std::vector<Primitive> objects;
    std::vector<Point> centers;
    for (int x = 4; x < 36; ++x) {
        int const y = 10 + static_cast<int>(std::lround(6.f * std::sin(x / 7.f)));
        cells[y * size + x] = 0;
        centers.push_back({x + 0.5f, y + 0.5f});
        objects.push_back({x + 0.5f, y + 0.5f, 1.f, 1.f, 0.f, 0, PrimitiveKind::Block, 2});
    }
    smoothPaintFragments(objects, cells, {0}, size, size);
    auto const forms = xformsOf(objects);
    for (std::size_t index = 1; index < centers.size(); ++index) {
        Point const contact{(centers[index - 1].x + centers[index].x) * 0.5f,
                            (centers[index - 1].y + centers[index].y) * 0.5f};
        if (std::none_of(forms.begin(), forms.end(), [&](auto const& form) {
                return form.contains(contact.x, contact.y);
            })) {
            return false;
        }
    }
    std::cout << "paint-fragment-contacts: objects=" << objects.size() << '\n';
    return objects.size() < centers.size() && fragmentCentersMatch(objects, cells, size, size);
}

bool paintFragmentsKeepIsolatedDetails() {
    constexpr int size = 24;
    std::vector<std::int32_t> cells(size * size, -1);
    cells[8 * size + 8] = cells[8 * size + 10] = 0;
    std::vector<Primitive> objects{
        {8.5f, 8.5f, 1.f, 1.f, 0.f, 0, PrimitiveKind::Block, 2},
        {10.5f, 8.5f, 1.f, 1.f, 0.f, 0, PrimitiveKind::Block, 2}
    };
    smoothPaintFragments(objects, cells, {0}, size, size);
    return objects.size() == 2 && fragmentCentersMatch(objects, cells, size, size);
}

bool paintFragmentsUseRoundPatches() {
    constexpr int size = 32;
    std::vector<std::int32_t> cells(size * size, -1);
    std::vector<Primitive> objects;
    for (int y = 10; y < 18; ++y) {
        for (int x = 10; x < 18; ++x) {
            float const dx = x + 0.5f - 14.f, dy = y + 0.5f - 14.f;
            if (dx * dx + dy * dy > 12.f) continue;
            cells[y * size + x] = 0;
            objects.push_back({x + 0.5f, y + 0.5f, 1.f, 1.f, 0.f, 0, PrimitiveKind::Block, 2});
        }
    }
    smoothPaintFragments(objects, cells, {0}, size, size);
    bool const round = std::any_of(objects.begin(), objects.end(), [](auto const& object) {
        return object.kind == PrimitiveKind::Circle;
    });
    return objects.size() < 8 && round && fragmentCentersMatch(objects, cells, size, size);
}

bool paintFragmentsPreserveInteriorAndForeground() {
    constexpr int size = 32;
    std::vector<std::int32_t> cells(size * size, 0);
    std::vector<Primitive> objects{{16.f, 16.f, 32.f, 32.f, 0.f, 0, PrimitiveKind::Block, 0}};
    for (int y = 8; y < 24; ++y) {
        for (int x = 8; x < 24; ++x) {
            cells[y * size + x] = 1;
            objects.push_back({x + 0.5f, y + 0.5f, 1.f, 1.f, 0.f, 1, PrimitiveKind::Block, 5});
        }
    }
    for (int y = 12; y < 20; ++y) {
        for (int x = 12; x < 20; ++x) cells[y * size + x] = 2;
    }
    objects.push_back({16.f, 16.f, 8.f, 8.f, 0.f, 2, PrimitiveKind::Block, 8});
    objects.push_back({16.f, 16.f, 32.f, 32.f, 0.f, 0, PrimitiveKind::Block, -1});
    smoothPaintFragments(objects, cells, {0, 1, 2}, size, size);
    if (!fragmentCentersMatch(objects, cells, size, size)) return false;
    auto const forms = xformsOf(objects);
    for (int y = 9; y < 23; ++y) {
        for (int x = 9; x < 23; ++x) {
            int const cell = y * size + x;
            int const expected = cells[cell];
            if (cells[cell - 1] != expected || cells[cell + 1] != expected ||
                cells[cell - size] != expected || cells[cell + size] != expected) {
                continue;
            }
            for (int sy = 0; sy < 8; ++sy) {
                for (int sx = 0; sx < 8; ++sx) {
                    int color = -1;
                    for (std::size_t index = 0; index < objects.size(); ++index) {
                        if (forms[index].contains(x + (sx + 0.5f) / 8.f, y + (sy + 0.5f) / 8.f)) {
                            color = objects[index].color;
                        }
                    }
                    if (color != expected) return false;
                }
            }
        }
    }
    for (std::size_t index = 0; index < objects.size(); ++index) {
        if (objects[index].kind != PrimitiveKind::Circle) continue;
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                if (forms[index].contains(x + 0.5f, y + 0.5f) &&
                    cells[y * size + x] != objects[index].color) {
                    return false;
                }
            }
        }
    }
    return std::any_of(objects.begin(), objects.end(), [](auto const& object) {
        return object.layer == -1 && object.width == 32.f && object.height == 32.f;
    });
}

bool paintRoundFragmentsPreserveCoverage() {
    constexpr int size = 48;
    int roundedCases = 0;
    for (float angle : {0.f, 7.f, 15.f, 26.f, 45.f, 63.f, 83.f, 90.f, 117.f, 155.f, 179.f}) {
        for (float thickness : {0.7f, 1.f, 1.5f, 2.f, 3.f, 5.f, 8.f}) {
            Primitive const original{20.2f, 20.3f, 14.f, thickness, angle,
                0, PrimitiveKind::Stroke, 1};
            auto const originalForm = xformOf(original);
            std::vector<int> positions;
            std::vector<std::uint8_t> target(size * size, 0);
            for (int y = 0; y < size; ++y) {
                for (int x = 0; x < size; ++x) {
                    if (!originalForm.contains(x + 0.5f, y + 0.5f)) continue;
                    positions.push_back(y * size + x);
                    target[static_cast<std::size_t>(y) * size + x] = 1;
                }
            }
            std::vector<Primitive> objects{original};
            roundPaintFragmentEnds(objects, positions, size, size);
            roundedCases += objects.size() > 1;
            auto const forms = xformsOf(objects);
            auto covered = [&](float x, float y) {
                return std::any_of(forms.begin(), forms.end(), [&](ShapeXform const& form) {
                    return form.contains(x, y);
                });
            };
            for (int position : positions) {
                int const x = position % size, y = position / size;
                if (!covered(x + 0.5f, y + 0.5f)) return false;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        int const xx = x + dx, yy = y + dy;
                        if ((dx == 0 && dy == 0) || xx < 0 || yy < 0 || xx >= size || yy >= size ||
                            !target[static_cast<std::size_t>(yy) * size + xx]) continue;
                        if (dx != 0 && dy != 0 &&
                            (target[static_cast<std::size_t>(y) * size + xx] ||
                             target[static_cast<std::size_t>(yy) * size + x])) continue;
                        float const px = x + 0.5f + dx * 0.5f;
                        float const py = y + 0.5f + dy * 0.5f;
                        if (originalForm.contains(px, py) && !covered(px, py)) return false;
                    }
                }
                bool const interior = x > 0 && y > 0 && x + 1 < size && y + 1 < size &&
                    target[position - 1] && target[position + 1] &&
                    target[position - size] && target[position + size];
                if (!interior) continue;
                for (int sy = 0; sy < 8; ++sy) {
                    for (int sx = 0; sx < 8; ++sx) {
                        float const px = x + (sx + 0.5f) / 8.f;
                        float const py = y + (sy + 0.5f) / 8.f;
                        if (originalForm.contains(px, py) && !covered(px, py)) return false;
                    }
                }
            }
            for (auto const& object : objects) {
                bool const expands = forEachSample(xformOf(object), size, size, 8, [&](int x, int y) {
                    return !originalForm.contains((x + 0.5f) / 8.f, (y + 0.5f) / 8.f);
                });
                if (expands) return false;
                if (object.kind != PrimitiveKind::Circle) continue;
                for (int y = 0; y < size; ++y) {
                    for (int x = 0; x < size; ++x) {
                        if (target[static_cast<std::size_t>(y) * size + x]) continue;
                        for (float dy : {0.4f, 0.5f, 0.6f}) {
                            for (float dx : {0.4f, 0.5f, 0.6f}) {
                                if (xformOf(object).contains(x + dx, y + dy)) return false;
                            }
                        }
                    }
                }
            }
            auto const count = objects.size();
            roundPaintFragmentEnds(objects, positions, size, size);
            if (objects.size() != count) return false;
        }
    }
    std::cout << "paint-round-fragments: cases=77 rounded=" << roundedCases << '\n';
    return roundedCases >= 10;
}

bool paintSmoothJointsRoundShallowBends() {
    float const shallow = std::cos(24.f * kPi / 180.f);
    return needsPaintRoundJoint(shallow, 1.f, false) &&
        !needsPaintRoundJoint(shallow, 1.f, true) &&
        !needsPaintRoundJoint(1.f, 3.f, false) &&
        !needsPaintRoundJoint(shallow, 0.4f, false);
}

bool paintRoundFragmentsKeepHiddenEnds() {
    constexpr int size = 48;
    Primitive const stroke{20.2f, 20.3f, 14.f, 1.f, 26.f, 0, PrimitiveKind::Stroke, 1};
    std::vector<Primitive> objects{stroke,
        {20.2f, 20.3f, 20.f, 20.f, 0.f, 0, PrimitiveKind::Block, 0}};
    std::vector<int> positions;
    for (int y = 10; y < 30; ++y) {
        for (int x = 10; x < 30; ++x) positions.push_back(y * size + x);
    }
    roundPaintFragmentEnds(objects, positions, size, size);
    return objects.size() == 2 && objects.front().width == stroke.width &&
        objects.front().height == stroke.height;
}

bool paintRoundCapsProtectForegroundSamples() {
    constexpr int size = 24;
    std::vector<std::uint8_t> blocked(size * size, 0);
    blocked[10 * size + 10] = 1;
    Primitive const cap{10.f, 10.f, 1.2f, 1.2f, 0.f, 0, PrimitiveKind::Circle, 1};
    return !xformOf(cap).contains(10.5f, 10.5f) &&
        coversBlocked(cap, size, size, blocked);
}

bool autoPaintKeepsSmallAndFlatSources() {
    auto const small = animation(290, 290, 1, 180, 180, 180);
    auto const flat = animation(736, 736, 1, 180, 180, 180);
    auto hidden = animation(736, 736, 1, 0, 0, 0, 0);
    for (int y = 0; y < hidden.height; ++y) {
        for (int x = 0; x < hidden.width; ++x) {
            if ((x + y) % 2) setPixel(hidden, 0, x, y, 255, 255, 255, 0);
        }
    }
    return autoPaintDimension(small) == 320 && autoPaintDimension(flat) == 320 &&
        autoPaintDimension(hidden) == 320;
}

bool autoPaintRaisesDetailedSources() {
    auto source = animation(736, 736, 1);
    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            if ((x + y) % 2) setPixel(source, 0, x, y, 255, 255, 255);
        }
    }
    auto medium = animation(400, 200, 1);
    for (int y = 0; y < medium.height; ++y) {
        for (int x = 0; x < medium.width; ++x) {
            if ((x + y) % 2) setPixel(medium, 0, x, y, 255, 255, 255);
        }
    }
    return autoPaintDimension(source) == 680 && autoPaintDimension(medium) == 400;
}

bool autoPaintChecksMiddleFrame() {
    auto source = animation(736, 736, 3);
    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            if ((x + y) % 2) setPixel(source, 1, x, y, 255, 255, 255);
        }
    }
    return autoPaintDimension(source) == 680;
}

bool autoPaintPreservesResolutionDuringSourceScaling() {
    auto options = paintOptions(320);
    if (sourceResolutionLimit(options) != 320) return false;
    options.autoResolution = true;
    if (sourceResolutionLimit(options) != 680) return false;
    options.mode = ImportMode::Blocks;
    return sourceResolutionLimit(options) == 320;
}

} // namespace

int main() {
    // circulos
    bool const c01 = circleModeNeverEmitsSquares();
    bool const c02 = circleModeCoversAllCells();
    bool const c03 = circleModeProducesDiscsAndSpindles();
    bool const c04 = circleModeRespectsColorBoundaries();
    bool const c05 = circleModeCurvesUseCirclesNotRotatedRects();
    bool const c06 = circleModeHandlesSinglePixel();
    bool const c07 = circleModeCoversDiagonalLine();
    bool const c08 = circleModeHandlesLShape();
    bool const c09 = circleModePreservesLayerOrder();
    bool const c10 = circleVectorizerMergesHorizontalRow();
    bool const c11 = circleVectorizerCoversSolidSquare();
    bool const c12 = circleVectorizerControlsSpillWithBlocked();
    bool const c13 = circleModeConvertsSquareToCircles();
    bool const c14 = circleModeAnimationStaysInBudget();
    bool const c15 = circleModeTwoColorCircles();
    bool const c16 = circleModeNoPerimeterGaps();
    bool const c17 = circleVectorizerHandlesDisconnectedComponents();
    bool const c18 = circleModeCoversDiamond();
    bool const c19 = circleModeObjectCountIsReasonable();

    // pintura + circulos
    bool const p01 = paintModeDetectsCircle();
    bool const p02 = paintModeDetectsSmallCircle();
    bool const p03 = paintModeCurvesUseBlocksNotRotatedSlivers();
    bool const p04 = paintModeCircleDoesNotBleedOverForeground();
    bool const p05 = paintModeDetectsEllipse();
    bool const p06 = paintModeSemicircleCoverage();
    bool const p07 = paintModeClosesSeamsOnCurvedBoundary();
    bool const p08 = paintModeCircleBeatsBlocks();
    bool const p09 = paintModeFitsDiamondAsTiltedBox();
    bool const p10 = paintModeSineWaveCoverage();
    bool const p11 = paintModeMulticolorCurvesNoGaps();
    bool const p15 = paintPrunesBoundaryTeeth();
    bool const p16 = paintRoundJointCapsElbow();
    bool const p17 = paintRoundJointLeavesRingClean();
    bool const p18 = paintRoundJointCoversZigzag();
    bool const p14 = paintSeparateDiscsStayRound();
    bool const p13 = paintRepairsKeepLongDiagonal();
    bool const p12 = paintModeSmoothDiamondOverBackground();
    bool const f01 = paintFragmentsJoinShallowSteps();
    bool const f02 = paintFragmentsJoinSteepSteps();
    bool const f03 = paintFragmentsKeepCurveContacts();
    bool const f04 = paintFragmentsKeepIsolatedDetails();
    bool const f05 = paintFragmentsUseRoundPatches();
    bool const f06 = paintFragmentsPreserveInteriorAndForeground();
    bool const f07 = paintRoundFragmentsPreserveCoverage();
    bool const f08 = paintSmoothJointsRoundShallowBends();
    bool const f09 = paintRoundFragmentsKeepHiddenEnds();
    bool const f10 = paintRoundCapsProtectForegroundSamples();
    bool const a01 = autoPaintKeepsSmallAndFlatSources();
    bool const a02 = autoPaintRaisesDetailedSources();
    bool const a03 = autoPaintChecksMiddleFrame();
    bool const a04 = autoPaintPreservesResolutionDuringSourceScaling();

    if (!c01) std::cerr << "FAIL: circleModeNeverEmitsSquares\n";
    if (!c02) std::cerr << "FAIL: circleModeCoversAllCells\n";
    if (!c03) std::cerr << "FAIL: circleModeProducesDiscsAndSpindles\n";
    if (!c04) std::cerr << "FAIL: circleModeRespectsColorBoundaries\n";
    if (!c05) std::cerr << "FAIL: circleModeCurvesUseCirclesNotRotatedRects\n";
    if (!c06) std::cerr << "FAIL: circleModeHandlesSinglePixel\n";
    if (!c07) std::cerr << "FAIL: circleModeCoversDiagonalLine\n";
    if (!c08) std::cerr << "FAIL: circleModeHandlesLShape\n";
    if (!c09) std::cerr << "FAIL: circleModePreservesLayerOrder\n";
    if (!c10) std::cerr << "FAIL: circleVectorizerMergesHorizontalRow\n";
    if (!c11) std::cerr << "FAIL: circleVectorizerCoversSolidSquare\n";
    if (!c12) std::cerr << "FAIL: circleVectorizerControlsSpillWithBlocked\n";
    if (!c13) std::cerr << "FAIL: circleModeConvertsSquareToCircles\n";
    if (!c14) std::cerr << "FAIL: circleModeAnimationStaysInBudget\n";
    if (!c15) std::cerr << "FAIL: circleModeTwoColorCircles\n";
    if (!c16) std::cerr << "FAIL: circleModeNoPerimeterGaps\n";
    if (!c17) std::cerr << "FAIL: circleVectorizerHandlesDisconnectedComponents\n";
    if (!c18) std::cerr << "FAIL: circleModeCoversDiamond\n";
    if (!c19) std::cerr << "FAIL: circleModeObjectCountIsReasonable\n";
    if (!p01) std::cerr << "FAIL: paintModeDetectsCircle\n";
    if (!p02) std::cerr << "FAIL: paintModeDetectsSmallCircle\n";
    if (!p03) std::cerr << "FAIL: paintModeCurvesUseBlocksNotRotatedSlivers\n";
    if (!p04) std::cerr << "FAIL: paintModeCircleDoesNotBleedOverForeground\n";
    if (!p05) std::cerr << "FAIL: paintModeDetectsEllipse\n";
    if (!p06) std::cerr << "FAIL: paintModeSemicircleCoverage\n";
    if (!p07) std::cerr << "FAIL: paintModeClosesSeamsOnCurvedBoundary\n";
    if (!p08) std::cerr << "FAIL: paintModeCircleBeatsBlocks\n";
    if (!p09) std::cerr << "FAIL: paintModeFitsDiamondAsTiltedBox\n";
    if (!p10) std::cerr << "FAIL: paintModeSineWaveCoverage\n";
    if (!p11) std::cerr << "FAIL: paintModeMulticolorCurvesNoGaps\n";
    if (!p15) std::cerr << "FAIL: paintPrunesBoundaryTeeth\n";
    if (!p16) std::cerr << "FAIL: paintRoundJointCapsElbow\n";
    if (!p17) std::cerr << "FAIL: paintRoundJointLeavesRingClean\n";
    if (!p18) std::cerr << "FAIL: paintRoundJointCoversZigzag\n";
    if (!p14) std::cerr << "FAIL: paintSeparateDiscsStayRound\n";
    if (!p13) std::cerr << "FAIL: paintRepairsKeepLongDiagonal\n";
    if (!p12) std::cerr << "FAIL: paintModeSmoothDiamondOverBackground\n";

    bool const pass =
        c01 && c02 && c03 && c04 && c05 && c06 && c07 && c08 && c09 &&
        c10 && c11 && c12 && c13 && c14 && c15 && c16 && c17 && c18 && c19 &&
        p01 && p02 && p03 && p04 && p05 && p06 && p07 && p08 && p09 &&
        p10 && p11 && p12 && p13 && p14 && p15 && p16 && p17 && p18;
    if (!f01) std::cerr << "FAIL: paintFragmentsJoinShallowSteps\n";
    if (!f02) std::cerr << "FAIL: paintFragmentsJoinSteepSteps\n";
    if (!f03) std::cerr << "FAIL: paintFragmentsKeepCurveContacts\n";
    if (!f04) std::cerr << "FAIL: paintFragmentsKeepIsolatedDetails\n";
    if (!f05) std::cerr << "FAIL: paintFragmentsUseRoundPatches\n";
    if (!f06) std::cerr << "FAIL: paintFragmentsPreserveInteriorAndForeground\n";
    if (!f07) std::cerr << "FAIL: paintRoundFragmentsPreserveCoverage\n";
    if (!f08) std::cerr << "FAIL: paintSmoothJointsRoundShallowBends\n";
    if (!f09) std::cerr << "FAIL: paintRoundFragmentsKeepHiddenEnds\n";
    if (!f10) std::cerr << "FAIL: paintRoundCapsProtectForegroundSamples\n";
    if (!a01) std::cerr << "FAIL: autoPaintKeepsSmallAndFlatSources\n";
    if (!a02) std::cerr << "FAIL: autoPaintRaisesDetailedSources\n";
    if (!a03) std::cerr << "FAIL: autoPaintChecksMiddleFrame\n";
    if (!a04) std::cerr << "FAIL: autoPaintPreservesResolutionDuringSourceScaling\n";
    return pass && f01 && f02 && f03 && f04 && f05 && f06 && f07 && f08 && f09 && f10 &&
        a01 && a02 && a03 && a04 ? 0 : 1;
}
