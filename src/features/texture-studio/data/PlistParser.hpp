#pragma once
//
// PlistParser.hpp - Parses cocos2d-x sprite sheet .plist files into our
// SpriteFrameInfo[] representation.
//
// Why a hand-written parser instead of pulling in a full XML library?
//
//   * cocos2d-x .plist files are intensely regular — they are emitted by
//     TexturePacker (or PackGen, etc.) with a deterministic shape. We only
//     need to handle <plist><dict>...</dict></plist> with a known set of
//     <key>/<value> pairs, no namespaces, no attributes, no DTD.
//   * Geode already pulls in matjson but NOT pugixml/tinyxml. Adding a 3rd
//     party dep just for this is overkill.
//   * A minimal SAX-like state machine fits in <300 lines, has zero alloc
//     overhead beyond the std::strings, and is trivial to fuzz.
//
// We support cocos2d-x formats 0..3. GD 2.2 ships format 3 exclusively, but
// older texture packs (and the cocos2d "old" format option in PackGen) use
// 0/1/2 — we want round-trip support for those too.
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
    // Parse from a UTF-8 XML string. The string is consumed read-only —
    // PlistParser does not retain any pointer into it after returning.
    //
    // On success returns a fully populated ParsedSpritesheet. On failure
    // returns an Err describing the first problem encountered. The parser
    // is best-effort: minor unknown fields in metadata are ignored, but
    // missing essentials (frames dict, format key) abort the parse.
    static geode::Result<ParsedSpritesheet> parseString(std::string_view xml);

    // Convenience wrapper around parseString that reads from a file path.
    static geode::Result<ParsedSpritesheet> parseFile(std::filesystem::path const& path);

    // Convenience: identify the format version embedded inside the .plist
    // without doing the full parse. Useful for the project editor UI to
    // warn users about format-2 packs that aren't fully round-trip-safe.
    static geode::Result<int> sniffFormat(std::string_view xml);

private:
    PlistParser() = delete;
};

}  // namespace paimon::texture_studio
