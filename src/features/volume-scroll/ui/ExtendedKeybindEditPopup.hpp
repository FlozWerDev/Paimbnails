#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/utils/Keyboard.hpp>

#include <functional>
#include <optional>
#include <string>

#include "../../../utils/ExtendedKeybind.hpp"

namespace paimon::volscroll {

// ────────────────────────────────────────────────────────────────────────
// ExtendedKeybindEditPopup
//
// Reemplaza el `KeybindEditPopup` nativo de Geode con uno que tambien
// captura clicks del mouse (left/right/middle/side) y, opcionalmente, el
// scroll wheel. El usuario presiona el boton "Record" y luego cualquier
// tecla/click/scroll del mundo real queda capturado.
//
// Cuando el usuario guarda:
//   - Si capturo una tecla del teclado → escribimos al `KeybindSettingV3`
//     nativo (compatibilidad con el resto del sistema).
//   - Si capturo mouse o scroll → escribimos al `ExtendedKeybind` del mod.
//
// Para keybinds tipo "hold + scroll" (volume scroll), pasamos
// `allowScroll=false` para que la opcion de scroll wheel no aparezca: en
// ese contexto el scroll mismo es la accion, no el trigger.
// ────────────────────────────────────────────────────────────────────────

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

    // Llamadas desde los listeners globales registrados en $execute (.cpp).
    // Devuelven true si el evento fue consumido.
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
    // Swap del sprite del boton "Record": rosa (GJ_button_03) cuando esta
    // idle y azul (GJ_button_02) cuando esta grabando, para que sea obvio
    // visualmente en que estado esta el popup.
    void updateRecordButtonAppearance();

    std::string m_settingKey;
    std::string m_title;
    SaveCallback m_onSave;
    bool m_allowScroll = true;

    // Edits "pendientes" — se guardan al pulsar Save.
    std::optional<geode::Keybind> m_pendingKeyboard;
    paimon::keybinds::ExtendedKeybind m_pendingExtended;

    bool m_isRecording = false;

    cocos2d::CCLabelBMFont* m_displayLabel = nullptr;
    cocos2d::CCLabelBMFont* m_hintLabel = nullptr;
    // CCMenuItemSpriteExtra es una clase de las bindings de Geode (no esta
    // en cocos2d::). Forward-declared en <Geode/binding/CCMenuItemSpriteExtra.hpp>
    // que se incluye via Popup.hpp.
    CCMenuItemSpriteExtra* m_recordButton = nullptr;
};

} // namespace paimon::volscroll
