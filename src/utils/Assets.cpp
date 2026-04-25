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

void limitSpriteSize(CCSprite* spr, float maxDim = 45.0f) {
    if (!spr) return;
    float currentSize = std::max(spr->getContentWidth(), spr->getContentHeight());
    if (currentSize > maxDim) {
        spr->setScale(maxDim / currentSize);
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
        // si no existe, creo un txt base explicando como va
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

    // primero miro si hay override en el .txt
    if (!directive.empty()) {
        // formato: frame:Nombre
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
            // file:RUTA o directamente una ruta a pelo
            std::string pathStr = directive;
            if (directive.rfind(filePrefix.data(), 0) == 0) {
                pathStr = directive.substr(filePrefix.size());
                pathStr = geode::utils::string::trim(pathStr);
            }

            if (!pathStr.empty()) {
                // si es relativa la tomo desde la carpeta del config
                std::filesystem::path p = pathStr;
                if (!p.is_absolute()) p = cfgPathFor(key).parent_path() / p;
                if (std::filesystem::exists(p, ecAsset)) {
                    if (auto spr = CCSprite::create(geode::utils::string::pathToString(p).c_str())) {
                        limitSpriteSize(spr);
                        return spr;
                    }
                }
            }
        }
    }

    // si no, pruebo en los recursos del mod: resources/buttons/{key}.png y luego resources/{key}.png
    auto modResourcePath = Mod::get()->getResourcesDir() / "buttons" / (key + ".png");
    if (std::filesystem::exists(modResourcePath, ecAsset)) {
        if (auto spr = CCSprite::create(geode::utils::string::pathToString(modResourcePath).c_str())) {
            limitSpriteSize(spr);
            return spr;
        }
    }
    auto modResourcePath2 = Mod::get()->getResourcesDir() / (key + ".png");
    if (std::filesystem::exists(modResourcePath2, ecAsset)) {
        if (auto spr = CCSprite::create(geode::utils::string::pathToString(modResourcePath2).c_str())) {
            limitSpriteSize(spr);
            return spr;
        }
    }

    // ultimo recurso: llamo al fallback que me pasaste
    return fallback();
}

} // namespace Assets

