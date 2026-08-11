#include "QuickHubManager.hpp"
#include "../ui/QuickHubRadial.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>

using namespace geode::prelude;
using namespace cocos2d;

// Hold Ctrl to open the radial. Modifier updates are reliable across platforms;
// key dispatch only cancels the hold when Ctrl is used as a shortcut.

namespace {

constexpr float kDeadZone = 0.5f;
constexpr float kFillDuration = 1.0f;
constexpr float kTotalHold = kDeadZone + kFillDuration;

struct HoldState {
    bool ctrlDown = false;
    float elapsed = 0.f;
    bool barVisible = false;
    bool radialOpened = false;
    bool cancelledByOtherKey = false;
    cocos2d::CCNode* progressBar = nullptr;
    cocos2d::CCNode* progressFill = nullptr;
};

static HoldState s_hold;

void cleanupProgressBar() {
    if (s_hold.progressBar) {
        s_hold.progressBar->removeFromParent();
        s_hold.progressBar = nullptr;
        s_hold.progressFill = nullptr;
    }
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
    if (!s_hold.progressFill) return;
    float maxW = 200.f;
    float w = maxW * std::clamp(progress, 0.f, 1.f);
    s_hold.progressFill->setContentSize({w, 4.f});
}

void syncHoldTicking();

void resetHold() {
    s_hold.ctrlDown = false;
    s_hold.elapsed = 0.f;
    s_hold.radialOpened = false;
    s_hold.cancelledByOtherKey = false;
    cleanupProgressBar();
    syncHoldTicking();
}

// Never-freed singleton so it outlives scene changes; the tick itself is
// registered and dropped as the hold starts and ends.
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

    // onUpdate only advances the hold timer, so the selector is registered for
    // that window instead of ticking every frame for the whole session.
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

    void onUpdate(float dt) {
        if (!paimon::quickhub::QuickHubManager::isHoldCtrlEnabled()) return;
        if (!s_hold.ctrlDown) return;
        if (s_hold.radialOpened) return;
        if (s_hold.cancelledByOtherKey) return;

        s_hold.elapsed += dt;

        if (s_hold.elapsed < kDeadZone) return;

        if (!s_hold.barVisible) {
            createProgressBar();
        }

        float fillProgress = (s_hold.elapsed - kDeadZone) / kFillDuration;
        updateProgressBar(fillProgress);

        if (s_hold.elapsed >= kTotalHold) {
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
        s_hold.ctrlDown && !s_hold.radialOpened && !s_hold.cancelledByOtherKey);
}

}

// Lets volume-scroll cancel an in-progress Ctrl hold.
namespace paimon::quickhub {
    void notifyVolumeScrollUsed() {
        if (s_hold.ctrlDown && !s_hold.radialOpened) {
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

class $modify(QuickHubModifierHook, CCKeyboardDispatcher) {
    void updateModifierKeys(bool shft, bool ctrl, bool alt, bool cmd) {
        CCKeyboardDispatcher::updateModifierKeys(shft, ctrl, alt, cmd);

        paimon::volscroll::onModifierKeysChanged(shft, ctrl, alt, cmd);

        // Raw flags make Cmd start the hold on macOS too.
        bool ctrlOrCmd = ctrl || cmd;

        if (!paimon::modules::isEnabled("paimbnails.quickhub.global") ||
            !paimon::quickhub::QuickHubManager::isHoldCtrlEnabled()) {
            if (s_hold.ctrlDown || s_hold.radialOpened) {
                paimon::quickhub::QuickHubManager::abortActiveHold();
            }
            return;
        }

        if (ctrlOrCmd && !s_hold.ctrlDown) {
            s_hold.ctrlDown = true;
            s_hold.elapsed = 0.f;
            s_hold.barVisible = false;
            s_hold.radialOpened = false;
            s_hold.cancelledByOtherKey = false;
            syncHoldTicking();
        } else if (!ctrlOrCmd && s_hold.ctrlDown) {
            if (!s_hold.radialOpened) {
                resetHold();
            } else {
                s_hold.ctrlDown = false;
                s_hold.elapsed = 0.f;
                syncHoldTicking();
            }
        }
    }
};

class $modify(QuickHubKeyHook, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double timestamp) {
        if (!paimon::quickhub::QuickHubManager::isHoldCtrlEnabled()) {
            return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, timestamp);
        }

        if (down && !repeat && s_hold.ctrlDown && !s_hold.radialOpened) {
            bool isModifier =
                key == KEY_Control || key == KEY_LeftControl || key == KEY_RightContol ||
                key == KEY_Shift   || key == KEY_LeftShift   || key == KEY_RightShift   ||
                key == KEY_Alt     || key == KEY_LeftMenu    || key == KEY_RightMenu;

            if (!isModifier) {
                s_hold.cancelledByOtherKey = true;
                cleanupProgressBar();
                syncHoldTicking();
            }
        }

        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, timestamp);
    }
};
