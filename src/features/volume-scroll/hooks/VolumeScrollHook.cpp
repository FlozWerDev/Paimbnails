#include "../services/VolumeScrollManager.hpp"
#include "../../../utils/ExtendedKeybind.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCMouseDispatcher.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/utils/Keyboard.hpp>
#include <Geode/utils/cocos.hpp>

#include <unordered_set>

#ifdef GEODE_IS_WINDOWS
    // Necesario para GetAsyncKeyState — usado en el re-sync de
    // modificadores antes de evaluar un scroll, para evitar que los
    // flags de modifier queden atascados en `true` cuando GD pierde foco
    // mientras una tecla está pulsada (mismo patrón que ExtendedKeybind
    // usa para los botones del mouse).
    #include <windows.h>
#endif

using namespace geode::prelude;
using namespace cocos2d;
using paimon::volscroll::VolumeKind;
using paimon::volscroll::VolumeScrollManager;

// ────────────────────────────────────────────────────────────────────────
// Función exportada por QuickHubKeybind para cancelar el hold del Ctrl
// cuando detectamos que el usuario lo está usando para Ctrl+Scroll de
// volumen (queremos el scroll del volumen, no el radial).
// ────────────────────────────────────────────────────────────────────────

namespace paimon::quickhub {
    void notifyVolumeScrollUsed();
}

// Hook de pause-zoom expuesto desde src/hooks/PlayLayer.cpp.
namespace paimon::pausezoom {
    void dispatchScroll(float y, float x);
}

namespace {
    constexpr float kVolumeStep = 0.05f; // 5% por click

    // Estado de modificadores. Lo actualizamos en DOS lugares para
    // resiliencia:
    //   - paimon::volscroll::onModifierKeysChanged (que llama
    //     QuickHubModifierHook desde su $modify de updateModifierKeys).
    //   - El listener KeyboardInputEvent (que recibe data.modifiers en
    //     cada evento de teclado).
    // Si uno falla, el otro mantiene el estado al dia.
    bool g_ctrlDown  = false;
    bool g_shiftDown = false;
    bool g_altDown   = false;

    // Set de teclas no-modificadoras actualmente presionadas.
    std::unordered_set<int> g_keysDown;

    // ── Settings keys ──────────────────────────────────────────────────
    constexpr char const* kMusicGameKey   = "volume-music-mod-game";
    constexpr char const* kSFXGameKey     = "volume-sfx-mod-game";
    constexpr char const* kMusicEditorKey = "volume-music-mod-editor";
    constexpr char const* kSFXEditorKey   = "volume-sfx-mod-editor";

    bool isInEditor() {
        auto* director = CCDirector::get();
        if (!director) return false;
        auto* scene = director->getRunningScene();
        if (!scene) return false;
        return scene->getChildByType<LevelEditorLayer>(0) != nullptr;
    }

    // Lee el primer Keybind de un setting tipo KeybindSettingV3.
    Keybind getKeybind(char const* key) {
        auto* mod = Mod::get();
        if (!mod || !mod->hasSetting(key)) return {};
        auto setting = cast::typeinfo_pointer_cast<KeybindSettingV3>(mod->getSetting(key));
        if (!setting) return {};
        auto const& binds = setting->getValue();
        if (binds.empty()) return {};
        return binds.front();
    }

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

    KeyboardModifier currentModifiers() {
        uint8_t m = KeyboardModifier::None;
        if (g_ctrlDown)  m |= KeyboardModifier::Control;
        if (g_shiftDown) m |= KeyboardModifier::Shift;
        if (g_altDown)   m |= KeyboardModifier::Alt;
        return KeyboardModifier(m);
    }

    // Convierte una key modifier en su KeyboardModifier asociado, o None.
    KeyboardModifier keyToModifier(enumKeyCodes k) {
        switch (k) {
            case KEY_Control: case KEY_LeftControl: case KEY_RightContol:
                return KeyboardModifier::Control;
            case KEY_Shift: case KEY_LeftShift: case KEY_RightShift:
                return KeyboardModifier::Shift;
            case KEY_Alt: case KEY_LeftMenu: case KEY_RightMenu:
                return KeyboardModifier::Alt;
            default: return KeyboardModifier::None;
        }
    }

    // ¿Esta el Keybind activo (todas sus teclas y modificadores presentes)?
    //
    // Geode guarda los Keybind de varias formas dependiendo de como se
    // creo el binding:
    //   - "Ctrl" puro → puede ser {KEY_None, Control} o {KEY_Control, Control}.
    //   - "Ctrl+T"   → {KEY_T, Control}.
    //   - "T"        → {KEY_T, None}.
    //
    // Para hacer match robusto:
    //   1. Si la "key" del bind es un modificador, lo movemos a "modifiers"
    //      (asi "Ctrl" como key puro pasa a ser solo-modificador).
    //   2. Empty bind → nunca activo.
    //   3. Si bind tiene key, esa key debe estar pulsada.
    //   4. Todos los modificadores del bind deben estar pulsados.
    //   5. NO exigimos que los modificadores sean exactos — si el bind es
    //      "Ctrl" y el user tiene Ctrl+Shift, hace match igual. Eso es lo
    //      que el usuario espera intuitivamente y evita problemas con
    //      el modifier "Lock" pegado tras perder foco.
    bool isKeybindActive(Keybind bind) {
        // Paso 1: normalizar
        auto extra = keyToModifier(bind.key);
        if (extra != KeyboardModifier::None) {
            bind.modifiers = bind.modifiers | extra;
            bind.key = KEY_None;
        }

        // Paso 2: empty bind = nunca activo
        if (bind.key == KEY_None && bind.modifiers == KeyboardModifier::None) {
            return false;
        }

        auto cur = currentModifiers();

        // Paso 3: la tecla del bind debe estar pulsada (si tiene)
        if (bind.key != KEY_None) {
            if (g_keysDown.count(static_cast<int>(bind.key)) == 0) {
                return false;
            }
        }

        // Paso 4: todos los modificadores del bind deben estar pulsados
        // (subset check: bind.modifiers ⊆ cur.modifiers)
        if ((cur.value & bind.modifiers.value) != bind.modifiers.value) {
            return false;
        }

        return true;
    }
}

namespace paimon::volscroll {
    void onModifierKeysChanged(bool shft, bool ctrl, bool alt, bool cmd) {
#ifdef GEODE_IS_MACOS
        g_ctrlDown = ctrl || cmd;
#else
        (void)cmd;
        g_ctrlDown = ctrl;
#endif
        g_shiftDown = shft;
        g_altDown   = alt;
    }
}

// ────────────────────────────────────────────────────────────────────────
// KeyboardInputEvent listener — Trackea teclas y mantiene los modifiers
// sincronizados.
//
// Geode envia data.modifiers en cada evento de teclado, lo que es mas
// confiable que dispatchKeyboardMSG (que solo da la key, sin modifiers).
// ────────────────────────────────────────────────────────────────────────

$execute {
    KeyboardInputEvent().listen(+[](KeyboardInputData& data) {
        // Re-sincronizar modificadores desde data.modifiers (autoritativo).
        // El cast a uint8_t evita ambiguedades de operator!= con int.
        uint8_t m = data.modifiers.value;
        g_ctrlDown  = (m & uint8_t(KeyboardModifier::Control)) != 0;
        g_shiftDown = (m & uint8_t(KeyboardModifier::Shift))   != 0;
        g_altDown   = (m & uint8_t(KeyboardModifier::Alt))     != 0;

        // Trackear la tecla en g_keysDown (excluyendo modificadores —
        // esos se manejan con los flags de arriba).
        if (!isModifierKey(data.key)) {
            switch (data.action) {
                case KeyboardInputData::Action::Press:
                case KeyboardInputData::Action::Repeat:
                    g_keysDown.insert(static_cast<int>(data.key));
                    break;
                case KeyboardInputData::Action::Release:
                    g_keysDown.erase(static_cast<int>(data.key));
                    break;
            }
        }
        return false; // no consumimos el evento
    }).leak();

    // Tambien escuchamos MouseInputEvent para mantener modifiers al dia
    // (los clicks tambien llevan modifiers en data).
    MouseInputEvent().listen(+[](MouseInputData& data) {
        uint8_t m = data.modifiers.value;
        g_ctrlDown  = (m & uint8_t(KeyboardModifier::Control)) != 0;
        g_shiftDown = (m & uint8_t(KeyboardModifier::Shift))   != 0;
        g_altDown   = (m & uint8_t(KeyboardModifier::Alt))     != 0;
        return false;
    }).leak();
}

// ────────────────────────────────────────────────────────────────────────
// Hook: CCMouseDispatcher::dispatchScrollMSG
// ────────────────────────────────────────────────────────────────────────
// NOTA: dispatchScrollMSG es inline en macOS/iOS, por lo que el hook
//       solo se compila en Windows. En otras plataformas el scroll de
//       volumen no está disponible por ahora.
// ────────────────────────────────────────────────────────────────────────

#ifdef GEODE_IS_WINDOWS
class $modify(PaimonVolumeScrollMouseHook, CCMouseDispatcher) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("cocos2d::CCMouseDispatcher::dispatchScrollMSG",
                                       geode::Priority::Early);
    }

    bool dispatchScrollMSG(float y, float x) {
        auto passthrough = [&]() -> bool {
            paimon::pausezoom::dispatchScroll(y, x);
            return CCMouseDispatcher::dispatchScrollMSG(y, x);
        };

        if (y == 0.f) return passthrough();

        // ── 1) Captor de scroll del ExtendedKeybind ────────────────────
        // Si hay un popup de edicion en modo recording, le pasamos el
        // scroll directamente y consumimos el evento.
        if (paimon::keybinds::hasScrollCaptor()) {
            auto const& captor = paimon::keybinds::currentScrollCaptor();
            if (captor) {
                bool consumed = captor(static_cast<double>(y),
                                       paimon::keybinds::currentModifiers());
                if (consumed) return true;
            }
        }

        // Re-sincronizar modificadores desde la fuente más autoritativa
        // disponible en este punto. Esto es CRÍTICO: sin este re-sync,
        // el scroll *podría* activar el cambio de volumen sin la tecla
        // modificadora pulsada porque los flags `g_ctrlDown` etc.
        // pueden quedar atascados en `true` si:
        //   - GD pierde foco mientras la tecla Ctrl/Shift estaba pulsada
        //     (loader/src/platform/windows/input.cpp limpia
        //     `RawInputQueue` y descarta el evento Release).
        //   - Algún popup intermedio bloquea los `KeyboardInputEvent`
        //     antes de que lleguen al listener global.
        //
        // Antes este re-sync usaba `kb->getControlKeyPressed() || g_ctrlDown`
        // (sticky-OR) lo que NUNCA reseteaba los flags a false; bastaba
        // con que un solo evento espurio dejara `g_ctrlDown=true` para
        // que cualquier scroll posterior se interpretara como
        // Ctrl+Scroll, aunque el usuario hubiera soltado la tecla hace
        // rato.
        //
        // El fix es REEMPLAZAR los flags con el estado real:
        //  - En Windows usamos `GetAsyncKeyState` (mismo patrón que
        //    `isMouseButtonHeld` en ExtendedKeybind.cpp).
        //  - En el resto de plataformas confiamos en
        //    `CCKeyboardDispatcher`, que sí refleja el release aunque
        //    Geode no haya despachado el evento todavía.
        {
            bool ctrlOS  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shiftOS = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
            bool altOS   = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
            g_ctrlDown  = ctrlOS;
            g_shiftDown = shiftOS;
            g_altDown   = altOS;
        }

        bool editor = isInEditor();
        char const* musicKey = editor ? kMusicEditorKey : kMusicGameKey;
        char const* sfxKey   = editor ? kSFXEditorKey   : kSFXGameKey;

        Keybind musicBind = getKeybind(musicKey);
        Keybind sfxBind   = getKeybind(sfxKey);

        auto musicExt = paimon::keybinds::loadExtendedKeybind(musicKey);
        auto sfxExt   = paimon::keybinds::loadExtendedKeybind(sfxKey);

        log::debug("[VolScroll] scroll y={:.2f} editor={} music={{kbKey={:#x},kbMods={:#x},extKind={}}} sfx={{kbKey={:#x},kbMods={:#x},extKind={}}} state ctrl={} shift={} alt={}",
            y, editor,
            (int)musicBind.key, (int)musicBind.modifiers.value, (int)musicExt.kind,
            (int)sfxBind.key,   (int)sfxBind.modifiers.value,   (int)sfxExt.kind,
            g_ctrlDown, g_shiftDown, g_altDown);

        VolumeKind kind;
        bool match = false;
        // Probamos en orden: keybind del teclado, luego extended (mouse).
        if (isKeybindActive(musicBind) || paimon::keybinds::isExtendedHeld(musicExt)) {
            kind = VolumeKind::Music;
            match = true;
        } else if (isKeybindActive(sfxBind) || paimon::keybinds::isExtendedHeld(sfxExt)) {
            kind = VolumeKind::SFX;
            match = true;
        }

        if (!match) {
            // Antes de pasar el scroll al juego, dejamos que el sistema de
            // ExtendedKeybind despache scroll-as-trigger (por ejemplo si el
            // usuario configuro "scroll up" como zoom-in-keybind).
            (void)paimon::keybinds::dispatchScrollAsTrigger(
                static_cast<double>(y),
                static_cast<double>(geode::utils::getInputTimestamp())
            );
            return passthrough();
        }

        log::info("[VolScroll] consuming scroll: kind={} y={}",
                  kind == VolumeKind::Music ? "music" : "sfx", y);

        const float delta = (y > 0.f) ? -kVolumeStep : +kVolumeStep;
        VolumeScrollManager::get().onScroll(kind, delta);

        if (g_ctrlDown) {
            paimon::quickhub::notifyVolumeScrollUsed();
        }
        return true;
    }
};
#endif

        if (y == 0.f) return passthrough();

        // ── 1) Captor de scroll del ExtendedKeybindEditPopup ────────────
        // Si hay un popup de edicion en modo recording, le pasamos el
        // scroll directamente y consumimos el evento.
        if (paimon::keybinds::hasScrollCaptor()) {
            auto const& captor = paimon::keybinds::currentScrollCaptor();
            if (captor) {
                bool consumed = captor(static_cast<double>(y),
                                       paimon::keybinds::currentModifiers());
                if (consumed) return true;
            }
        }

        // Re-sincronizar modificadores desde la fuente más autoritativa
        // disponible en este punto. Esto es CRÍTICO: sin este re-sync,
        // el scroll *podría* activar el cambio de volumen sin la tecla
        // modificadora pulsada porque los flags `g_ctrlDown` etc.
        // pueden quedar atascados en `true` si:
        //   - GD pierde foco mientras la tecla Ctrl/Shift estaba pulsada
        //     (loader/src/platform/windows/input.cpp limpia
        //     `RawInputQueue` y descarta el evento Release).
        //   - Algún popup intermedio bloquea los `KeyboardInputEvent`
        //     antes de que lleguen al listener global.
        //
        // Antes este re-sync usaba `kb->getControlKeyPressed() || g_ctrlDown`
        // (sticky-OR) lo que NUNCA reseteaba los flags a false; bastaba
        // con que un solo evento espurio dejara `g_ctrlDown=true` para
        // que cualquier scroll posterior se interpretara como
        // Ctrl+Scroll, aunque el usuario hubiera soltado la tecla hace
        // rato.
        //
        // El fix es REEMPLAZAR los flags con el estado real:
        //  - En Windows usamos `GetAsyncKeyState` (mismo patrón que
        //    `isMouseButtonHeld` en ExtendedKeybind.cpp).
        //  - En el resto de plataformas confiamos en
        //    `CCKeyboardDispatcher`, que sí refleja el release aunque
        //    Geode no haya despachado el evento todavía.
#ifdef GEODE_IS_WINDOWS
        {
            bool ctrlOS  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shiftOS = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
            bool altOS   = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
            g_ctrlDown  = ctrlOS;
            g_shiftDown = shiftOS;
            g_altDown   = altOS;
        }
#else
        if (auto* kb = CCKeyboardDispatcher::get()) {
            g_ctrlDown  = kb->getControlKeyPressed();
            g_shiftDown = kb->getShiftKeyPressed();
            g_altDown   = kb->getAltKeyPressed();
        }
#endif

        bool editor = isInEditor();
        char const* musicKey = editor ? kMusicEditorKey : kMusicGameKey;
        char const* sfxKey   = editor ? kSFXEditorKey   : kSFXGameKey;

        Keybind musicBind = getKeybind(musicKey);
        Keybind sfxBind   = getKeybind(sfxKey);

        auto musicExt = paimon::keybinds::loadExtendedKeybind(musicKey);
        auto sfxExt   = paimon::keybinds::loadExtendedKeybind(sfxKey);

        log::debug("[VolScroll] scroll y={:.2f} editor={} music={{kbKey={:#x},kbMods={:#x},extKind={}}} sfx={{kbKey={:#x},kbMods={:#x},extKind={}}} state ctrl={} shift={} alt={}",
            y, editor,
            (int)musicBind.key, (int)musicBind.modifiers.value, (int)musicExt.kind,
            (int)sfxBind.key,   (int)sfxBind.modifiers.value,   (int)sfxExt.kind,
            g_ctrlDown, g_shiftDown, g_altDown);

        VolumeKind kind;
        bool match = false;
        // Probamos en orden: keybind del teclado, luego extended (mouse).
        if (isKeybindActive(musicBind) || paimon::keybinds::isExtendedHeld(musicExt)) {
            kind = VolumeKind::Music;
            match = true;
        } else if (isKeybindActive(sfxBind) || paimon::keybinds::isExtendedHeld(sfxExt)) {
            kind = VolumeKind::SFX;
            match = true;
        }

        if (!match) {
            // Antes de pasar el scroll al juego, dejamos que el sistema de
            // ExtendedKeybind despache scroll-as-trigger (por ejemplo si el
            // usuario configuro "scroll up" como zoom-in-keybind).
            (void)paimon::keybinds::dispatchScrollAsTrigger(
                static_cast<double>(y),
                static_cast<double>(geode::utils::getInputTimestamp())
            );
            return passthrough();
        }

        log::info("[VolScroll] consuming scroll: kind={} y={}",
                  kind == VolumeKind::Music ? "music" : "sfx", y);

        const float delta = (y > 0.f) ? -kVolumeStep : +kVolumeStep;
        VolumeScrollManager::get().onScroll(kind, delta);

        if (g_ctrlDown) {
            paimon::quickhub::notifyVolumeScrollUsed();
        }
        return true;
    }
};

// ────────────────────────────────────────────────────────────────────────
// Ticker
// ────────────────────────────────────────────────────────────────────────

class VolumeScrollTickerNode : public CCNode {
    CCScene* m_lastScene = nullptr;
public:
    static VolumeScrollTickerNode* create() {
        auto ret = new VolumeScrollTickerNode();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
    bool init() override {
        if (!CCNode::init()) return false;
        this->setID("paimon-volume-scroll-ticker"_spr);
        return true;
    }
    void update(float dt) override {
        auto& mgr = VolumeScrollManager::get();
        mgr.update(dt);
        auto* scene = CCDirector::get()->getRunningScene();
        if (scene != m_lastScene) {
            m_lastScene = scene;
            mgr.onSceneChange();
        }
    }
};

static Ref<VolumeScrollTickerNode> s_volumeScrollTicker = nullptr;

void initVolumeScrollTicker() {
    if (s_volumeScrollTicker) return;
    auto* director = CCDirector::get();
    if (!director) return;
    auto* scheduler = director->getScheduler();
    if (!scheduler) return;
    s_volumeScrollTicker = VolumeScrollTickerNode::create();
    if (!s_volumeScrollTicker) return;
    scheduler->scheduleUpdateForTarget(s_volumeScrollTicker.data(), 0, false);
    log::info("[VolumeScroll] Ticker initialized");
}

void shutdownVolumeScrollTicker() {
    if (!s_volumeScrollTicker) return;
    if (auto* director = CCDirector::get()) {
        if (auto* scheduler = director->getScheduler()) {
            scheduler->unscheduleUpdateForTarget(s_volumeScrollTicker.data());
        }
    }
    VolumeScrollManager::get().releaseSharedResources();
    (void)s_volumeScrollTicker.take();
}

$on_game(Exiting) {
    shutdownVolumeScrollTicker();
}
