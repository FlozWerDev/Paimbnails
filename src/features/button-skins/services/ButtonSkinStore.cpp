#include "ButtonSkinStore.hpp"

#include "../../quick-hub/services/QuickHubManager.hpp"
#include "../../../utils/FilesystemPath.hpp"

#include <algorithm>
#include <functional>
#include <unordered_set>

using namespace geode::prelude;

namespace paimon::buttonskin {

namespace {

constexpr char const* kSavedKey = "button-skins";

matjson::Value colorToJson(ccColor3B c) {
    auto arr = matjson::Value::array();
    arr.push(matjson::Value(static_cast<int>(c.r)));
    arr.push(matjson::Value(static_cast<int>(c.g)));
    arr.push(matjson::Value(static_cast<int>(c.b)));
    return arr;
}

ccColor3B colorFromJson(matjson::Value const& v, ccColor3B fallback) {
    if (!v.isArray()) return fallback;
    auto arr = v.asArray();
    if (!arr.isOk() || arr.unwrap().size() != 3) return fallback;
    auto const& items = arr.unwrap();
    auto channel = [&](size_t i, GLubyte def) {
        return static_cast<GLubyte>(std::clamp<int64_t>(items[i].asInt().unwrapOr(def), 0, 255));
    };
    return {channel(0, fallback.r), channel(1, fallback.g), channel(2, fallback.b)};
}

} // namespace

matjson::Value skinToJson(ButtonSkin const& s) {
    auto o = matjson::Value::object();
    o["id"] = s.id;
    o["target"] = quickhub::customQuickButtonToJson(s.target);
    o["signature"] = s.signature;
    o["scope"] = static_cast<int>(s.scope);

    o["fill"] = static_cast<int>(s.fill);
    o["imagePath"] = s.imagePath;
    o["fit"] = static_cast<int>(s.fit);
    o["blend"] = static_cast<int>(s.blend);
    o["maskToShape"] = s.maskToShape;
    o["fillOpacity"] = s.fillOpacity;
    o["fillScale"] = s.fillScale;
    o["fillRotation"] = s.fillRotation;
    o["fillOffsetX"] = s.fillOffsetX;
    o["fillOffsetY"] = s.fillOffsetY;
    o["fillFlipX"] = s.fillFlipX;
    o["fillFlipY"] = s.fillFlipY;
    o["fillColor"] = colorToJson(s.fillColor);
    o["fillColorB"] = colorToJson(s.fillColorB);

    o["tintEnabled"] = s.tintEnabled;
    o["tint"] = colorToJson(s.tint);
    o["baseOpacity"] = s.baseOpacity;
    o["hideOriginal"] = s.hideOriginal;

    o["scale"] = s.scale;
    o["rotation"] = s.rotation;
    o["offsetX"] = s.offsetX;
    o["offsetY"] = s.offsetY;

    o["muteOriginal"] = s.muteOriginal;
    return o;
}

ButtonSkin skinFromJson(matjson::Value const& v) {
    ButtonSkin s;
    if (!v.isObject()) return s;
    auto num = [&](char const* key, float def) {
        return static_cast<float>(v[key].asDouble().unwrapOr(def));
    };
    auto integer = [&](char const* key, int def) {
        return static_cast<int>(v[key].asInt().unwrapOr(def));
    };
    auto flag = [&](char const* key, bool def) { return v[key].asBool().unwrapOr(def); };

    s.id = v["id"].asString().unwrapOr("");
    s.target = quickhub::customQuickButtonFromJson(v["target"]);
    s.signature = v["signature"].asString().unwrapOr("");
    s.scope = static_cast<SkinScope>(integer("scope", 0));

    s.fill = static_cast<SkinFill>(integer("fill", 0));
    s.imagePath = v["imagePath"].asString().unwrapOr("");
    s.fit = static_cast<SkinFit>(integer("fit", 0));
    s.blend = static_cast<SkinBlend>(integer("blend", 0));
    s.maskToShape = flag("maskToShape", true);
    s.fillOpacity = num("fillOpacity", 1.f);
    s.fillScale = num("fillScale", 1.f);
    s.fillRotation = num("fillRotation", 0.f);
    s.fillOffsetX = num("fillOffsetX", 0.f);
    s.fillOffsetY = num("fillOffsetY", 0.f);
    s.fillFlipX = flag("fillFlipX", false);
    s.fillFlipY = flag("fillFlipY", false);
    s.fillColor = colorFromJson(v["fillColor"], s.fillColor);
    s.fillColorB = colorFromJson(v["fillColorB"], s.fillColorB);

    s.tintEnabled = flag("tintEnabled", false);
    s.tint = colorFromJson(v["tint"], s.tint);
    s.baseOpacity = num("baseOpacity", 1.f);
    s.hideOriginal = flag("hideOriginal", false);

    s.scale = num("scale", 1.f);
    s.rotation = num("rotation", 0.f);
    s.offsetX = num("offsetX", 0.f);
    s.offsetY = num("offsetY", 0.f);

    s.muteOriginal = flag("muteOriginal", false);
    sanitize(s);
    return s;
}

size_t skinHash(ButtonSkin const& skin) {
    return std::hash<std::string>{}(skinToJson(skin).dump(matjson::NO_INDENTATION));
}

ButtonSkinStore& ButtonSkinStore::get() {
    static ButtonSkinStore instance;
    return instance;
}

std::filesystem::path ButtonSkinStore::imagesDir() {
    return paimon::paths::toStdPath(Mod::get()->getConfigDir()) / "button-skins" / "images";
}

void ButtonSkinStore::ensureLoaded() {
    if (m_loaded) return;
    m_loaded = true;
    m_skins.clear();

    auto saved = Mod::get()->getSavedValue<matjson::Value>(kSavedKey, matjson::Value());
    if (saved.isArray()) {
        if (auto arr = saved.asArray(); arr.isOk()) {
            for (auto const& entry : arr.unwrap()) {
                auto skin = skinFromJson(entry);
                if (skin.id.empty()) continue;
                m_skins.push_back(std::move(skin));
            }
        }
    }
    rehash();
}

void ButtonSkinStore::rehash() {
    m_hashes.clear();
    for (auto const& skin : m_skins) m_hashes[skin.id] = skinHash(skin);
}

void ButtonSkinStore::write() {
    auto arr = matjson::Value::array();
    for (auto const& skin : m_skins) arr.push(skinToJson(skin));
    Mod::get()->setSavedValue(kSavedKey, arr);
    rehash();
    ++m_revision;
}

std::vector<ButtonSkin> const& ButtonSkinStore::all() {
    ensureLoaded();
    return m_skins;
}

size_t ButtonSkinStore::hashOf(std::string const& id) {
    ensureLoaded();
    auto found = m_hashes.find(id);
    return found == m_hashes.end() ? 0 : found->second;
}

std::optional<ButtonSkin> ButtonSkinStore::find(std::string const& id) {
    ensureLoaded();
    for (auto const& skin : m_skins) {
        if (skin.id == id) return skin;
    }
    return std::nullopt;
}

bool ButtonSkinStore::save(ButtonSkin skin) {
    ensureLoaded();
    if (skin.id.empty()) return false;
    sanitize(skin);
    auto it = std::ranges::find(m_skins, skin.id, &ButtonSkin::id);
    if (it != m_skins.end()) {
        *it = std::move(skin);
    } else {
        m_skins.push_back(std::move(skin));
    }
    write();
    return true;
}

bool ButtonSkinStore::remove(std::string const& id) {
    ensureLoaded();
    auto it = std::ranges::find(m_skins, id, &ButtonSkin::id);
    if (it == m_skins.end()) return false;
    m_skins.erase(it);
    write();
    return true;
}

std::string ButtonSkinStore::makeUniqueId(std::string const& name) {
    ensureLoaded();
    std::unordered_set<std::string> used;
    for (auto const& skin : m_skins) used.insert(skin.id);

    std::string base = "skin:" + quickhub::slugify(name);
    std::string candidate = base;
    int n = 1;
    while (used.contains(candidate)) {
        candidate = base + "-" + std::to_string(++n);
    }
    return candidate;
}

} // namespace paimon::buttonskin
