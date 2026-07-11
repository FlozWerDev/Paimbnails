#include "Assets.hpp"
#include "SpriteHelper.hpp"
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <filesystem>
#include <sstream>
#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace {

std::filesystem::path cfgPathFor(std::string const& key) {
    auto base = Mod::get()->getSaveDir() / "assets" / "buttons";
    std::error_code ec; std::filesystem::create_directories(base, ec);
    return base / (key + ".txt");
}

// Scale the sprite to a target dimension (keeping aspect ratio) so buttons look
// uniform regardless of the source PNG resolution.
void normalizeSpriteSize(CCSprite* spr, float targetDim = 45.0f) {
    if (!spr) return;
    float currentSize = std::max(spr->getContentWidth(), spr->getContentHeight());
    if (currentSize > 0.f) {
        spr->setScale(targetDim / currentSize);
    }
}
}

namespace Assets {

CCSprite* loadButtonSprite(
    std::string const& key,
    std::string const& defaultContent,
    geode::CopyableFunction<CCSprite*()> fallback
) {
    auto path = cfgPathFor(key);
    std::error_code ecAsset;
    if (!std::filesystem::exists(path, ecAsset)) {
        // if missing, write a base txt explaining the format
        std::stringstream ss;
        ss << "# Button: " << key << "\n";
        ss << "# Supported formats (first non-empty line):\n";
        ss << "#   frame:FrameName.png\n";
        ss << "#   file:C:/path/to/my_button.png\n";
        ss << "#   C:/path/to/my_button.png\n";
        ss << "# Leave empty to use the default icon.\n";
        if (!defaultContent.empty()) {
            ss << defaultContent << "\n";
        }
        (void)file::writeString(path, ss.str());
    }

    auto txt = file::readString(path).unwrapOr("");
    std::stringstream s(txt);
    std::string line;
    std::string directive;
    while (std::getline(s, line)) {
        auto t = geode::utils::string::trim(line);
        if (t.empty() || t[0] == '#') continue;
        directive = t; break;
    }

    // first check for an override in the .txt
    if (!directive.empty()) {
        // format: frame:Name
        constexpr std::string_view framePrefix = "frame:";
        constexpr std::string_view filePrefix = "file:";
        if (directive.rfind(framePrefix.data(), 0) == 0) {
            auto name = directive.substr(framePrefix.size());
            name = geode::utils::string::trim(name);
            if (!name.empty()) {
                if (auto spr = paimon::SpriteHelper::safeCreateWithFrameName(name.c_str())) {
                    return spr;
                }
            }
        } else {
            // file:PATH or a bare path
            std::string pathStr = directive;
            if (directive.rfind(filePrefix.data(), 0) == 0) {
                pathStr = directive.substr(filePrefix.size());
                pathStr = geode::utils::string::trim(pathStr);
            }

            if (!pathStr.empty()) {
                // if relative, resolve it from the config folder
                std::filesystem::path p = pathStr;
                if (!p.is_absolute()) p = cfgPathFor(key).parent_path() / p;
                if (std::filesystem::exists(p, ecAsset)) {
                    if (auto spr = CCSprite::create(geode::utils::string::pathToString(p).c_str())) {
                        normalizeSpriteSize(spr);
                        return spr;
                    }
                }
            }
        }
    }

    // otherwise try the mod resources: resources/buttons/{key}.png then resources/{key}.png
    auto modResourcePath = Mod::get()->getResourcesDir() / "buttons" / (key + ".png");
    if (std::filesystem::exists(modResourcePath, ecAsset)) {
        if (auto spr = CCSprite::create(geode::utils::string::pathToString(modResourcePath).c_str())) {
            normalizeSpriteSize(spr);
            return spr;
        }
    }
    auto modResourcePath2 = Mod::get()->getResourcesDir() / (key + ".png");
    if (std::filesystem::exists(modResourcePath2, ecAsset)) {
        if (auto spr = CCSprite::create(geode::utils::string::pathToString(modResourcePath2).c_str())) {
            normalizeSpriteSize(spr);
            return spr;
        }
    }

    // last resort: call the provided fallback
    return fallback();
}

} // namespace Assets

