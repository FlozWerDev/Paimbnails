#include "PackMetadataBuilder.hpp"

#include <matjson.hpp>

#include <algorithm>
#include <cctype>

namespace paimon::texture_studio {

namespace {

// PackGen-compatible slug: [a-z0-9] only, everything else → "_".
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

struct Rgb {
    int r = 0, g = 0, b = 0;
};

// PackGen's darkenColor: Math.floor(channel * factor).
Rgb darken(cocos2d::ccColor3B c, float factor) {
    return {
        static_cast<int>(c.r * factor),
        static_cast<int>(c.g * factor),
        static_cast<int>(c.b * factor),
    };
}

// colors.json entries are {r,g,b,a} objects, matching PackGen/HappyTextures.
matjson::Value rgbaObj(int r, int g, int b, int a) {
    auto obj = matjson::Value::object();
    obj["r"] = r;
    obj["g"] = g;
    obj["b"] = b;
    obj["a"] = a;
    return obj;
}

matjson::Value rgbaObj(Rgb c, int a) { return rgbaObj(c.r, c.g, c.b, a); }

// Layer-json "color" attributes carry no alpha; opacity is a sibling key.
matjson::Value rgbObj(int r, int g, int b) {
    auto obj = matjson::Value::object();
    obj["r"] = r;
    obj["g"] = g;
    obj["b"] = b;
    return obj;
}

// Wrap `content` as {"children": {"node": {<name>: content}}} — the nesting
// HappyTextures layer-jsons use to address a node by ID.
matjson::Value nodeChild(std::string_view name, matjson::Value content) {
    auto node = matjson::Value::object();
    node[std::string(name)] = std::move(content);
    auto children = matjson::Value::object();
    children["node"] = std::move(node);
    auto outer = matjson::Value::object();
    outer["children"] = std::move(children);
    return outer;
}

}  // namespace

std::string PackMetadataBuilder::buildPackId(std::string_view packName) {
    return std::string("paimbnails.texture_studio.") + slugify(packName);
}

std::string PackMetadataBuilder::buildPackJson(std::string_view packName,
                                               std::string_view author) {
    auto obj = matjson::Value::object();
    // Texture Loader version this pack targets. PackGen ships "1.6.2";
    // claiming a version newer than the installed loader trips its check.
    obj["textureldr"] = "1.6.2";
    obj["name"]    = std::string("Paimon Studio - ") + std::string(packName);
    obj["id"]      = buildPackId(packName);
    obj["version"] = "1.0.0";
    obj["author"]  = std::string(author.empty() ? "Paimbnails" : author);
    return obj.dump(4);
}

// Port of PackGen's generateUiColors(): same keys, same darkening factors
// (0.5 / 0.4 / 0.3 / 0.2), same transparent-list variants.
std::string PackMetadataBuilder::buildUiColorsJson(PackExportConfig const& cfg) {
    auto primary   = cfg.colors.color1;
    auto secondary = cfg.colors.color2;

    Rgb primaryDark1   = darken(primary, 0.5f);
    Rgb primaryDarkMid = darken(primary, 0.4f);
    Rgb primaryDark2   = darken(primary, 0.3f);
    Rgb primaryDark3   = darken(primary, 0.2f);

    Rgb secondaryDark1   = darken(secondary, 0.5f);
    Rgb secondaryDarkMid = darken(secondary, 0.4f);
    Rgb secondaryDark2   = darken(secondary, 0.3f);

    auto obj = matjson::Value::object();
    obj["play-loading-outer"]    = rgbaObj(secondaryDark2, 255);
    obj["play-loading-center"]   = rgbaObj(secondaryDark2, 255);
    obj["play-loading-inner"]    = rgbaObj(secondaryDark1, 255);
    obj["play-loading-progress"] = rgbaObj(primary.r, primary.g, primary.b, 255);

    obj["info-description-bg"]   = rgbaObj(0, 0, 0, 90);
    obj["edit-description-bg"]   = rgbaObj(0, 0, 0, 100);
    obj["edit-name-bg"]          = rgbaObj(0, 0, 0, 150);
    obj["level-search-bg"]       = rgbaObj(0, 0, 0, 120);
    obj["level-search-bar-bg"]   = rgbaObj(0, 0, 0, 120);
    obj["quick-search-bg"]       = rgbaObj(0, 0, 0, 120);
    obj["difficulty-filters-bg"] = rgbaObj(0, 0, 0, 120);
    obj["length-filters-bg"]     = rgbaObj(0, 0, 0, 120);

    obj["comment-list-layer-bg"]      = rgbaObj(primaryDark3, 255);
    obj["comment-list-outline-brown"] = rgbaObj(primaryDark2, 255);
    obj["comment-list-outline-blue"]  = rgbaObj(secondaryDark2, 255);

    obj["chest-opened-overlay"]     = rgbaObj(255, 255, 255, 0);
    obj["25-chest-opened-overlay"]  = rgbaObj(255, 255, 255, 0);
    obj["50-chest-opened-overlay"]  = rgbaObj(255, 255, 255, 0);
    obj["100-chest-opened-overlay"] = rgbaObj(255, 255, 255, 0);

    obj["comment-cell-odd"]        = rgbaObj(primaryDarkMid, 255);
    obj["comment-cell-even"]       = rgbaObj(secondaryDarkMid, 255);
    obj["comment-cell-small-odd"]  = rgbaObj(primaryDarkMid, 255);
    obj["comment-cell-small-even"] = rgbaObj(secondaryDarkMid, 255);
    obj["comment-cell-bg-odd"]     = rgbaObj(primaryDarkMid, 255);
    obj["comment-cell-bg-even"]    = rgbaObj(secondaryDarkMid, 255);

    if (cfg.transparentLists) {
        obj["list-layer-bg"]      = rgbaObj(0, 0, 0, 20);
        obj["list-cell-odd"]      = rgbaObj(0, 0, 0, 50);
        obj["list-cell-even"]     = rgbaObj(0, 0, 0, 90);
        obj["list-cell-selected"] = rgbaObj(0, 0, 0, 200);
    } else {
        obj["list-layer-bg"]      = rgbaObj(primaryDark3, 255);
        obj["list-cell-odd"]      = rgbaObj(primaryDark2, 255);
        obj["list-cell-even"]     = rgbaObj(secondaryDark2, 255);
        obj["list-cell-selected"] = rgbaObj(primary.r, primary.g, primary.b, 255);
    }

    if (cfg.colorMainMenu) {
        obj["main-menu-bg"]     = rgbaObj(primaryDark1, 255);
        obj["main-menu-ground"] = rgbaObj(secondaryDarkMid, 255);
    }
    if (cfg.colorGradientBg) {
        obj["background"] = rgbaObj(secondaryDarkMid, 255);
    }

    return obj.dump(4);
}

// Port of PackGen's generateModsLayerJson(): recolors the Geode mod-list
// frame background and the search bar via the layer-json node tree.
std::string PackMetadataBuilder::buildModsLayerJson(PackExportConfig const& cfg) {
    auto frameBg = matjson::Value::object();
    {
        auto attrs = matjson::Value::object();
        if (cfg.transparentLists) {
            attrs["color"]   = rgbObj(90, 90, 90);
            attrs["opacity"] = 20;
        } else {
            Rgb secondaryDark2 = darken(cfg.colors.color2, 0.3f);
            attrs["color"] = rgbObj(secondaryDark2.r, secondaryDark2.g, secondaryDark2.b);
        }
        frameBg["attributes"] = std::move(attrs);
    }

    auto searchId = matjson::Value::object();
    {
        auto attrs = matjson::Value::object();
        attrs["color"]         = rgbObj(0, 0, 0);
        attrs["opacity"]       = 120;
        attrs["update-layout"] = "parent";
        searchId["attributes"] = std::move(attrs);
    }

    auto modList = nodeChild("top-container",
        nodeChild("search-menu",
            nodeChild("search-id", std::move(searchId))));

    // frame-bg and ModList are siblings under mod-list-frame's node map.
    auto innerNode = matjson::Value::object();
    innerNode["frame-bg"] = std::move(frameBg);
    innerNode["ModList"]  = std::move(modList);
    auto innerChildren = matjson::Value::object();
    innerChildren["node"] = std::move(innerNode);
    auto modListFrame = matjson::Value::object();
    modListFrame["children"] = std::move(innerChildren);

    return nodeChild("mod-list-frame", std::move(modListFrame)).dump(4);
}

// Port of PackGen's generateLoadingLayerJson(): swap the loading screen
// background for the custom image, or tint it to match the pack.
std::string PackMetadataBuilder::buildLoadingLayerJson(PackExportConfig const& cfg) {
    auto bgTexture = matjson::Value::object();
    auto attrs = matjson::Value::object();

    if (!cfg.customLoadingBgPng.empty()) {
        attrs["sprite"] = "LoadingLayerBG.png";
        attrs["color"]  = rgbObj(255, 255, 255);
        auto relative = matjson::Value::object();
        relative["relative"] = "screen";
        auto scale = matjson::Value::object();
        scale["fit"] = std::move(relative);
        attrs["scale"] = std::move(scale);
    } else {
        Rgb secondaryDark1 = darken(cfg.colors.color2, 0.5f);
        attrs["color"] = rgbObj(secondaryDark1.r, secondaryDark1.g, secondaryDark1.b);
    }

    bgTexture["attributes"] = std::move(attrs);
    return nodeChild("bg-texture", std::move(bgTexture)).dump(4);
}

}  // namespace paimon::texture_studio
