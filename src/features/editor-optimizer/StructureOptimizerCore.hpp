#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace paimon::optimizer {

enum class StretchCapability : std::uint8_t {
    None = 0,          // Corner blocks, slopes, circles, asymmetric/ornate art
    HorizontalOnly,    // Local X only (e.g., horizontal edge line, half-slab)
    VerticalOnly,      // Local Y only (e.g., vertical pillar, vertical edge)
    Axis1D,            // Bordered symmetric block: stretchable along X OR Y (not 2D)
    Uniform2D,         // Flat/borderless uniform square: stretchable in 1D and 2D rectangles
};

struct ObjectRecord {
    std::size_t index = 0;
    int objectID = 0;
    bool isSolid = false;
    bool isEligible = false;
    StretchCapability stretch = StretchCapability::None;

    float x = 0.f;
    float y = 0.f;
    float baseWidth = 30.f;
    float baseHeight = 30.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
    float rotation = 0.f; // Clockwise degrees (Cocos2d / GD convention)
    bool flipX = false;
    bool flipY = false;

    std::string compatibilityKey;
};

struct OptimizerOptions {
    float maxCollisionScale = 4.f;
    float maxDecoScale = 8.f;
    float maxPixelSpan = 480.f;
    bool preserveCorners = true;
    bool merge2DFills = true;
    float posTolerance = 0.25f;
    float scaleTolerance = 0.005f;
};

struct MergedGroup {
    std::vector<std::size_t> sourceIndices;
    float newX = 0.f;
    float newY = 0.f;
    float newScaleX = 1.f;
    float newScaleY = 1.f;
    std::size_t keeperIndex = 0;
};

struct OptimizationPlan {
    std::size_t inputCount = 0;
    std::size_t eligibleCount = 0;
    std::size_t preservedCornerCount = 0;
    std::size_t excludedCount = 0;
    std::size_t mergedObjectCount = 0;
    std::size_t outputCount = 0;

    std::size_t solidInputCount = 0;
    std::size_t solidOutputCount = 0;
    std::size_t decoInputCount = 0;
    std::size_t decoOutputCount = 0;

    std::vector<MergedGroup> merges;
    std::vector<std::size_t> preservedCorners;
};

// Extracts key 2 (x) and key 3 (y) from a GD object save string; returns true if both exist.
bool extractSaveXY(std::string_view saveString, float& outX, float& outY);

// Rewrites an object's GD save string with updated position (2, 3) and scale (128, 129),
// stripping any legacy uniform scale key (32) so asymmetric scaling always applies cleanly.
std::string rewriteObjectTransform(
    std::string_view saveString,
    float x,
    float y,
    float scaleX,
    float scaleY
);

OptimizationPlan planStructureOptimization(
    std::vector<ObjectRecord> const& records,
    OptimizerOptions const& options = {}
);

} // namespace paimon::optimizer
