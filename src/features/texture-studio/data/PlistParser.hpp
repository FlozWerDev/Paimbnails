#pragma once

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
