#include "ExtendedKeybind.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/Keyboard.hpp>
#include <matjson.hpp>

#include <array>
#include <functional>
#include <unordered_set>
#include <utility>

#ifdef GEODE_IS_WINDOWS
    #include <windows.h>
#endif

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::keybinds {

namespace {

// Keep a local modifier mirror, resynced from keyboard and mouse events.
KeyboardModifier g_currentMods{};

std::array<bool, 5> g_mouseDown = { false, false, false, false, false };

bool g_systemInitialized = false;

constexpr char const* kPrefix = "paimon-extkb-";

std::string makeSavedKey(std::string_view settingKey) {
    std::string out;
    out.reserve(std::char_traits<char>::length(kPrefix) + settingKey.size());
    out.append(kPrefix);
    out.append(settingKey);
    return out;
}

// Keybind settings handled by the trigger dispatcher; keep this in sync with mod.json.
std::vector<std::string> const& managedList() {
    static std::vector<std::string> const kKeys = {
        "capture-keybind",
        "capture-menu-keybind",
        "zoom-in-keybind",
        "zoom-out-keybind",
        "zoom-reset-keybind",
        "zoom-toggle-menu-keybind",
        "settings-panel-keybind",
        "main-menu-layout-keybind",
        "level-search-enter",
        "volume-music-mod-game",
        "volume-sfx-mod-game",
        "volume-music-mod-editor",
        "volume-sfx-mod-editor",
    };
    return kKeys;
}

// Keybinds with instant trigger events; volume-scroll uses hold + scroll instead.
std::vector<std::string> const& triggerOnlyList() {
    static std::vector<std::string> const kKeys = {
        "capture-keybind",
        "capture-menu-keybind",
        "zoom-in-keybind",
        "zoom-out-keybind",
        "zoom-reset-keybind",
        "zoom-toggle-menu-keybind",
        "settings-panel-keybind",
        "main-menu-layout-keybind",
        "level-search-enter",
    };
    return kKeys;
}

bool isMouseButtonIndexValid(int idx) {
    return idx >= 0 && idx < static_cast<int>(g_mouseDown.size());
}

MouseButton fromGeodeMouseButton(MouseInputData::Button btn) {
    switch (btn) {
        case MouseInputData::Button::Left:    return MouseButton::Left;
        case MouseInputData::Button::Right:   return MouseButton::Right;
        case MouseInputData::Button::Middle:  return MouseButton::Middle;
        case MouseInputData::Button::Button4: return MouseButton::Button4;
        case MouseInputData::Button::Button5: return MouseButton::Button5;
    }
    return MouseButton::Left;
}

char const* mouseButtonName(MouseButton b) {
    switch (b) {
        case MouseButton::Left:    return "Left Click";
        case MouseButton::Right:   return "Right Click";
        case MouseButton::Middle:  return "Middle Click";
        case MouseButton::Button4: return "Mouse 4";
        case MouseButton::Button5: return "Mouse 5";
    }
    return "?";
}

std::string modifiersPrefix(KeyboardModifier mods) {
    std::string out;
    if (mods.value & KeyboardModifier::Control) out += "Ctrl+";
    if (mods.value & KeyboardModifier::Shift)   out += "Shift+";
    if (mods.value & KeyboardModifier::Alt)     out += "Alt+";
    if (mods.value & KeyboardModifier::Super)   out += "Super+";
    return out;
}

}

std::string ExtendedKeybind::toDisplayString() const {
    if (kind == ExtendedKind::None) return "";
    auto prefix = modifiersPrefix(modifiers);
    switch (kind) {
        case ExtendedKind::None: return "";
        case ExtendedKind::Keyboard: {
            Keybind kb;
            kb.key = key;
            kb.modifiers = modifiers;
            return kb.toString();
        }
        case ExtendedKind::Mouse:
            return prefix + mouseButtonName(button);
        case ExtendedKind::ScrollUp:
            return prefix + "Scroll Up";
        case ExtendedKind::ScrollDown:
            return prefix + "Scroll Down";
    }
    return "";
}

std::string formatKeyboardKeybind(Keybind const& kb) {
    bool const hasKey = (kb.key != KEY_None);
    bool const hasMods = (kb.modifiers != KeyboardModifier::None);
    if (!hasKey && !hasMods) return "";

    if (!hasKey) {
        // Omit Geode's trailing "+Unknown" for modifier-only KEY_None binds.
        std::string out = modifiersPrefix(kb.modifiers);
        if (!out.empty() && out.back() == '+') out.pop_back();
        return out;
    }

    return kb.toString();
}

ExtendedKeybind loadExtendedKeybind(std::string_view settingKey) {
    auto* mod = Mod::get();
    if (!mod) return {};

    auto savedKey = makeSavedKey(settingKey);
    if (!mod->hasSavedValue(savedKey)) return {};

    auto json = mod->getSavedValue<matjson::Value>(savedKey, matjson::Value());
    if (!json.isObject()) return {};

    ExtendedKeybind out;
    auto kindRaw = json["kind"].asInt().unwrapOr(0);
    if (kindRaw < 0 || kindRaw > static_cast<int>(ExtendedKind::ScrollDown)) return {};
    out.kind = static_cast<ExtendedKind>(kindRaw);
    if (out.kind == ExtendedKind::None) return {};

    out.key = static_cast<enumKeyCodes>(json["key"].asInt().unwrapOr(0));

    auto btnRaw = json["btn"].asInt().unwrapOr(0);
    if (btnRaw < 0 || btnRaw > static_cast<int>(MouseButton::Button5)) {
        out.button = MouseButton::Left;
    } else {
        out.button = static_cast<MouseButton>(btnRaw);
    }

    out.modifiers = KeyboardModifier(static_cast<uint8_t>(
        json["mods"].asInt().unwrapOr(0)
    ));

    return out;
}

void saveExtendedKeybind(std::string_view settingKey, ExtendedKeybind const& bind) {
    auto* mod = Mod::get();
    if (!mod) return;

    auto savedKey = makeSavedKey(settingKey);

    if (bind.kind == ExtendedKind::None) {
        // Geode cannot delete saved values; an empty object is treated as absent.
        auto empty = matjson::Value::object();
        empty["kind"] = static_cast<int>(ExtendedKind::None);
        mod->setSavedValue<matjson::Value>(savedKey, empty);
        return;
    }

    auto obj = matjson::Value::object();
    obj["kind"] = static_cast<int>(bind.kind);
    obj["key"]  = static_cast<int>(bind.key);
    obj["btn"]  = static_cast<int>(bind.button);
    obj["mods"] = static_cast<int>(bind.modifiers.value);
    mod->setSavedValue<matjson::Value>(savedKey, obj);
}

bool isMouseButtonHeld(MouseButton button) {
    int idx = static_cast<int>(button);
    if (!isMouseButtonIndexValid(idx)) return false;

#ifdef GEODE_IS_WINDOWS
    // Focus loss can drop Release events, so resync the OS state or
    // volume-scroll may remain held forever.
    int vk = 0;
    switch (button) {
        case MouseButton::Left:    vk = VK_LBUTTON;  break;
        case MouseButton::Right:   vk = VK_RBUTTON;  break;
        case MouseButton::Middle:  vk = VK_MBUTTON;  break;
        case MouseButton::Button4: vk = VK_XBUTTON1; break;
        case MouseButton::Button5: vk = VK_XBUTTON2; break;
    }
    bool actualDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
    g_mouseDown[idx] = actualDown;
    return actualDown;
#else
    return g_mouseDown[idx];
#endif
}

KeyboardModifier currentModifiers() {
    return g_currentMods;
}

namespace {
    bool modsMatchSubset(KeyboardModifier required, KeyboardModifier current) {
        return (current.value & required.value) == required.value;
    }
}

bool isExtendedHeld(ExtendedKeybind const& bind) {
    if (bind.isEmpty()) return false;
    if (bind.isScrollTrigger()) return false;

    if (bind.kind == ExtendedKind::Mouse) {
        if (!isMouseButtonHeld(bind.button)) return false;
        if (!modsMatchSubset(bind.modifiers, g_currentMods)) return false;
        return true;
    }

    // VolumeScrollHook owns keyboard hold state; this helper handles mouse binds.
    return false;
}

bool extendedMatchesMouseTrigger(
    ExtendedKeybind const& bind,
    MouseButton button,
    KeyboardModifier currentMods
) {
    if (bind.kind != ExtendedKind::Mouse) return false;
    if (bind.button != button) return false;
    return modsMatchSubset(bind.modifiers, currentMods);
}

bool extendedMatchesScrollTrigger(
    ExtendedKeybind const& bind,
    bool scrollUp,
    KeyboardModifier currentMods
) {
    if (scrollUp) {
        if (bind.kind != ExtendedKind::ScrollUp) return false;
    } else {
        if (bind.kind != ExtendedKind::ScrollDown) return false;
    }
    return modsMatchSubset(bind.modifiers, currentMods);
}

std::vector<std::string> const& allManagedKeybinds() {
    return managedList();
}

void emitExtendedTrigger(std::string_view settingKey, double timestamp) {
    // Notify local listeners first.
    ExtendedKeybindTriggerEvent(std::string(settingKey)).send(timestamp);

    // Mirror Geode's native event with a synthetic keybind so existing setting
    // listeners react too.
    auto* mod = Mod::get();
    if (!mod) return;

    Keybind synthetic;
    synthetic.key = cocos2d::KEY_None;
    synthetic.modifiers = KeyboardModifier(KeyboardModifier::None);

    std::string modID = std::string(mod->getID());
    std::string settingKeyStr = std::string(settingKey);

    KeybindSettingPressedEventV3(modID, settingKeyStr).send(
        synthetic,
        /*down=*/true,
        /*repeat=*/false,
        timestamp
    );

    // Send the matching release so reset-on-release listeners stay symmetric.
    KeybindSettingPressedEventV3(modID, settingKeyStr).send(
        synthetic,
        /*down=*/false,
        /*repeat=*/false,
        timestamp
    );
}

void initExtendedKeybindSystem() {
    if (g_systemInitialized) return;
    g_systemInitialized = true;

    KeyboardInputEvent().listen(+[](KeyboardInputData& data) {
        g_currentMods = data.modifiers;
        return false;
    }).leak();

    MouseInputEvent().listen(+[](MouseInputData& data) {
        g_currentMods = data.modifiers;

        int idx = static_cast<int>(fromGeodeMouseButton(data.button));
        if (!isMouseButtonIndexValid(idx)) return false;

        bool isPress = (data.action == MouseInputData::Action::Press);
        g_mouseDown[idx] = isPress;

        // Do not filter Press by the previous local state: focus loss can drop
        // Release events and leave a button marked down.
        if (!isPress) return false;

        auto button = fromGeodeMouseButton(data.button);
        for (auto const& key : triggerOnlyList()) {
            auto bind = loadExtendedKeybind(key);
            if (extendedMatchesMouseTrigger(bind, button, data.modifiers)) {
                emitExtendedTrigger(key, data.timestamp);
            }
        }

        return false;
    }).leak();

    log::info("[ExtendedKeybind] System initialized - managed keybinds: {}",
              managedList().size());
}

bool dispatchScrollAsTrigger(double y, double timestamp) {
    if (y == 0.0) return false;
    bool scrollUp = (y > 0.0);
    bool anyMatch = false;

    for (auto const& key : triggerOnlyList()) {
        auto bind = loadExtendedKeybind(key);
        if (extendedMatchesScrollTrigger(bind, scrollUp, g_currentMods)) {
            emitExtendedTrigger(key, timestamp);
            anyMatch = true;
        }
    }
    return anyMatch;
}


namespace {
    ScrollCaptureCallback g_scrollCaptor = nullptr;
}

void setScrollCaptor(ScrollCaptureCallback callback) {
    g_scrollCaptor = std::move(callback);
}

ScrollCaptureCallback const& currentScrollCaptor() {
    return g_scrollCaptor;
}

bool hasScrollCaptor() {
    return static_cast<bool>(g_scrollCaptor);
}

}
