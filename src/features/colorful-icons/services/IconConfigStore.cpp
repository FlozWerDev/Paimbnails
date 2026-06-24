#include "IconConfigStore.hpp"

#include <Geode/Geode.hpp>
#include <matjson.hpp>

#include <utility>

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

}  // anonymous namespace

template <>
struct matjson::Serialize<paimon::icons::GamemodePalette> {
    static matjson::Value toJson(paimon::icons::GamemodePalette const& p) {
        auto obj = matjson::Value::object();
        obj["c1"] = colorToJson(p.color1);
        obj["c2"] = colorToJson(p.color2);
        obj["cg"] = colorToJson(p.glow);
        return obj;
    }
    static geode::Result<paimon::icons::GamemodePalette> fromJson(matjson::Value const& v) {
        paimon::icons::GamemodePalette p;
        p.color1 = colorFromJson(v["c1"], p.color1);
        p.color2 = colorFromJson(v["c2"], p.color2);
        p.glow   = colorFromJson(v["cg"], p.glow);
        return Ok(p);
    }
};

template <>
struct matjson::Serialize<paimon::icons::PresetEntry> {
    static matjson::Value toJson(paimon::icons::PresetEntry const& p) {
        auto obj = matjson::Value::object();
        obj["name"] = p.name;
        obj["data"] = p.serialized;
        return obj;
    }
    static geode::Result<paimon::icons::PresetEntry> fromJson(matjson::Value const& v) {
        paimon::icons::PresetEntry p;
        p.name       = v["name"].asString().unwrapOr("");
        p.serialized = v["data"].asString().unwrapOr("");
        return Ok(p);
    }
};

template <>
struct matjson::Serialize<paimon::icons::PaimonIconConfig> {
    static matjson::Value toJson(paimon::icons::PaimonIconConfig const& c) {
        auto obj = matjson::Value::object();
        obj["schema"]            = c.schemaVersion;
        obj["mode"]              = static_cast<int>(c.mode);
        obj["custom1"]           = colorToJson(c.custom1);
        obj["custom2"]           = colorToJson(c.custom2);
        obj["customGlow"]        = colorToJson(c.customGlow);
        obj["hueShift"]          = c.hueShiftDegrees;
        obj["satMul"]            = c.saturationMul;
        obj["valMul"]            = c.brightnessMul;
        obj["gradStart"]         = colorToJson(c.gradientStart);
        obj["gradEnd"]           = colorToJson(c.gradientEnd);
        obj["mono"]              = colorToJson(c.monochromeBase);
        obj["randPalette"]       = static_cast<int>(c.randomPalette);
        obj["rainbowSpeed"]      = c.rainbowSpeed;
        obj["rainbowSpread"]     = c.rainbowSpread;

        auto perMode = matjson::Value::array();
        for (auto const& slot : c.perMode) {
            if (slot) {
                perMode.push(matjson::Serialize<paimon::icons::GamemodePalette>::toJson(*slot));
            } else {
                perMode.push(matjson::Value(nullptr));
            }
        }
        obj["perMode"]           = perMode;
        obj["lockStyle"]         = static_cast<int>(c.lockStyle);
        obj["dimOpacity"]        = c.dimOpacity;
        obj["dimUnobtainable"]   = c.dimUnobtainable;
        obj["unobtainableOpacity"] = c.unobtainableOpacity;
        obj["unobtainableTint"]    = colorToJson(c.unobtainableTint);
        obj["lockTint"]            = colorToJson(c.lockTint);
        obj["silhouetteColor"]     = colorToJson(c.silhouetteColor);
        obj["pulseLocked"]       = c.anim.pulseLocked;
        obj["pulseSpeed"]        = c.anim.pulseSpeed;
        obj["hoverGlow"]         = c.anim.hoverGlow;
        obj["idleFloat"]         = c.anim.idleFloat;
        obj["floatAmount"]       = c.anim.floatAmount;
        obj["selectedHighlight"] = c.anim.selectedHighlight;
        obj["highlightColor"]    = colorToJson(c.anim.highlightColor);
        obj["beatSync"]          = c.anim.beatSync;
        obj["applyKit"]          = c.apply.kit;
        obj["applyShops"]        = c.apply.shops;
        obj["applyAchievements"] = c.apply.achievements;
        obj["applyRewards"]      = c.apply.rewards;
        obj["applyProfiles"]     = c.apply.profiles;
        obj["applyComments"]     = c.apply.comments;
        obj["applyLevelCells"]   = c.apply.levelCells;
        auto presets = matjson::Value::array();
        for (auto const& p : c.presets) {
            presets.push(matjson::Serialize<paimon::icons::PresetEntry>::toJson(p));
        }
        obj["presets"]           = presets;
        return obj;
    }

    static geode::Result<paimon::icons::PaimonIconConfig> fromJson(matjson::Value const& v) {
        paimon::icons::PaimonIconConfig c;
        c.schemaVersion = v["schema"].asInt().unwrapOr(1);
        c.mode             = static_cast<paimon::icons::ColorMode>(
            v["mode"].asInt().unwrapOr(static_cast<int>(c.mode)));
        c.custom1          = colorFromJson(v["custom1"], c.custom1);
        c.custom2          = colorFromJson(v["custom2"], c.custom2);
        c.customGlow       = colorFromJson(v["customGlow"], c.customGlow);
        c.hueShiftDegrees  = static_cast<float>(v["hueShift"].asDouble().unwrapOr(0.0));
        c.saturationMul    = static_cast<float>(v["satMul"].asDouble().unwrapOr(1.0));
        c.brightnessMul    = static_cast<float>(v["valMul"].asDouble().unwrapOr(1.0));
        c.gradientStart    = colorFromJson(v["gradStart"], c.gradientStart);
        c.gradientEnd      = colorFromJson(v["gradEnd"], c.gradientEnd);
        c.monochromeBase   = colorFromJson(v["mono"], c.monochromeBase);
        c.randomPalette    = static_cast<paimon::icons::RandomPalette>(
            v["randPalette"].asInt().unwrapOr(static_cast<int>(c.randomPalette)));
        c.rainbowSpeed     = static_cast<float>(v["rainbowSpeed"].asDouble().unwrapOr(1.0));
        c.rainbowSpread    = static_cast<float>(v["rainbowSpread"].asDouble().unwrapOr(60.0));

        if (auto arr = v["perMode"].asArray(); arr.isOk()) {
            auto items = arr.unwrap();
            for (size_t i = 0; i < items.size() && i < c.perMode.size(); ++i) {
                if (items[i].isNull()) {
                    c.perMode[i].reset();
                } else {
                    auto pal = matjson::Serialize<paimon::icons::GamemodePalette>::fromJson(items[i]);
                    if (pal.isOk()) c.perMode[i] = pal.unwrap();
                }
            }
        }

        c.lockStyle        = static_cast<paimon::icons::LockStyle>(
            v["lockStyle"].asInt().unwrapOr(static_cast<int>(c.lockStyle)));
        c.dimOpacity         = static_cast<int>(v["dimOpacity"].asInt().unwrapOr(c.dimOpacity));
        c.dimUnobtainable    = v["dimUnobtainable"].asBool().unwrapOr(c.dimUnobtainable);
        c.unobtainableOpacity = static_cast<int>(v["unobtainableOpacity"].asInt().unwrapOr(c.unobtainableOpacity));
        c.unobtainableTint   = colorFromJson(v["unobtainableTint"], c.unobtainableTint);
        c.lockTint           = colorFromJson(v["lockTint"], c.lockTint);
        c.silhouetteColor    = colorFromJson(v["silhouetteColor"], c.silhouetteColor);

        c.anim.pulseLocked       = v["pulseLocked"].asBool().unwrapOr(c.anim.pulseLocked);
        c.anim.pulseSpeed        = static_cast<float>(v["pulseSpeed"].asDouble().unwrapOr(c.anim.pulseSpeed));
        c.anim.hoverGlow         = v["hoverGlow"].asBool().unwrapOr(c.anim.hoverGlow);
        c.anim.idleFloat         = v["idleFloat"].asBool().unwrapOr(c.anim.idleFloat);
        c.anim.floatAmount       = static_cast<float>(v["floatAmount"].asDouble().unwrapOr(c.anim.floatAmount));
        c.anim.selectedHighlight = v["selectedHighlight"].asBool().unwrapOr(c.anim.selectedHighlight);
        c.anim.highlightColor    = colorFromJson(v["highlightColor"], c.anim.highlightColor);
        c.anim.beatSync          = v["beatSync"].asBool().unwrapOr(c.anim.beatSync);

        c.apply.kit          = v["applyKit"].asBool().unwrapOr(c.apply.kit);
        c.apply.shops        = v["applyShops"].asBool().unwrapOr(c.apply.shops);
        c.apply.achievements = v["applyAchievements"].asBool().unwrapOr(c.apply.achievements);
        c.apply.rewards      = v["applyRewards"].asBool().unwrapOr(c.apply.rewards);
        c.apply.profiles     = v["applyProfiles"].asBool().unwrapOr(c.apply.profiles);
        c.apply.comments     = v["applyComments"].asBool().unwrapOr(c.apply.comments);
        c.apply.levelCells   = v["applyLevelCells"].asBool().unwrapOr(c.apply.levelCells);

        if (auto arr = v["presets"].asArray(); arr.isOk()) {
            auto items = arr.unwrap();
            c.presets.clear();
            c.presets.reserve(items.size());
            for (auto const& pv : items) {
                auto p = matjson::Serialize<paimon::icons::PresetEntry>::fromJson(pv);
                if (p.isOk()) c.presets.push_back(p.unwrap());
            }
        }
        return Ok(c);
    }
};

namespace paimon::icons {

namespace {
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
    persist();
    IconConfigChangedEvent("").send();
}

void IconConfigStore::notifyChangedExternally() {
    if (!m_loaded) load();
    IconConfigChangedEvent("").send();
}

bool IconConfigStore::isFeatureEnabled() const {
    return Mod::get()->getSettingValue<bool>("colorful-icons-enabled");
}

bool IconConfigStore::saveCurrentAsPreset(std::string const& name) {
    if (name.empty()) return false;
    if (!m_loaded) load();

    // Serialize the config minus the presets list itself, to avoid
    // exponential nesting.
    PaimonIconConfig snapshot = m_config;
    snapshot.presets.clear();
    auto data = matjson::Serialize<PaimonIconConfig>::toJson(snapshot).dump();

    auto it = std::ranges::find_if(m_config.presets,
        [&](PresetEntry const& p) { return p.name == name; });
    if (it != m_config.presets.end()) {
        it->serialized = std::move(data);
    } else {
        m_config.presets.push_back({name, std::move(data)});
    }
    persist();
    IconConfigChangedEvent("").send();
    return true;
}

bool IconConfigStore::loadPreset(std::string const& name) {
    if (!m_loaded) load();
    auto it = std::ranges::find_if(m_config.presets,
        [&](PresetEntry const& p) { return p.name == name; });
    if (it == m_config.presets.end()) return false;

    auto parsed = matjson::Value::parse(it->serialized);
    if (parsed.isErr()) return false;
    auto cfg = matjson::Serialize<PaimonIconConfig>::fromJson(parsed.unwrap());
    if (cfg.isErr()) return false;

    auto snapshot = cfg.unwrap();
    // Don't replace the preset list with the (empty) saved snapshot's list.
    snapshot.presets = m_config.presets;
    m_config = std::move(snapshot);

    persist();
    IconConfigChangedEvent("").send();
    return true;
}

bool IconConfigStore::deletePreset(std::string const& name) {
    if (!m_loaded) load();
    auto before = m_config.presets.size();
    std::erase_if(m_config.presets,
        [&](PresetEntry const& p) { return p.name == name; });
    if (m_config.presets.size() == before) return false;
    persist();
    IconConfigChangedEvent("").send();
    return true;
}

std::string IconConfigStore::exportToString() const {
    return matjson::Serialize<PaimonIconConfig>::toJson(m_config).dump();
}

bool IconConfigStore::importFromString(std::string const& serialized) {
    auto parsed = matjson::Value::parse(serialized);
    if (parsed.isErr()) return false;
    auto cfg = matjson::Serialize<PaimonIconConfig>::fromJson(parsed.unwrap());
    if (cfg.isErr()) return false;
    m_config = cfg.unwrap();
    m_loaded = true;
    persist();
    IconConfigChangedEvent("").send();
    return true;
}

}  // namespace paimon::icons
