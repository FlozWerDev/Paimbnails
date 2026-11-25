#include "QuickHubManager.hpp"
#include "../data/QuickHubCategories.hpp"
#include "../../../utils/JsonHelper.hpp"
#include <matjson.hpp>
#include <algorithm>
#include <cmath>
#include <unordered_set>

using namespace geode::prelude;

namespace paimon::quickhub {

std::vector<std::string> QuickHubManager::getActiveOptions() const {
    auto saved = Mod::get()->getSavedValue<matjson::Value>(kSavedKey, matjson::Value());

    if (!saved.isArray()) {
        return getDefaultRadialOrder();
    }

    auto arrRes = saved.asArray();
    if (!arrRes.isOk()) {
        return getDefaultRadialOrder();
    }

    auto arr = arrRes.unwrap();
    if (arr.empty()) {
        return getDefaultRadialOrder();
    }

    std::vector<std::string> result;
    for (auto const& item : arr) {
        auto strRes = item.asString();
        if (strRes.isOk()) {
            result.push_back(strRes.unwrap());
        }
    }

    if (result.empty()) {
        return getDefaultRadialOrder();
    }

    return result;
}

void QuickHubManager::setActiveOptions(std::vector<std::string> const& options) {
    auto arr = matjson::Value::array();
    for (auto const& id : options) {
        arr.push(matjson::Value(id));
    }
    Mod::get()->setSavedValue(kSavedKey, arr);
}

void QuickHubManager::resetToDefault() {
    setActiveOptions(getDefaultRadialOrder());
}

static constexpr char const* kCustomButtonsKey = "quick-hub-custom-buttons";

CustomQuickButton customQuickButtonFromJson(matjson::Value const& v) {
    CustomQuickButton b;
    if (!v.isObject()) return b;
    b.id            = paimon::json::stringOr(v["id"]);
    b.name          = paimon::json::stringOr(v["name"]);
    b.icon          = paimon::json::stringOr(v["icon"]);
    b.labelText     = paimon::json::stringOr(v["labelText"]);
    b.targetNodeId  = paimon::json::stringOr(v["targetNodeId"]);
    b.parentId      = paimon::json::stringOr(v["parentId"]);
    b.ownerClass    = paimon::json::stringOr(v["ownerClass"]);
    b.sceneClass    = paimon::json::stringOr(v["sceneClass"]);
    b.itemClass     = paimon::json::stringOr(v["itemClass"]);
    b.listenerClass = paimon::json::stringOr(v["listenerClass"]);
    b.relX          = paimon::json::floatOr(v["relX"], -1.f);
    b.relY          = paimon::json::floatOr(v["relY"], -1.f);
    b.tag           = paimon::json::integerOr<int>(v["tag"]);
    b.shape         = static_cast<RadialButtonShape>(paimon::json::integerOr<int>(v["shape"]));
    // skin/sfx: missing keys = old defaults.
    b.imagePath     = paimon::json::stringOr(v["imagePath"]);
    b.imageScale    = paimon::json::floatOr(v["imageScale"], 1.f);
    b.imageRotation = paimon::json::floatOr(v["imageRotation"]);
    b.imageFlipX    = v["imageFlipX"].asBool().unwrapOr(false);
    b.imageFlipY    = v["imageFlipY"].asBool().unwrapOr(false);
    b.sfxKind       = paimon::json::integerOr<int>(v["sfxKind"]);
    b.sfxPath       = paimon::json::stringOr(v["sfxPath"]);
    b.sfxId         = paimon::json::integerOr<int>(v["sfxId"]);
    b.sfxVolume     = paimon::json::floatOr(v["sfxVolume"], 1.f);
    b.sfxSpeed      = paimon::json::floatOr(v["sfxSpeed"], 1.f);
    b.sfxStartMs    = paimon::json::integerOr<int>(v["sfxStartMs"]);
    b.sfxEndMs      = paimon::json::integerOr<int>(v["sfxEndMs"]);
    b.sfxFadeInMs   = paimon::json::integerOr<int>(v["sfxFadeInMs"]);
    b.sfxFadeOutMs  = paimon::json::integerOr<int>(v["sfxFadeOutMs"]);
    b.color.r       = static_cast<GLubyte>(paimon::json::integerOr<int>(v["colorR"], 120));
    b.color.g       = static_cast<GLubyte>(paimon::json::integerOr<int>(v["colorG"], 200));
    b.color.b       = static_cast<GLubyte>(paimon::json::integerOr<int>(v["colorB"], 255));
    paimon::json::forEachInArray(v["nodePath"], [&](matjson::Value const& n) {
        b.nodePath.push_back(paimon::json::integerOr<int>(n));
    });
    paimon::json::forEachInArray(v["idPath"], [&](matjson::Value const& n) {
        b.idPath.push_back(paimon::json::stringOr(n));
    });
    return b;
}

matjson::Value customQuickButtonToJson(CustomQuickButton const& b) {
    auto o = matjson::Value::object();
    o["id"]            = b.id;
    o["name"]          = b.name;
    o["icon"]          = b.icon;
    o["labelText"]     = b.labelText;
    o["targetNodeId"]  = b.targetNodeId;
    o["parentId"]      = b.parentId;
    o["ownerClass"]    = b.ownerClass;
    o["sceneClass"]    = b.sceneClass;
    o["itemClass"]     = b.itemClass;
    o["listenerClass"] = b.listenerClass;
    o["relX"]          = b.relX;
    o["relY"]          = b.relY;
    o["tag"]           = b.tag;
    o["shape"]         = static_cast<int>(b.shape);
    o["imagePath"]     = b.imagePath;
    o["imageScale"]    = b.imageScale;
    o["imageRotation"] = b.imageRotation;
    o["imageFlipX"]    = b.imageFlipX;
    o["imageFlipY"]    = b.imageFlipY;
    o["sfxKind"]       = b.sfxKind;
    o["sfxPath"]       = b.sfxPath;
    o["sfxId"]         = b.sfxId;
    o["sfxVolume"]     = b.sfxVolume;
    o["sfxSpeed"]      = b.sfxSpeed;
    o["sfxStartMs"]    = b.sfxStartMs;
    o["sfxEndMs"]      = b.sfxEndMs;
    o["sfxFadeInMs"]   = b.sfxFadeInMs;
    o["sfxFadeOutMs"]  = b.sfxFadeOutMs;
    o["colorR"]        = static_cast<int>(b.color.r);
    o["colorG"]        = static_cast<int>(b.color.g);
    o["colorB"]        = static_cast<int>(b.color.b);
    auto np = matjson::Value::array();
    for (int i : b.nodePath) np.push(matjson::Value(i));
    o["nodePath"] = np;
    auto ip = matjson::Value::array();
    for (auto const& s : b.idPath) ip.push(matjson::Value(s));
    o["idPath"] = ip;
    return o;
}

std::vector<CustomQuickButton> QuickHubManager::getCustomButtons() const {
    std::vector<CustomQuickButton> out;
    auto saved = Mod::get()->getSavedValue<matjson::Value>(kCustomButtonsKey, matjson::Value());
    if (!saved.isArray()) return out;
    auto arrRes = saved.asArray();
    if (!arrRes.isOk()) return out;
    for (auto const& v : arrRes.unwrap()) {
        if (!v.isObject()) continue;
        out.push_back(customQuickButtonFromJson(v));
    }
    return out;
}

std::optional<CustomQuickButton> QuickHubManager::getCustomButton(std::string const& id) const {
    for (auto& b : getCustomButtons()) {
        if (b.id == id) return b;
    }
    return std::nullopt;
}

std::vector<RadialOptionDef> QuickHubManager::getAllRadialOptions() const {
    auto options = getAllAvailableOptions();
    auto customButtons = getCustomButtons();
    options.reserve(options.size() + customButtons.size());

    for (auto const& button : customButtons) {
        options.push_back(toRadialDef(button));
    }
    return options;
}

void QuickHubManager::writeCustomButtons(std::vector<CustomQuickButton> const& all) {
    auto arr = matjson::Value::array();
    for (auto const& b : all) {
        arr.push(customQuickButtonToJson(b));
    }
    Mod::get()->setSavedValue(kCustomButtonsKey, arr);
}

bool QuickHubManager::saveCustomButton(CustomQuickButton const& button) {
    if (button.id.empty()) return false;
    auto all = getCustomButtons();
    bool replaced = false;
    for (auto& b : all) {
        if (b.id == button.id) { b = button; replaced = true; break; }
    }
    if (!replaced) all.push_back(button);
    writeCustomButtons(all);
    return true;
}

bool QuickHubManager::deleteCustomButton(std::string const& id) {
    auto all = getCustomButtons();
    auto it = std::ranges::find(all, id, &CustomQuickButton::id);
    if (it == all.end()) return false;
    all.erase(it);
    writeCustomButtons(all);

    auto active = getActiveOptions();
    if (auto found = std::ranges::find(active, id); found != active.end()) {
        active.erase(found);
        setActiveOptions(active);
    }
    return true;
}

std::string QuickHubManager::makeUniqueCustomId(std::string const& suggestedName) {
    // load the id set once: getCustomButton would re-parse the saved json
    // for every candidate suffix.
    std::unordered_set<std::string> used;
    for (auto const& b : getCustomButtons()) used.insert(b.id);

    std::string base = "custom:" + slugify(suggestedName);
    std::string candidate = base;
    int n = 1;
    while (used.contains(candidate)) {
        candidate = base + "-" + std::to_string(++n);
    }
    return candidate;
}

bool QuickHubManager::isHoldCtrlEnabled() {
    return Mod::get()->getSavedValue<bool>(kHoldCtrlKey, true);
}

void QuickHubManager::setHoldCtrlEnabled(bool enabled) {
    abortActiveHold();
    Mod::get()->setSavedValue<bool>(kHoldCtrlKey, enabled);
}

std::vector<paimon::keybinds::ExtendedKeybind> const& QuickHubManager::holdButtonOptions() {
    using namespace paimon::keybinds;
    static auto const options = [] {
        std::vector<ExtendedKeybind> buttons;
        auto addKey = [&](enumKeyCodes key) {
            ExtendedKeybind button;
            button.kind = ExtendedKind::Keyboard;
            button.key = key;
            buttons.push_back(button);
        };
        for (auto key : {KEY_Control, KEY_Shift, KEY_Alt, KEY_LeftWindowsKey,
                         KEY_Tab, KEY_Space, KEY_Enter, KEY_Backspace,
                         KEY_Insert, KEY_Delete, KEY_Home, KEY_End, KEY_PageUp, KEY_PageDown}) {
            addKey(key);
        }
        for (int key = KEY_A; key <= KEY_Z; ++key) addKey(static_cast<enumKeyCodes>(key));
        for (int key = KEY_Zero; key <= KEY_Nine; ++key) addKey(static_cast<enumKeyCodes>(key));
        for (int key = KEY_F1; key <= KEY_F12; ++key) addKey(static_cast<enumKeyCodes>(key));
        for (auto mouse : {MouseButton::Left, MouseButton::Right, MouseButton::Middle,
                           MouseButton::Button4, MouseButton::Button5}) {
            ExtendedKeybind button;
            button.kind = ExtendedKind::Mouse;
            button.button = mouse;
            buttons.push_back(button);
        }
        return buttons;
    }();
    return options;
}

paimon::keybinds::ExtendedKeybind QuickHubManager::holdButton() {
    using namespace paimon::keybinds;
    auto saved = loadExtendedKeybind("quick-hub-hold-button");
    for (auto const& option : holdButtonOptions()) {
        if (saved.kind == option.kind &&
            ((saved.kind == ExtendedKind::Keyboard && saved.key == option.key) ||
             (saved.kind == ExtendedKind::Mouse && saved.button == option.button))) {
            return option;
        }
    }
#ifdef GEODE_IS_MACOS
    return holdButtonOptions()[3]; // Command on macOS, Ctrl on other platforms.
#else
    return holdButtonOptions().front();
#endif
}

void QuickHubManager::setHoldButton(paimon::keybinds::ExtendedKeybind const& button) {
    abortActiveHold();
    paimon::keybinds::saveExtendedKeybind("quick-hub-hold-button", button);
}

float QuickHubManager::holdSeconds() {
    float v = Mod::get()->getSavedValue<float>(kHoldSecondsKey, kHoldSecondsDefault);
    if (!std::isfinite(v)) return kHoldSecondsDefault;
    return std::clamp(v, kHoldSecondsMin, kHoldSecondsMax);
}

void QuickHubManager::setHoldSeconds(float seconds) {
    abortActiveHold();
    if (!std::isfinite(seconds)) seconds = kHoldSecondsDefault;
    Mod::get()->setSavedValue<float>(
        kHoldSecondsKey, std::clamp(std::round(seconds * 10.f) / 10.f,
                                   kHoldSecondsMin, kHoldSecondsMax));
}

bool QuickHubManager::canOpenInCurrentContext() {
    auto* director = cocos2d::CCDirector::get();
    if (!director || !director->getRunningScene()) return false;

    // overlays don't make a scene safe: playlayer still owns touches below.
    if (PlayLayer::get()) return false;

    // editor keeps its layer while playtesting: playback state tells editing from gameplay.
    if (auto* editor = LevelEditorLayer::get()) {
        return editor->m_playbackMode == PlaybackMode::Not;
    }

    return true;
}

} // namespace paimon::quickhub
