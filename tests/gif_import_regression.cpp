#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include "../src/features/gif-import/services/GifImportPipeline.hpp"
#include "../src/features/gif-import/services/GifVectorMath.cpp"
#include "../src/features/gif-import/services/GifArtVectorizer.cpp"
#include "../src/features/gif-import/services/GifPaintVectorizer.cpp"
#include "../src/features/gif-import/services/GifImportPipeline.cpp"

using namespace paimon::gifimport;

namespace {

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

bool solidAreaBecomesOneRect() {
    auto source = animation(8, 8, 1, 230, 40, 60);
    auto result = buildPlan(source, exactOptions(8));
    bool const pass = result && result.plan.visualObjects == 1 &&
        result.plan.staticObjects.size() == 1 &&
        result.plan.staticObjects.front().width == 8 &&
        result.plan.staticObjects.front().height == 8;
    std::cout << "solid: rects=" << (result ? result.plan.visualObjects : 0) << '\n';
    return pass;
}

bool borderBackgroundIsRemoved() {
    auto source = animation(6, 6, 1, 255, 255, 255);
    for (int y = 2; y < 4; ++y) {
        for (int x = 2; x < 4; ++x) setPixel(source, 0, x, y, 220, 20, 30);
    }
    auto options = exactOptions(6);
    options.background = BackgroundMode::AutoBorder;
    options.backgroundTolerance = 8;
    auto result = buildPlan(source, options);
    bool const pass = result && result.plan.visualObjects == 1 &&
        result.plan.staticObjects.front().x == 3 && result.plan.staticObjects.front().y == 3 &&
        result.plan.staticObjects.front().width == 2 && result.plan.staticObjects.front().height == 2;
    std::cout << "background: " << (result ? "removed" : result.error) << '\n';
    return pass;
}

bool temporalStrategyKeepsStaticArt() {
    auto source = animation(4, 2, 2, 0, 0, 0, 0);
    for (int frame = 0; frame < 2; ++frame) {
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < 2; ++x) setPixel(source, frame, x, y, 220, 30, 40);
        }
    }
    setPixel(source, 0, 2, 0, 20, 80, 240);
    setPixel(source, 1, 3, 0, 20, 80, 240);

    auto result = buildPlan(source, exactOptions(4));
    bool const pass = result && result.plan.strategy == "temporal" &&
        result.plan.staticObjects.size() == 1 && result.plan.visualObjects == 3;
    std::cout << "temporal: strategy=" << (result ? result.plan.strategy : result.error)
              << " visuals=" << (result ? result.plan.visualObjects : 0) << '\n';
    return pass;
}

bool duplicateFramesCollapse() {
    auto source = animation(5, 5, 2, 40, 210, 90);
    source.frames[0].delayMs = 70;
    source.frames[1].delayMs = 130;
    auto result = buildPlan(source, exactOptions(5));
    bool const pass = result && result.plan.frames.size() == 1 &&
        result.plan.frames.front().delayMs == 200 && result.plan.triggerObjects == 0;
    std::cout << "duplicates: frames=" << (result ? result.plan.frames.size() : 0) << '\n';
    return pass;
}

bool frameZeroDoesNotNeedAFullReset() {
    auto source = animation(4, 4, 3, 220, 30, 40);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            setPixel(source, 1, x, y, 30, 210, 80);
            setPixel(source, 2, x, y, 40, 90, 230);
        }
    }
    auto result = buildPlan(source, exactOptions(4));
    bool const pass = result && result.plan.frames.size() == 3 &&
        result.plan.tracks.size() == 3 && result.plan.triggerObjects == 12;
    std::cout << "schedule: triggers=" << (result ? result.plan.triggerObjects : 0) << '\n';
    return pass;
}

bool nonLoopScheduleStopsAtLastFrame() {
    auto source = animation(4, 4, 3, 220, 30, 40);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            setPixel(source, 1, x, y, 30, 210, 80);
            setPixel(source, 2, x, y, 40, 90, 230);
        }
    }
    auto options = exactOptions(4);
    options.loop = false;
    auto result = buildPlan(source, options);
    bool const pass = result && result.plan.frames.size() == 3 &&
        result.plan.tracks.size() == 3 && result.plan.triggerObjects == 8;
    std::cout << "no-loop: triggers=" << (result ? result.plan.triggerObjects : 0) << '\n';
    return pass;
}

bool playbackStrategyCapsTriggers() {
    auto source = animation(12, 12, 16);
    std::array<std::uint16_t, 12> const masks{
        0xaaaa, 0xcccc, 0xf0f0, 0x9696, 0xa5a5, 0xc3c3,
        0x5a5a, 0x3c3c, 0x6996, 0x9669, 0x87e1, 0x1e78
    };
    for (int frame = 0; frame < 16; ++frame) {
        for (int y = 0; y < 12; ++y) {
            for (int x = 0; x < 12; ++x) {
                bool const light = (x + y) % 2 == 0;
                setPixel(source, frame, x, y, light ? 235 : 20, light ? 235 : 20, light ? 235 : 20);
            }
        }
        for (int pixel = 0; pixel < static_cast<int>(masks.size()); ++pixel) {
            if ((masks[static_cast<std::size_t>(pixel)] & (std::uint16_t{1} << frame)) != 0) {
                int const position = pixel * 13;
                setPixel(source, frame, position % 12, position / 12, 220, 35, 55);
            }
        }
    }

    auto result = buildPlan(source, exactOptions(12));
    bool const pass = result && result.plan.strategy == "por-frame" &&
        result.plan.triggerObjects <= result.plan.frames.size() * 4;
    std::cout << "playback: strategy=" << (result ? result.plan.strategy : result.error)
              << " triggers=" << (result ? result.plan.triggerObjects : 0) << '\n';
    return pass;
}

bool budgetLowersResolution() {
    auto source = animation(16, 16, 1);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            bool const white = (x + y) % 2 == 0;
            setPixel(source, 0, x, y, white ? 245 : 15, white ? 245 : 15, white ? 245 : 15);
        }
    }
    auto options = exactOptions(16);
    options.maxColors = 2;
    options.objectBudget = 100;
    auto result = buildPlan(source, options);
    options.mode = ImportMode::Art;
    auto art = buildPlan(source, options);
    bool const pass = result && result.plan.totalObjects <= 100 &&
        result.plan.actualDimension < 16 && art && art.plan.totalObjects <= 100 &&
        art.plan.actualDimension < 16;
    std::cout << "budget: dimension=" << (result ? result.plan.actualDimension : 0)
              << " objects=" << (result ? result.plan.totalObjects : 0)
              << " art=" << (art ? art.plan.totalObjects : 0) << '\n';
    return pass;
}

Options artOptions(int dimension) {
    auto options = exactOptions(dimension);
    options.mode = ImportMode::Art;
    options.sampling = SamplingMode::Smooth;
    return options;
}

bool artModeFitsCircles() {
    auto source = animation(15, 15, 1, 0, 0, 0, 0);
    for (int y = 0; y < 15; ++y) {
        for (int x = 0; x < 15; ++x) {
            float const dx = x + 0.5f - 7.5f;
            float const dy = y + 0.5f - 7.5f;
            if (dx * dx + dy * dy <= 30.25f) {
                setPixel(source, 0, x, y, 225, 45, 70);
            }
        }
    }
    auto result = buildPlan(source, artOptions(15));
    bool const pass = result && result.plan.circleObjects == 1 &&
        result.plan.visualObjects == 1;
    std::cout << "art-circle: circles=" << (result ? result.plan.circleObjects : 0)
              << " objects=" << (result ? result.plan.visualObjects : 0) << '\n';
    return pass;
}

bool artModeRotatesStrokes() {
    auto source = animation(16, 16, 1, 0, 0, 0, 0);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            if (std::abs(x - y) <= 1) setPixel(source, 0, x, y, 40, 180, 235);
        }
    }
    auto result = buildPlan(source, artOptions(16));
    bool rotated = false;
    if (result) {
        for (auto const& object : result.plan.staticObjects) {
            if (object.kind == PrimitiveKind::Stroke && std::abs(object.rotation) > 10.f) {
                rotated = true;
                break;
            }
        }
    }
    bool const pass = result && result.plan.strokeObjects > 0 && rotated &&
        result.plan.visualObjects < 16;
    std::cout << "art-stroke: strokes=" << (result ? result.plan.strokeObjects : 0)
              << " objects=" << (result ? result.plan.visualObjects : 0) << '\n';
    return pass;
}

bool artModeFitsTriangles() {
    auto source = animation(12, 12, 1, 0, 0, 0, 0);
    for (int y = 0; y < 11; ++y) {
        for (int x = 0; x < 11 - y; ++x) {
            setPixel(source, 0, x, y, 245, 185, 35);
        }
    }
    auto result = buildPlan(source, artOptions(12));
    bool const pass = result && result.plan.triangleObjects == 1 &&
        result.plan.visualObjects == 1;
    std::cout << "art-triangle: triangles=" << (result ? result.plan.triangleObjects : 0)
              << " objects=" << (result ? result.plan.visualObjects : 0) << '\n';
    return pass;
}

bool artModeSegmentsCurves() {
    auto source = animation(24, 24, 1, 0, 0, 0, 0);
    for (int y = 0; y < 24; ++y) {
        for (int x = 0; x < 24; ++x) {
            float const dx = x + 0.5f - 12.f;
            float const dy = y + 0.5f - 12.f;
            float const radius = std::sqrt(dx * dx + dy * dy);
            if (radius >= 7.5f && radius <= 9.5f) {
                setPixel(source, 0, x, y, 175, 70, 240);
            }
        }
    }
    auto blocks = buildPlan(source, exactOptions(24));
    auto art = buildPlan(source, artOptions(24));
    bool const pass = blocks && art && art.plan.strokeObjects >= 4 &&
        art.plan.visualObjects < blocks.plan.visualObjects;
    std::cout << "art-curve: blocks=" << (blocks ? blocks.plan.visualObjects : 0)
              << " art=" << (art ? art.plan.visualObjects : 0)
              << " strokes=" << (art ? art.plan.strokeObjects : 0) << '\n';
    return pass;
}

bool artModeProtectsOtherColors() {
    auto source = animation(24, 24, 1, 0, 0, 0, 0);
    for (int y = 0; y < 24; ++y) {
        for (int x = 0; x < 24; ++x) {
            float const dx = x + 0.5f - 12.f;
            float const dy = y + 0.5f - 12.f;
            float const radius = std::sqrt(dx * dx + dy * dy);
            if (radius < 7.5f) setPixel(source, 0, x, y, 35, 115, 235);
            if (radius >= 7.5f && radius <= 9.5f) {
                setPixel(source, 0, x, y, 235, 55, 80);
            }
        }
    }
    auto result = buildPlan(source, artOptions(24));
    if (!result) return false;
    auto preview = renderPlanFrame(result.plan, 0, 1);
    auto const& cells = result.plan.frames.front().cells;
    bool pass = true;
    for (std::size_t position = 0; position < cells.size(); ++position) {
        int const colorIndex = cells[position];
        if (colorIndex < 0) continue;
        auto const& color = result.plan.palette[static_cast<std::size_t>(colorIndex)];
        std::size_t const pixel = position * 4;
        if (preview[pixel] != color.r || preview[pixel + 1] != color.g ||
            preview[pixel + 2] != color.b || preview[pixel + 3] != 255) {
            pass = false;
            break;
        }
    }
    std::cout << "art-colors: " << (pass ? "protected" : "overlap") << '\n';
    return pass;
}

bool artAnimationKeepsTriggersBounded() {
    auto source = animation(14, 10, 8, 0, 0, 0, 0);
    for (int frame = 0; frame < 8; ++frame) {
        float const centerX = 2.5f + frame;
        for (int y = 0; y < 10; ++y) {
            for (int x = 0; x < 14; ++x) {
                float const dx = x + 0.5f - centerX;
                float const dy = y + 0.5f - 5.f;
                if (dx * dx + dy * dy <= 4.f) {
                    setPixel(source, frame, x, y, 60, 225, 130);
                }
            }
        }
    }
    auto result = buildPlan(source, artOptions(14));
    bool const pass = result && result.plan.triggerObjects <= result.plan.frames.size() * 4 &&
        result.plan.totalObjects <= 50000;
    std::cout << "art-animation: strategy=" << (result ? result.plan.strategy : result.error)
              << " triggers=" << (result ? result.plan.triggerObjects : 0) << '\n';
    return pass;
}

} // namespace

int main() {
    bool const solid = solidAreaBecomesOneRect();
    bool const background = borderBackgroundIsRemoved();
    bool const temporal = temporalStrategyKeepsStaticArt();
    bool const duplicates = duplicateFramesCollapse();
    bool const schedule = frameZeroDoesNotNeedAFullReset();
    bool const noLoop = nonLoopScheduleStopsAtLastFrame();
    bool const playback = playbackStrategyCapsTriggers();
    bool const budget = budgetLowersResolution();
    bool const circle = artModeFitsCircles();
    bool const stroke = artModeRotatesStrokes();
    bool const triangle = artModeFitsTriangles();
    bool const curve = artModeSegmentsCurves();
    bool const colors = artModeProtectsOtherColors();
    bool const artAnimation = artAnimationKeepsTriggersBounded();

    if (!solid) std::cerr << "FAIL: solid area was not merged into one rectangle\n";
    if (!background) std::cerr << "FAIL: connected border background was not removed\n";
    if (!temporal) std::cerr << "FAIL: temporal optimization did not preserve static art\n";
    if (!duplicates) std::cerr << "FAIL: duplicate frames were not collapsed\n";
    if (!schedule) std::cerr << "FAIL: frame zero still emits redundant reset triggers\n";
    if (!noLoop) std::cerr << "FAIL: non-loop playback emitted extra triggers\n";
    if (!playback) std::cerr << "FAIL: playback strategy did not cap trigger work\n";
    if (!budget) std::cerr << "FAIL: object budget did not lower quality\n";
    if (!circle) std::cerr << "FAIL: art mode did not fit a circle\n";
    if (!stroke) std::cerr << "FAIL: art mode did not fit a rotated stroke\n";
    if (!triangle) std::cerr << "FAIL: art mode did not fit a triangle\n";
    if (!curve) std::cerr << "FAIL: art mode did not segment a curved outline\n";
    if (!colors) std::cerr << "FAIL: art mode covered another color\n";
    if (!artAnimation) std::cerr << "FAIL: art animation exceeded the trigger cap\n";
    return solid && background && temporal && duplicates && schedule && noLoop && playback &&
        budget && circle && stroke && triangle && curve && colors && artAnimation ? 0 : 1;
}
