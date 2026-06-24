#pragma once
//
// Serializes a ParsedSpritesheet back into cocos2d-x .plist XML (format 3).
// Output is byte-compatible with PackGen / TexturePacker so Texture Loader
// can consume it without surprises.
//

#include "SpriteFrameInfo.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>
#include <string>

namespace paimon::texture_studio {

class PlistBuilder final {
public:
    static geode::Result<std::string> buildString(ParsedSpritesheet const& sheet);
    static geode::Result<> buildFile(ParsedSpritesheet const& sheet,
                                     std::filesystem::path const& path);

private:
    PlistBuilder() = delete;
};

}  // namespace paimon::texture_studio
