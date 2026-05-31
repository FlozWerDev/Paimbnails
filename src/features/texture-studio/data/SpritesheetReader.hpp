#pragma once
//
// SpritesheetReader.hpp - Composes PlistParser + ImageBuffer to produce, for
// each frame in the .plist, an isolated ImageBuffer holding that sprite's
// pixels in their *un-rotated*, logical orientation.
//
// This is the layer the engine pipeline consumes: clustering and tinting
// don't care about the rotation/packing of the original atlas, they just
// want "give me the play button as a 90x90 RGBA8 image".
//

#include "ImageBuffer.hpp"
#include "SpriteFrameInfo.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace paimon::texture_studio {

// One sprite extracted from the atlas, ready to be processed.
struct ExtractedFrame {
    SpriteFrameInfo info;        // metadata (rect, offset, rotated, etc.)
    ImageBuffer     pixels;      // (spriteW, spriteH) RGBA8, un-rotated
};

// Result of reading a full sheet.
struct LoadedSpritesheet {
    PlistMetadata metadata;       // copied from the plist
    std::vector<ExtractedFrame> frames;
    int atlasWidth  = 0;          // size of the source PNG (may differ from
    int atlasHeight = 0;          // metadata.size if the user mismatched files)
};

class SpritesheetReader final {
public:
    // Load a sheet given paths to its plist and image. The image format is
    // anything stb_image recognises (PNG, JPG, BMP, etc.) — though GD only
    // ships PNG.
    static geode::Result<LoadedSpritesheet> loadFromPaths(
        std::filesystem::path const& plistPath,
        std::filesystem::path const& pngPath);

    // Load from already-parsed pieces. Used by callers that have the .plist
    // text in memory (e.g. from an in-RAM zip during pack import).
    static geode::Result<LoadedSpritesheet> loadFromMemory(
        ParsedSpritesheet const& parsed,
        std::span<std::uint8_t const> pngBytes);

    // Extract one frame's pixels from an already-loaded atlas image.
    // Handles the cocos2d "textureRotated" convention: when rotated, the
    // pixels in the atlas are rotated 90° clockwise relative to their
    // logical orientation, and we must rotate counter-clockwise to undo.
    static ImageBuffer extractFrame(ImageBuffer const& atlas, SpriteFrameInfo const& f);

private:
    SpritesheetReader() = delete;
};

}  // namespace paimon::texture_studio
