#include "../services/VolumeScrollManager.hpp"
#include "../../../utils/ExtendedKeybind.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCMouseDispatcher.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/utils/Keyboard.hpp>

#include <unordered_set>

#ifdef GEODE_IS_WINDOWS
    // Needed for GetAsyncKeyState — re-syncs modifier flags before a scroll so they don't
    // stay stuck "true" when GD loses focus while a key is held.
    #include <windows.h>
#endif

using namespace geode::prelude;
using namespace cocos2d;
using paimon::volscroll::VolumeKind;
using paimon::volscroll::VolumeScrollManager;

// Exported by QuickHubKeybind to cancel the Ctrl hold when it's used for Ctrl+Scroll volume (not the radial).

namespace paimon::quickhub {
    void notifyVolumeScrollUsed();
}

// Pause-zoom hook exposed from src/hooks/PlayLayer.cpp.
namespace paimon::pausezoom {
    void dispatchScroll(float y, float x);
}

namespace {
    constexpr float kVolumeStep = 0.05f; // 5% per click

    // Modifier state, updated in two places for resilience:
    // onModifierKeysChanged and the KeyboardInputEvent listener.
    bool g_ctrlDown  = false;
    bool g_shiftDown = false;
    bool g_altDown   = false;

    // Set of currently-pressed non-modifier keys.
    std::unordered_set<int> g_keysDown;

    // Settings keys
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

    // Read the first Keybind from a KeybindSettingV3 setting.
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

    // Convert a modifier key into its KeyboardModifier, or None.
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

    // Is the Keybind active (all its keys and modifiers held)? Geode stores binds in several
    // shapes, so we move a modifier "key" into modifiers, require the bind's key (if any) and all
    // its modifiers to be held, but don't require an exact modifier match (avoids stuck-Lock issues).
    bool isKeybindActive(Keybind bind) {
        // Step 1: normalize
        auto extra = keyToModifier(bind.key);
        if (extra != KeyboardModifier::None) {
            bind.modifiers = bind.modifiers | extra;
            bind.key = KEY_None;
        }

        // Step 2: empty bind = never active
        if (bind.key == KEY_None && bind.modifiers == KeyboardModifier::None) {
            return false;
        }

        auto cur = currentModifiers();

        // Step 3: the bind's key must be held (if any)
        if (bind.key != KEY_None) {
            if (g_keysDown.count(static_cast<int>(bind.key)) == 0) {
                return false;
            }
        }

        // Step 4: all the bind's modifiers must be held (subset check)
        if ((cur.value & bind.modifiers.value) != bind.modifiers.value) {
            return false;
        }

        return true;
    }

    // Re-sync the modifier flags with the real OS state. On Windows this avoids
    // stuck flags when GD lost focus mid-keypress (dropped Release). No-op elsewhere.
    void resyncModifiersFromOS() {
#ifdef GEODE_IS_WINDOWS
        g_ctrlDown  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        g_shiftDown = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
        g_altDown   = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
#endif
    }

    // True if a music/sfx volume keybind (for the current game/editor context) is
    // currently held. Mirrors the matching the hook performs. outKind = matched kind.
    bool matchVolumeGesture(VolumeKind& outKind) {
        bool editor = isInEditor();
        char const* musicKey = editor ? kMusicEditorKey : kMusicGameKey;
        char const* sfxKey   = editor ? kSFXEditorKey   : kSFXGameKey;

        Keybind musicBind = getKeybind(musicKey);
        Keybind sfxBind   = getKeybind(sfxKey);
        auto musicExt = paimon::keybinds::loadExtendedKeybind(musicKey);
        auto sfxExt   = paimon::keybinds::loadExtendedKeybind(sfxKey);

        if (isKeybindActive(musicBind) || paimon::keybinds::isExtendedHeld(musicExt)) {
            outKind = VolumeKind::Music;
            return true;
        }
        if (isKeybindActive(sfxBind) || paimon::keybinds::isExtendedHeld(sfxExt)) {
            outKind = VolumeKind::SFX;
            return true;
        }
        return false;
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

    // Used by smooth-scroll to detect a volume-scroll gesture (Ctrl/Shift + wheel)
    // and bypass smoothing, so the wheel tick reaches the volume hook as a single
    // discrete step instead of being replayed as many momentum steps.
    bool isVolumeGestureActive() {
        resyncModifiersFromOS();
        VolumeKind kind;
        return matchVolumeGesture(kind);
    }
}

// KeyboardInputEvent listener — tracks keys and keeps modifiers in sync
// (data.modifiers is more reliable than dispatchKeyboardMSG).

$execute {
    KeyboardInputEvent().listen(+[](KeyboardInputData& data) {
        // Re-sync modifiers from data.modifiers (authoritative); cast to uint8_t avoids operator!= ambiguity.
        uint8_t m = data.modifiers.value;
        g_ctrlDown  = (m & uint8_t(KeyboardModifier::Control)) != 0;
        g_shiftDown = (m & uint8_t(KeyboardModifier::Shift))   != 0;
        g_altDown   = (m & uint8_t(KeyboardModifier::Alt))     != 0;

        // Track the key in g_keysDown (modifiers handled by the flags above).
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
        return false; // don't consume the event
    }).leak();

    // Also listen to MouseInputEvent to keep modifiers current (clicks carry modifiers too).
    MouseInputEvent().listen(+[](MouseInputData& data) {
        uint8_t m = data.modifiers.value;
        g_ctrlDown  = (m & uint8_t(KeyboardModifier::Control)) != 0;
        g_shiftDown = (m & uint8_t(KeyboardModifier::Shift))   != 0;
        g_altDown   = (m & uint8_t(KeyboardModifier::Alt))     != 0;
        return false;
    }).leak();
}

// Hook: CCMouseDispatcher::dispatchScrollMSG. Inline on macOS/iOS, so this hook is Windows-only.

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

        // 1) ExtendedKeybind scroll captor: if a recording popup is open, forward scroll and consume it.
        if (paimon::keybinds::hasScrollCaptor()) {
            auto const& captor = paimon::keybinds::currentScrollCaptor();
            if (captor) {
                bool consumed = captor(static_cast<double>(y),
                                       paimon::keybinds::currentModifiers());
                if (consumed) return true;
            }
        }

        // Re-sync modifiers from the most authoritative source here. Critical: stuck g_ctrlDown
        // flags (e.g. GD lost focus mid-keypress, dropping the Release) could otherwise trigger
        // volume scroll without the modifier held. Replace the flags with the real OS state.
        resyncModifiersFromOS();

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
        // Try in order: keyboard keybind, then extended (mouse).
        if (isKeybindActive(musicBind) || paimon::keybinds::isExtendedHeld(musicExt)) {
            kind = VolumeKind::Music;
            match = true;
        } else if (isKeybindActive(sfxBind) || paimon::keybinds::isExtendedHeld(sfxExt)) {
            kind = VolumeKind::SFX;
            match = true;
        }

        if (!match) {
            // Before passing scroll to the game, let ExtendedKeybind dispatch scroll-as-trigger.
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

// Ticker

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
