#include "PackMetadataBuilder.hpp"

#include <matjson.hpp>

#include <algorithm>
#include <cctype>

namespace paimon::texture_studio {

namespace {

// Slugify a user-provided name into a Texture Loader pack id segment.
// PackGen does the same: [a-z0-9] only, everything else → "_".
std::string slugify(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc)) {
            out += static_cast<char>(std::tolower(uc));
        } else {
            out += '_';
        }
    }
    if (out.empty()) out = "pack";
    return out;
}

// Convert ccColor3B to a Geode-compatible RGB JSON array. HappyTextures
// reads the 0..255 representation directly, so no normalisation needed.
matjson::Value rgbToJson(cocos2d::ccColor3B c) {
    auto arr = matjson::Value::array();
    arr.push(static_cast<int>(c.r));
    arr.push(static_cast<int>(c.g));
    arr.push(static_cast<int>(c.b));
    return arr;
}

}  // anonymous namespace

std::string PackMetadataBuilder::buildPackId(std::string_view packName) {
    return std::string("paimbnails.texture_studio.") + slugify(packName);
}

std::string PackMetadataBuilder::buildPackJson(std::string_view packName,
                                               std::string_view author) {
    auto obj = matjson::Value::object();
    // Latest stable Texture Loader as of GD 2.2081.
    obj["textureldr"] = "1.10.0";
    obj["name"]    = std::string("Paimon Studio - ") + std::string(packName);
    obj["id"]      = buildPackId(packName);
    obj["version"] = "1.0.0";
    obj["author"]  = std::string(author.empty() ? "Paimbnails" : author);
    return obj.dump(4);  // 4-space indent for human readability
}

std::string PackMetadataBuilder::buildUiColorsJson(PackExportConfig const& cfg) {
    // ui/colors.json overrides specific GD UI colors for HappyTextures-aware
    // mods. The exact key set comes from PackGen's generateUiColors().
    auto obj = matjson::Value::object();

    if (cfg.colorGradientBg) {
        // Tint the gradient background to Color 1.
        obj["gradient-bg-1"] = rgbToJson(cfg.colors.color1);
        obj["gradient-bg-2"] = rgbToJson(cfg.colors.color2);
    }
    if (cfg.colorMainMenu) {
        // Tint the main menu ground/background.
        obj["main-menu-ground"] = rgbToJson(cfg.colors.color1);
        obj["main-menu-bg"]     = rgbToJson(cfg.colors.color2);
    }
    if (cfg.transparentLists) {
        // Hint key — HappyTextures interprets this as "make level lists alpha=0".
        obj["transparent-lists"] = true;
    }
    return obj.dump(4);
}

std::string PackMetadataBuilder::buildModsLayerJson(PackExportConfig const& cfg) {
    // ModsLayer.json controls the appearance of cdc/CDC's mods popup. PackGen
    // uses it to flip the list background to transparent. Most users won't
    // care, but the option is here for parity.
    auto obj = matjson::Value::object();
    if (cfg.transparentLists) {
        // Empty alpha array → fully transparent. HappyTextures picks this up
        // automatically via the standard color-override pathway.
        auto cell = matjson::Value::object();
        cell["color"] = rgbToJson({0, 0, 0});
        cell["opacity"] = 0;
        obj["mod-list-cell-bg"] = cell;
    }
    return obj.dump(4);
}

std::string PackMetadataBuilder::buildLoadingLayerJson(PackExportConfig const& cfg) {
    // ui/LoadingLayer.json — controls the GD loading screen. PackGen tints
    // the background to Color 2 (or whatever the user picks); we follow.
    auto obj = matjson::Value::object();
    obj["bar-color"]   = rgbToJson(cfg.colors.color2);
    obj["text-color"]  = rgbToJson(cfg.colors.color1);
    obj["has-custom-bg"] = !cfg.customLoadingBgPng.empty();
    return obj.dump(4);
}

}  // namespace paimon::texture_studio
