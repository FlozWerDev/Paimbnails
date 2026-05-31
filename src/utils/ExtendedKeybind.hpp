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

// ────────────────────────────────────────────────────────────────────────────
// ExtendedKeybind — un binding que extiende el Keybind nativo de Geode con
// soporte para botones de mouse (left/right/middle/side) y scroll wheel.
//
// Por que existe: Geode v5 solo permite asignar teclas del teclado a un
// keybind. `MouseInputData::buttonToKeyCode` retorna `KEY_None` para los
// clicks Left/Right/Middle (solo Button4/Button5 estan mapeados a MOUSE_4/5),
// y `KeybindEditPopup` no escucha `ScrollWheelEvent`. Esto significa que el
// usuario no puede asignar click derecho ni scroll desde el popup nativo.
//
// Solucion: por cada setting `keybind` del mod, guardamos AL LADO un
// `ExtendedKeybind` opcional en saved-values con clave
// `paimon-extkb-{settingKey}`. La logica de match consulta AMBOS bindings y
// el keybind se considera activo si cualquiera de los dos hace match.
//
// El custom popup (`ExtendedKeybindEditPopup`) escribe al binding correcto
// segun lo que el usuario presiono:
//   - Tecla del teclado → setting nativo de Geode (KeybindSettingV3).
//   - Boton del mouse / scroll → saved-value `paimon-extkb-{key}`.
// ────────────────────────────────────────────────────────────────────────────

enum class ExtendedKind : int {
    None      = 0,  // sin binding extendido
    Keyboard  = 1,  // (key + mods) — duplica el nativo, solo para casos avanzados
    Mouse     = 2,  // boton del mouse + mods
    ScrollUp  = 3,  // scroll hacia arriba + mods (solo trigger, no hold)
    ScrollDown = 4, // scroll hacia abajo + mods (solo trigger, no hold)
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
    cocos2d::enumKeyCodes key = cocos2d::KEY_None; // valido si kind=Keyboard
    MouseButton button = MouseButton::Left;        // valido si kind=Mouse
    geode::KeyboardModifier modifiers{};

    bool isEmpty() const { return kind == ExtendedKind::None; }
    bool isMouseHold() const { return kind == ExtendedKind::Mouse; }
    bool isScrollTrigger() const {
        return kind == ExtendedKind::ScrollUp || kind == ExtendedKind::ScrollDown;
    }

    // Texto humano para mostrar en la UI ("Right Click", "Ctrl+Scroll Up", ...)
    std::string toDisplayString() const;
};

// ── Formato de Keybind nativo ───────────────────────────────────────────
// Formatea un `geode::Keybind` para la UI. A diferencia de `Keybind::toString()`,
// maneja correctamente los binds compuestos solo por modificadores
// (ej. `key=KEY_None, modifiers=Ctrl`), devolviendo "Ctrl" en lugar de
// "Ctrl+Unknown". Si el keybind esta totalmente vacio devuelve "".
std::string formatKeyboardKeybind(geode::Keybind const& kb);

// ── Persistencia ────────────────────────────────────────────────────────
// Lee el ExtendedKeybind asociado a un setting. Si no existe o esta corrupto,
// devuelve uno con kind=None.
ExtendedKeybind loadExtendedKeybind(std::string_view settingKey);

// Guarda. Pasar uno con kind=None para borrar.
void saveExtendedKeybind(std::string_view settingKey, ExtendedKeybind const& bind);

// ── Estado de mouse ─────────────────────────────────────────────────────
// Verdadero si el boton del mouse esta presionado en este momento.
bool isMouseButtonHeld(MouseButton button);

// Modificadores actualmente presionados (resincronizados desde
// KeyboardInputEvent y MouseInputEvent — son los mismos flags que usa el
// resto del codigo del volume-scroll).
geode::KeyboardModifier currentModifiers();

// ── Match de extended bindings ──────────────────────────────────────────
// "Hold-style" — el binding debe estar siendo sostenido en este momento.
// Se usa por VolumeScrollHook para decidir si convertir un scroll del mouse
// en un cambio de volumen.
//
// Reglas:
//   - kind=None → false
//   - kind=Keyboard → tecla en g_keysDown y modificadores ⊆ actuales
//   - kind=Mouse → boton del mouse presionado y modificadores ⊆ actuales
//   - kind=ScrollUp/Down → false (un scroll instantaneo no es "hold")
bool isExtendedHeld(ExtendedKeybind const& bind);

// "Trigger-style" — el binding hizo match con el evento dado. Se usa para
// dispatch en respuesta a click de mouse o tick de scroll wheel.
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

// ── Trigger event broadcast ─────────────────────────────────────────────
// Cuando un click de mouse o un scroll matchea con el ExtendedKeybind de
// algun setting, este evento dispara los listeners equivalentes a
// KeybindSettingPressedEventV3 (sin reemplazarlo — los listeners del
// teclado nativos siguen funcionando).
//
// Modulos como capture-keybind, zoom, layout-editor, etc. consumen este
// evento ademas del nativo para reaccionar a click derecho / scroll.
class ExtendedKeybindTriggerEvent final
    : public geode::Event<ExtendedKeybindTriggerEvent, bool(double timestamp), std::string>
{
public:
    using Event::Event;
};

// Emite el evento para `settingKey` desde el listener global. No es necesario
// llamarla manualmente — el dispatcher interno la invoca al detectar mouse/
// scroll que matchea con algun ExtendedKeybind registrado.
void emitExtendedTrigger(std::string_view settingKey, double timestamp);

// ── Lista de keybinds del mod ───────────────────────────────────────────
// Iteramos esta lista para saber que settings tienen ExtendedKeybind y a
// cuales hay que mandar `emitExtendedTrigger` cuando llega un evento.
//
// Si en el futuro se agregan mas keybinds al mod, basta con registrarlos
// aqui (manteniendo la lista en un solo lugar).
std::vector<std::string> const& allManagedKeybinds();

// ── Inicializacion ──────────────────────────────────────────────────────
// Llamada UNA vez desde Bootstrap.cpp / FrameworkInit.cpp. Registra los
// listeners globales de KeyboardInputEvent / MouseInputEvent para mantener
// `g_mouseButtonsDown` y los modificadores al dia, y para disparar
// `ExtendedKeybindTriggerEvent` cuando haga falta.
void initExtendedKeybindSystem();

// ── Helpers para dispatchScrollMSG ──────────────────────────────────────
// Revisa todos los settings registrados y dispara el evento extendido si
// alguno hace match con el scroll. Retorna true si al menos uno matcheo
// (para que el caller pueda decidir si "consumir" el scroll). Solo dispara
// triggers — no toca volume-scroll.
//
// Nota: y > 0 = scroll arriba, y < 0 = scroll abajo (convencion Geode).
bool dispatchScrollAsTrigger(double y, double timestamp);

// ── Captura de scroll para el popup ─────────────────────────────────────
// El popup `ExtendedKeybindEditPopup` puede registrarse como "captor" de
// scroll mientras esta en modo recording. Si hay un captor activo, el
// VolumeScrollHook redirige el evento de scroll al callback en lugar de
// procesarlo como cambio de volumen / trigger.
//
// Solo un captor activo a la vez (el ultimo en registrarse gana). Pasar
// nullptr para desregistrarse.
using ScrollCaptureCallback = std::function<bool(double y, geode::KeyboardModifier mods)>;
void setScrollCaptor(ScrollCaptureCallback callback);
ScrollCaptureCallback const& currentScrollCaptor();
bool hasScrollCaptor();

} // namespace paimon::keybinds
