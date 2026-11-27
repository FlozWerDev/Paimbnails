#pragma once

#include "../GifImportTypes.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace paimon::gifimport {

// Matches OpenVector basic wizard INPUT QUALITY (0 = High, 1 = Medium, 2 = Low).
// Low produces cleaner, smoother paths with fewer nodes -> best GD object efficiency.
enum class OpenVectorQuality {
    High = 0,
    Medium = 1,
    Low = 2,
};

struct OpenVectorResult {
    std::string svgContent;
    std::string error;

    explicit operator bool() const { return error.empty() && !svgContent.empty(); }
};

struct OpenVectorFramesResult {
    std::vector<SvgFrame> frames;
    int sourceFrames = 0;
    std::string error;

    explicit operator bool() const { return error.empty() && !frames.empty(); }
};

using OpenVectorProgressCallback = std::function<void(int done, int total)>;

class OpenVectorRunner {
public:
    // Worker thread only: encodes RGBA frame to a temp PNG, runs OpenVector CLI with
    // --shrink-source --layout stacked and the selected quality preset, returns the SVG.
    static OpenVectorResult vectorizeRgbaToSvg(
        std::uint8_t const* rgba,
        int width,
        int height,
        OpenVectorQuality quality = OpenVectorQuality::Low,
        std::atomic<bool> const* cancelled = nullptr
    );

    // Worker thread only: selects up to maxFrames (merging skipped delays), deduplicates
    // identical frames, and distributes unique frames across parallel OpenVector workers.
    static OpenVectorFramesResult vectorizeFramesToSvg(
        SourceAnimation const& source,
        int maxFrames,
        OpenVectorQuality quality = OpenVectorQuality::Low,
        OpenVectorProgressCallback progress = {},
        std::atomic<bool> const* cancelled = nullptr
    );
};

} // namespace paimon::gifimport
