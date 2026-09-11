// Tests de regresion para el modo pintura y el modo circulos del convertidor de
// imagen a objetos de GD.
//
// Enfoque: El modo circulos dibuja con circulos/elipses exclusivamente. En el
// juego, los circulos viven en una hoja de sprites distinta a la de los cuadrados,
// asi que mezclarlos rompe el orden Z. Las curvas en pintura usan cuadrados
// (bloques o trazos axis-aligned) y no rectangulos girados porque el borde girado
// deja picos de subpixel entre los objetos cuando la curva pasa por muchas celdas.
//
// Cada prueba es una funcion bool que devuelve true si pasa. main() las ejecuta
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

// ---------------------------------------------------------------------------
// Utilidades de construccion de imagenes sinteticas
// ---------------------------------------------------------------------------

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

// Deshace la marca de agua para poder contar objetos y medir geometria limpia.
std::vector<Primitive> unmarked(ImportPlan const& plan) {
    auto objects = plan.staticObjects;
    for (auto& object : objects) {
        object.rotation = std::fmod(object.rotation, 360.f);
    }
    prunePaintObjects(objects, std::max(plan.width, 1), std::max(plan.height, 1));
    return objects;
}

// Mascara de celdas vacias: 1 donde no hay nada pintado, 0 donde si.
std::vector<std::uint8_t> emptyOutside(std::vector<int> const& positions, int cells) {
    std::vector<std::uint8_t> empty(static_cast<std::size_t>(cells), 1);
    for (int position : positions) empty[static_cast<std::size_t>(position)] = 0;
    return empty;
}

// Cuenta cuantos objetos de cada tipo hay.
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

// Verifica si cada celda pintada tiene cobertura subpixel suficiente.
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
        // Huecos interiores: solo en celdas rodeadas del mismo color.
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

// =========================================================================
//  TESTS DE MODO CIRCULOS
// =========================================================================

// -------------------------------------------------------------------------
// 1. El modo circulos solo produce PrimitiveKind::Circle. Nada de cuadrados,
//    trazos ni triangulos, que estarian en otra hoja de sprites y GD los
//    pondria debajo de todos los circulos sin importar la capa.
// -------------------------------------------------------------------------
bool circleModeNeverEmitsSquares() {
    auto source = animation(48, 48, 1, 0, 0, 0, 0);
    // Un circulo grande y un rectangulo: los dos deben acabar hechos de circulos.
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

// -------------------------------------------------------------------------
// 2. El modo circulos cubre todas las celdas sin dejar huecos. Cada celda
//    pintada debe tener al menos un circulo/elipse que la toque.
// -------------------------------------------------------------------------
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

// -------------------------------------------------------------------------
// 3. El modo circulos produce tanto discos gordos (W y H > 4) como husos
//    estirados (W > 2*H) para cubrir zonas macizas y lineas finas.
// -------------------------------------------------------------------------
bool circleModeProducesDiscsAndSpindles() {
    auto source = animation(48, 48, 1, 0, 0, 0, 0);
    // Un disco macizo.
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 48; ++x) {
            float const dx = x + 0.5f - 24.f;
            float const dy = y + 0.5f - 16.f;
            if (dx * dx + dy * dy <= 100.f) setPixel(source, 0, x, y, 90, 200, 240);
        }
    }
    // Una barra horizontal fina.
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

// -------------------------------------------------------------------------
// 4. El modo circulos no deja que un circulo invada celdas de otro color
//    (blocked). Cada circulo solo puede crecer sobre sus propias celdas o
//    las que quedan tapadas por capas superiores.
// -------------------------------------------------------------------------
bool circleModeRespectsColorBoundaries() {
    auto source = animation(40, 40, 1, 0, 0, 0, 0);
    // Dos bloques de distinto color, adyacentes.
    for (int y = 10; y < 30; ++y) {
        for (int x = 4; x < 20; ++x) setPixel(source, 0, x, y, 200, 60, 60);
        for (int x = 20; x < 36; ++x) setPixel(source, 0, x, y, 60, 60, 200);
    }
    auto result = buildPlan(source, circleOptions(40));
    if (!result) {
        std::cout << "circle-boundaries: " << result.error << '\n';
        return false;
    }
    // Verificar que ningun pixel central de una celda bien interior (al menos 2
    // celdas desde la frontera) tenga el color del otro lado. Las celdas justo en
    // la frontera pueden tener un pico de spill legitimo (kSpill = 14%).
    auto const preview = renderPlanFrame(result.plan, 0, 4);
    int const scale = 4;
    int bleeds = 0;
    for (int y = 12; y < 28; ++y) {
        for (int x = 4; x < 36; ++x) {
            // Saltar las 2 columnas junto a la frontera (x=18,19,20,21).
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

// -------------------------------------------------------------------------
// 5. Los circulos en modo circulos usan cuadrados (bloques axis-aligned o
//    circulos sin giro) en las curvas, no rectangulos girados. Es decir,
//    los circulos salen con rotation==0 o girados, pero siempre como
//    PrimitiveKind::Circle. Si son Stroke girados, algo esta mal.
// -------------------------------------------------------------------------
bool circleModeCurvesUseCirclesNotRotatedRects() {
    auto source = animation(48, 48, 1, 0, 0, 0, 0);
    // Un anillo: circulo exterior menos circulo interior.
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

// -------------------------------------------------------------------------
// 6. Un solo pixel debe cubrirse con un circulo minimo, no con un bloque.
// -------------------------------------------------------------------------
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
    // Debe haber al menos 1 circulo y debe cubrir el pixel.
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

// -------------------------------------------------------------------------
// 7. Una linea diagonal fina en modo circulos debe cubrirse enteramente
//    con circulos/elipses estiradas, sin dejar huecos.
// -------------------------------------------------------------------------
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

// -------------------------------------------------------------------------
// 8. En modo circulos, una forma en L (no convexa) debe cubrirse sin que
//    ningun circulo se salga demasiado sobre el fondo.
// -------------------------------------------------------------------------
bool circleModeHandlesLShape() {
    auto source = animation(32, 32, 1, 0, 0, 0, 0);
    // Pata vertical de la L.
    for (int y = 4; y < 28; ++y) {
        for (int x = 4; x < 10; ++x) setPixel(source, 0, x, y, 60, 180, 220);
    }
    // Pata horizontal de la L.
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
    // Cobertura.
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

// -------------------------------------------------------------------------
// 9. En modo circulos con dos colores, el de arriba no puede quedar debajo
//    del de abajo. Dado que todos son circulos del mismo sprite, el Z si
//    manda y el orden de emision importa.
// -------------------------------------------------------------------------
bool circleModePreservesLayerOrder() {
    auto source = animation(32, 32, 1, 0, 0, 0, 0);
    // Color de fondo: verde grande.
    for (int y = 4; y < 28; ++y) {
        for (int x = 4; x < 28; ++x) setPixel(source, 0, x, y, 60, 200, 80);
    }
    // Color de arriba: punto rojo centrado.
    for (int y = 12; y < 20; ++y) {
        for (int x = 12; x < 20; ++x) setPixel(source, 0, x, y, 230, 60, 60);
    }
    auto result = buildPlan(source, circleOptions(32));
    if (!result) {
        std::cout << "circle-layers: " << result.error << '\n';
        return false;
    }
    // El rojo tiene que ser visible en el centro.
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

// -------------------------------------------------------------------------
// 10. vectorizeCircles directamente: una fila horizontal de 20 celdas debe
//     cubrirse con unas pocas elipses estiradas, no con 20 circulos individuales.
// -------------------------------------------------------------------------
bool circleVectorizerMergesHorizontalRow() {
    constexpr int width = 24;
    constexpr int height = 6;
    std::vector<int> positions;
    for (int x = 2; x < 22; ++x) positions.push_back(3 * width + x);
    auto objects = vectorizeCircles(
        positions, width, height, 0, 0, {},
        emptyOutside(positions, width * height));
    // Con 20 celdas en fila, deberian ser pocas elipses (no una por celda).
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

// -------------------------------------------------------------------------
// 11. vectorizeCircles: un cuadrado macizo de 10x10 debe cubrirse sin huecos.
// -------------------------------------------------------------------------
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
    // Verificar cobertura.
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

// -------------------------------------------------------------------------
// 12. Spill en circulos con dos colores: los circulos de un color no deben
//     asomar sobre las celdas de otro color vecino. Se verifica que cuando
//     la region vecina no es hueco (empty=0) ni esta tapada por capas
//     superiores (blocked=0), los circulos respetan el limite y no invaden
//     las celdas interiores del otro color.
// -------------------------------------------------------------------------
bool circleVectorizerControlsSpillWithBlocked() {
    constexpr int width = 24;
    constexpr int height = 24;
    constexpr std::size_t cells = static_cast<std::size_t>(width) * height;
    // Color A: mitad izquierda [2, 12).
    std::vector<int> positionsA;
    for (int y = 4; y < 20; ++y) {
        for (int x = 2; x < 12; ++x) positionsA.push_back(y * width + x);
    }
    // Color B: mitad derecha [12, 22). No es hueco (empty=0) ni capa superior (blocked=0).
    std::vector<std::uint8_t> empty(cells, 1);
    for (int y = 4; y < 20; ++y) {
        for (int x = 2; x < 22; ++x) {
            empty[static_cast<std::size_t>(y * width + x)] = 0;
        }
    }
    std::vector<std::uint8_t> const blocked(cells, 0);

    auto objects = vectorizeCircles(
        positionsA, width, height, 0, 0, blocked, empty);

    // Verificar que ningun circulo cubre el centro de celdas interiores de B (x >= 14).
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

// =========================================================================
//  TESTS DE MODO PINTURA - ENFOQUE EN CURVAS Y CIRCULOS
// =========================================================================

// -------------------------------------------------------------------------
// 13. En modo pintura, un circulo grande debe usar PrimitiveKind::Circle,
//     no una pila de cuadraditos.
// -------------------------------------------------------------------------
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

// -------------------------------------------------------------------------
// 14. En modo pintura, un circulo pequeno (4-6 celdas de diametro) aun
//     debe caber como Circle y no se descompone en bloques.
// -------------------------------------------------------------------------
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

// -------------------------------------------------------------------------
// 15. Las curvas en modo pintura usan cuadrados/trazos (bloques y strokes)
//     y NO rectangulos girados pequenos que dejan picos. Un arco suave
//     debe cubrirse con pocos objetos sin picos subpixel.
// -------------------------------------------------------------------------
bool paintModeCurvesUseBlocksNotRotatedSlivers() {
    auto source = animation(48, 48, 1, 0, 0, 0, 0);
    // Un arco grueso: semicirculo exterior - semicirculo interior.
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
    auto const objects = unmarked(result.plan);
    // Contar picos: objetos girados mas pequenos que 1.6 celdas.
    int spikes = 0;
    for (auto const& object : objects) {
        float const angle = std::fmod(std::abs(object.rotation), 90.f);
        if (angle > 5.f && angle < 85.f &&
            object.width <= 1.6f && object.height <= 1.6f) {
            ++spikes;
        }
    }
    std::cout << "paint-curve-blocks: spikes=" << spikes
              << " objects=" << objects.size()
              << " review=" << result.plan.similarity << "%\n";
    // En un arco grueso de 48 celdas se tolera hasta 2 picos: un par de
    // astillas en el borde exterior no son una regresion visible.
    if (spikes > 2) std::cerr << "FAIL: paint mode left " << spikes << " spikes on a curve\n";
    return spikes <= 2 && result.plan.similarity >= 93.f;
}

// -------------------------------------------------------------------------
// 16. Un circulo en pintura no puede invadir celdas de otro color.
//     GD pinta los circulos en otra hoja de sprites, asi que un circulo
//     que asoma sobre otro color se ve porque el cuadrado de debajo siempre
//     queda abajo.
// -------------------------------------------------------------------------
bool paintModeCircleDoesNotBleedOverForeground() {
    auto source = animation(32, 32, 1, 0, 0, 0, 0);
    // Circulo rojo.
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            float const dx = x + 0.5f - 12.f;
            float const dy = y + 0.5f - 16.f;
            if (dx * dx + dy * dy <= 64.f) setPixel(source, 0, x, y, 230, 60, 60);
        }
    }
    // Banda azul superpuesta.
    for (int y = 12; y < 20; ++y) {
        for (int x = 16; x < 30; ++x) setPixel(source, 0, x, y, 60, 60, 230);
    }
    auto result = buildPlan(source, paintOptions(32));
    if (!result) return false;
    // Verificar que en las celdas azules no se vea rojo.
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

// -------------------------------------------------------------------------
// 17. Una elipse (no circulo perfecto) en pintura debe encajar como Circle
//     si el aspecto es <= 1.8.
// -------------------------------------------------------------------------
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

// -------------------------------------------------------------------------
// 18. En modo pintura un semicirculo debe cubrirse con cobertura >= 93%.
// -------------------------------------------------------------------------
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

// -------------------------------------------------------------------------
// 19. Las costuras entre dos colores en una frontera curva no deben dejar
//     huecos. Esto prueba la reparacion de costuras del modo pintura en
//     bordes curvos.
// -------------------------------------------------------------------------
bool paintModeClosesSeamsOnCurvedBoundary() {
    auto source = animation(40, 40, 1, 238, 231, 218);
    // Frontera curva sinusoidal.
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

// -------------------------------------------------------------------------
// 20. El modo circulos es estrictamente circulos: incluso una imagen que
//     seria perfecta como un bloque (cuadrado macizo) debe convertirse en
//     circulos, no bloques.
// -------------------------------------------------------------------------
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

// -------------------------------------------------------------------------
// 21. Modo pintura con un disco sobre fondo pintado: el disco debe usar
//     Circle y usar menos objetos que el modo bloques.
// -------------------------------------------------------------------------
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

// -------------------------------------------------------------------------
// 22. Modo circulos con animacion: los objetos no deben exceder el presupuesto.
// -------------------------------------------------------------------------
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
    // Verificar que todos los objetos visuales sean circulos.
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

// -------------------------------------------------------------------------
// 23. Dos circulos de distinto color en modo circulos: cada uno mantiene
//     su color, y los dos estan cubiertos.
// -------------------------------------------------------------------------
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
    // Cobertura.
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

// -------------------------------------------------------------------------
// 24. Una onda sinusoidal gruesa en modo pintura: la cobertura y la fidelidad
//     deben ser buenas, y no debe haber picos diminutos.
// -------------------------------------------------------------------------
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
        float const angle = std::fmod(std::abs(object.rotation), 90.f);
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

// -------------------------------------------------------------------------
// 25. En modo circulos, el contorno de un circulo grande no debe tener
//     huecos visibles entre elipses: al nivel de 1 muestra/celda, cada
//     celda debe estar cubierta.
// -------------------------------------------------------------------------
bool circleModeNoPerimeterGaps() {
    auto source = animation(48, 48, 1, 0, 0, 0, 0);
    // Solo el anillo perimetral (sin interior macizo).
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

// -------------------------------------------------------------------------
// 26. vectorizeCircles: componentes desconectados deben procesarse todos.
//     Dos manchas separadas, las dos cubiertas.
// -------------------------------------------------------------------------
bool circleVectorizerHandlesDisconnectedComponents() {
    constexpr int width = 20;
    constexpr int height = 10;
    std::vector<int> positions;
    // Mancha 1: izquierda.
    for (int y = 2; y < 5; ++y) {
        for (int x = 2; x < 6; ++x) positions.push_back(y * width + x);
    }
    // Mancha 2: derecha.
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

// -------------------------------------------------------------------------
// 27. En modo pintura, un rombo (cuadrado girado 45 grados) debe encajar
//     como una pieza girada, no como 50 bloques axis-aligned. El modo
//     pintura debe detectar el angulo principal por la envolvente convexa.
// -------------------------------------------------------------------------
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
    // Con un buen ajuste, no deberian ser mas de 4 objetos.
    return tilted && objects.size() <= 6;
}

// -------------------------------------------------------------------------
// 28. En modo circulos un rombo tambien debe quedar cubierto solo con
//     circulos, nunca con bloques.
// -------------------------------------------------------------------------
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

// -------------------------------------------------------------------------
// 29. Modo pintura con multiples colores en curvas: la frontera entre los
//     colores no debe dejar huecos ni sangrado cruzado.
// -------------------------------------------------------------------------
bool paintModeMulticolorCurvesNoGaps() {
    auto source = animation(48, 48, 1, 0, 0, 0, 0);
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 48; ++x) {
            float const dx = x + 0.5f - 24.f;
            float const dy = y + 0.5f - 24.f;
            float const dist = std::sqrt(dx * dx + dy * dy);
            if (dist <= 20.f) {
                // Sector por angulo.
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
    auto const& cells = result.plan.frames.front().cells;
    for (std::size_t position = 0; position < cells.size(); ++position) {
        if (cells[position] < 0) continue;
        int const cx = static_cast<int>(position % result.plan.width);
        int const cy = static_cast<int>(position / result.plan.width);
        // Revisar el centro de la celda.
        std::size_t const pixel =
            (static_cast<std::size_t>(cy * scale + scale / 2) * result.plan.width * scale +
             cx * scale + scale / 2) * 4;
        if (preview[pixel + 3] == 0) ++holes;
    }
    std::cout << "paint-multicolor-curves: holes=" << holes
              << " objects=" << result.plan.visualObjects
              << " review=" << result.plan.similarity << "%\n";
    // En un punto donde tres colores se encuentran es normal tener 2-3
    // celdas sin cubrir en el centro exacto de la celda.
    if (holes > 4) std::cerr << "FAIL: multicolor curves left " << holes << " center-pixel holes\n";
    return holes <= 4 && result.plan.similarity >= 90.f;
}

// -------------------------------------------------------------------------
// 30. Modo circulos: el tamaño total de objetos debe ser razonable. Un
//     circulo simple de ~14 celdas de diametro no necesita cientos de
//     ellipses diminutas.
// -------------------------------------------------------------------------
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
    // Un circulo de ~14px diametro tiene ~150 celdas; deberian ser muchas menos
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

} // namespace

int main() {
    // Circulos
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

    // Pintura + circulos
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

    bool const pass =
        c01 && c02 && c03 && c04 && c05 && c06 && c07 && c08 && c09 &&
        c10 && c11 && c12 && c13 && c14 && c15 && c16 && c17 && c18 && c19 &&
        p01 && p02 && p03 && p04 && p05 && p06 && p07 && p08 && p09 &&
        p10 && p11;
    return pass ? 0 : 1;
}
