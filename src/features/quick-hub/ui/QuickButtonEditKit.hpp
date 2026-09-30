#pragma once

// shared by the quick-button editors (image + sound) and quickbuttonpopup.

#include "../data/QuickHubCategories.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/string.hpp>

#include <filesystem>
#include <functional>
#include <string>

namespace paimon::quickhub {

inline std::filesystem::path uniqueDestIn(
    std::filesystem::path const& dir, std::string const& stem, std::string const& ext) {
    auto candidate = dir / (stem + ext);
    int n = 2;
    std::error_code ec;
    while (std::filesystem::exists(candidate, ec) && n < 1000) {
        candidate = dir / fmt::format("{}-{}{}", stem, n++, ext);
        ec.clear();
    }
    return candidate;
}

inline std::string fileNameOf(std::string const& path) {
    if (path.empty()) return "";
    auto pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

inline char const* sfxKindName(int kind) {
    switch (static_cast<QuickButtonSfxKind>(kind)) {
        case QuickButtonSfxKind::Game: return "Juego";
        case QuickButtonSfxKind::File: return "Archivo";
        case QuickButtonSfxKind::Online: return "Online";
        default: return "Original";
    }
}

inline ::CCMenuItemSpriteExtra* makeMiniButton(
    char const* label, bool selected, std::function<void()> cb) {
    auto* spr = ::ButtonSprite::create(
        label, "bigFont.fnt", selected ? "GJ_button_02.png" : "GJ_button_04.png", .8f);
    spr->setScale(0.42f);
    return geode::cocos::CCMenuItemExt::createSpriteExtra(spr, [cb = std::move(cb)](::CCMenuItemSpriteExtra*) {
        cb();
    });
}

inline std::string importFileToConfigDir(
    std::filesystem::path const& src,
    std::filesystem::path const& dir,
    std::string const& stem,
    std::function<bool(std::filesystem::path const& src, std::string const& destStr)> validate) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        PaimonNotify::create("No se pudo crear la carpeta.", geode::NotificationIcon::Error)->show();
        return "";
    }
    auto ext = geode::utils::string::toLower(
        geode::utils::string::pathToString(src.extension()));
    auto dest = uniqueDestIn(dir, slugify(stem), ext);
    std::filesystem::copy_file(
        src, dest, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        PaimonNotify::create("No se pudo copiar el archivo.", geode::NotificationIcon::Error)->show();
        return "";
    }
    auto destStr = geode::utils::string::pathToString(dest);
    if (!validate(src, destStr)) {
        std::filesystem::remove(dest, ec);
        return "";
    }
    return destStr;
}

} // namespace paimon::quickhub
