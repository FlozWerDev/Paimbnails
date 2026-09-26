#include "QuickHubManager.hpp"
#include "../data/QuickHubCategories.hpp"
#include <matjson.hpp>
#include <algorithm>

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

std::vector<CustomQuickButton> QuickHubManager::getCustomButtons() const {
    std::vector<CustomQuickButton> out;
    auto saved = Mod::get()->getSavedValue<matjson::Value>(kCustomButtonsKey, matjson::Value());
    if (!saved.isArray()) return out;
    auto arrRes = saved.asArray();
    if (!arrRes.isOk()) return out;
    for (auto const& v : arrRes.unwrap()) {
        if (!v.isObject()) continue;
        CustomQuickButton b;
        b.id            = v["id"].asString().unwrapOr("");
        b.name          = v["name"].asString().unwrapOr("");
        b.icon          = v["icon"].asString().unwrapOr("");
        b.labelText     = v["labelText"].asString().unwrapOr("");
        b.targetNodeId  = v["targetNodeId"].asString().unwrapOr("");
        b.parentId      = v["parentId"].asString().unwrapOr("");
        b.ownerClass    = v["ownerClass"].asString().unwrapOr("");
        b.sceneClass    = v["sceneClass"].asString().unwrapOr("");
        b.itemClass     = v["itemClass"].asString().unwrapOr("");
        b.listenerClass = v["listenerClass"].asString().unwrapOr("");
        b.relX          = static_cast<float>(v["relX"].asDouble().unwrapOr(-1.0));
        b.relY          = static_cast<float>(v["relY"].asDouble().unwrapOr(-1.0));
        b.tag           = static_cast<int>(v["tag"].asInt().unwrapOr(0));
        b.shape         = static_cast<RadialButtonShape>(static_cast<int>(v["shape"].asInt().unwrapOr(0)));
        // Skin/SFX: missing keys = old defaults.
        b.imagePath     = v["imagePath"].asString().unwrapOr("");
        b.imageScale    = static_cast<float>(v["imageScale"].asDouble().unwrapOr(1.0));
        b.imageRotation = static_cast<float>(v["imageRotation"].asDouble().unwrapOr(0.0));
        b.imageFlipX    = v["imageFlipX"].asBool().unwrapOr(false);
        b.imageFlipY    = v["imageFlipY"].asBool().unwrapOr(false);
        b.sfxKind       = static_cast<int>(v["sfxKind"].asInt().unwrapOr(0));
        b.sfxPath       = v["sfxPath"].asString().unwrapOr("");
        b.sfxId         = static_cast<int>(v["sfxId"].asInt().unwrapOr(0));
        b.sfxVolume     = static_cast<float>(v["sfxVolume"].asDouble().unwrapOr(1.0));
        b.sfxSpeed      = static_cast<float>(v["sfxSpeed"].asDouble().unwrapOr(1.0));
        b.sfxStartMs    = static_cast<int>(v["sfxStartMs"].asInt().unwrapOr(0));
        b.sfxEndMs      = static_cast<int>(v["sfxEndMs"].asInt().unwrapOr(0));
        b.sfxFadeInMs   = static_cast<int>(v["sfxFadeInMs"].asInt().unwrapOr(0));
        b.sfxFadeOutMs  = static_cast<int>(v["sfxFadeOutMs"].asInt().unwrapOr(0));
        b.color.r       = static_cast<GLubyte>(v["colorR"].asInt().unwrapOr(120));
        b.color.g       = static_cast<GLubyte>(v["colorG"].asInt().unwrapOr(200));
        b.color.b       = static_cast<GLubyte>(v["colorB"].asInt().unwrapOr(255));
        if (v["nodePath"].isArray()) {
            if (auto npRes = v["nodePath"].asArray(); npRes.isOk()) {
                for (auto const& n : npRes.unwrap()) {
                    b.nodePath.push_back(static_cast<int>(n.asInt().unwrapOr(0)));
                }
            }
        }
        if (v["idPath"].isArray()) {
            if (auto ipRes = v["idPath"].asArray(); ipRes.isOk()) {
                for (auto const& n : ipRes.unwrap()) {
                    b.idPath.push_back(n.asString().unwrapOr(""));
                }
            }
        }
        out.push_back(std::move(b));
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
        arr.push(o);
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
    std::string base = "custom:" + slugify(suggestedName);
    std::string candidate = base;
    int n = 1;
    while (getCustomButton(candidate).has_value()) {
        candidate = base + "-" + std::to_string(++n);
    }
    return candidate;
}

bool QuickHubManager::isHoldCtrlEnabled() {
    return Mod::get()->getSavedValue<bool>(kHoldCtrlKey, true);
}

void QuickHubManager::setHoldCtrlEnabled(bool enabled) {
    Mod::get()->setSavedValue<bool>(kHoldCtrlKey, enabled);
}

bool QuickHubManager::canOpenInCurrentContext() {
    auto* director = cocos2d::CCDirector::get();
    if (!director || !director->getRunningScene()) return false;

    // Overlays don't make a scene safe: PlayLayer still owns touches below.
    if (PlayLayer::get()) return false;

    // Editor keeps its layer while playtesting: playback state tells editing from gameplay.
    if (auto* editor = LevelEditorLayer::get()) {
        return editor->m_playbackMode == PlaybackMode::Not;
    }

    return true;
}

} // namespace paimon::quickhub
