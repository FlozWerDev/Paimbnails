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

// ────────────────────────────────────────────────────────────────────────
// Estado global
// ────────────────────────────────────────────────────────────────────────
namespace {

// Modificadores actuales — resincronizados desde KeyboardInputEvent y
// MouseInputEvent. Son los mismos flags que usa VolumeScrollHook (que tiene
// su propio mirror para no acoplarse), pero aqui mantenemos un duplicado
// porque ambos modulos se inicializan independientemente y queremos que
// cada uno sea autocontenido.
KeyboardModifier g_currentMods{};

// Set de botones del mouse presionados.
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

// Lista canonica de settings keybind manejados por el mod. Si se agregan
// mas en mod.json, deben listarse aqui para que el dispatcher de triggers
// los considere.
//
// Mantenida sincronizada manualmente con mod.json — un comentario abajo de
// cada bloque te lo recuerda.
std::vector<std::string> const& managedList() {
    static std::vector<std::string> const kKeys = {
        // gameplay / capture
        "capture-keybind",
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
        // volume scroll (hold-while-scroll, no usan trigger-event)
        "volume-music-mod-game",
        "volume-sfx-mod-game",
        "volume-music-mod-editor",
        "volume-sfx-mod-editor",
    };
    return kKeys;
}

// Subset: keybinds que SI deben recibir trigger-events extendidos. Los del
// volume-scroll se excluyen porque su flujo es "hold + scroll", no "trigger
// instantaneo".
std::vector<std::string> const& triggerOnlyList() {
    static std::vector<std::string> const kKeys = {
        "capture-keybind",
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

// ────────────────────────────────────────────────────────────────────────
// ExtendedKeybind public methods
// ────────────────────────────────────────────────────────────────────────

std::string ExtendedKeybind::toDisplayString() const {
    if (kind == ExtendedKind::None) return "";
    auto prefix = modifiersPrefix(modifiers);
    switch (kind) {
        case ExtendedKind::None: return "";
        case ExtendedKind::Keyboard: {
            // Para keys del teclado preferimos delegar al toString de Geode.
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

// ────────────────────────────────────────────────────────────────────────
// Formato de Keybind nativo
// ────────────────────────────────────────────────────────────────────────

std::string formatKeyboardKeybind(Keybind const& kb) {
    bool const hasKey = (kb.key != KEY_None);
    bool const hasMods = (kb.modifiers != KeyboardModifier::None);
    if (!hasKey && !hasMods) return "";

    if (!hasKey) {
        // Solo modificador (ej. "Ctrl" como hold-key). Geode formatea esto
        // como "Ctrl+Unknown" porque key=KEY_None; preferimos solo los
        // modificadores sin "+Unknown" colgando al final.
        std::string out = modifiersPrefix(kb.modifiers);
        if (!out.empty() && out.back() == '+') out.pop_back();
        return out;
    }

    return kb.toString();
}

// ────────────────────────────────────────────────────────────────────────
// Persistencia
// ────────────────────────────────────────────────────────────────────────

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
        // Limpiar — Geode no expone una API publica para borrar saved values,
        // asi que guardamos un objeto vacio. loadExtendedKeybind lo trata
        // como ausente.
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

// ────────────────────────────────────────────────────────────────────────
// Estado del mouse / mods
// ────────────────────────────────────────────────────────────────────────

bool isMouseButtonHeld(MouseButton button) {
    int idx = static_cast<int>(button);
    if (!isMouseButtonIndexValid(idx)) return false;

#ifdef GEODE_IS_WINDOWS
    // Sincronizamos con el estado real del SO via GetAsyncKeyState. Esto
    // evita que el estado quede `true` permanentemente si el evento Release
    // se descarta cuando GD pierde el foco (loader/src/platform/windows/
    // input.cpp limpia `RawInputQueue` mientras la ventana no esta en
    // foreground, lo que silenciosamente descarta el Release del raton).
    //
    // Si no resincronizaramos, volume-scroll (que consulta isExtendedHeld)
    // veria el boton como "presionado para siempre" y aplicaria cambios
    // de volumen al hacer scroll sin que el usuario lo este sosteniendo.
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

// ────────────────────────────────────────────────────────────────────────
// Match
// ────────────────────────────────────────────────────────────────────────

namespace {
    // Subset check: required ⊆ current (no exigimos modificadores extra ausentes).
    bool modsMatchSubset(KeyboardModifier required, KeyboardModifier current) {
        return (current.value & required.value) == required.value;
    }
}

bool isExtendedHeld(ExtendedKeybind const& bind) {
    if (bind.isEmpty()) return false;
    if (bind.isScrollTrigger()) return false; // scroll no es "hold"

    if (bind.kind == ExtendedKind::Mouse) {
        if (!isMouseButtonHeld(bind.button)) return false;
        if (!modsMatchSubset(bind.modifiers, g_currentMods)) return false;
        return true;
    }

    // Keyboard kind: VolumeScrollHook ya consulta el set g_keysDown del modulo
    // de volume-scroll, por lo que NO duplicamos la verificacion aqui.
    // Un bind extended de tipo Keyboard se considera siempre inactivo desde
    // el punto de vista de este helper (su pareja nativa cubre el caso).
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

// ────────────────────────────────────────────────────────────────────────
// Lista publica
// ────────────────────────────────────────────────────────────────────────

std::vector<std::string> const& allManagedKeybinds() {
    return managedList();
}

// ────────────────────────────────────────────────────────────────────────
// Trigger broadcast
// ────────────────────────────────────────────────────────────────────────

void emitExtendedTrigger(std::string_view settingKey, double timestamp) {
    // 1) Evento custom para listeners propios del mod (puntos donde
    //    queremos hacer logica especifica de mouse/scroll).
    ExtendedKeybindTriggerEvent(std::string(settingKey)).send(timestamp);

    // 2) Re-disparamos el evento nativo de Geode con un Keybind sintetico
    //    para que los listeners ya existentes (`KeybindSettingPressedEventV3`)
    //    tambien reaccionen sin tener que duplicar codigo en cada call site.
    //
    //    La firma de send() para el evento filtrado es:
    //      send(Keybind const&, bool down, bool repeat, double timestamp)
    //    y se construye con (modID, settingKey) para que solo los listeners
    //    de este setting reciban el evento.
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

    // El "release" se envia inmediatamente despues para mantener simetria
    // con un click instantaneo (el listener tipico ignora el release pero
    // algunos lo usan para limpiar estado).
    KeybindSettingPressedEventV3(modID, settingKeyStr).send(
        synthetic,
        /*down=*/false,
        /*repeat=*/false,
        timestamp
    );
}

// ────────────────────────────────────────────────────────────────────────
// Inicializacion del sistema
// ────────────────────────────────────────────────────────────────────────

void initExtendedKeybindSystem() {
    if (g_systemInitialized) return;
    g_systemInitialized = true;

    // Listener de teclado: solo para mantener `g_currentMods` al dia.
    KeyboardInputEvent().listen(+[](KeyboardInputData& data) {
        g_currentMods = data.modifiers;
        return false;
    }).leak();

    // Listener de mouse: actualizar estado de botones y disparar triggers.
    MouseInputEvent().listen(+[](MouseInputData& data) {
        // Mantener mods sincronizados.
        g_currentMods = data.modifiers;

        int idx = static_cast<int>(fromGeodeMouseButton(data.button));
        if (!isMouseButtonIndexValid(idx)) return false;

        bool isPress = (data.action == MouseInputData::Action::Press);
        g_mouseDown[idx] = isPress;

        // Solo nos interesa el evento de Press para disparar el trigger.
        //
        // IMPORTANTE: NO filtramos por `wasDown` (el estado previo).
        //
        // Geode entrega un MouseInputEvent por cada accion real del raton y
        // nunca emite dos Press consecutivos sin un Release intermedio.
        // Sin embargo, `g_mouseDown[idx]` puede quedar atorado en `true`
        // si el usuario alt-tabbea mientras mantiene el boton presionado:
        // en ese caso `RawInputQueue` se limpia silenciosamente
        // (loader/src/platform/windows/input.cpp `pumpRawInput`) y el
        // evento Release jamas llega a este listener. Filtrar por
        // `wasDown` haria que el siguiente right-click despues del alt-tab
        // se ignore, dando la sensacion de que el keybind dejo de existir
        // para los usuarios que cambian de ventana con el raton presionado
        // (ver bug "click derecho no dispara atajo/captura para algunos").
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

// ────────────────────────────────────────────────────────────────────────
// Scroll captor (usado por ExtendedKeybindEditPopup)
// ────────────────────────────────────────────────────────────────────────

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
