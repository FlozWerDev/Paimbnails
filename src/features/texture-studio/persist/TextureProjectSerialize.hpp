#pragma once

#include "TextureProject.hpp"

#include <Geode/Geode.hpp>
#include <matjson.hpp>

#include <algorithm>

namespace paimon::texture_studio::serial {

inline matjson::Value colorToJson(cocos2d::ccColor3B c) {
    auto arr = matjson::Value::array();
    arr.push(static_cast<int>(c.r));
    arr.push(static_cast<int>(c.g));
    arr.push(static_cast<int>(c.b));
    return arr;
}

inline cocos2d::ccColor3B colorFromJson(matjson::Value const& v,
                                        cocos2d::ccColor3B fallback) {
    auto arr = v.asArray();
    if (arr.isErr()) return fallback;
    auto items = arr.unwrap();
    if (items.size() < 3) return fallback;
    auto r = items[0].asInt().unwrapOr(fallback.r);
    auto g = items[1].asInt().unwrapOr(fallback.g);
    auto b = items[2].asInt().unwrapOr(fallback.b);
    return cocos2d::ccColor3B{
        static_cast<std::uint8_t>(std::clamp<std::int64_t>(r, 0, 255)),
        static_cast<std::uint8_t>(std::clamp<std::int64_t>(g, 0, 255)),
        static_cast<std::uint8_t>(std::clamp<std::int64_t>(b, 0, 255)),
    };
}

}  // namespace paimon::texture_studio::serial


template <>
struct matjson::Serialize<paimon::texture_studio::ProjectSheetRef> {
    static matjson::Value toJson(paimon::texture_studio::ProjectSheetRef const& s) {
        auto obj = matjson::Value::object();
        obj["base"]    = s.baseName;
        obj["quality"] = s.qualitySuffix;
        obj["plist"]   = s.sourcePlistPath;
        obj["png"]     = s.sourcePngPath;
        return obj;
    }
    static geode::Result<paimon::texture_studio::ProjectSheetRef> fromJson(
        matjson::Value const& v) {
        paimon::texture_studio::ProjectSheetRef s;
        s.baseName        = v["base"].asString().unwrapOr("");
        s.qualitySuffix   = v["quality"].asString().unwrapOr("-uhd");
        s.sourcePlistPath = v["plist"].asString().unwrapOr("");
        s.sourcePngPath   = v["png"].asString().unwrapOr("");
        return geode::Ok(s);
    }
};

template <>
struct matjson::Serialize<paimon::texture_studio::ManualOverrideRef> {
    static matjson::Value toJson(paimon::texture_studio::ManualOverrideRef const& r) {
        auto obj = matjson::Value::object();
        obj["sprite"]   = r.spriteName;
        obj["w"]        = r.width;
        obj["h"]        = r.height;
        obj["version"]  = r.version;
        obj["modified"] = r.modifiedAt;
        return obj;
    }
    static geode::Result<paimon::texture_studio::ManualOverrideRef> fromJson(
        matjson::Value const& v) {
        paimon::texture_studio::ManualOverrideRef r;
        r.spriteName  = v["sprite"].asString().unwrapOr("");
        r.width       = static_cast<int>(v["w"].asInt().unwrapOr(0));
        r.height      = static_cast<int>(v["h"].asInt().unwrapOr(0));
        r.version     = static_cast<int>(v["version"].asInt().unwrapOr(1));
        r.modifiedAt  = static_cast<std::int64_t>(v["modified"].asInt().unwrapOr(0));
        return geode::Ok(r);
    }
};

template <>
struct matjson::Serialize<paimon::texture_studio::AutoCacheRef> {
    static matjson::Value toJson(paimon::texture_studio::AutoCacheRef const& r) {
        auto obj = matjson::Value::object();
        obj["sprite"]   = r.spriteName;
        // Hash stored signed; the sign bit is meaningless for hashing.
        obj["hash"]     = static_cast<std::int64_t>(r.spriteHash);
        obj["clusters"] = r.clusterCount;
        return obj;
    }
    static geode::Result<paimon::texture_studio::AutoCacheRef> fromJson(
        matjson::Value const& v) {
        paimon::texture_studio::AutoCacheRef r;
        r.spriteName   = v["sprite"].asString().unwrapOr("");
        r.spriteHash   = static_cast<std::uint64_t>(v["hash"].asInt().unwrapOr(0));
        r.clusterCount = static_cast<int>(v["clusters"].asInt().unwrapOr(0));
        return geode::Ok(r);
    }
};

template <>
struct matjson::Serialize<paimon::texture_studio::ImageTransform> {
    static matjson::Value toJson(paimon::texture_studio::ImageTransform const& t) {
        auto obj = matjson::Value::object();
        obj["fit"]      = static_cast<int>(t.fitMode);
        obj["scale"]    = t.scale;
        obj["offX"]     = t.offsetX;
        obj["offY"]     = t.offsetY;
        obj["rot"]      = t.rotationDeg;
        obj["opacity"]  = t.opacity;
        obj["flipX"]    = t.flipX;
        obj["flipY"]    = t.flipY;
        return obj;
    }

    static geode::Result<paimon::texture_studio::ImageTransform> fromJson(
        matjson::Value const& v) {
        using namespace paimon::texture_studio;
        ImageTransform t;
        t.fitMode = static_cast<ImageFitMode>(std::clamp<std::int64_t>(
            v["fit"].asInt().unwrapOr(0), 0, 2));
        t.scale       = static_cast<float>(v["scale"].asDouble().unwrapOr(1.0));
        t.offsetX     = static_cast<float>(v["offX"].asDouble().unwrapOr(0.0));
        t.offsetY     = static_cast<float>(v["offY"].asDouble().unwrapOr(0.0));
        t.rotationDeg = static_cast<float>(v["rot"].asDouble().unwrapOr(0.0));
        t.opacity     = static_cast<int>(std::clamp<std::int64_t>(
            v["opacity"].asInt().unwrapOr(255), 0, 255));
        t.flipX       = v["flipX"].asBool().unwrapOr(false);
        t.flipY       = v["flipY"].asBool().unwrapOr(false);
        return geode::Ok(t);
    }
};

template <>
struct matjson::Serialize<paimon::texture_studio::SpriteSetting> {
    static matjson::Value toJson(paimon::texture_studio::SpriteSetting const& s) {
        using namespace paimon::texture_studio;
        auto obj = matjson::Value::object();
        obj["skip"] = s.skip;
        obj["useCustomColors"] = s.useCustomColors;
        obj["hasCustomImage"] = s.hasCustomImage;
        obj["imageOverlay"] = s.imageOverlay;
        obj["color1"] = serial::colorToJson(s.color1);
        obj["color2"] = serial::colorToJson(s.color2);
        obj["colorGlow"] = serial::colorToJson(s.colorGlow);
        obj["colorDetail"] = serial::colorToJson(s.colorDetail);
        if (s.hasCustomImage && !s.imageTransform.isDefault()) {
            obj["imageTransform"] = matjson::Value(s.imageTransform);
        }
        return obj;
    }

    static geode::Result<paimon::texture_studio::SpriteSetting> fromJson(
        matjson::Value const& v) {
        using namespace paimon::texture_studio;
        SpriteSetting s;
        s.skip = v["skip"].asBool().unwrapOr(false);
        s.useCustomColors = v["useCustomColors"].asBool().unwrapOr(false);
        s.hasCustomImage = v["hasCustomImage"].asBool().unwrapOr(false);
        s.imageOverlay = v["imageOverlay"].asBool().unwrapOr(false);
        s.color1 = serial::colorFromJson(v["color1"], s.color1);
        s.color2 = serial::colorFromJson(v["color2"], s.color2);
        s.colorGlow = serial::colorFromJson(v["colorGlow"], s.colorGlow);
        s.colorDetail = serial::colorFromJson(v["colorDetail"], s.colorDetail);
        if (v["imageTransform"].isObject()) {
            if (auto t = v["imageTransform"].as<ImageTransform>(); t.isOk()) {
                s.imageTransform = t.unwrap();
            }
        }
        return geode::Ok(s);
    }
};

template <>
struct matjson::Serialize<paimon::texture_studio::TextureProject> {
    static matjson::Value toJson(paimon::texture_studio::TextureProject const& p) {
        using namespace paimon::texture_studio;
        auto obj = matjson::Value::object();
        obj["schemaVersion"] = p.schemaVersion;
        obj["id"]            = p.id;
        obj["name"]          = p.name;
        obj["author"]        = p.author;
        obj["createdAt"]     = p.createdAt;
        obj["modifiedAt"]    = p.modifiedAt;

        auto sheets = matjson::Value::array();
        for (auto const& s : p.sheets) {
            sheets.push(matjson::Value(s));
        }
        obj["sheets"] = sheets;
        obj["representativeFrame"] = p.representativeFrame;
        obj["representativeSheetIndex"] = p.representativeSheetIndex;

        obj["color1"]    = serial::colorToJson(p.color1);
        obj["color2"]    = serial::colorToJson(p.color2);
        obj["colorGlow"] = serial::colorToJson(p.colorGlow);
        obj["colorDetail"] = serial::colorToJson(p.colorDetail);
        obj["brightness"] = p.brightness;

        obj["maskSoftness"]     = p.maskSoftness;
        obj["clusterPrecision"] = p.clusterPrecision;
        obj["edgeCleanup"]      = p.edgeCleanup;
        obj["outlineProtect"]   = p.outlineProtect;
        obj["saturation"]       = p.saturation;
        obj["contrast"]         = p.contrast;

        obj["includeMediumPort"]     = p.includeMediumPort;
        obj["alternativeGlowOverlay"]= p.alternativeGlowOverlay;
        obj["transparentLists"]      = p.transparentLists;
        obj["colorGradientBg"]       = p.colorGradientBg;
        obj["colorMainMenu"]         = p.colorMainMenu;

        obj["usePackGenAssets"]   = p.usePackGenAssets;
        obj["tintGoldFont"]       = p.tintGoldFont;
        obj["colorGoldTitles"]    = p.colorGoldTitles;
        obj["colorDemonFaces"]    = p.colorDemonFaces;
        obj["mythicCompat"]       = p.mythicCompat;
        obj["includeModTextures"] = p.includeModTextures;

        auto overrides = matjson::Value::object();
        for (auto const& [k, v] : p.overrides) overrides[k] = matjson::Value(v);
        obj["overrides"] = overrides;

        auto autoCache = matjson::Value::object();
        for (auto const& [k, v] : p.autoCache) autoCache[k] = matjson::Value(v);
        obj["autoCache"] = autoCache;

        auto spriteSettings = matjson::Value::object();
        for (auto const& [k, setting] : p.spriteSettings) {
            spriteSettings[k] = matjson::Value(setting);
        }
        obj["spriteSettings"] = spriteSettings;
        obj["tintScope"] = static_cast<int>(p.tintScope);

        obj["hasBuiltOnce"]   = p.hasBuiltOnce;
        obj["lastBuiltAt"]    = p.lastBuiltAt;
        obj["lastZipRelPath"] = p.lastZipRelPath;
        return obj;
    }

    static geode::Result<paimon::texture_studio::TextureProject> fromJson(
        matjson::Value const& v) {
        using namespace paimon::texture_studio;
        TextureProject p;
        p.schemaVersion = static_cast<int>(v["schemaVersion"].asInt().unwrapOr(1));
        p.id            = v["id"].asString().unwrapOr("");
        p.name          = v["name"].asString().unwrapOr("");
        p.author        = v["author"].asString().unwrapOr("");
        p.createdAt     = static_cast<std::int64_t>(v["createdAt"].asInt().unwrapOr(0));
        p.modifiedAt    = static_cast<std::int64_t>(v["modifiedAt"].asInt().unwrapOr(0));

        if (auto sheetsArr = v["sheets"].asArray(); sheetsArr.isOk()) {
            for (auto const& s : sheetsArr.unwrap()) {
                auto entry = s.as<ProjectSheetRef>();
                if (entry.isOk()) p.sheets.push_back(entry.unwrap());
            }
        }
        p.representativeFrame = v["representativeFrame"].asString().unwrapOr("");
        p.representativeSheetIndex = static_cast<int>(
            v["representativeSheetIndex"].asInt().unwrapOr(-1));

        p.color1     = serial::colorFromJson(v["color1"],     p.color1);
        p.color2     = serial::colorFromJson(v["color2"],     p.color2);
        p.colorGlow  = serial::colorFromJson(v["colorGlow"],  p.colorGlow);
        p.colorDetail = serial::colorFromJson(v["colorDetail"], p.colorDetail);
        p.brightness = static_cast<int>(v["brightness"].asInt().unwrapOr(160));

        p.maskSoftness = static_cast<float>(
            v["maskSoftness"].asDouble().unwrapOr(0.35));
        p.clusterPrecision = static_cast<int>(std::clamp<std::int64_t>(
            v["clusterPrecision"].asInt().unwrapOr(5), 2, 10));
        p.edgeCleanup = static_cast<int>(std::clamp<std::int64_t>(
            v["edgeCleanup"].asInt().unwrapOr(1), 0, 4));
        p.outlineProtect = static_cast<int>(std::clamp<std::int64_t>(
            v["outlineProtect"].asInt().unwrapOr(0), 0, 255));
        p.saturation = static_cast<float>(
            v["saturation"].asDouble().unwrapOr(1.0));
        p.contrast = static_cast<float>(
            v["contrast"].asDouble().unwrapOr(0.0));

        p.includeMediumPort     = v["includeMediumPort"].asBool().unwrapOr(false);
        p.alternativeGlowOverlay= v["alternativeGlowOverlay"].asBool().unwrapOr(false);
        p.transparentLists      = v["transparentLists"].asBool().unwrapOr(false);
        p.colorGradientBg       = v["colorGradientBg"].asBool().unwrapOr(false);
        p.colorMainMenu         = v["colorMainMenu"].asBool().unwrapOr(false);

        p.usePackGenAssets   = v["usePackGenAssets"].asBool().unwrapOr(true);
        p.tintGoldFont       = v["tintGoldFont"].asBool().unwrapOr(false);
        p.colorGoldTitles    = v["colorGoldTitles"].asBool().unwrapOr(false);
        p.colorDemonFaces    = v["colorDemonFaces"].asBool().unwrapOr(false);
        p.mythicCompat       = v["mythicCompat"].asBool().unwrapOr(false);
        p.includeModTextures = v["includeModTextures"].asBool().unwrapOr(true);

        if (v["overrides"].isObject()) {
            for (auto const& val : v["overrides"]) {
                auto entry = val.as<ManualOverrideRef>();
                if (entry.isOk()) {
                    auto refVal = entry.unwrap();
                    auto key = val.getKey().value_or(std::string{});
                    if (refVal.spriteName.empty()) refVal.spriteName = key;
                    if (key.empty()) key = refVal.spriteName;
                    if (!key.empty()) p.overrides.emplace(key, std::move(refVal));
                }
            }
        }
        if (v["autoCache"].isObject()) {
            for (auto const& val : v["autoCache"]) {
                auto entry = val.as<AutoCacheRef>();
                if (entry.isOk()) {
                    auto refVal = entry.unwrap();
                    auto key = val.getKey().value_or(std::string{});
                    if (refVal.spriteName.empty()) refVal.spriteName = key;
                    if (key.empty()) key = refVal.spriteName;
                    if (!key.empty()) p.autoCache.emplace(key, std::move(refVal));
                }
            }
        }
        if (v["spriteSettings"].isObject()) {
            for (auto const& val : v["spriteSettings"]) {
                auto setting = val.as<SpriteSetting>();
                auto key = val.getKey().value_or(std::string{});
                if (setting && !key.empty()) {
                    p.spriteSettings.emplace(std::move(key), setting.unwrap());
                }
            }
        }
        p.tintScope = static_cast<TintScope>(std::clamp<std::int64_t>(
            v["tintScope"].asInt().unwrapOr(0), 0, 2));

        p.hasBuiltOnce   = v["hasBuiltOnce"].asBool().unwrapOr(false);
        p.lastBuiltAt    = static_cast<std::int64_t>(v["lastBuiltAt"].asInt().unwrapOr(0));
        p.lastZipRelPath = v["lastZipRelPath"].asString().unwrapOr("");
        return geode::Ok(std::move(p));
    }
};
