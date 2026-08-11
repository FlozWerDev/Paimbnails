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

// Adds mouse buttons and scroll triggers beside Geode's keyboard-only keybinds.
// Extended bindings live in saved values under paimon-extkb-{settingKey}.

enum class ExtendedKind : int {
    None      = 0,
    Keyboard  = 1,
    Mouse     = 2,
    ScrollUp  = 3,
    ScrollDown = 4,
};

enum class MouseButton : int {
    Left    = 0,
    Right   = 1,
    Middle  = 2,
    Button4 = 3,
    Button5 = 4,
};

struct ExtendedKeybind {
    ExtendedKind kind = ExtendedKind::None;
    cocos2d::enumKeyCodes key = cocos2d::KEY_None;
    MouseButton button = MouseButton::Left;
    geode::KeyboardModifier modifiers{};

    bool isEmpty() const { return kind == ExtendedKind::None; }
    bool isMouseHold() const { return kind == ExtendedKind::Mouse; }
    bool isScrollTrigger() const {
        return kind == ExtendedKind::ScrollUp || kind == ExtendedKind::ScrollDown;
    }

    std::string toDisplayString() const;
};

std::string formatKeyboardKeybind(geode::Keybind const& kb);

ExtendedKeybind loadExtendedKeybind(std::string_view settingKey);

void saveExtendedKeybind(std::string_view settingKey, ExtendedKeybind const& bind);

bool isMouseButtonHeld(MouseButton button);

// Currently pressed modifiers (resynced from KeyboardInputEvent and
// MouseInputEvent — the same flags the rest of the volume-scroll code uses).
geode::KeyboardModifier currentModifiers();

bool isExtendedHeld(ExtendedKeybind const& bind);

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

std::vector<std::string> const& allManagedKeybinds();

void initExtendedKeybindSystem();

bool dispatchScrollAsTrigger(double y, double timestamp);

using ScrollCaptureCallback = std::function<bool(double y, geode::KeyboardModifier mods)>;
void setScrollCaptor(ScrollCaptureCallback callback);
ScrollCaptureCallback const& currentScrollCaptor();
bool hasScrollCaptor();

}
