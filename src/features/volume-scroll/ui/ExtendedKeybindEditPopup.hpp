#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/Keyboard.hpp>

#include <functional>
#include <optional>
#include <string>

#include "../../../utils/ExtendedKeybind.hpp"

namespace paimon::volscroll {

// Geode keybind editor extended with mouse buttons and optional wheel capture.
// Keyboard saves to KeybindSettingV3; mouse/wheel saves to ExtendedKeybind.

class ExtendedKeybindEditPopup : public geode::Popup {
public:
    using SaveCallback = std::function<void(
        std::optional<geode::Keybind> keyboardBind,
        paimon::keybinds::ExtendedKeybind extendedBind
    )>;

    static ExtendedKeybindEditPopup* create(
        std::string settingKey,
        std::string title,
        std::optional<geode::Keybind> currentKeyboard,
        paimon::keybinds::ExtendedKeybind currentExtended,
        bool allowScroll,
        SaveCallback onSave
    );

    bool isRecording() const { return m_isRecording; }

    // Called by global listeners; true means the event was consumed.
    bool captureKeyboard(cocos2d::enumKeyCodes key, geode::KeyboardModifier mods);
    bool captureMouse(paimon::keybinds::MouseButton btn, geode::KeyboardModifier mods);
    bool captureScroll(bool up, geode::KeyboardModifier mods);

protected:
    bool init(
        std::string settingKey,
        std::string title,
        std::optional<geode::Keybind> currentKeyboard,
        paimon::keybinds::ExtendedKeybind currentExtended,
        bool allowScroll,
        SaveCallback onSave
    );

    void onExit() override;

    void onRecord(cocos2d::CCObject*);
    void onSave(cocos2d::CCObject*);
    void onClear(cocos2d::CCObject*);

    void enterRecordingMode();
    void exitRecordingMode();
    void refreshDisplay();
    // Show the recording state with the vanilla button sprites.
    void updateRecordButtonAppearance();

    std::string m_settingKey;
    std::string m_title;
    SaveCallback m_onSave;
    bool m_allowScroll = true;

    // Pending edits, committed by Save.
    std::optional<geode::Keybind> m_pendingKeyboard;
    paimon::keybinds::ExtendedKeybind m_pendingExtended;

    bool m_isRecording = false;

    cocos2d::CCLabelBMFont* m_displayLabel = nullptr;
    cocos2d::CCLabelBMFont* m_hintLabel = nullptr;
    CCMenuItemSpriteExtra* m_recordButton = nullptr;
};

}
