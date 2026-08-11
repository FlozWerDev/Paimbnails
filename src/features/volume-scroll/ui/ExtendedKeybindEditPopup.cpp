#include "ExtendedKeybindEditPopup.hpp"

#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>

using namespace cocos2d;
using namespace geode::prelude;
using paimon::keybinds::ExtendedKeybind;
using paimon::keybinds::ExtendedKind;
using paimon::keybinds::MouseButton;

namespace paimon::volscroll {

namespace {
    constexpr float kPopupW = 360.f;
    constexpr float kPopupH = 220.f;

    // Store modifier-only keys as modifiers with KEY_None.
    bool isModifierKey(enumKeyCodes k) {
        switch (k) {
            case KEY_Control: case KEY_LeftControl: case KEY_RightContol:
            case KEY_Shift:   case KEY_LeftShift:   case KEY_RightShift:
            case KEY_Alt:     case KEY_LeftMenu:    case KEY_RightMenu:
                return true;
            default:
                return false;
        }
    }
}

ExtendedKeybindEditPopup* ExtendedKeybindEditPopup::create(
    std::string settingKey,
    std::string title,
    std::optional<Keybind> currentKeyboard,
    ExtendedKeybind currentExtended,
    bool allowScroll,
    SaveCallback onSave
) {
    auto ret = new ExtendedKeybindEditPopup();
    if (ret && ret->init(
        std::move(settingKey),
        std::move(title),
        std::move(currentKeyboard),
        std::move(currentExtended),
        allowScroll,
        std::move(onSave)
    )) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ExtendedKeybindEditPopup::init(
    std::string settingKey,
    std::string title,
    std::optional<Keybind> currentKeyboard,
    ExtendedKeybind currentExtended,
    bool allowScroll,
    SaveCallback onSave
) {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);

    m_settingKey = std::move(settingKey);
    m_title = std::move(title);
    m_pendingKeyboard = std::move(currentKeyboard);
    m_pendingExtended = std::move(currentExtended);
    m_allowScroll = allowScroll;
    m_onSave = std::move(onSave);

    this->setTitle(m_title.c_str());
    m_noElasticity = true;

    auto winSize = m_mainLayer->getContentSize();

    m_displayLabel = CCLabelBMFont::create("(none)", "bigFont.fnt");
    m_displayLabel->setScale(0.6f);
    m_displayLabel->setAnchorPoint({0.5f, 0.5f});
    m_displayLabel->setPosition({winSize.width / 2.f, winSize.height / 2.f + 18.f});
    m_mainLayer->addChild(m_displayLabel);

    auto hintText = m_allowScroll
        ? "Click 'Record' then press a key, mouse button, or scroll"
        : "Click 'Record' then press a key or mouse button";
    m_hintLabel = CCLabelBMFont::create(hintText, "chatFont.fnt");
    m_hintLabel->setScale(0.55f);
    m_hintLabel->setAnchorPoint({0.5f, 0.5f});
    m_hintLabel->setPosition({winSize.width / 2.f, winSize.height / 2.f - 10.f});
    m_hintLabel->setOpacity(180);
    m_mainLayer->addChild(m_hintLabel);

    auto bottomMenu = CCMenu::create();
    bottomMenu->setContentSize({winSize.width - 30.f, 36.f});

    auto recordSpr = ButtonSprite::create("Record", "bigFont.fnt", "GJ_button_03.png", 0.6f);
    m_recordButton = CCMenuItemSpriteExtra::create(
        recordSpr, this, menu_selector(ExtendedKeybindEditPopup::onRecord)
    );
    bottomMenu->addChild(m_recordButton);

    auto saveSpr = ButtonSprite::create("Save", "bigFont.fnt", "GJ_button_01.png", 0.6f);
    auto saveBtn = CCMenuItemSpriteExtra::create(
        saveSpr, this, menu_selector(ExtendedKeybindEditPopup::onSave)
    );
    bottomMenu->addChild(saveBtn);

    auto clearSpr = ButtonSprite::create("Clear", "bigFont.fnt", "GJ_button_06.png", 0.6f);
    auto clearBtn = CCMenuItemSpriteExtra::create(
        clearSpr, this, menu_selector(ExtendedKeybindEditPopup::onClear)
    );
    bottomMenu->addChild(clearBtn);

    bottomMenu->setLayout(RowLayout::create()->setGap(10.f));
    bottomMenu->setPosition({winSize.width / 2.f, 26.f});
    m_mainLayer->addChild(bottomMenu);

    // Register input listeners only while recording.

    this->refreshDisplay();

    return true;
}

void ExtendedKeybindEditPopup::onExit() {
    if (m_isRecording) {
        exitRecordingMode();
    }
    Popup::onExit();
}

void ExtendedKeybindEditPopup::refreshDisplay() {
    if (!m_displayLabel) return;

    std::string text;
    if (m_pendingKeyboard.has_value() &&
        (m_pendingKeyboard->key != KEY_None || m_pendingKeyboard->modifiers != KeyboardModifier::None))
    {
    // Modifier-only binds display as "Ctrl", not "Ctrl+Unknown".
        text = paimon::keybinds::formatKeyboardKeybind(*m_pendingKeyboard);
    }
    if (!m_pendingExtended.isEmpty()) {
        if (!text.empty()) text += " / ";
        text += m_pendingExtended.toDisplayString();
    }
    if (text.empty()) text = "(none)";

    m_displayLabel->setString(text.c_str());
    m_displayLabel->limitLabelWidth(kPopupW - 40.f, 0.7f, 0.2f);

    if (m_hintLabel) {
        if (m_isRecording) {
            m_hintLabel->setString("Press a key, click a button, or scroll...");
        } else {
            auto def = m_allowScroll
                ? "Click 'Record' then press a key, mouse button, or scroll"
                : "Click 'Record' then press a key or mouse button";
            m_hintLabel->setString(def);
        }
    }
}

void ExtendedKeybindEditPopup::onRecord(CCObject*) {
    if (m_isRecording) {
        exitRecordingMode();
    } else {
        enterRecordingMode();
    }
    this->refreshDisplay();
}

void ExtendedKeybindEditPopup::onClear(CCObject*) {
    m_pendingKeyboard.reset();
    m_pendingExtended = ExtendedKeybind{};
    this->refreshDisplay();
}

void ExtendedKeybindEditPopup::onSave(CCObject*) {
    if (m_isRecording) exitRecordingMode();
    if (m_onSave) {
        m_onSave(m_pendingKeyboard, m_pendingExtended);
    }
    this->onClose(nullptr);
}

    // Recording mode owns the global recorder pointer.

namespace {
    // Only one popup records at a time.
    ExtendedKeybindEditPopup* g_activeRecorder = nullptr;
}

    // Global listeners are installed once and stay dormant without a recorder.
$execute {
    KeyboardInputEvent().listen(+[](KeyboardInputData& data) {
        if (g_activeRecorder == nullptr) return false;
        if (!g_activeRecorder->isRecording()) return false;
        if (data.action != KeyboardInputData::Action::Press) return false;
        g_activeRecorder->captureKeyboard(data.key, data.modifiers);
        return true;
    }).leak();

    MouseInputEvent().listen(+[](MouseInputData& data) {
        if (g_activeRecorder == nullptr) return false;
        if (!g_activeRecorder->isRecording()) return false;
        if (data.action != MouseInputData::Action::Press) return false;
        MouseButton btn;
        switch (data.button) {
            case MouseInputData::Button::Left:    btn = MouseButton::Left;    break;
            case MouseInputData::Button::Right:   btn = MouseButton::Right;   break;
            case MouseInputData::Button::Middle:  btn = MouseButton::Middle;  break;
            case MouseInputData::Button::Button4: btn = MouseButton::Button4; break;
            case MouseInputData::Button::Button5: btn = MouseButton::Button5; break;
            default: return false;
        }
        g_activeRecorder->captureMouse(btn, data.modifiers);
        return true;
    }).leak();
}

void ExtendedKeybindEditPopup::enterRecordingMode() {
    if (g_activeRecorder && g_activeRecorder != this) {
        g_activeRecorder->exitRecordingMode();
    }
    g_activeRecorder = this;
    m_isRecording = true;
    this->updateRecordButtonAppearance();

    // VolumeScrollHook forwards scroll events here before applying the action.
    if (m_allowScroll) {
        paimon::keybinds::setScrollCaptor(
            [this](double y, KeyboardModifier mods) -> bool {
                if (!m_isRecording) return false;
                bool up = (y > 0.0);
                this->captureScroll(up, mods);
    return true;
            }
        );
    }
}

void ExtendedKeybindEditPopup::exitRecordingMode() {
    m_isRecording = false;
    paimon::keybinds::setScrollCaptor(nullptr);
    if (g_activeRecorder == this) {
        g_activeRecorder = nullptr;
    }
    this->updateRecordButtonAppearance();
}

void ExtendedKeybindEditPopup::updateRecordButtonAppearance() {
    if (!m_recordButton) return;

    char const* asset = m_isRecording ? "GJ_button_02.png" : "GJ_button_03.png";
    char const* label = m_isRecording ? "Recording" : "Record";

    auto* newSpr = ButtonSprite::create(label, "bigFont.fnt", asset, 0.6f);
    if (!newSpr) return;
    m_recordButton->setNormalImage(newSpr);

    // Reflow the parent menu because the label changes size.
    if (auto* parent = m_recordButton->getParent()) {
        if (parent->getLayout()) {
            parent->updateLayout();
        }
    }
}

bool ExtendedKeybindEditPopup::captureKeyboard(enumKeyCodes key, KeyboardModifier mods) {
    if (key == KEY_Escape) {
    // Escape cancels without saving.
        exitRecordingMode();
        this->refreshDisplay();
        return true;
    }

    Keybind kb;
    if (isModifierKey(key)) {
    // Save modifier-only binds as a hold style.
        kb.key = KEY_None;
        kb.modifiers = mods;
        if (kb.modifiers == KeyboardModifier::None) {
    // Recover the modifier when the key event omitted its modifier flags.
            if (key == KEY_Control || key == KEY_LeftControl || key == KEY_RightContol) {
                kb.modifiers = KeyboardModifier(KeyboardModifier::Control);
            } else if (key == KEY_Shift || key == KEY_LeftShift || key == KEY_RightShift) {
                kb.modifiers = KeyboardModifier(KeyboardModifier::Shift);
            } else if (key == KEY_Alt || key == KEY_LeftMenu || key == KEY_RightMenu) {
                kb.modifiers = KeyboardModifier(KeyboardModifier::Alt);
            }
        }
    } else {
        kb.key = key;
        kb.modifiers = mods;
    }

    // Ignore a completely empty keyboard bind.
    if (kb.key == KEY_None && kb.modifiers == KeyboardModifier::None) {
        return false;
    }

    m_pendingKeyboard = kb;
    // A keyboard bind replaces the extended bind.
    m_pendingExtended = ExtendedKeybind{};
    exitRecordingMode();
    this->refreshDisplay();
    return true;
}

bool ExtendedKeybindEditPopup::captureMouse(MouseButton btn, KeyboardModifier mods) {
    ExtendedKeybind ext;
    ext.kind = ExtendedKind::Mouse;
    ext.button = btn;
    ext.modifiers = mods;

    m_pendingExtended = ext;
    // A mouse bind replaces the keyboard bind.
    m_pendingKeyboard.reset();
    exitRecordingMode();
    this->refreshDisplay();
    return true;
}

bool ExtendedKeybindEditPopup::captureScroll(bool up, KeyboardModifier mods) {
    if (!m_allowScroll) return false;

    ExtendedKeybind ext;
    ext.kind = up ? ExtendedKind::ScrollUp : ExtendedKind::ScrollDown;
    ext.modifiers = mods;

    m_pendingExtended = ext;
    m_pendingKeyboard.reset();
    exitRecordingMode();
    this->refreshDisplay();
    return true;
}

}
