#pragma once

#include "../GifImportTypes.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <string_view>
#include <vector>

namespace paimon::gifimport {

struct SvgOptions {
    int dimension = 256;
    // 0 keeps all perceptually distinct colors (after minColorDistance merging).
    int maxColors = 24;
    int objectBudget = 12000;
    BackgroundMode background = BackgroundMode::Keep;
    // Perceptual OkLab merge threshold for near-identical fills.
    float minColorDistance = 0.022f;
    // All geometric tolerances below are expressed in normalized 1000-unit canvas space
    // so behavior is consistent regardless of the SVG's raw viewBox scale.
    float curveTolerance = 0.55f;
    float simplifyTolerance = 0.70f;
    // Visvalingam-Whyatt effective area threshold (in 1000x1000 units): vertices
    // whose removal alters polygon area by <= minTriangleArea (and deviates <= simplifyTolerance)
    // are collapsed before triangulation, preventing microscopic sliver triangles.
    float minTriangleArea = 8.0f;
    float quadTolerance = 0.55f;
    float ellipseTolerance = 1.15f;
    // Planar regions smaller than this area (in 1000x1000 units) and below
    // minVisualImpact are absorbed into their dominant neighbor before triangulation.
    float minRegionArea = 22.f;
    // Post-triangulation visibility audit: primitives below both area and perceptual
    // impact (visibleArea1000 * okLabDeltaToUnderneath) are pruned.
    float minVisibleArea = 5.0f;
    float minVisualImpact = 1.8f;
    // Small overlap added along internal shared edges (in 1000-unit canvas space)
    // to prevent 1px rasterization cracks between adjacent triangles/quads.
    float internalSeamBleed = 0.10f;
    bool useCanvasBackdrop = true;
    bool eliminateCoveredHoles = true;
    bool mergeSameColorRegions = true;
    bool detectEllipses = true;
    bool mergeParallelograms = true;
    bool pruneMicroObjects = true;
};

struct SvgStats {
    int parsedShapes = 0;
    int uniqueInputColors = 0;
    bool planarPartition = false;
    int mergedRegions = 0;
    int absorbedMicroRegions = 0;
    int eliminatedHoles = 0;
    int skippedBackdropRegions = 0;
    int ellipsesFitted = 0;
    int parallelogramsMerged = 0;
    int occludedPruned = 0;
    int microPruned = 0;
    int coverVerticesCollapsed = 0;
    int triPairsCollapsed = 0;
    double elapsedMs = 0.0;
};

struct SvgBuildResult {
    ImportPlan plan;
    SvgStats stats;
    std::string error;

    explicit operator bool() const { return error.empty(); }
};

bool isSvgFile(std::filesystem::path const& path);
bool isSvgBytes(std::uint8_t const* data, std::size_t size);
bool probeSvgDimensions(std::string_view svgContent, int& width, int& height);
SvgOptions svgOptionsFromImportOptions(Options const& options);

using SvgAnimProgressCallback = std::function<void(int done, int total)>;

SvgBuildResult buildSvgPlan(std::string_view svgContent, SvgOptions const& options = {});

SvgBuildResult buildAnimatedSvgPlan(
    std::vector<SvgFrame> const& svgFrames,
    SvgOptions const& options = {},
    bool loop = true,
    int sourceFrameCount = 0,
    SvgAnimProgressCallback progress = {},
    std::atomic<bool> const* cancelled = nullptr
);

SvgBuildResult buildSvgPlanFromFile(
    std::filesystem::path const& path,
    SvgOptions const& options = {}
);

} // namespace paimon::gifimport
