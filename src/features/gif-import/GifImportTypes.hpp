#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace paimon::gifimport {

enum class BackgroundMode {
    Keep,
    AutoBorder,
};

enum class SamplingMode {
    Pixel,
    Smooth,
};

enum class ImportMode {
    Blocks,
    Art,
    Paint,
};

struct Options {
    int maxDimension = 48;
    int minDimension = 6;
    int maxColors = 16;
    int maxFrames = 60;
    int objectBudget = 12000;
    int alphaThreshold = 96;
    int backgroundTolerance = 28;
    float pixelSize = 6.f;
    BackgroundMode background = BackgroundMode::AutoBorder;
    SamplingMode sampling = SamplingMode::Smooth;
    ImportMode mode = ImportMode::Blocks;
    bool dither = false;
    bool loop = true;
};

inline std::size_t animationEventGroupCount(std::size_t frames, bool loop) {
    if (frames <= 1) return 0;
    return loop ? frames : frames - 1;
}

struct SourceFrame {
    int delayMs = 100;
    std::vector<std::uint8_t> rgba;
};

struct SourceAnimation {
    int width = 0;
    int height = 0;
    std::vector<SourceFrame> frames;
};

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    bool operator==(Color const&) const = default;
};

struct GridFrame {
    int delayMs = 100;
    std::vector<std::int16_t> cells;
};

enum class PrimitiveKind {
    Block,
    Stroke,
    Circle,
    Triangle,
    WideTriangle,
};

struct Primitive {
    float x = 0.f;
    float y = 0.f;
    float width = 0.f;
    float height = 0.f;
    float rotation = 0.f;
    std::uint16_t color = 0;
    PrimitiveKind kind = PrimitiveKind::Block;
    std::int16_t layer = 0;
};

struct VisibilityTrack {
    std::vector<std::uint64_t> mask;
    std::vector<Primitive> objects;
};

struct ImportPlan {
    int width = 0;
    int height = 0;
    int sourceFrames = 0;
    int requestedDimension = 0;
    int actualDimension = 0;
    ImportMode mode = ImportMode::Blocks;
    std::string strategy;
    std::vector<Color> palette;
    std::vector<GridFrame> frames;
    std::vector<Primitive> staticObjects;
    std::vector<VisibilityTrack> tracks;
    std::size_t visualObjects = 0;
    std::size_t triggerObjects = 0;
    std::size_t totalObjects = 0;
    std::size_t blockObjects = 0;
    std::size_t strokeObjects = 0;
    std::size_t circleObjects = 0;
    std::size_t triangleObjects = 0;

    bool animated() const { return frames.size() > 1; }
};

struct BuildResult {
    ImportPlan plan;
    std::string error;

    explicit operator bool() const { return error.empty(); }
};

} // namespace paimon::gifimport
