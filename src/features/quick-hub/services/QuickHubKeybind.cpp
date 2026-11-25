#include "QuickHubManager.hpp"
#include "../ui/QuickHubRadial.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/SceneIdentity.hpp"

#include <Geode/Geode.hpp>
#include <chrono>

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#endif

using namespace geode::prelude;
using namespace cocos2d;

// only an explicit press can begin a hold; modifier snapshots never start one.

namespace {

struct HoldState {
    bool buttonDown = false;
    float elapsed = 0.f;
    std::chrono::steady_clock::time_point startedAt;
    int nativeKey = 0;
    bool barVisible = false;
    bool radialOpened = false;
    bool cancelledByOtherKey = false;
    paimon::SceneIdentity startScene;
    WeakRef<CCNode> progressBar;
    WeakRef<CCNode> progressFill;
};

static HoldState s_hold;

void cleanupProgressBar() {
    if (auto bar = s_hold.progressBar.lock()) bar->removeFromParent();
    s_hold.progressBar = nullptr;
    s_hold.progressFill = nullptr;
    s_hold.barVisible = false;
}

void createProgressBar() {
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return;

    auto winSize = CCDirector::get()->getWinSize();

    auto container = CCNode::create();
    container->setPosition({winSize.width / 2.f, winSize.height - 6.f});
    container->setContentSize({200.f, 4.f});
    container->setAnchorPoint({0.5f, 0.5f});
    container->setZOrder(99999);
    scene->addChild(container);

    auto bg = CCLayerColor::create({40, 40, 50, 180});
    bg->setContentSize({200.f, 4.f});
    bg->setPosition({-100.f, -2.f});
    container->addChild(bg, 0);

    auto fill = CCLayerColor::create({255, 255, 255, 220});
    fill->setContentSize({0.f, 4.f});
    fill->setPosition({-100.f, -2.f});
    container->addChild(fill, 1);

    s_hold.progressBar = container;
    s_hold.progressFill = fill;
    s_hold.barVisible = true;
}

void updateProgressBar(float progress) {
    auto fill = s_hold.progressFill.lock();
    if (!fill) return;
    float maxW = 200.f;
    float w = maxW * std::clamp(progress, 0.f, 1.f);
    fill->setContentSize({w, 4.f});
}

void syncHoldTicking();

void resetHold() {
    s_hold.buttonDown = false;
    s_hold.elapsed = 0.f;
    s_hold.radialOpened = false;
    s_hold.cancelledByOtherKey = false;
    s_hold.startScene = {};
    cleanupProgressBar();
    syncHoldTicking();
}

// never-freed singleton: outlives scene changes; the tick registers only for the hold window.
class QuickHubScheduler : public CCNode {
public:
    static QuickHubScheduler* get() {
        static QuickHubScheduler* s_instance = nullptr;
        if (!s_instance) {
            s_instance = new QuickHubScheduler();
            s_instance->init();
            s_instance->retain();
        }
        return s_instance;
    }

    // onupdate only advances the hold timer: register for that window, not the whole session.
    static void setTicking(bool on) {
        auto* self = get();
        if (self->m_ticking == on) return;
        auto* director = CCDirector::get();
        auto* scheduler = director ? director->getScheduler() : nullptr;
        if (!scheduler) return;

        self->m_ticking = on;
        if (on) {
            scheduler->scheduleSelector(
                schedule_selector(QuickHubScheduler::onUpdate), self, 0.f, false);
        } else {
            scheduler->unscheduleSelector(
                schedule_selector(QuickHubScheduler::onUpdate), self);
        }
    }

    void onUpdate(float) {
        if (!s_hold.buttonDown) return;
        if (s_hold.radialOpened) return;
        if (s_hold.cancelledByOtherKey) return;
        if (!paimon::modules::isEnabled("paimbnails.quickhub.global") ||
            !paimon::quickhub::QuickHubManager::isHoldCtrlEnabled()) {
            resetHold();
            return;
        }

#ifdef GEODE_IS_WINDOWS
        DWORD foregroundProcess = 0;
        auto foreground = GetForegroundWindow();
        if (foreground) GetWindowThreadProcessId(foreground, &foregroundProcess);
        // validate exactly the button that started this hold, including missed releases.
        if (foregroundProcess != GetCurrentProcessId() || s_hold.nativeKey <= 0 ||
            !(GetAsyncKeyState(s_hold.nativeKey) & 0x8000)) {
            resetHold();
            return;
        }
#endif

        if (!paimon::quickhub::QuickHubManager::canOpenInCurrentContext() ||
            !s_hold.startScene.matches(CCDirector::get()->getRunningScene())) {
            resetHold();
            return;
        }

        s_hold.elapsed = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - s_hold.startedAt).count();

        // first third is a dead zone; the remaining time fills the bar.
        float total = paimon::quickhub::QuickHubManager::holdSeconds();
        float dead = total / 3.f;

        if (s_hold.elapsed < dead) return;

        if (!s_hold.barVisible) {
            createProgressBar();
        }

        float fillProgress = (s_hold.elapsed - dead) / (total - dead);
        updateProgressBar(fillProgress);

        if (s_hold.elapsed >= total) {
            cleanupProgressBar();
            s_hold.radialOpened = true;
            syncHoldTicking();
            paimon::quickhub::QuickHubRadial::openRadial();
        }
    }

private:
    bool m_ticking = false;
};

void syncHoldTicking() {
    QuickHubScheduler::setTicking(
        s_hold.buttonDown && !s_hold.radialOpened && !s_hold.cancelledByOtherKey);
}

void beginHold(int nativeKey) {
    if (s_hold.buttonDown || paimon::quickhub::QuickHubRadial::isOpen()) return;
    resetHold();
    s_hold.startScene = paimon::SceneIdentity(CCDirector::get()->getRunningScene());
    s_hold.buttonDown = true;
    s_hold.startedAt = std::chrono::steady_clock::now();
    s_hold.nativeKey = nativeKey;
    syncHoldTicking();
}

enumKeyCodes normalizeModifierKey(enumKeyCodes key) {
    switch (key) {
        case KEY_LeftControl: case KEY_RightContol: return KEY_Control;
        case KEY_LeftShift: case KEY_RightShift: return KEY_Shift;
        case KEY_LeftMenu: case KEY_RightMenu: return KEY_Alt;
        case KEY_RightWindowsKey: return KEY_LeftWindowsKey;
        default: return key;
    }
}

bool holdIsAllowed() {
    return paimon::modules::isEnabled("paimbnails.quickhub.global") &&
        paimon::quickhub::QuickHubManager::isHoldCtrlEnabled() &&
        paimon::quickhub::QuickHubManager::canOpenInCurrentContext();
}

int mouseVirtualKey(MouseInputData::Button button) {
    // Windows virtual key values; unused by the scheduler on other platforms.
    switch (button) {
        case MouseInputData::Button::Left: return 1;
        case MouseInputData::Button::Right: return 2;
        case MouseInputData::Button::Middle: return 4;
        case MouseInputData::Button::Button4: return 5;
        case MouseInputData::Button::Button5: return 6;
    }
    return 0;
}

}

// volume-scroll cancels an in-progress ctrl hold.
namespace paimon::quickhub {
    void notifyVolumeScrollUsed() {
        if (s_hold.buttonDown && !s_hold.radialOpened) {
            s_hold.cancelledByOtherKey = true;
            cleanupProgressBar();
            syncHoldTicking();
        }
    }

    void QuickHubManager::abortActiveHold() {
        if (QuickHubRadial::isOpen()) {
            QuickHubRadial::closeRadial();
        }
        resetHold();
    }
}

namespace paimon::volscroll {
    void onModifierKeysChanged(bool shft, bool ctrl, bool alt, bool cmd);
}

$execute {
    // retain the listener handles for the mod's lifetime.
    static auto keyboardListener = KeyboardInputEvent().listen(+[](KeyboardInputData& data) {
        auto const modifiers = data.modifiers.value;
        bool shft = (modifiers & uint8_t(KeyboardModifier::Shift)) != 0;
        bool ctrl = (modifiers & uint8_t(KeyboardModifier::Control)) != 0;
        bool alt = (modifiers & uint8_t(KeyboardModifier::Alt)) != 0;
        bool cmd = (modifiers & uint8_t(KeyboardModifier::Super)) != 0;
        paimon::volscroll::onModifierKeysChanged(shft, ctrl, alt, cmd);

        if (!holdIsAllowed()) {
            if (s_hold.buttonDown || s_hold.radialOpened) {
                paimon::quickhub::QuickHubManager::abortActiveHold();
            }
            return false;
        }

        auto const button = paimon::quickhub::QuickHubManager::holdButton();
        auto const key = normalizeModifierKey(data.key);
        if (button.kind == paimon::keybinds::ExtendedKind::Keyboard &&
            key == normalizeModifierKey(button.key)) {
            if (data.action == KeyboardInputData::Action::Release) {
                resetHold();
            } else if (data.action == KeyboardInputData::Action::Press) {
                auto const nativeKey = data.native.code > 0 && data.native.code < 256
                    ? static_cast<int>(data.native.code) : static_cast<int>(data.key);
                beginHold(nativeKey);
            }
            return false;
        }

        if (data.action == KeyboardInputData::Action::Press &&
            s_hold.buttonDown && !s_hold.radialOpened) {
            bool isModifier =
                key == KEY_Control || key == KEY_Shift || key == KEY_Alt ||
                key == KEY_LeftWindowsKey;

            if (!isModifier) {
                s_hold.cancelledByOtherKey = true;
                cleanupProgressBar();
                syncHoldTicking();
            }
        }

        return false;
    });

    static auto mouseListener = MouseInputEvent().listen(+[](MouseInputData& data) {
        if (!holdIsAllowed()) {
            if (s_hold.buttonDown) paimon::quickhub::QuickHubManager::abortActiveHold();
            return false;
        }
        auto const button = paimon::quickhub::QuickHubManager::holdButton();
        if (button.kind == paimon::keybinds::ExtendedKind::Mouse &&
            static_cast<int>(button.button) == static_cast<int>(data.button)) {
            if (data.action == MouseInputData::Action::Release) resetHold();
            else beginHold(mouseVirtualKey(data.button));
        } else if (data.action == MouseInputData::Action::Press) {
            paimon::quickhub::notifyVolumeScrollUsed();
        }
        return false;
    });
}
