#include <Geode/Geode.hpp>
#include <Geode/modify/CCMouseDispatcher.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/utils/Keyboard.hpp>

#include "../services/VolumeScrollManager.hpp"
#include "../../../utils/ExtendedKeybind.hpp"
#include "../../../utils/Debug.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"

#include <cmath>
#include <unordered_set>

#ifdef GEODE_IS_WINDOWS
// getasynckeystate re-syncs modifiers after focus loss drops release events.
    #include <windows.h>
#endif

using namespace geode::prelude;
using namespace cocos2d;
using paimon::volscroll::VolumeKind;
using paimon::volscroll::VolumeScrollManager;

// lets quickhubkeybind cancel ctrl-hold when ctrl+scroll changes volume.

namespace paimon::quickhub {
    void notifyVolumeScrollUsed();
}

namespace paimon::pausezoom {
    void dispatchScroll(float y, float x);
}

namespace {
constexpr float kVolumeStep = 0.05f;

// modifier state is updated by both keybind and keyboard listeners.
    bool g_ctrlDown  = false;
    bool g_shiftDown = false;
    bool g_altDown   = false;

    std::unordered_set<int> g_keysDown;

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

    // normalize geode's bind shapes, then require its key and modifier subset.
    bool isKeybindActive(Keybind bind) {
        auto extra = keyToModifier(bind.key);
        if (extra != KeyboardModifier::None) {
            bind.modifiers = bind.modifiers | extra;
            bind.key = KEY_None;
        }

        if (bind.key == KEY_None && bind.modifiers == KeyboardModifier::None) {
            return false;
        }

        auto cur = currentModifiers();

        if (bind.key != KEY_None) {
            if (g_keysDown.count(static_cast<int>(bind.key)) == 0) {
                return false;
            }
        }

        if ((cur.value & bind.modifiers.value) != bind.modifiers.value) {
            return false;
        }

        return true;
    }

    // re-sync modifiers from the os on windows; no-op elsewhere.
    void resyncModifiersFromOS() {
#ifdef GEODE_IS_WINDOWS
        g_ctrlDown  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        g_shiftDown = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
        g_altDown   = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
#endif
    }

    // whether the current-context music/sfx bind is held.
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

    // smooth-scroll reads this to bypass momentum on volume gestures.
    bool isVolumeGestureActive() {
        if (!paimon::modules::isEnabled("paimbnails.volumescroll.global")) return false;
        resyncModifiersFromOS();
        VolumeKind kind;
        return matchVolumeGesture(kind);
    }
}

$execute {
    KeyboardInputEvent().listen(+[](KeyboardInputData& data) {
        uint8_t m = data.modifiers.value;
        g_ctrlDown  = (m & uint8_t(KeyboardModifier::Control)) != 0;
        g_shiftDown = (m & uint8_t(KeyboardModifier::Shift))   != 0;
        g_altDown   = (m & uint8_t(KeyboardModifier::Alt))     != 0;

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
    return false;
    }).leak();

    MouseInputEvent().listen(+[](MouseInputData& data) {
        uint8_t m = data.modifiers.value;
        g_ctrlDown  = (m & uint8_t(KeyboardModifier::Control)) != 0;
        g_shiftDown = (m & uint8_t(KeyboardModifier::Shift))   != 0;
        g_altDown   = (m & uint8_t(KeyboardModifier::Alt))     != 0;
        return false;
    }).leak();
}

// hookable on desktop; on ios it is inlined, so the touch gestures below cover mobile.

#if defined(GEODE_IS_DESKTOP)
class $modify(PaimonVolumeScrollMouseHook, CCMouseDispatcher) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("cocos2d::CCMouseDispatcher::dispatchScrollMSG",
                                       geode::Priority::Early);
    }

    bool dispatchScrollMSG(float y, float x) {
        auto passthrough = [&]() -> bool {
            return CCMouseDispatcher::dispatchScrollMSG(y, x);
        };

        auto notOurs = [&]() -> bool {
            (void)paimon::keybinds::dispatchScrollAsTrigger(
                static_cast<double>(y),
                static_cast<double>(geode::utils::getInputTimestamp())
            );
            return passthrough();
        };

        if (y == 0.f) return passthrough();

        // let extendedkeybind capture scroll while a recording popup is open.
        if (paimon::keybinds::hasScrollCaptor()) {
            auto const& captor = paimon::keybinds::currentScrollCaptor();
            if (captor) {
                bool consumed = captor(static_cast<double>(y),
                                       paimon::keybinds::currentModifiers());
                if (consumed) return true;
            }
        }

        if (!paimon::modules::isEnabled("paimbnails.volumescroll.global")) return notOurs();

        // refresh os modifiers here, so dropped releases can't fake volume scroll.
        resyncModifiersFromOS();

        bool editor = isInEditor();
        char const* musicKey = editor ? kMusicEditorKey : kMusicGameKey;
        char const* sfxKey   = editor ? kSFXEditorKey   : kSFXGameKey;

        Keybind musicBind = getKeybind(musicKey);
        Keybind sfxBind   = getKeybind(sfxKey);

        auto musicExt = paimon::keybinds::loadExtendedKeybind(musicKey);
        auto sfxExt   = paimon::keybinds::loadExtendedKeybind(sfxKey);

        // this path runs on every wheel event; keep debug formatting guarded.
        PaimonDebug::log("[VolScroll] scroll y={:.2f} editor={} music={{kbKey={:#x},kbMods={:#x},extKind={}}} sfx={{kbKey={:#x},kbMods={:#x},extKind={}}} state ctrl={} shift={} alt={}",
            y, editor,
            (int)musicBind.key, (int)musicBind.modifiers.value, (int)musicExt.kind,
            (int)sfxBind.key,   (int)sfxBind.modifiers.value,   (int)sfxExt.kind,
            g_ctrlDown, g_shiftDown, g_altDown);

        VolumeKind kind;
        bool match = false;
        if (isKeybindActive(musicBind) || paimon::keybinds::isExtendedHeld(musicExt)) {
            kind = VolumeKind::Music;
            match = true;
        } else if (isKeybindActive(sfxBind) || paimon::keybinds::isExtendedHeld(sfxExt)) {
            kind = VolumeKind::SFX;
            match = true;
        }

        if (!match) return notOurs();

        PaimonDebug::log("[VolScroll] consuming scroll: kind={} y={}",
                  kind == VolumeKind::Music ? "music" : "sfx", y);

        const float delta = (y > 0.f) ? -kVolumeStep : +kVolumeStep;
        VolumeScrollManager::get().onScroll(kind, delta);

        if (g_ctrlDown) {
            paimon::quickhub::notifyVolumeScrollUsed();
        }
        return true;
    }
};

// late, so smooth-scroll momentum replays arrive normalized.
class $modify(PaimonPauseZoomMouseHook, CCMouseDispatcher) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre(
            "cocos2d::CCMouseDispatcher::dispatchScrollMSG",
            geode::Priority::Late
        );
    }

    bool dispatchScrollMSG(float y, float x) {
        paimon::pausezoom::dispatchScroll(y, x);
        return CCMouseDispatcher::dispatchScrollMSG(y, x);
    }
};
#endif

// three-finger drag replaces the wheel; touches are only observed (post,
// never consumed) and stay out of unpaused gameplay.
#if defined(GEODE_IS_MOBILE)
#include <Geode/modify/CCEGLViewProtocol.hpp>

namespace {
// raw handletouches coords are view pixels with y down, so dragging up
// on screen decreases the average y.
constexpr float kTouchDeadzonePx = 36.f; // drift before the first step
constexpr float kTouchStepPx     = 28.f; // pixels per volume step
constexpr int   kGestureFingers  = 3;

struct VolumeTouchState {
    int fingerCount = 0;
    bool baselineValid = false;
    float lastAvgX = 0.f;
    float lastAvgY = 0.f;
    float accUp = 0.f;    // > 0 = dragged up
    float accRight = 0.f; // > 0 = dragged right
};

VolumeTouchState g_volTouch;

bool volumeTouchAllowed() {
    if (!paimon::modules::isEnabled("paimbnails.volumescroll.global")) return false;
    if (auto* pl = PlayLayer::get()) {
        if (!pl->m_isPaused) return false;
    }
    return true;
}

void volumeTouchReset() {
    g_volTouch.fingerCount = 0;
    g_volTouch.baselineValid = false;
    g_volTouch.accUp = 0.f;
    g_volTouch.accRight = 0.f;
}

void volumeTouchSyncCount() {
    if (g_volTouch.fingerCount != kGestureFingers) {
        g_volTouch.baselineValid = false;
        g_volTouch.accUp = 0.f;
        g_volTouch.accRight = 0.f;
    }
}

// feed one axis: deadzone first, then one step per ktouchsteppx.
void volumeTouchPush(VolumeKind kind, float deltaPixels, float& acc) {
    acc += deltaPixels;
    float sign = (acc < 0.f) ? -1.f : 1.f;
    float over = std::fabs(acc) - kTouchDeadzonePx;
    if (over < kTouchStepPx) return;
    float steps = std::floor(over / kTouchStepPx);
    VolumeScrollManager::get().onScroll(kind, sign * steps * kVolumeStep);
    acc = sign * (kTouchDeadzonePx + (over - steps * kTouchStepPx));
}

float touchAvg(float const* v, int n) {
    float sum = 0.f;
    for (int i = 0; i < n; ++i) sum += v[i];
    return n > 0 ? sum / static_cast<float>(n) : 0.f;
}
}

class $modify(VolumeScrollTouchView, CCEGLViewProtocol) {
    static void onModify(auto& self) {
        // gameplay first: touches are observed, never consumed.
        (void)self.setHookPriorityPost("CCEGLViewProtocol::handleTouchesBegin", geode::Priority::Normal);
        (void)self.setHookPriorityPost("CCEGLViewProtocol::handleTouchesMove", geode::Priority::Normal);
        (void)self.setHookPriorityPost("CCEGLViewProtocol::handleTouchesEnd", geode::Priority::Normal);
        (void)self.setHookPriorityPost("CCEGLViewProtocol::handleTouchesCancel", geode::Priority::Normal);
    }

    void trackBegin(int num, float xs[], float ys[]) {
        g_volTouch.fingerCount += num;
        volumeTouchSyncCount();
        if (g_volTouch.fingerCount == kGestureFingers && volumeTouchAllowed()) {
            g_volTouch.baselineValid = true;
            g_volTouch.lastAvgX = touchAvg(xs, num);
            g_volTouch.lastAvgY = touchAvg(ys, num);
        } else {
            g_volTouch.baselineValid = false;
        }
    }

    void handleTouchesBegin(int num, int ids[], float xs[], float ys[], double timestamp) {
        CCEGLViewProtocol::handleTouchesBegin(num, ids, xs, ys, timestamp);
        trackBegin(num, xs, ys);
    }

    void handleTouchesMove(int num, int ids[], float xs[], float ys[], double timestamp) {
        CCEGLViewProtocol::handleTouchesMove(num, ids, xs, ys, timestamp);

        if (g_volTouch.fingerCount != kGestureFingers) return;
        if (!volumeTouchAllowed()) {
            volumeTouchReset();
            return;
        }
        // only full-set moves feed: partial subsets would skew the average.
        if (num != kGestureFingers) {
            g_volTouch.baselineValid = false;
            return;
        }
        float avgX = touchAvg(xs, num);
        float avgY = touchAvg(ys, num);
        if (!g_volTouch.baselineValid) {
            g_volTouch.baselineValid = true;
            g_volTouch.lastAvgX = avgX;
            g_volTouch.lastAvgY = avgY;
            return;
        }
        float dx = avgX - g_volTouch.lastAvgX;
        float dy = avgY - g_volTouch.lastAvgY;
        g_volTouch.lastAvgX = avgX;
        g_volTouch.lastAvgY = avgY;
        volumeTouchPush(VolumeKind::Music, -dy, g_volTouch.accUp);
        volumeTouchPush(VolumeKind::SFX, dx, g_volTouch.accRight);
    }

    void handleTouchesEnd(int num, int ids[], float xs[], float ys[], double timestamp) {
        CCEGLViewProtocol::handleTouchesEnd(num, ids, xs, ys, timestamp);
        g_volTouch.fingerCount -= num;
        if (g_volTouch.fingerCount < 0) g_volTouch.fingerCount = 0;
        volumeTouchSyncCount();
        g_volTouch.baselineValid = false;
    }

    void handleTouchesCancel(int num, int ids[], float xs[], float ys[], double timestamp) {
        CCEGLViewProtocol::handleTouchesCancel(num, ids, xs, ys, timestamp);
        volumeTouchReset();
    }
};
#endif // defined(geode_is_mobile)


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
