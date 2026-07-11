#include "FontTag.hpp"
#include <Geode/Geode.hpp>
#include <cctype>

using namespace cocos2d;

namespace paimon::fonts {

static constexpr const char* DEFAULT_FONT = "chatFont.fnt";

// Resolve a font ID to a .fnt filename.
static std::string resolveId(std::string const& id) {
    if (id.empty()) return DEFAULT_FONT;

    // Named aliases
    if (id == "big") return "bigFont.fnt";
    if (id == "chat") return "chatFont.fnt";
    if (id == "gold") return "goldFont.fnt";

    // Numeric IDs: 01-59 → gjFontXX.fnt
    if (id.size() == 2 && std::isdigit(static_cast<unsigned char>(id[0]))
                       && std::isdigit(static_cast<unsigned char>(id[1]))) {
        auto numResult = geode::utils::numFromString<int>(id);
        if (numResult.isOk()) {
            int num = numResult.unwrap();
            if (num >= 1 && num <= 59) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "gjFont%02d.fnt", num);
                return buf;
            }
        }
    }

    // Custom: if it already ends with .fnt, use as-is; otherwise append
    if (id.size() >= 4 && id.substr(id.size() - 4) == ".fnt") {
        return id;
    }
    return id + ".fnt";
}

// Check whether a .fnt file is loadable by Cocos2d.
static bool fontFileExists(std::string const& fontFile) {
    auto fullPath = CCFileUtils::sharedFileUtils()->fullPathForFilename(fontFile.c_str(), false);
    return !fullPath.empty() && fullPath != fontFile;
}

FontTagResult parseFontTag(std::string const& text) {
    FontTagResult result;
    result.fontFile = DEFAULT_FONT;
    result.remainingText = text;
    result.hasTag = false;

    // Must start with "<f:"
    if (text.size() < 4 || text[0] != '<' || text[1] != 'f' || text[2] != ':') {
        return result;
    }

    // Find closing '>'
    auto closePos = text.find('>', 3);
    if (closePos == std::string::npos || closePos == 3) {
        return result; // no closing > or empty ID
    }

    std::string id = text.substr(3, closePos - 3);
    std::string resolved = resolveId(id);

    result.hasTag = true;
    result.remainingText = text.substr(closePos + 1);

    // Validate font exists; fall back to default if missing
    if (fontFileExists(resolved)) {
        result.fontFile = resolved;
    } else {
        result.fontFile = DEFAULT_FONT;
    }

    return result;
}

std::string extractFontId(std::string const& text) {
    if (text.size() < 4 || text[0] != '<' || text[1] != 'f' || text[2] != ':') {
        return "";
    }
    auto closePos = text.find('>', 3);
    if (closePos == std::string::npos || closePos == 3) {
        return "";
    }
    return text.substr(3, closePos - 3);
}

} // namespace paimon::fonts
