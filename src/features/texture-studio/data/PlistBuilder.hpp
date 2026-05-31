#pragma once
//
// PlistBuilder.hpp - Serializes a ParsedSpritesheet back into cocos2d-x .plist
// XML. The output is byte-compatible with PackGen / TexturePacker so existing
// tools (and Texture Loader) can consume it without surprises.
//
// We always emit cocos2d-x format 3 — that's what GD 2.2 expects and the
// shape PackGen targets. The format2/legacy decoders in PlistParser exist
// for read-only round-trip; we never write those formats.
//

#include "SpriteFrameInfo.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>
#include <string>

namespace paimon::texture_studio {

class PlistBuilder final {
public:
    // Generate a .plist XML string from the given parsed sheet. Returns
    // Err only if the input is malformed (negative dims, etc.) — encoding
    // is otherwise infallible.
    //
    // Note: callers are responsible for ensuring metadata.format == 3 if
    // they want strictly modern output. We don't enforce it because legacy
    // packs round-trip through us during quality conversion.
    static geode::Result<std::string> buildString(ParsedSpritesheet const& sheet);

    // Write the plist directly to disk.
    static geode::Result<> buildFile(ParsedSpritesheet const& sheet,
                                     std::filesystem::path const& path);

private:
    PlistBuilder() = delete;
};

}  // namespace paimon::texture_studio
