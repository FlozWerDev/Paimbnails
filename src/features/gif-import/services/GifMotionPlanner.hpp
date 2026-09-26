#pragma once

#include "../GifImportTypes.hpp"

namespace paimon::gifimport {

// One silhouette tracked across frames, with each reference-pose cell's color
// and where that pose lands per frame.
struct MotionGroup {
    std::vector<std::uint64_t> mask;
    std::vector<MotionKey> keys;
    std::vector<int> positions;
    std::vector<std::int32_t> colors;
};

struct MotionAnalysis {
    std::vector<MotionGroup> groups;
    std::vector<GridFrame> residual;
};

inline bool visibleAt(std::vector<std::uint64_t> const& mask, int frame) {
    if (mask.empty()) return false;
    return (mask[static_cast<std::size_t>(frame / 64)] &
            (std::uint64_t{1} << (frame % 64))) != 0;
}

inline MotionKey const* keyAt(MotionTrack const& track, int frame) {
    for (auto const& key : track.keys) {
        if (key.frame == frame) return &key;
    }
    return nullptr;
}

// Finds silhouettes the next frame repeats shifted. Hits land in `groups` and
// leave `residual`, where their gap refills with the background they covered:
// background turns uniform again and stops costing an object per frame.
MotionAnalysis analyzeMotion(
    std::vector<GridFrame> const& frames,
    int width,
    int height
);

std::size_t motionMoveCount(
    std::vector<MotionTrack> const& tracks,
    int frames,
    bool loop
);

std::size_t motionTriggerCount(
    std::vector<MotionTrack> const& tracks,
    int frames,
    bool loop
);

} // namespace paimon::gifimport
