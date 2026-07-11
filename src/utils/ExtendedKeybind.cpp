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

// Current modifiers — resynced from KeyboardInputEvent and MouseInputEvent.
// Same flags VolumeScrollHook uses (it keeps its own mirror to stay decoupled);
// we duplicate here so each module is self-contained.
KeyboardModifier g_currentMods{};

// Set of pressed mouse buttons.
std::array<bool, 5> g_mouseDown = { false, false, false, false, false };

bool g_systemInitialized = false;

constexpr char const* kPrefix = "paimon-extkb-";

// Helper: build saved-value key
std::string makeSavedKey(std::string_view settingKey) {
    std::string out;
    out.reserve(std::char_traits<char>::length(kPrefix) + settingKey.size());
    out.append(kPrefix);
    out.append(settingKey);
    return out;
}

// Canonical list of keybind settings the mod manages. New ones in mod.json
// must be listed here so the trigger dispatcher considers them.
// Kept in sync with mod.json manually.
std::vector<std::string> const& managedList() {
    static std::vector<std::string> const kKeys = {
        // gameplay / capture
        "capture-keybind",
        "capture-menu-keybind",
        // pause zoom
        "zoom-in-keybind",
        "zoom-out-keybind",
        "zoom-reset-keybind",
        "zoom-toggle-menu-keybind",
        // global UI
        "settings-panel-keybind",
        "main-menu-layout-keybind",
        "level-search-enter",
        // editor music
        "editorMusicToggleKeybind",
        // volume scroll (hold-while-scroll, no trigger-event)
        "volume-music-mod-game",
        "volume-sfx-mod-game",
        "volume-music-mod-editor",
        "volume-sfx-mod-editor",
    };
    return kKeys;
}

// Subset: keybinds that DO get extended trigger-events. Volume-scroll ones are
// excluded because their flow is "hold + scroll", not an instant trigger.
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
        "editorMusicToggleKeybind",
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

} // namespace

std::string ExtendedKeybind::toDisplayString() const {
    if (kind == ExtendedKind::None) return "";
    auto prefix = modifiersPrefix(modifiers);
    switch (kind) {
        case ExtendedKind::None: return "";
        case ExtendedKind::Keyboard: {
            // For keyboard keys, delegate to Geode's toString.
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
        // Modifier only (e.g. "Ctrl" as a hold-key). Geode formats this as
        // "Ctrl+Unknown" since key=KEY_None; we want just the modifiers without
        // the trailing "+Unknown".
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
        // Clear — Geode exposes no public API to delete saved values, so store
        // an empty object. loadExtendedKeybind treats it as absent.
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
    // Sync with the real OS state via GetAsyncKeyState. This stops the state
    // from staying `true` forever if the Release event is dropped when GD loses
    // focus (Windows input.cpp clears RawInputQueue while unfocused, silently
    // discarding the mouse Release).
    //
    // Without resyncing, volume-scroll (which calls isExtendedHeld) would see
    // the button as held forever and change volume on scroll without the user
    // holding it.
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
    // Subset check: required is a subset of current (extra modifiers allowed).
    bool modsMatchSubset(KeyboardModifier required, KeyboardModifier current) {
        return (current.value & required.value) == required.value;
    }
}

bool isExtendedHeld(ExtendedKeybind const& bind) {
    if (bind.isEmpty()) return false;
    if (bind.isScrollTrigger()) return false; // scroll isn't a "hold"

    if (bind.kind == ExtendedKind::Mouse) {
        if (!isMouseButtonHeld(bind.button)) return false;
        if (!modsMatchSubset(bind.modifiers, g_currentMods)) return false;
        return true;
    }

    // Keyboard kind: VolumeScrollHook already checks volume-scroll's g_keysDown,
    // so we don't duplicate it here. A Keyboard extended bind is always treated
    // as inactive from this helper's view (its native pair covers the case).
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
    // 1) Custom event for the mod's own listeners (where we want mouse/scroll-
    //    specific logic).
    ExtendedKeybindTriggerEvent(std::string(settingKey)).send(timestamp);

    // 2) Re-fire Geode's native event with a synthetic Keybind so existing
    //    listeners (KeybindSettingPressedEventV3) react too, without duplicating
    //    code at every call site. The filtered send() signature is
    //    send(Keybind const&, bool down, bool repeat, double timestamp), built
    //    with (modID, settingKey) so only this setting's listeners receive it.
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

    // The "release" is sent immediately after to stay symmetric with an instant
    // click (most listeners ignore it, but some use it to reset state).
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

    // Keyboard listener: only to keep g_currentMods current.
    KeyboardInputEvent().listen(+[](KeyboardInputData& data) {
        g_currentMods = data.modifiers;
        return false;
    }).leak();

    // Mouse listener: update button state and fire triggers.
    MouseInputEvent().listen(+[](MouseInputData& data) {
        g_currentMods = data.modifiers;

        int idx = static_cast<int>(fromGeodeMouseButton(data.button));
        if (!isMouseButtonIndexValid(idx)) return false;

        bool isPress = (data.action == MouseInputData::Action::Press);
        g_mouseDown[idx] = isPress;

        // Only the Press event matters for firing the trigger.
        //
        // IMPORTANT: don't filter by `wasDown` (the previous state).
        //
        // Geode delivers a MouseInputEvent per real mouse action and never emits
        // two consecutive Presses without a Release. But g_mouseDown[idx] can get
        // stuck `true` if the user alt-tabs while holding the button: RawInputQueue
        // is cleared silently (Windows input.cpp pumpRawInput) and the Release
        // never reaches this listener. Filtering by `wasDown` would ignore the
        // next right-click after the alt-tab, making the keybind seem gone for
        // users who switch windows with the mouse held.
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

    log::info("[ExtendedKeybind] System initialized — managed keybinds: {}",
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

// Scroll captor (used by ExtendedKeybindEditPopup)

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

} // namespace paimon::keybinds
