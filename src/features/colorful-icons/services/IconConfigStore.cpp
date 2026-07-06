#include "IconConfigStore.hpp"

#include <Geode/Geode.hpp>
#include <matjson.hpp>

#include <algorithm>
#include <cstdint>

using namespace geode::prelude;

namespace {

// matjson requires a lot of boilerplate; this keeps it tidy.
matjson::Value colorToJson(cocos2d::ccColor3B c) {
    auto arr = matjson::Value::array();
    arr.push(static_cast<int>(c.r));
    arr.push(static_cast<int>(c.g));
    arr.push(static_cast<int>(c.b));
    return arr;
}

cocos2d::ccColor3B colorFromJson(matjson::Value const& v, cocos2d::ccColor3B fallback) {
    auto arr = v.asArray();
    if (arr.isErr()) return fallback;
    auto items = arr.unwrap();
    if (items.size() < 3) return fallback;
    auto r = items[0].asInt().unwrapOr(fallback.r);
    auto g = items[1].asInt().unwrapOr(fallback.g);
    auto b = items[2].asInt().unwrapOr(fallback.b);
    return cocos2d::ccColor3B{
        static_cast<std::uint8_t>(std::clamp<int>(r, 0, 255)),
        static_cast<std::uint8_t>(std::clamp<int>(g, 0, 255)),
        static_cast<std::uint8_t>(std::clamp<int>(b, 0, 255)),
    };
}

// Old saves may hold modes/styles that the redesign removed; snap them to the
// closest surviving value so the enums never carry an out-of-range int.
paimon::icons::ColorMode sanitizeMode(int v) {
    using paimon::icons::ColorMode;
    switch (v) {
        case 0: case 1: case 2: case 4: case 5: case 6: case 8: case 9:
            return static_cast<ColorMode>(v);
        case 3:  return ColorMode::HueShift;  // legacy SatBoost
        default: return ColorMode::Player;    // legacy PerGamemode & unknown
    }
}

paimon::icons::LockStyle sanitizeLockStyle(int v) {
    using paimon::icons::LockStyle;
    switch (v) {
        case 0: case 1: case 2: case 3: case 5:
            return static_cast<LockStyle>(v);
        case 4:  return LockStyle::ShowDimmed;  // legacy CustomMix
        default: return LockStyle::Default;
    }
}

paimon::icons::RandomPalette sanitizePalette(int v) {
    // 4 was the legacy Monoschemed palette.
    return v >= 0 && v <= 3
        ? static_cast<paimon::icons::RandomPalette>(v)
        : paimon::icons::RandomPalette::Vibrant;
}

}  // anonymous namespace

template <>
struct matjson::Serialize<paimon::icons::PaimonIconConfig> {
    static matjson::Value toJson(paimon::icons::PaimonIconConfig const& c) {
        auto obj = matjson::Value::object();
        obj["schema"]        = c.schemaVersion;
        obj["mode"]          = static_cast<int>(c.mode);
        obj["custom1"]       = colorToJson(c.custom1);
        obj["custom2"]       = colorToJson(c.custom2);
        obj["customGlow"]    = colorToJson(c.customGlow);
        obj["hueShift"]      = c.hueShiftDegrees;
        obj["gradStart"]     = colorToJson(c.gradientStart);
        obj["gradEnd"]       = colorToJson(c.gradientEnd);
        obj["mono"]          = colorToJson(c.monochromeBase);
        obj["randPalette"]   = static_cast<int>(c.randomPalette);
        obj["rainbowSpeed"]  = c.rainbowSpeed;
        obj["rainbowSpread"] = c.rainbowSpread;

        obj["lockStyle"]           = static_cast<int>(c.lockStyle);
        obj["dimOpacity"]          = c.dimOpacity;
        obj["dimUnobtainable"]     = c.dimUnobtainable;
        obj["unobtainableOpacity"] = c.unobtainableOpacity;
        obj["unobtainableTint"]    = colorToJson(c.unobtainableTint);
        obj["lockTint"]            = colorToJson(c.lockTint);
        obj["silhouetteColor"]     = colorToJson(c.silhouetteColor);

        obj["applyKit"]   = c.apply.kit;
        obj["applyShops"] = c.apply.shops;
        return obj;
    }

    static geode::Result<paimon::icons::PaimonIconConfig> fromJson(matjson::Value const& v) {
        paimon::icons::PaimonIconConfig c;
        c.schemaVersion   = v["schema"].asInt().unwrapOr(1);
        c.mode            = sanitizeMode(static_cast<int>(
            v["mode"].asInt().unwrapOr(static_cast<int>(c.mode))));
        c.custom1         = colorFromJson(v["custom1"], c.custom1);
        c.custom2         = colorFromJson(v["custom2"], c.custom2);
        c.customGlow      = colorFromJson(v["customGlow"], c.customGlow);
        c.hueShiftDegrees = std::clamp(
            static_cast<float>(v["hueShift"].asDouble().unwrapOr(0.0)), 0.0f, 360.0f);
        c.gradientStart   = colorFromJson(v["gradStart"], c.gradientStart);
        c.gradientEnd     = colorFromJson(v["gradEnd"], c.gradientEnd);
        c.monochromeBase  = colorFromJson(v["mono"], c.monochromeBase);
        c.randomPalette   = sanitizePalette(static_cast<int>(
            v["randPalette"].asInt().unwrapOr(static_cast<int>(c.randomPalette))));
        c.rainbowSpeed    = std::clamp(
            static_cast<float>(v["rainbowSpeed"].asDouble().unwrapOr(1.0)), 0.1f, 3.0f);
        c.rainbowSpread   = std::clamp(
            static_cast<float>(v["rainbowSpread"].asDouble().unwrapOr(60.0)), 0.0f, 180.0f);

        c.lockStyle           = sanitizeLockStyle(static_cast<int>(
            v["lockStyle"].asInt().unwrapOr(static_cast<int>(c.lockStyle))));
        c.dimOpacity          = std::clamp(static_cast<int>(
            v["dimOpacity"].asInt().unwrapOr(c.dimOpacity)), 0, 255);
        c.dimUnobtainable     = v["dimUnobtainable"].asBool().unwrapOr(c.dimUnobtainable);
        c.unobtainableOpacity = std::clamp(static_cast<int>(
            v["unobtainableOpacity"].asInt().unwrapOr(c.unobtainableOpacity)), 0, 255);
        c.unobtainableTint    = colorFromJson(v["unobtainableTint"], c.unobtainableTint);
        c.lockTint            = colorFromJson(v["lockTint"], c.lockTint);
        c.silhouetteColor     = colorFromJson(v["silhouetteColor"], c.silhouetteColor);

        c.apply.kit   = v["applyKit"].asBool().unwrapOr(c.apply.kit);
        c.apply.shops = v["applyShops"].asBool().unwrapOr(c.apply.shops);
        return Ok(c);
    }
};

namespace paimon::icons {

namespace {
// Kept from before the redesign: old saves simply carry extra keys we ignore.
constexpr char const* kSaveKey = "paimon-icons.config.v1";
}  // namespace

IconConfigStore& IconConfigStore::get() {
    static IconConfigStore instance;
    return instance;
}

IconConfigStore::IconConfigStore() = default;

void IconConfigStore::load() {
    if (m_loaded) return;
    auto stored = Mod::get()->getSavedValue<PaimonIconConfig>(kSaveKey, m_config);
    m_config = std::move(stored);
    m_loaded = true;
}

void IconConfigStore::persist() {
    Mod::get()->setSavedValue(kSaveKey, m_config);
}

void IconConfigStore::update(std::function<void(PaimonIconConfig&)> const& mutator) {
    if (!m_loaded) load();
    mutator(m_config);
    persist();
    IconConfigChangedEvent("").send();
}

void IconConfigStore::resetToDefaults() {
    m_config = PaimonIconConfig{};
    m_loaded = true;
    persist();
    IconConfigChangedEvent("").send();
}

bool IconConfigStore::isFeatureEnabled() const {
    return Mod::get()->getSettingValue<bool>("colorful-icons-enabled");
}

void IconConfigStore::setFeatureEnabled(bool enabled) {
    Mod::get()->setSettingValue<bool>("colorful-icons-enabled", enabled);
    IconConfigChangedEvent("").send();
}

}  // namespace paimon::icons
