#pragma once

#include <array>
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
    Render,
    Free,
    Circles,
    Blur,
    Vert,
    VertX,
};

enum class GlowMode {
    Off,
    Soft,
    Strong,
};

inline bool usesPaintGeometry(ImportMode mode) {
    return mode == ImportMode::Paint || mode == ImportMode::Render ||
        mode == ImportMode::Free || mode == ImportMode::Circles;
}

// circles mode draws with the shape, not the grid: gd paints it on another
// sprite sheet and the seam patch would land underneath.
inline bool matchesGridExactly(ImportMode mode) {
    return usesPaintGeometry(mode) && mode != ImportMode::Circles;
}

enum class BuildStage {
    Preparing,
    Resizing,
    Palette,
    Geometry,
    Reviewing,
    Refining,
    Done,
};

struct BuildProgress {
    BuildStage stage = BuildStage::Preparing;
    float value = 0.f;
    int pass = 0;
    int passes = 0;
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

// each cell is a palette index, or -1 where there is nothing to paint.
struct GridFrame {
    int delayMs = 100;
    std::vector<std::int32_t> cells;
};

// a decoration-library figure reduced to what tracing needs: which part of
// its box paints. rows run top-down, like the grid's.
struct StampMask {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> coverage;

    bool empty() const { return coverage.empty(); }
};

// each orientation is its own entry so the plan skips the emitter trigonometry.
struct PlanStamp {
    int objectId = 0;
    float baseWidth = 30.f;
    float baseHeight = 30.f;
    // object art rarely fills its frame, so the mold box is the painting part
    // only; this shifts the object (in box fractions) to land that part right.
    float offsetX = 0.f;
    float offsetY = 0.f;
    float rotation = 0.f;
    bool flipX = false;
    // code-generated spare molds (analytic gaussian/ramp) for missing native glow:
    // same blending and opacity as natives, but blending fakes the halo.
    bool analyticFallback = false;
    StampMask mask;
};

struct Options {
    int maxDimension = 320;
    int minDimension = 6;
    int maxColors = 24;
    int maxFrames = 90;
    int objectBudget = 12000;
    int alphaThreshold = 96;
    int backgroundTolerance = 28;
    float pixelSize = 6.f;
    BackgroundMode background = BackgroundMode::AutoBorder;
    SamplingMode sampling = SamplingMode::Smooth;
    ImportMode mode = ImportMode::Blocks;
    GlowMode glow = GlowMode::Off;
    bool dither = false;
    bool loop = true;
    bool motion = true;
    bool autoResolution = false;
    float blurRadius = 1.f;
    float blurGlowDiameter = 4.f;
    bool softBackdrop = true;
    // vertx drops a 2903 wash with the image's vertical flow.
    bool gradientWash = true;
    // snapshot of native alpha masks, prepared on the gl thread.
    std::vector<PlanStamp> softStamps;
    // best native match errors (radial, vertical, quarter) from the last
    // buildsoftstamplibrary() run; only explains a soft-mode failure.
    std::array<double, 3> softMatchErrors{1.0, 1.0, 1.0};
};

inline int sourceResolutionLimit(Options const& options) {
    return options.autoResolution && options.mode == ImportMode::Paint
        ? 680 : options.maxDimension;
}

inline bool usesSoftGeometry(ImportMode mode) {
    return mode == ImportMode::Blur || mode == ImportMode::Vert ||
        mode == ImportMode::VertX;
}

enum class PrimitiveKind {
    Block,
    Stroke,
    Circle,
    Triangle,
    WideTriangle,
    Glow,
    Stamp,
};

inline constexpr std::size_t kPrimitiveKinds = 7;

struct Primitive {
    float x = 0.f;
    float y = 0.f;
    float width = 0.f;
    float height = 0.f;
    float rotation = 0.f;
    std::uint16_t color = 0;
    PrimitiveKind kind = PrimitiveKind::Block;
    std::int16_t layer = 0;
    // stamp figures only: index into `importplan::stamps`. trailing so list
    // initializers keep working.
    std::uint16_t stamp = 0;
};

struct VisibilityTrack {
    std::vector<std::uint64_t> mask;
    std::vector<Primitive> objects;
};

// where the figure sits in a frame, in cells, against the reference pose.
struct MotionKey {
    int frame = 0;
    int x = 0;
    int y = 0;
};

// a silhouette repeated across frames at shifting spots. drawn once and run
// by move triggers instead of paying a full copy per frame.
struct MotionTrack {
    std::vector<std::uint64_t> mask;
    std::vector<Primitive> objects;
    std::vector<MotionKey> keys;
};

struct ImportPlan {
    int width = 0;
    int height = 0;
    int sourceFrames = 0;
    int requestedDimension = 0;
    int actualDimension = 0;
    int sourceColorCount = 0;
    ImportMode mode = ImportMode::Blocks;
    std::string strategy;
    std::vector<Color> palette;
    std::vector<PlanStamp> stamps;
    std::vector<GridFrame> frames;
    std::vector<Primitive> staticObjects;
    std::vector<VisibilityTrack> tracks;
    std::vector<MotionTrack> motionTracks;
    // from here the palette is glow channels: blended at half opacity.
    std::size_t glowPaletteStart = static_cast<std::size_t>(-1);
    float glowOpacity = 1.f;
    std::vector<float> glowOpacityScales;
    int softBackdropColor = -1;
    // vertx 2903 wash: palette indices for top and bottom.
    bool gradientWash = false;
    int washTop = -1;
    int washBottom = -1;
    std::size_t visualObjects = 0;
    std::size_t triggerObjects = 0;
    std::size_t totalObjects = 0;
    std::size_t blockObjects = 0;
    std::size_t strokeObjects = 0;
    std::size_t circleObjects = 0;
    std::size_t triangleObjects = 0;
    std::size_t glowObjects = 0;
    std::size_t stampObjects = 0;
    std::size_t moveTriggers = 0;
    float similarity = 100.f;
    float geometrySimilarity = 100.f;
    float detailSimilarity = 100.f;
    int renderPasses = 0;

    bool animated() const { return frames.size() > 1; }
};

struct BuildResult {
    ImportPlan plan;
    std::string error;

    explicit operator bool() const { return error.empty(); }
};

// one rasterized frame for the progressive preview: produced by the worker,
// the popup only uploads it to texture.
struct PreviewImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

} // namespace paimon::gifimport
