#pragma once

#include <Geode/Geode.hpp>
#include <Geode/loader/Event.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/utils/Keyboard.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace paimon::keybinds {

// ExtendedKeybind — extends Geode's native Keybind with support for mouse
// buttons (left/right/middle/side) and the scroll wheel.
//
// Why it exists: Geode v5 only allows keyboard keys for a keybind.
// MouseInputData::buttonToKeyCode returns KEY_None for Left/Right/Middle
// (only Button4/Button5 map to MOUSE_4/5), and KeybindEditPopup ignores
// ScrollWheelEvent, so right-click and scroll can't be assigned natively.
//
// Solution: for each `keybind` setting we store an optional ExtendedKeybind
// alongside it in saved-values under `paimon-extkb-{settingKey}`. Matching
// checks both bindings; the keybind is active if either matches.
//
// The custom popup (ExtendedKeybindEditPopup) writes to the right binding
// based on input:
//   - keyboard key -> native Geode setting (KeybindSettingV3).
//   - mouse button / scroll -> saved-value `paimon-extkb-{key}`.

enum class ExtendedKind : int {
    None      = 0,  // no extended binding
    Keyboard  = 1,  // (key + mods) — duplicates native, advanced cases only
    Mouse     = 2,  // mouse button + mods
    ScrollUp  = 3,  // scroll up + mods (trigger only, not hold)
    ScrollDown = 4, // scroll down + mods (trigger only, not hold)
};

enum class MouseButton : int {
    Left    = 0,
    Right   = 1,
    Middle  = 2,
    Button4 = 3, // side back
    Button5 = 4, // side forward
};

struct ExtendedKeybind {
    ExtendedKind kind = ExtendedKind::None;
    cocos2d::enumKeyCodes key = cocos2d::KEY_None; // valid if kind=Keyboard
    MouseButton button = MouseButton::Left;        // valid if kind=Mouse
    geode::KeyboardModifier modifiers{};

    bool isEmpty() const { return kind == ExtendedKind::None; }
    bool isMouseHold() const { return kind == ExtendedKind::Mouse; }
    bool isScrollTrigger() const {
        return kind == ExtendedKind::ScrollUp || kind == ExtendedKind::ScrollDown;
    }

    // Human-readable text for the UI ("Right Click", "Ctrl+Scroll Up", ...)
    std::string toDisplayString() const;
};

// Native Keybind formatting.
// Formats a geode::Keybind for the UI. Unlike Keybind::toString(), handles
// modifier-only binds (e.g. key=KEY_None, modifiers=Ctrl) by returning "Ctrl"
// instead of "Ctrl+Unknown". Returns "" if the keybind is entirely empty.
std::string formatKeyboardKeybind(geode::Keybind const& kb);

// Persistence.
// Reads the ExtendedKeybind for a setting. Returns kind=None if missing or corrupt.
ExtendedKeybind loadExtendedKeybind(std::string_view settingKey);

// Save. Pass kind=None to clear.
void saveExtendedKeybind(std::string_view settingKey, ExtendedKeybind const& bind);

// Mouse state.
// True if the mouse button is currently pressed.
bool isMouseButtonHeld(MouseButton button);

// Currently pressed modifiers (resynced from KeyboardInputEvent and
// MouseInputEvent — the same flags the rest of the volume-scroll code uses).
geode::KeyboardModifier currentModifiers();

// Extended binding matching.
// "Hold-style" — the binding must be held right now. Used by VolumeScrollHook
// to decide whether to turn a mouse scroll into a volume change.
//
// Rules:
//   - kind=None -> false
//   - kind=Keyboard -> key in g_keysDown and modifiers subset of current
//   - kind=Mouse -> mouse button pressed and modifiers subset of current
//   - kind=ScrollUp/Down -> false (an instant scroll isn't a "hold")
bool isExtendedHeld(ExtendedKeybind const& bind);

// "Trigger-style" — the binding matched the given event. Used to dispatch in
// response to a mouse click or scroll-wheel tick.
bool extendedMatchesMouseTrigger(
    ExtendedKeybind const& bind,
    MouseButton button,
    geode::KeyboardModifier currentMods
);

bool extendedMatchesScrollTrigger(
    ExtendedKeybind const& bind,
    bool scrollUp,
    geode::KeyboardModifier currentMods
);

// Trigger event broadcast.
// When a mouse click or scroll matches a setting's ExtendedKeybind, this event
// fires the equivalent of KeybindSettingPressedEventV3 (without replacing it —
// native keyboard listeners still work).
//
// Modules like capture-keybind, zoom, layout-editor, etc. consume this event in
// addition to the native one to react to right-click / scroll.
class ExtendedKeybindTriggerEvent final
    : public geode::Event<ExtendedKeybindTriggerEvent, bool(double timestamp), std::string>
{
public:
    using Event::Event;
};

// Emits the event for `settingKey` from the global listener. No need to call it
// manually — the internal dispatcher invokes it when mouse/scroll matches a
// registered ExtendedKeybind.
void emitExtendedTrigger(std::string_view settingKey, double timestamp);

// Mod keybind list.
// Iterated to know which settings have an ExtendedKeybind and which to call
// emitExtendedTrigger on. Register new keybinds here (single source of truth).
std::vector<std::string> const& allManagedKeybinds();

// Initialization.
// Called ONCE from Bootstrap.cpp / FrameworkInit.cpp. Registers the global
// KeyboardInputEvent / MouseInputEvent listeners to keep g_mouseButtonsDown and
// modifiers current, and to fire ExtendedKeybindTriggerEvent when needed.
void initExtendedKeybindSystem();

// dispatchScrollMSG helpers.
// Checks all registered settings and fires the extended event if any matches
// the scroll. Returns true if at least one matched (so the caller can decide
// whether to "consume" the scroll). Triggers only — doesn't touch volume-scroll.
//
// Note: y > 0 = scroll up, y < 0 = scroll down (Geode convention).
bool dispatchScrollAsTrigger(double y, double timestamp);

// Scroll capture for the popup.
// ExtendedKeybindEditPopup can register as a scroll "captor" while recording.
// If a captor is active, VolumeScrollHook routes scroll events to the callback
// instead of processing them as volume change / trigger.
//
// Only one captor at a time (last to register wins). Pass nullptr to unregister.
using ScrollCaptureCallback = std::function<bool(double y, geode::KeyboardModifier mods)>;
void setScrollCaptor(ScrollCaptureCallback callback);
ScrollCaptureCallback const& currentScrollCaptor();
bool hasScrollCaptor();

} // namespace paimon::keybinds
