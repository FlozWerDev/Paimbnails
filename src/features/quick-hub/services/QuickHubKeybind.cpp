#include "QuickHubManager.hpp"
#include "../ui/QuickHubRadial.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;
using namespace cocos2d;

// hold ctrl to open the radial; key dispatch only cancels the hold when ctrl is a shortcut.

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

    void onUpdate(float dt) {
        if (!paimon::quickhub::QuickHubManager::isHoldCtrlEnabled()) return;
        if (!s_hold.ctrlDown) return;
        if (s_hold.radialOpened) return;
        if (s_hold.cancelledByOtherKey) return;

        if (!paimon::quickhub::QuickHubManager::canOpenInCurrentContext()) {
            resetHold();
            return;
        }

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

// volume-scroll cancels an in-progress ctrl hold.
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

$execute {
    // geode input event works on every target; the generated cckeyboarddispatcher header is invalid on ios.
    KeyboardInputEvent().listen(+[](KeyboardInputData& data) {
        auto const modifiers = data.modifiers.value;
        bool shft = (modifiers & uint8_t(KeyboardModifier::Shift)) != 0;
        bool ctrl = (modifiers & uint8_t(KeyboardModifier::Control)) != 0;
        bool alt = (modifiers & uint8_t(KeyboardModifier::Alt)) != 0;
        bool cmd = (modifiers & uint8_t(KeyboardModifier::Super)) != 0;
        paimon::volscroll::onModifierKeysChanged(shft, ctrl, alt, cmd);

        // super is cmd on apple platforms.
        bool ctrlOrCmd = ctrl || cmd;

        if (!paimon::modules::isEnabled("paimbnails.quickhub.global") ||
            !paimon::quickhub::QuickHubManager::isHoldCtrlEnabled() ||
            !paimon::quickhub::QuickHubManager::canOpenInCurrentContext()) {
            if (s_hold.ctrlDown || s_hold.radialOpened) {
                paimon::quickhub::QuickHubManager::abortActiveHold();
            }
            return false;
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

        if (data.action == KeyboardInputData::Action::Press &&
            s_hold.ctrlDown && !s_hold.radialOpened) {
            bool isModifier =
                data.key == KEY_Control || data.key == KEY_LeftControl ||
                data.key == KEY_RightContol || data.key == KEY_Shift ||
                data.key == KEY_LeftShift || data.key == KEY_RightShift ||
                data.key == KEY_Alt || data.key == KEY_LeftMenu ||
                data.key == KEY_RightMenu;

            if (!isModifier) {
                s_hold.cancelledByOtherKey = true;
                cleanupProgressBar();
                syncHoldTicking();
            }
        }

        return false;
    }).leak();
}
