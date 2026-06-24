#pragma once
//
// Hand-written cocos2d-x .plist parser.
//
// We do not pull in a full XML library because .plist files are intensely
// regular: known <key>/<value> pairs, no namespaces, no DTD. A SAX-like
// state machine fits in <300 lines and is trivial to fuzz.
//
// Supports cocos2d-x formats 0..3 (GD 2.2 ships format 3 exclusively, but
// older packs and PackGen's "old" option use 0/1/2 for round-trip).
//

#include "SpriteFrameInfo.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace paimon::texture_studio {

class PlistParser final {
public:
    static geode::Result<ParsedSpritesheet> parseString(std::string_view xml);
    static geode::Result<ParsedSpritesheet> parseFile(std::filesystem::path const& path);
    static geode::Result<int> sniffFormat(std::string_view xml);

private:
    PlistParser() = delete;
};

}  // namespace paimon::texture_studio
