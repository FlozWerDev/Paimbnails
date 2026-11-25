#include "ShopPreset.hpp"
#include "../../../utils/Base64.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace paimon::shop {
namespace {
bool hasText(std::string const& value, size_t max) {
    return !value.empty() && value.size() <= max && value.find_first_not_of(" \r\n\t") != std::string::npos
        && value.find('\0') == std::string::npos;
}

bool boundedTree(matjson::Value const& value, int depth = 0) {
    if (depth > 8) return false;
    if (value.isNumber()) return std::isfinite(value.asDouble().unwrapOr(NAN));
    if (value.isString()) return value.asString().unwrap().size() <= 256;
    if (value.isArray() || value.isObject()) {
        if (value.size() > 1024) return false;
        for (auto const& child : value) if (!boundedTree(child, depth + 1)) return false;
    }
    return true;
}

bool validLayout(matjson::Value const& layout) {
    if (!layout.isObject() || !layout["buttons"].isArray() || !layout["shapes"].isArray()) return false;
    auto validEntries = [](matjson::Value const& entries, char const* idKey) {
        if (entries.size() > 512) return false;
        std::vector<std::string> ids;
        for (auto const& entry : entries) {
            auto id = entry[idKey].asString().unwrapOr("");
            if (!entry.isObject() || !hasText(id, 256) || std::find(ids.begin(), ids.end(), id) != ids.end()) return false;
            ids.push_back(id);
            for (auto key : {"hidden", "locked", "hasColor"}) if (entry.contains(key) && !entry[key].isBool()) return false;
            for (auto key : {"r", "g", "b", "layer", "zOrder"}) {
                if (entry.contains(key) && !entry[key].asInt()) return false;
            }
            for (auto key : {"x", "y", "scale", "scaleX", "scaleY", "rotation", "opacity", "width", "height", "cornerRadius"}) {
                if (!entry.contains(key)) continue;
                auto n = entry[key].asDouble();
                if (!n || !std::isfinite(n.unwrap()) || std::abs(n.unwrap()) > 10000.0) return false;
                if ((std::string_view(key).starts_with("scale") || std::string_view(key) == "width" || std::string_view(key) == "height")
                    && n.unwrap() < 0) return false;
                if (std::string_view(key).starts_with("scale") && n.unwrap() > 20) return false;
                if (std::string_view(key) == "opacity" && (n.unwrap() < 0 || n.unwrap() > 1)) return false;
            }
            // font resources are installed assets, never filesystem paths.
            auto font = entry["fontFile"].asString().unwrapOr("");
            if (font.find_first_of("/\\:") != std::string::npos || font.find("..") != std::string::npos) return false;
        }
        return true;
    };
    return validEntries(layout["buttons"], "key") && validEntries(layout["shapes"], "id");
}
}

char const* categoryId(Category category) {
    switch (category) {
        case Category::Layout: return "layout";
        case Category::RTX: return "rtx";
        case Category::BeatShaders: return "beat-shaders";
        case Category::Pack: return "pack";
    }
    return "layout";
}

char const* categoryName(Category category) {
    switch (category) {
        case Category::Layout: return "Custom Layout";
        case Category::RTX: return "Paimon RTX";
        case Category::BeatShaders: return "Beat Shaders";
        case Category::Pack: return "Pack";
    }
    return "Custom Layout";
}

geode::Result<Category> parseCategory(std::string_view id) {
    for (auto category : {Category::Layout, Category::RTX, Category::BeatShaders, Category::Pack}) {
        if (id == categoryId(category)) return geode::Ok(category);
    }
    return geode::Err("Categoria desconocida / Unknown category");
}

geode::Result<> validateConfiguration(Category category, matjson::Value const& configuration) {
    if (!configuration.isObject() || !boundedTree(configuration)) return geode::Err("Configuracion invalida / Invalid configuration");
    for (auto kind : {Category::Layout, Category::RTX, Category::BeatShaders}) {
        auto key = categoryId(kind);
        bool expected = category == Category::Pack || category == kind;
        if (configuration.contains(key) != expected) return geode::Err("La categoria no coincide con el contenido / Category mismatch");
        if (!expected) continue;
        auto const& part = configuration[key];
        if (!part.isObject()) return geode::Err("Configuracion incompleta / Incomplete configuration");
        if (part.contains("moduleEnabled") && !part["moduleEnabled"].isBool()) return geode::Err("Estado de modulo invalido / Invalid module state");
        if (kind == Category::Layout && !validLayout(part)) return geode::Err("Custom Layout invalido / Invalid Custom Layout");
        if (kind == Category::RTX) {
            if (part["schema"].asInt().unwrapOr(-1) != 6) return geode::Err("Version RTX incompatible / Incompatible RTX version");
            for (auto const& field : part) {
                if (!field.isBool() && !field.isNumber()) return geode::Err("Valor RTX invalido / Invalid RTX value");
                if (field.isNumber() && std::abs(field.asDouble().unwrap()) > 10000.0) return geode::Err("Valor RTX fuera de rango / RTX value out of range");
            }
            for (auto keyName : {"enabled", "adaptive", "ghostClamp", "adaptEnabled", "inGameplay", "inEditor", "inMenus", "skipWhenPaused"}) {
                if (part.contains(keyName) && !part[keyName].isBool()) return geode::Err("Estado RTX invalido / Invalid RTX state");
            }
        }
        if (kind == Category::BeatShaders) {
            if (!hasText(part["shaderName"].asString().unwrapOr(""), 64)) return geode::Err("Shader invalido / Invalid shader");
            for (auto keyName : {"intensity", "bassMult", "midMult", "trebleMult", "beatMult", "energyMult"}) {
                auto n = part[keyName].asDouble();
                if (!n || n.unwrap() < 0 || n.unwrap() > 3) return geode::Err("Valor de shader invalido / Invalid shader value");
            }
            if (part.contains("enabled") && !part["enabled"].isBool()) return geode::Err("Estado de shader invalido / Invalid shader state");
            if (part.contains("layers")) {
                if (!part["layers"].isObject()) return geode::Err("Capas invalidas / Invalid layers");
                for (auto const& enabled : part["layers"]) if (!enabled.isBool()) return geode::Err("Capa invalida / Invalid layer");
            }
        }
    }
    auto count = category == Category::Pack ? 3u : 1u;
    if (configuration.size() != count) return geode::Err("Contenido de configuracion desconocido / Unknown configuration content");
    return geode::Ok();
}

geode::Result<> validatePreview(std::vector<uint8_t> const& png) {
    constexpr std::array<uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (png.size() < 33 || png.size() > kMaxPreviewBytes || !std::equal(signature.begin(), signature.end(), png.begin())
        || std::string_view(reinterpret_cast<char const*>(png.data() + 12), 4) != "IHDR") {
        return geode::Err("Usa una imagen PNG de hasta 1 MB / Use a PNG up to 1 MB");
    }
    auto number = [&png](size_t offset) {
        return (uint32_t(png[offset]) << 24) | (uint32_t(png[offset + 1]) << 16) | (uint32_t(png[offset + 2]) << 8) | png[offset + 3];
    };
    if (number(16) == 0 || number(20) == 0 || number(16) > 1920 || number(20) > 1080) {
        return geode::Err("Preview: maximo 1920 x 1080 / Preview: maximum 1920 x 1080");
    }
    return geode::Ok();
}

geode::Result<> validatePreset(Preset const& preset) {
    if (!hasText(preset.title, 48) || !hasText(preset.author, 48) || preset.description.size() > 400
        || preset.description.find('\0') != std::string::npos || preset.accountID < 0 || preset.createdAt < 0) {
        return geode::Err("Revisa el nombre, autor y descripcion / Check title, author and description");
    }
    auto config = validateConfiguration(preset.category, preset.configuration);
    if (!config) return config;
    if (preset.configuration.dump().size() > 384 * 1024 || preset.preview.size() > (kMaxPreviewBytes + 2) / 3 * 4) {
        return geode::Err("Preset demasiado grande / Preset too large");
    }
    std::vector<uint8_t> png;
    if (!paimon::base64Decode(preset.preview, png)) return geode::Err("PNG invalido / Invalid PNG");
    return validatePreview(png);
}

geode::Result<matjson::Value> parseBoundedJson(std::string_view content) {
    if (content.empty() || content.size() > kMaxPresetBytes) return geode::Err("JSON demasiado grande o vacio / JSON too large or empty");
    // bound nesting before entering matjson's recursive parser. Braces inside
    // quoted strings do not count; malformed JSON is rejected by the parser.
    int depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (char c : content) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
            continue;
        }
        if (c == '"') quoted = true;
        else if (c == '/') return geode::Err("Usa JSON sin comentarios / Use JSON without comments");
        else if (c == '[' || c == '{') {
            if (++depth > 16) return geode::Err("JSON demasiado anidado / JSON nested too deeply");
        } else if (c == ']' || c == '}') {
            if (--depth < 0) return geode::Err("JSON invalido / Invalid JSON");
        }
    }
    auto parsed = matjson::parse(content);
    if (!parsed) return geode::Err("JSON invalido / Invalid JSON");
    return geode::Ok(parsed.unwrap());
}

matjson::Value presetToJson(Preset const& preset) {
    return matjson::makeObject({{"format", "paimon-shop"}, {"version", 1},
        {"title", preset.title}, {"description", preset.description}, {"author", preset.author},
        {"accountID", preset.accountID}, {"createdAt", preset.createdAt},
        {"category", categoryId(preset.category)}, {"configuration", preset.configuration}, {"preview", preset.preview}});
}

geode::Result<Preset> presetFromJson(matjson::Value const& value) {
    if (value["format"].asString().unwrapOr("") != "paimon-shop" || value["version"].asInt().unwrapOr(0) != 1) {
        return geode::Err("Formato o version incompatible / Incompatible format or version");
    }
    auto category = parseCategory(value["category"].asString().unwrapOr(""));
    if (!category) return geode::Err(category.unwrapErr());
    auto account = value["accountID"].asInt().unwrapOr(0);
    if (account < 0 || account > std::numeric_limits<int>::max()) return geode::Err("Cuenta invalida / Invalid account");
    Preset preset{value["title"].asString().unwrapOr(""), value["description"].asString().unwrapOr(""),
        value["author"].asString().unwrapOr(""), static_cast<int>(account), value["createdAt"].asInt().unwrapOr(0),
        category.unwrap(), value["configuration"], value["preview"].asString().unwrapOr("")};
    auto valid = validatePreset(preset);
    if (!valid) return geode::Err(valid.unwrapErr());
    return geode::Ok(std::move(preset));
}
} // namespace paimon::shop
