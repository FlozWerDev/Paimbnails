// Exercises the production converter; no running editor or private image needed.
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

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
#include "../src/features/gif-import/services/GifSurfaceVectorizer.cpp"
#include "../src/features/gif-import/services/GifSurfaceOptimizer.cpp"
#include "../src/features/gif-import/services/GifPlanOptimizer.cpp"
#include "../src/features/gif-import/services/ImageWatermark.cpp"
#include "../src/features/gif-import/services/GifSmoothVectorizer.cpp"
#include "../src/features/gif-import/services/GifImportPipeline.cpp"
#include "../src/features/gif-import/services/GifPrimitiveSerializer.hpp"

using namespace paimon::gifimport;

namespace {

bool preservesSmallFlatColors() {
    SourceAnimation source;
    source.width = source.height = 96;
    source.frames.resize(1);
    auto& rgba = source.frames[0].rgba;
    rgba.resize(96 * 96 * 4);
    // Large skin/hair regions with smaller blush and highlights.
    std::array<Color, 6> const colors{{{37, 37, 39}, {246, 231, 208},
        {238, 209, 174}, {248, 199, 194}, {104, 130, 155}, {255, 248, 225}}};
    for (int y = 0; y < 96; ++y) for (int x = 0; x < 96; ++x) {
        int color = x < 20 ? 0 : y < 20 ? 1 : y > 75 ? 2 : 5;
        if (x >= 32 && x < 44 && y >= 54 && y < 61) color = 3;
        if (x >= 65 && x < 74 && y >= 38 && y < 55) color = 4;
        auto const pixel = static_cast<std::size_t>(y * 96 + x) * 4;
        rgba[pixel] = colors[color].r;
        rgba[pixel + 1] = colors[color].g;
        rgba[pixel + 2] = colors[color].b;
        rgba[pixel + 3] = 255;
    }
    Options options;
    options.mode = ImportMode::Paint;
    options.background = BackgroundMode::Keep;
    options.maxDimension = 96;
    options.maxColors = 12;
    auto const result = buildPlan(source, options);
    if (!result) return false;
    auto const image = renderPlanFrame(result.plan, 0, 1);
    auto const blush = static_cast<std::size_t>(57 * 96 + 38) * 4;
    return image[blush] > image[blush + 1] + 25 &&
        image[blush + 2] > image[blush + 1] - 15;
}

bool preservesUnseededAccent() {
    auto histogram = emptyHistogram();
    addSample(histogram, {37, 37, 39}, 20000);
    addSample(histogram, {246, 231, 208}, 18000);
    addSample(histogram, {238, 209, 174}, 9000);
    addSample(histogram, {248, 199, 194}, 400);
    auto const palette = refinePalette(histogram,
        {{40, 40, 40}, {245, 230, 210}, {240, 225, 205}, {235, 215, 180}});
    auto const pink = rgbToOkLab({248, 199, 194});
    return std::any_of(palette.begin(), palette.end(), [&](Color color) {
        return oklabDistance(rgbToOkLab(color), pink) < 0.01f;
    });
}

bool preservesModerateAreaDetail() {
    SourceAnimation source;
    source.width = source.height = 640;
    source.frames.resize(1);
    auto& rgba = source.frames[0].rgba;
    rgba.assign(640 * 640 * 4, 255);
    for (int y = 100; y < 164; ++y) for (int x = 100; x < 164; ++x) {
        auto const pixel = static_cast<std::size_t>(y * 640 + x) * 4;
        for (int channel = 0; channel < 3; ++channel) rgba[pixel + channel] = (x + y) % 2 ? 0 : 255;
    }
    return autoPaintDimension(source) == 640;
}

bool pruningKeepsVisibleSubpixelStroke() {
    std::vector<Primitive> objects{{8.f, 8.f, 16.f, 16.f, 0.f, 0},
                                  {8.f, 2.1f, 12.f, 0.18f, 0.f, 1, PrimitiveKind::Stroke, 1}};
    prunePaintObjects(objects, 16, 16);
    return std::any_of(objects.begin(), objects.end(), [](auto const& object) { return object.color == 1; });
}

bool adjacentOpaqueShapesDoNotLeaveAntialiasSeams() {
    ImportPlan plan;
    plan.width = 8; plan.height = 4; plan.mode = ImportMode::Paint;
    plan.palette = {{255, 255, 255}};
    plan.staticObjects = {{1.7f, 2.f, 3.4f, 4.f}, {5.7f, 2.f, 4.6f, 4.f}};
    auto const image = renderPlanFrame(plan, 0, 1, true);
    return std::all_of(image.begin(), image.end(), [](auto value) { return value == 255; });
}

bool watermarkPreservesAffineCoverage() {
    Primitive original{10.123f, 20.237f, 4.f, 20.f, 15.f, 0, PrimitiveKind::Stroke};
    original.skew = 25.f;
    auto const parts = splitPrimitive(original, 0);
    auto const before = xformOf(original), first = xformOf(parts[0]), second = xformOf(parts[1]);
    for (int y = 8; y < 32; ++y) for (int x = 0; x < 20; ++x)
        for (float offset : {0.25f, 0.75f}) {
            bool const expected = before.contains(x + offset, y + offset);
            bool const actual = first.contains(x + offset, y + offset) || second.contains(x + offset, y + offset);
            if (actual != expected) return false;
        }
    return true;
}

bool emptyPaletteEntryProducesNoObjects() {
    return vectorizePaint({}, 16, 16, 0, 0, {}, {}, false).empty();
}

bool layeredSlantedSurfaceUsesFewObjects() {
    SourceAnimation source;
    source.width = source.height = 96;
    source.frames.resize(1);
    auto& pixels = source.frames.front().rgba;
    pixels.resize(96 * 96 * 4, 255);
    for (int y = 0; y < 96; ++y) for (int x = 0; x < 96; ++x) {
        Color color{24, 30, 45};
        if (y >= 10 && y < 80 && x >= 4 + y / 3 && x < 57 + y / 3)
            color = {235, 190, 130};
        if (x >= 40 && x < 50 && y >= 35 && y < 55) color = {240, 80, 120};
        auto const pixel = static_cast<std::size_t>(y * 96 + x) * 4;
        pixels[pixel] = color.r; pixels[pixel + 1] = color.g; pixels[pixel + 2] = color.b;
    }
    Options options;
    options.mode = ImportMode::Paint;
    options.background = BackgroundMode::Keep;
    options.maxDimension = 96;
    options.maxColors = 3;
    auto const result = buildPlan(source, options);
    if (!result) return false;
    auto const image = renderPlanFrame(result.plan, 0, 1);
    int mismatches = 0;
    for (std::size_t pixel = 0; pixel < pixels.size(); pixel += 4)
        mismatches += std::abs(image[pixel] - pixels[pixel]) > 15 ||
            std::abs(image[pixel + 1] - pixels[pixel + 1]) > 15;
    std::cout << "layered-surface: objects=" << result.plan.totalObjects
              << " mismatches=" << mismatches << '\n';
    // Three drawing primitives, their watermark splits and one opaque base.
    return result.plan.totalObjects <= 7 && mismatches == 0;
}

bool roundedShapesKeepLowCostAndCoverage() {
    constexpr int width = 160, height = 120;
    float const angle = 27.f * std::acos(-1.f) / 180.f;
    std::vector<Primitive> reference{{50.f, 36.f, 42.f, 18.f, 27.f, 0, PrimitiveKind::Stroke},
        {50.f - 21.f * std::cos(angle), 36.f - 21.f * std::sin(angle), 18.f, 18.f, 0.f, 0, PrimitiveKind::Circle},
        {50.f + 21.f * std::cos(angle), 36.f + 21.f * std::sin(angle), 18.f, 18.f, 0.f, 0, PrimitiveKind::Circle},
        {108.f, 84.f, 34.f, 14.f, 13.f, 0, PrimitiveKind::Circle}};
    auto const forms = xformsOf(reference);
    std::vector<int> positions;
    std::vector<std::uint8_t> own(width * height, 0);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
        if (std::any_of(forms.begin(), forms.end(), [&](auto const& shape) { return shape.contains(x + 0.5f, y + 0.5f); })) {
            positions.push_back(y * width + x);
            own[y * width + x] = 1;
        }
    auto const output = vectorizePaintSurface(positions, width, height, 0, 0, {}, 0.65f);
    auto const fitted = xformsOf(output);
    int mismatches = 0;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        bool const covered = std::any_of(fitted.begin(), fitted.end(), [&](auto const& shape) {
            return shape.contains(x + 0.5f, y + 0.5f);
        });
        mismatches += covered != (own[y * width + x] != 0);
    }
    std::cout << "rounded-surfaces: objects=" << output.size() << " mismatches=" << mismatches << '\n';
    return output.size() <= 4 && mismatches <= static_cast<int>(positions.size() / 100);
}

bool steepSurfaceCapsDoNotExtrapolateSpikes() {
    constexpr int width = 80, height = 32;
    std::vector<int> positions;
    for (int y = 10; y < 12; ++y) for (int x = 10 + (y - 10) * 10; x < 60; ++x)
        positions.push_back(y * width + x);
    float worst = 0.f;
    bool complete = true;
    std::size_t maximumObjects = 0;
    for (float tolerance : {0.48f, 0.8f, 1.2f, 2.f}) {
        auto const shapes = vectorizePaintSurface(positions, width, height, 0, 0, {}, tolerance);
        if (shapes.empty()) return false;
        maximumObjects = std::max(maximumObjects, shapes.size());
        auto const forms = xformsOf(shapes);
        for (int cell : positions)
            complete &= std::any_of(forms.begin(), forms.end(), [&](auto const& shape) {
                return shape.contains(cell % width + 0.5f, cell / width + 0.5f);
            });
        for (auto const& shape : forms) forEachSample(shape, width, height, 8, [&](int sx, int sy) {
            float const x = (sx + 0.5f) / 8, y = (sy + 0.5f) / 8;
            float nearest = 1e9f;
            for (int cell : positions) {
                float const dx = std::max({cell % width - x, 0.f, x - cell % width - 1});
                float const dy = std::max({cell / width - y, 0.f, y - cell / width - 1});
                nearest = std::min(nearest, std::hypot(dx, dy));
            }
            worst = std::max(worst, nearest);
            return false;
        });
    }
    std::cout << "steep-caps: maximum-spill=" << worst << " objects=" << maximumObjects << '\n';
    return complete && worst <= 0.65f && maximumObjects <= 4;
}

bool continuousSurfaceHasNoStepsBetweenBands() {
    constexpr int width = 96, height = 128;
    std::vector<int> positions;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        float const left = 24.f + 12.f * std::sin((y + 0.5f) * 0.075f);
        if (x + 0.5f >= left && x + 0.5f < left + 28.f)
            positions.push_back(y * width + x);
    }
    std::vector<std::uint8_t> mask(width * height, 0);
    for (int cell : positions) mask[cell] = 1;
    auto const objects = surface::fitSurface(mask, mask,
        width, height, 0, 0, 0.8f, SurfaceFit::Continuous);
    if (objects.empty()) return false;
    auto const forms = xformsOf(objects);
    auto leftAt = [&](float y) {
        for (int sample = 0; sample < width * 32; ++sample) {
            float const x = (sample + 0.5f) / 32;
            if (std::any_of(forms.begin(), forms.end(), [&](auto const& form) {
                return form.contains(x, y);
            })) return x;
        }
        return static_cast<float>(width);
    };
    float step = 0.f, error = 0.f;
    for (int row = 2; row + 2 < height; ++row) {
        step = std::max(step, std::abs(leftAt(row - 0.001f) - leftAt(row + 0.001f)));
        error = std::max(error, std::abs(leftAt(row + 0.5f) -
            (24.f + 12.f * std::sin((row + 0.5f) * 0.075f))));
    }
    std::cout << "continuous-bands: objects=" << objects.size()
              << " step=" << step << " edge-error=" << error << '\n';
    return step <= 0.125f && error < 1.f && objects.size() < 96;
}

bool sharpCornerDoesNotDiscardOtherContinuousEdges() {
    constexpr int width = 96, height = 128;
    std::vector<int> positions;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        float const left = 24.f + 12.f * std::sin((y + 0.5f) * 0.075f) +
            (y >= 64 && y < 80 ? 8.f : 0.f);
        if (x + 0.5f >= left && x + 0.5f < left + 28.f)
            positions.push_back(y * width + x);
    }
    std::vector<std::uint8_t> mask(width * height, 0);
    for (int cell : positions) mask[cell] = 1;
    auto const objects = surface::fitSurface(mask, mask, width, height, 0, 0,
        0.8f, SurfaceFit::Continuous);
    std::cout << "mixed-continuous-edges: objects=" << objects.size() << '\n';
    if (objects.empty()) return false;
    auto const forms = xformsOf(objects);
    for (int y : {16, 32, 48, 68, 72, 88, 104}) {
        float const left = 24.f + 12.f * std::sin((y + 0.5f) * 0.075f) +
            (y >= 64 && y < 80 ? 8.f : 0.f);
        if (std::none_of(forms.begin(), forms.end(), [&](auto const& form) {
            return form.contains(left + 14.f, y + 0.5f);
        })) return false;
    }
    return true;
}

bool continuousSurfaceRetainsSteepShortStrips() {
    constexpr int width = 80, height = 32;
    std::vector<std::uint8_t> mask(width * height, 0);
    for (int y = 10; y < 12; ++y) for (int x = 10 + (y - 10) * 10; x < 60; ++x)
        mask[y * width + x] = 1;
    auto const objects = surface::fitSurface(mask, mask, width, height, 0, 0,
        0.8f, SurfaceFit::Continuous);
    std::cout << "continuous-steep-strip: objects=" << objects.size() << '\n';
    return !objects.empty();
}

bool inclinedContinuousCurvesAvoidExcessiveSplits() {
    constexpr int width = 256, height = 64;
    std::vector<std::uint8_t> mask(width * height, 0);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        float const left = 20.f + 3.f * (y + 0.5f) + 4.f * std::sin((y + 0.5f) * 0.14f);
        if (x + 0.5f >= left && x + 0.5f < left + 24.f) mask[y * width + x] = 1;
    }
    auto const objects = surface::fitSurface(mask, mask, width, height, 0, 0,
        0.8f, SurfaceFit::Continuous);
    if (objects.empty()) return false;
    auto const forms = xformsOf(objects);
    int missing = 0;
    for (int y = 2; y + 2 < height; ++y) {
        float const left = 20.f + 3.f * (y + 0.5f) + 4.f * std::sin((y + 0.5f) * 0.14f);
        missing += std::none_of(forms.begin(), forms.end(), [&](auto const& form) {
            return form.contains(left + 12.f, y + 0.5f);
        });
    }
    std::cout << "inclined-continuous-curve: objects=" << objects.size()
              << " missing=" << missing << '\n';
    return missing == 0 && objects.size() <= 10;
}

bool digitalCornersStayOnOneStroke() {
    Skeleton skeleton;
    skeleton.width = skeleton.height = 6;
    skeleton.cells.assign(36, 0);
    for (int cell : {7, 8, 14, 21, 27}) skeleton.cells[cell] = 1;
    auto const paths = skeletonPaths(skeleton, true);
    return paths.size() == 1 && paths.front().size() == 5;
}

bool diagonalJunctionsKeepAllBranches() {
    Skeleton skeleton;
    skeleton.width = skeleton.height = 5;
    skeleton.cells.assign(25, 0);
    for (int cell : {6, 8, 12, 16, 18}) skeleton.cells[cell] = 1;
    auto const paths = skeletonPaths(skeleton, true);
    return paths.size() == 4 && std::all_of(paths.begin(), paths.end(), [](auto const& path) {
        return path.size() == 2 && (path.front() == 12 || path.back() == 12);
    });
}

bool softStrokeWidthIncludesItsAntialiasFringe() {
    constexpr int dimension = 80;
    std::vector<int> positions;
    std::vector<float> coverage(dimension * dimension, 0.f);
    for (int y = 16; y < 24; ++y) for (int x = 10; x < 70; ++x) {
        int const cell = y * dimension + x;
        if (y >= 17 && y < 23) positions.push_back(cell);
        coverage[cell] = y == 16 || y == 23 ? 0.5f : 1.f;
    }
    auto const objects = vectorizeRoundedPaintStrokes(positions, dimension, dimension, 0, 0, {}, coverage);
    int bodies = 0;
    for (auto const& object : objects) if (object.kind == PrimitiveKind::Stroke) {
        ++bodies;
        if (std::abs(object.height - 7.f) > 0.2f) return false;
    }
    return bodies > 0;
}

bool curvedLinesUseRotatedStrokesAndRoundJoins() {
    constexpr int dimension = 96;
    SourceAnimation source;
    source.width = source.height = dimension;
    source.frames.resize(1);
    auto& rgba = source.frames.front().rgba;
    rgba.assign(dimension * dimension * 4, 255);
    for (int y = 0; y < dimension; ++y) for (int x = 0; x < dimension; ++x) {
        int inside = 0;
        for (int sy = 0; sy < 4; ++sy) for (int sx = 0; sx < 4; ++sx) {
            float const dx = x + (sx + 0.5f) / 4 - 48.f;
            float const dy = y + (sy + 0.5f) / 4 - 48.f;
            float const angle = std::atan2(dy, dx);
            float const distance = angle > -2.8f && angle < 1.2f ? std::abs(std::hypot(dx, dy) - 29.f)
                : std::min(std::hypot(dx - 29.f * std::cos(-2.8f), dy - 29.f * std::sin(-2.8f)),
                           std::hypot(dx - 29.f * std::cos(1.2f), dy - 29.f * std::sin(1.2f)));
            inside += distance < 3.5f;
        }
        auto const value = static_cast<std::uint8_t>(255 - inside * 235 / 16);
        auto const pixel = static_cast<std::size_t>(y * dimension + x) * 4;
        rgba[pixel] = rgba[pixel + 1] = rgba[pixel + 2] = value;
    }
    Options options;
    options.mode = ImportMode::Paint;
    options.sampling = SamplingMode::Smooth;
    options.background = BackgroundMode::Keep;
    options.maxDimension = dimension;
    options.maxColors = 2;
    auto const result = buildPlan(source, options);
    if (!result) return false;
    auto const& objects = result.plan.staticObjects;
    auto const circles = std::count_if(objects.begin(), objects.end(), [](auto const& shape) {
        return shape.kind == PrimitiveKind::Circle;
    });
    auto const strokes = std::count_if(objects.begin(), objects.end(), [](auto const& shape) {
        return shape.kind == PrimitiveKind::Stroke && std::abs(shape.skew) < 0.001f &&
            std::abs(std::sin(shape.rotation * std::acos(-1.f) / 90.f)) > 0.1f;
    });
    std::cout << "native-rounded-lines: objects=" << objects.size() << " rotated=" << strokes
              << " circles=" << circles << '\n';
    return circles >= 3 && strokes >= 3 && objects.size() <= 80;
}

bool irregularFragmentsDoNotPreventRoundRegions() {
    constexpr int width = 160, height = 120;
    float const angle = 27.f * std::acos(-1.f) / 180.f;
    std::vector<Primitive> reference{{50.f, 36.f, 42.f, 18.f, 27.f, 0, PrimitiveKind::Stroke},
        {50.f - 21.f * std::cos(angle), 36.f - 21.f * std::sin(angle), 18.f, 18.f, 0.f, 0, PrimitiveKind::Circle},
        {50.f + 21.f * std::cos(angle), 36.f + 21.f * std::sin(angle), 18.f, 18.f, 0.f, 0, PrimitiveKind::Circle}};
    auto const forms = xformsOf(reference);
    std::vector<int> positions;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        bool const eye = std::any_of(forms.begin(), forms.end(), [&](auto const& form) { return form.contains(x + 0.5f, y + 0.5f); });
        bool const fragment = x >= 95 && x < 130 && y >= 75 && y < 80 + (x - 95) / 2;
        if (eye || fragment) positions.push_back(y * width + x);
    }
    auto const existing = packBlocks(positions, width, height, 0);
    auto const result = roundPaintRegions(existing, positions, width, height, 0, 0, {});
    auto const fitted = xformsOf(result);
    int missing = 0;
    for (int cell : positions) missing += std::none_of(fitted.begin(), fitted.end(), [&](auto const& form) {
        return form.contains(cell % width + 0.5f, cell / width + 0.5f);
    });
    auto const circles = std::count_if(result.begin(), result.end(), [](auto const& shape) { return shape.kind == PrimitiveKind::Circle; });
    std::cout << "partial-rounds: before=" << existing.size() << " after=" << result.size()
              << " circles=" << circles << " missing=" << missing << '\n';
    return circles >= 2 && result.size() < existing.size() && missing <= static_cast<int>(positions.size() / 100);
}

bool surfaceOptimizationKeepsOpaqueRegionInteriors() {
    SourceAnimation source;
    source.width = source.height = 128;
    source.frames.resize(1);
    auto& rgba = source.frames.front().rgba;
    rgba.resize(128 * 128 * 4, 255);
    for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x) {
        Color ink{35, 35, 39};
        int const left = 25 + static_cast<int>(18.f * std::sin(y * 0.073f));
        int const right = 89 + static_cast<int>(17.f * std::cos(y * 0.092f));
        if (y >= 8 && y < 120 && x >= left && x < right) ink = {237, 209, 182};
        if (x >= 48 + y / 5 && x < 55 + y / 5 && y >= 20 && y < 105) ink = {98, 86, 98};
        auto const pixel = static_cast<std::size_t>(y * 128 + x) * 4;
        rgba[pixel] = ink.r; rgba[pixel + 1] = ink.g; rgba[pixel + 2] = ink.b;
    }
    Options options;
    options.mode = ImportMode::Paint; options.sampling = SamplingMode::Smooth;
    options.background = BackgroundMode::Keep; options.maxDimension = 128; options.maxColors = 3;
    auto const result = buildPlan(source, options);
    if (!result) return false;
    auto const& plan = result.plan;
    auto const& labels = plan.frames.front().cells;
    auto const preview = renderPlanFrame(plan, 0, 4);
    int defects = 0;
    for (int y = 2; y + 2 < plan.height; ++y) for (int x = 2; x + 2 < plan.width; ++x) {
        int const color = labels[y * plan.width + x];
        if (color < 0) continue;
        bool interior = true;
        for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx)
            interior &= labels[(y + dy) * plan.width + x + dx] == color;
        if (!interior) continue;
        auto const ink = plan.palette[color];
        for (int sy : {1, 2}) for (int sx : {1, 2}) {
            auto const pixel = (static_cast<std::size_t>(y * 4 + sy) * plan.width * 4 + x * 4 + sx) * 4;
            defects += std::max({std::abs(preview[pixel] - ink.r), std::abs(preview[pixel + 1] - ink.g),
                std::abs(preview[pixel + 2] - ink.b)}) > 8;
        }
    }
    std::cout << "opaque-region-interiors: defects=" << defects << " objects=" << plan.totalObjects << '\n';
    return defects == 0;
}

bool canvasPaddingDoesNotRelaxDetailQuality() {
    auto optimizeDetail = [](int dimension) {
        std::vector<Color> palette{{245, 235, 225}, {40, 20, 30}};
        std::vector<int> ranks{0, 1};
        std::vector<std::vector<int>> positions(2);
        std::vector<std::vector<std::uint8_t>> later(2, std::vector<std::uint8_t>(dimension * dimension, 0));
        std::vector<std::uint8_t> rgba(dimension * dimension * 4, 255);
        for (int y = 0; y < dimension; ++y) for (int x = 0; x < dimension; ++x) {
            float const left = 18.f + 7.f * std::sin((y + 0.5f) * 0.18f);
            int const color = y >= 8 && y < 56 && x + 0.5f >= left && x + 0.5f < left + 9.f;
            int const cell = y * dimension + x;
            positions[color].push_back(cell);
            later[0][cell] = color == 1;
            rgba[cell * 4] = palette[color].r;
            rgba[cell * 4 + 1] = palette[color].g;
            rgba[cell * 4 + 2] = palette[color].b;
        }
        std::vector<Primitive> objects{{dimension * 0.5f, dimension * 0.5f,
            static_cast<float>(dimension), static_cast<float>(dimension), 0.f, 0}};
        auto detail = packBlocks(positions[1], dimension, dimension, 1);
        for (auto& shape : detail) shape.layer = kPaintSublayers;
        objects.insert(objects.end(), detail.begin(), detail.end());
        optimizePaintSurfaces(objects, positions, {dimension, dimension, palette, ranks, later, rgba});
        std::vector<std::uint8_t> coverage(64 * 64 * 16, 0);
        for (auto const& shape : objects) if (shape.color == 1)
            forEachSample(xformOf(shape), 64, 64, 4, [&](int x, int y) {
                coverage[y * 64 * 4 + x] = 1; return false;
            });
        return coverage;
    };
    auto const small = optimizeDetail(64), large = optimizeDetail(192);
    auto const differences = std::inner_product(small.begin(), small.end(), large.begin(), 0,
        std::plus<>(), std::not_equal_to<>());
    std::cout << "canvas-padding: changed-samples=" << differences << '\n';
    return differences == 0;
}

bool opaqueGapsAreRepairedBeforeCountingSavings() {
    constexpr int width = 64, height = 64;
    std::vector<Color> palette{{35, 35, 39}, {237, 209, 182}};
    std::vector<int> ranks{0, 1};
    std::vector<std::vector<std::uint8_t>> later(2, std::vector<std::uint8_t>(width * height, 0));
    std::vector<std::vector<int>> positions(2);
    std::vector<std::uint8_t> rgba(width * height * 4, 255);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        auto const cell = y * width + x;
        int const color = x >= 16 && x < 48 && y >= 16 && y < 48 ? 1 : 0;
        positions[color].push_back(cell); later[0][cell] = color == 1;
        rgba[cell * 4] = palette[color].r; rgba[cell * 4 + 1] = palette[color].g; rgba[cell * 4 + 2] = palette[color].b;
    }
    std::vector<Primitive> shapes{{32.f, 32.f, 64.f, 64.f},
        {23.f, 32.f, 14.f, 32.f, 0.f, 1, PrimitiveKind::Block, 3},
        {41.f, 32.f, 14.f, 32.f, 0.f, 1, PrimitiveKind::Block, 3}};
    SurfaceImage const image{width, height, palette, ranks, later, rgba};
    optimizePaintSurfaces(shapes, positions, image);
    int holes = 0;
    for (int y = 17; y < 47; ++y) for (int x = 17; x < 47; ++x)
        holes += std::none_of(shapes.begin(), shapes.end(), [&](auto const& shape) {
            return shape.color == 1 && xformOf(shape).contains(x + 0.5f, y + 0.5f);
        });
    std::cout << "opaque-gap-repair: holes=" << holes << " objects=" << shapes.size() << '\n';
    return holes == 0 && shapes.size() <= 3;
}

bool maximalRunsReducePiecesOnSmoothContours() {
    constexpr int width = 128, height = 256;
    std::vector<int> positions;
    for (int y = 8; y < 248; ++y) {
        int const left = static_cast<int>(25.f + y * 0.15f + 12.f * std::sin(y * 0.05f));
        int const right = left + 30 + static_cast<int>(4.f * std::sin(y * 0.023f));
        for (int x = left; x < right; ++x) positions.push_back(y * width + x);
    }
    auto const adaptive = vectorizePaintSurface(positions, width, height, 0, 0, {}, 0.48f);
    auto const maximal = vectorizePaintSurface(positions, width, height, 0, 0, {}, 0.48f, SurfaceFit::MaximalRuns);
    auto const forms = xformsOf(maximal);
    int missing = 0;
    for (int cell : positions) missing += std::none_of(forms.begin(), forms.end(), [&](auto const& form) {
        return form.contains(cell % width + 0.5f, cell / width + 0.5f);
    });
    std::cout << "maximal-run-efficiency: adaptive=" << adaptive.size() << " maximal=" << maximal.size()
              << " missing=" << missing << '\n';
    return !maximal.empty() && maximal.size() < adaptive.size() && missing == 0;
}

bool hiddenWidthsUseOneAffineBlock() {
    constexpr int width = 128, height = 128;
    std::vector<int> positions;
    std::vector<std::uint8_t> later(width * height, 0);
    for (int y = 10; y < 110; ++y) {
        int const left = 10 + y / 5, right = left + 20;
        for (int x = left; x < right; ++x) positions.push_back(y * width + x);
        for (int x = right; x < width; ++x) later[y * width + x] = 1;
    }
    auto const shapes = vectorizePaintSurface(positions, width, height, 0, 0, later, 0.48f, SurfaceFit::MaximalRuns);
    auto const forms = xformsOf(shapes);
    int missing = 0;
    for (int cell : positions) missing += std::none_of(forms.begin(), forms.end(), [&](auto const& form) {
        return form.contains(cell % width + 0.5f, cell / width + 0.5f);
    });
    std::cout << "hidden-parallel-boundaries: objects=" << shapes.size() << " missing=" << missing << '\n';
    return shapes.size() == 1 && shapes.front().kind == PrimitiveKind::Block && missing == 0;
}

bool transparentBackgroundDoesNotProtectStrayObjects() {
    constexpr int width = 64, height = 64;
    std::vector<Color> palette{{240, 220, 190}};
    std::vector<int> ranks{0};
    std::vector<std::vector<std::uint8_t>> later(1, std::vector<std::uint8_t>(width * height, 0));
    std::vector<std::vector<int>> positions(1);
    std::vector<std::uint8_t> rgba(width * height * 4, 0);
    for (int y = 16; y < 48; ++y) for (int x = 16; x < 48; ++x) {
        int const cell = y * width + x;
        positions[0].push_back(cell);
        rgba[cell * 4] = 240; rgba[cell * 4 + 1] = 220; rgba[cell * 4 + 2] = 190; rgba[cell * 4 + 3] = 255;
    }
    std::vector<Primitive> shapes{{32.f, 32.f, 32.f, 32.f}, {8.f, 8.f, 4.f, 4.f}, {8.f, 56.f, 4.f, 4.f}};
    SurfaceImage const image{width, height, palette, ranks, later, rgba};
    optimizePaintSurfaces(shapes, positions, image);
    auto const forms = xformsOf(shapes);
    bool const stray = std::any_of(forms.begin(), forms.end(), [](auto const& form) {
        return form.contains(8.f, 8.f) || form.contains(8.f, 56.f);
    });
    bool const covered = std::any_of(forms.begin(), forms.end(), [](auto const& form) { return form.contains(32.f, 32.f); });
    std::cout << "transparent-strays: objects=" << shapes.size() << " stray=" << stray << '\n';
    return shapes.size() == 1 && !stray && covered;
}

bool laterColorsDoNotInvadeEarlierInteriors() {
    constexpr int width = 64, height = 64;
    std::vector<Color> palette{{250, 240, 228}, {249, 231, 209}};
    std::vector<int> ranks{0, 1};
    std::vector<std::vector<std::uint8_t>> later(2, std::vector<std::uint8_t>(width * height, 0));
    std::vector<std::vector<int>> positions(2);
    std::vector<std::uint8_t> rgba(width * height * 4, 255);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        int const color = x >= 32 ? 1 : 0, cell = y * width + x;
        positions[color].push_back(cell); later[0][cell] = color == 1;
        rgba[cell * 4] = palette[color].r; rgba[cell * 4 + 1] = palette[color].g; rgba[cell * 4 + 2] = palette[color].b;
    }
    std::vector<Primitive> shapes{{32.f, 32.f, 64.f, 64.f},
        {44.f, 32.f, 40.f, 64.f, 0.f, 1, PrimitiveKind::Block, 3}};
    SurfaceImage const image{width, height, palette, ranks, later, rgba};
    optimizePaintSurfaces(shapes, positions, image);
    int owner = -1;
    for (auto const& shape : shapes) if (xformOf(shape).contains(28.5f, 32.5f)) owner = shape.color;
    std::cout << "foreign-interior-protection: owner=" << owner << " objects=" << shapes.size() << '\n';
    return owner == 0 && shapes.size() <= 2;
}

bool nativeCircleFringeIsNotSolidInk() {
    Primitive circle{20.f, 20.f, 20.f, 12.f, 27.f, 0, PrimitiveKind::Circle};
    auto const form = xformOf(circle);
    auto atRadius = [&](float radius) {
        float const dx = circle.width * 0.5f * radius;
        return Point{circle.x + dx * form.cosine, circle.y + dx * form.sine};
    };
    auto const fringe = atRadius(0.997f), interior = atRadius(0.97f);
    return form.contains(fringe.x, fringe.y) &&
        !surfaceopt::isOpaqueSample(form, fringe.x, fringe.y) &&
        surfaceopt::isOpaqueSample(form, interior.x, interior.y);
}

bool nativeTriangleFringesCannotServeAsSolidInterior() {
    constexpr int width = 64, height = 64;
    std::vector<Color> palette{{240, 220, 190}};
    std::vector<int> ranks{0};
    std::vector<std::vector<std::uint8_t>> later(1, std::vector<std::uint8_t>(width * height, 0));
    std::vector<std::vector<int>> positions(1);
    std::vector<std::uint8_t> rgba(width * height * 4, 0);
    for (int y = 16; y < 48; ++y) for (int x = 16; x < 48; ++x) {
        int const cell = y * width + x;
        positions[0].push_back(cell);
        rgba[cell * 4] = 240; rgba[cell * 4 + 1] = 220; rgba[cell * 4 + 2] = 190; rgba[cell * 4 + 3] = 255;
    }
    // These triangles exactly tile a rectangle mathematically. Their native
    // antialiased diagonals still need solid ink behind the shared edge.
    std::vector<Primitive> shapes{{32.f, 32.f, 32.f, 32.f, 0.f, 0, PrimitiveKind::Triangle},
        {32.f, 32.f, 32.f, 32.f, 180.f, 0, PrimitiveKind::Triangle}};
    SurfaceImage const image{width, height, palette, ranks, later, rgba};
    optimizePaintSurfaces(shapes, positions, image);
    std::cout << "native-triangle-interior: objects=" << shapes.size() << '\n';
    return shapes.size() == 1 && shapes.front().kind == PrimitiveKind::Block;
}

} // namespace

int main() {
    bool const colors = preservesSmallFlatColors();
    std::cout << "small-flat-colors: " << (colors ? "PASS" : "FAIL") << '\n';
    Primitive primitive{10.f, 20.f, 8.f, 12.f};
    primitive.skew = 30.f;
    auto const shape = xformOf(primitive);
    bool const skew = shape.contains(10.f, 20.f) && !shape.contains(15.f, 25.f) &&
        shape.contains(6.f, 24.f);
    std::string save;
    appendSerializedPrimitive(save, primitive, {defaultShapes()[0], 7, nullptr},
                              {2.f, 100.f, 200.f, 50});
    bool const placement = save.find(",2,120.000000,3,260.000000,") != std::string::npos &&
        save.find(",131,30.000000,132,0.000000") != std::string::npos;
    std::cout << "affine-coverage: " << (skew ? "PASS" : "FAIL") << '\n';
    std::cout << "native-placement: " << (placement ? "PASS" : "FAIL") << '\n';
    std::vector<Primitive> fineLines{{20.f, 10.f, 30.f, 0.18f},
                                    {20.f, 11.f, 30.f, 0.2f}};
    pruneMicroObjects(fineLines, 64, 64);
    bool const thin = fineLines.size() == 2;
    std::cout << "subpixel-lines: " << (thin ? "PASS" : "FAIL") << '\n';
    bool passed = colors && skew && placement && thin;
    for (auto const& [name, test] : std::initializer_list<std::pair<char const*, bool(*)()>>{
        {"rare-accent", preservesUnseededAccent},
        {"automatic-source-detail", preservesModerateAreaDetail},
        {"subpixel-visibility", pruningKeepsVisibleSubpixelStroke},
        {"adjacent-opaque-antialias", adjacentOpaqueShapesDoNotLeaveAntialiasSeams},
        {"affine-watermark", watermarkPreservesAffineCoverage},
        {"unused-palette-entry", emptyPaletteEntryProducesNoObjects},
        {"layered-surface-efficiency", layeredSlantedSurfaceUsesFewObjects},
        {"rounded-surface-efficiency", roundedShapesKeepLowCostAndCoverage},
        {"steep-cap-spikes", steepSurfaceCapsDoNotExtrapolateSpikes},
        {"continuous-band-joins", continuousSurfaceHasNoStepsBetweenBands},
        {"mixed-continuous-edges", sharpCornerDoesNotDiscardOtherContinuousEdges},
        {"continuous-steep-strip", continuousSurfaceRetainsSteepShortStrips},
        {"inclined-continuous-efficiency", inclinedContinuousCurvesAvoidExcessiveSplits},
        {"native-rounded-lines", curvedLinesUseRotatedStrokesAndRoundJoins},
        {"digital-corner-continuity", digitalCornersStayOnOneStroke},
        {"diagonal-junction-topology", diagonalJunctionsKeepAllBranches},
        {"source-antialias-stroke-width", softStrokeWidthIncludesItsAntialiasFringe},
        {"partial-rounded-regions", irregularFragmentsDoNotPreventRoundRegions},
        {"opaque-region-interiors", surfaceOptimizationKeepsOpaqueRegionInteriors},
        {"canvas-independent-quality", canvasPaddingDoesNotRelaxDetailQuality},
        {"opaque-gap-repair", opaqueGapsAreRepairedBeforeCountingSavings},
        {"maximal-run-efficiency", maximalRunsReducePiecesOnSmoothContours},
        {"hidden-parallel-boundaries", hiddenWidthsUseOneAffineBlock},
        {"transparent-stray-removal", transparentBackgroundDoesNotProtectStrayObjects},
        {"foreign-interior-protection", laterColorsDoNotInvadeEarlierInteriors},
        {"native-circle-fringe", nativeCircleFringeIsNotSolidInk},
        {"native-triangle-interior", nativeTriangleFringesCannotServeAsSolidInterior}}) {
        bool const success = test();
        std::cout << name << ": " << (success ? "PASS" : "FAIL") << '\n';
        passed &= success;
    }
    return passed ? 0 : 1;
}
