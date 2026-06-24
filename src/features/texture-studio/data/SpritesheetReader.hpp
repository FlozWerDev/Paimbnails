#pragma once
//
// Composes PlistParser + ImageBuffer to extract each frame's un-rotated pixels.
// Clustering and tinting just consume SpriteFrameInfo + ImageBuffer without
// caring about the original atlas packing.
//

#include "ImageBuffer.hpp"
#include "SpriteFrameInfo.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace paimon::texture_studio {

struct ExtractedFrame {
    SpriteFrameInfo info;
    ImageBuffer     pixels;
};

struct LoadedSpritesheet {
    PlistMetadata metadata;
    std::vector<ExtractedFrame> frames;
    int atlasWidth  = 0;
    int atlasHeight = 0;
};

class SpritesheetReader final {
public:
    static geode::Result<LoadedSpritesheet> loadFromPaths(
        std::filesystem::path const& plistPath,
        std::filesystem::path const& pngPath);

    static geode::Result<LoadedSpritesheet> loadFromMemory(
        ParsedSpritesheet const& parsed,
        std::span<std::uint8_t const> pngBytes);

    static ImageBuffer extractFrame(ImageBuffer const& atlas, SpriteFrameInfo const& f);
    static ImageBuffer composeLogicalFrame(ImageBuffer const& pixels,
                                           SpriteFrameInfo const& f);

private:
    SpritesheetReader() = delete;
};

}  // namespace paimon::texture_studio
