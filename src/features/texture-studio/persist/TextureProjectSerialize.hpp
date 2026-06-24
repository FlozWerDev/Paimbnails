#pragma once
//
// TextureProjectSerialize.hpp - matjson::Serialize specialisations that
// teach Geode how to convert a TextureProject (and its nested types) to
// and from matjson::Value.
//
// Pattern follows IconConfigStore.cpp: include this header in the .cpp
// that needs to write/read project.json. The matjson::Serialize<> template
// resolves at the call site and pulls the conversion in.
//
// Why a separate header instead of inline in TextureProject.hpp?
//   - matjson is a fairly heavy header. Many consumers of TextureProject
//     (e.g. UI code that just reads the name and modifiedAt) don't need
//     serialization, and shouldn't pay the compile cost.
//   - Multiple .cpp files needing serialization can include this header
//     without ODR conflicts (specialisations are inline-friendly).
//

#include "TextureProject.hpp"

#include <Geode/Geode.hpp>
#include <matjson.hpp>

#include <algorithm>

namespace paimon::texture_studio::serial {

// Local helpers (exposed in the namespace for unit tests if needed).

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


// matjson specialisations

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
        // matjson stores ints as 64-bit signed; we store the hash as
        // signed because the sign bit is meaningless for hashing.
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
struct matjson::Serialize<paimon::texture_studio::SpriteSetting> {
    static matjson::Value toJson(paimon::texture_studio::SpriteSetting const& s) {
        using namespace paimon::texture_studio;
        auto obj = matjson::Value::object();
        obj["skip"] = s.skip;
        obj["useCustomColors"] = s.useCustomColors;
        obj["hasCustomImage"] = s.hasCustomImage;
        obj["color1"] = serial::colorToJson(s.color1);
        obj["color2"] = serial::colorToJson(s.color2);
        obj["colorGlow"] = serial::colorToJson(s.colorGlow);
        return obj;
    }

    static geode::Result<paimon::texture_studio::SpriteSetting> fromJson(
        matjson::Value const& v) {
        using namespace paimon::texture_studio;
        SpriteSetting s;
        s.skip = v["skip"].asBool().unwrapOr(false);
        s.useCustomColors = v["useCustomColors"].asBool().unwrapOr(false);
        s.hasCustomImage = v["hasCustomImage"].asBool().unwrapOr(false);
        s.color1 = serial::colorFromJson(v["color1"], s.color1);
        s.color2 = serial::colorFromJson(v["color2"], s.color2);
        s.colorGlow = serial::colorFromJson(v["colorGlow"], s.colorGlow);
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
        obj["brightness"] = p.brightness;

        obj["includeMediumPort"]     = p.includeMediumPort;
        obj["alternativeGlowOverlay"]= p.alternativeGlowOverlay;
        obj["transparentLists"]      = p.transparentLists;
        obj["colorGradientBg"]       = p.colorGradientBg;
        obj["colorMainMenu"]         = p.colorMainMenu;

        // overrides + autoCache: store as object keyed by sprite name.
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
        p.brightness = static_cast<int>(v["brightness"].asInt().unwrapOr(160));

        p.includeMediumPort     = v["includeMediumPort"].asBool().unwrapOr(false);
        p.alternativeGlowOverlay= v["alternativeGlowOverlay"].asBool().unwrapOr(false);
        p.transparentLists      = v["transparentLists"].asBool().unwrapOr(false);
        p.colorGradientBg       = v["colorGradientBg"].asBool().unwrapOr(false);
        p.colorMainMenu         = v["colorMainMenu"].asBool().unwrapOr(false);

        // overrides / autoCache come back as objects keyed by sprite name.
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
