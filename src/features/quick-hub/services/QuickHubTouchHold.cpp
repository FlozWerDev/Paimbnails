#include "QuickHubManager.hpp"
#include "../ui/QuickHubRadial.hpp"
#include "../../main-menu-layout/ui/MainMenuLayoutEditor.hpp"
#include "../../main-menu-layout/services/MainMenuLayoutManager.hpp"
#include "../../main-menu-layout/hooks/LayoutEditorKeybind.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;
using namespace cocos2d;

// Android hold gestures: one finger opens the radial menu, two open the layout
// editor. Movement, early release, or an active popup cancels the gesture.

#if defined(GEODE_IS_ANDROID)

namespace {

constexpr float kDeadZone = 0.5f;
constexpr float kFillDuration = 1.0f;
constexpr float kTotalHold = kDeadZone + kFillDuration;
constexpr float kMoveThreshold = 15.f; // px before cancelling

struct TouchHoldState {
    bool active = false;
    int fingerCount = 0;
    float elapsed = 0.f;
    bool barVisible = false;
    bool completed = false;
    CCPoint startPos = CCPointZero;
    CCNode* progressBar = nullptr;
    CCNode* progressFill = nullptr;
};

static TouchHoldState s_touch;

void cleanupBar() {
    if (s_touch.progressBar) {
        s_touch.progressBar->removeFromParent();
        s_touch.progressBar = nullptr;
        s_touch.progressFill = nullptr;
    }
    s_touch.barVisible = false;
}

void syncTouchTicking();

void resetTouch() {
    s_touch.active = false;
    s_touch.fingerCount = 0;
    s_touch.elapsed = 0.f;
    s_touch.completed = false;
    cleanupBar();
    syncTouchTicking();
}

void createBar() {
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return;

    auto winSize = CCDirector::get()->getWinSize();

    auto container = CCNode::create();
    container->setPosition({winSize.width / 2.f, winSize.height - 8.f});
    container->setContentSize({180.f, 4.f});
    container->setAnchorPoint({0.5f, 0.5f});
    container->setZOrder(99999);
    scene->addChild(container);

    auto bg = CCLayerColor::create({40, 40, 50, 180});
    bg->setContentSize({180.f, 4.f});
    bg->setPosition({-90.f, -2.f});
    container->addChild(bg, 0);

    auto fill = CCLayerColor::create({255, 255, 255, 220});
    fill->setContentSize({0.f, 4.f});
    fill->setPosition({-90.f, -2.f});
    container->addChild(fill, 1);

    s_touch.progressBar = container;
    s_touch.progressFill = fill;
    s_touch.barVisible = true;
}

void updateBar(float progress) {
    if (!s_touch.progressFill) return;
    float w = 180.f * std::clamp(progress, 0.f, 1.f);
    s_touch.progressFill->setContentSize({w, 4.f});
}

class TouchHoldScheduler : public CCNode {
public:
    static TouchHoldScheduler* get() {
        static TouchHoldScheduler* s_instance = nullptr;
        if (!s_instance) {
            s_instance = new TouchHoldScheduler();
            s_instance->init();
            s_instance->retain();
        }
        return s_instance;
    }

    // Registered only while a touch hold is in progress; outside that window the
    // tick has nothing to advance.
    static void setTicking(bool on) {
        auto* self = get();
        if (self->m_ticking == on) return;
        auto* director = CCDirector::get();
        auto* scheduler = director ? director->getScheduler() : nullptr;
        if (!scheduler) return;

        self->m_ticking = on;
        if (on) {
            scheduler->scheduleSelector(
                schedule_selector(TouchHoldScheduler::onUpdate), self, 0.f, false);
        } else {
            scheduler->unscheduleSelector(
                schedule_selector(TouchHoldScheduler::onUpdate), self);
        }
    }

    void onUpdate(float dt) {
        if (!s_touch.active) return;
        if (s_touch.completed) return;

        s_touch.elapsed += dt;

        if (s_touch.elapsed < kDeadZone) return;

        if (!s_touch.barVisible) {
            createBar();
        }

        float fillProgress = (s_touch.elapsed - kDeadZone) / kFillDuration;
        updateBar(fillProgress);

        if (s_touch.elapsed >= kTotalHold) {
            cleanupBar();
            s_touch.completed = true;
            syncTouchTicking();

            if (s_touch.fingerCount >= 2) {
                openLayoutEditor();
            } else {
                paimon::quickhub::QuickHubRadial::openRadial();
            }
        }
    }

private:
    void openLayoutEditor() {
        using namespace paimon::menu_layout;

    // Do not open another popup while one is active.
        if (MainMenuLayoutEditor::isActive()) return;

    // Find the top-most interactive layer in the scene.
        auto* scene = CCDirector::get()->getRunningScene();
        if (!scene) return;

        CCLayer* topLayer = nullptr;
        auto* children = scene->getChildren();
        if (children) {
            for (int i = static_cast<int>(children->count()) - 1; i >= 0; --i) {
                auto* node = typeinfo_cast<CCNode*>(children->objectAtIndex(i));
                if (!node || !node->isVisible()) continue;
                auto* layer = typeinfo_cast<CCLayer*>(node);
                if (layer) { topLayer = layer; break; }
            }
        }

        if (!topLayer) return;

        MainMenuLayoutManager::get().captureDefaultsAndApply(topLayer);
        MainMenuLayoutEditor::open(topLayer);
    }

    bool m_ticking = false;
};

void syncTouchTicking() {
    TouchHoldScheduler::setTicking(s_touch.active && !s_touch.completed);
}

}

// Hooks CCEGLViewProtocol: handleTouches* is declared there, not on CCEGLView.
#include <Geode/modify/CCEGLViewProtocol.hpp>

class $modify(TouchHoldView, CCEGLViewProtocol) {
    void handleTouchesBegin(int num, int ids[], float xs[], float ys[], double timestamp) {
        CCEGLViewProtocol::handleTouchesBegin(num, ids, xs, ys, timestamp);

    // Both gestures are gated features.
        if (!paimon::modules::isEnabled("paimbnails.quickhub.global") &&
            !paimon::modules::isEnabled("paimbnails.menulayout.menu")) return;

        if (paimon::quickhub::QuickHubRadial::isOpen()) return;
        if (paimon::menu_layout::MainMenuLayoutEditor::isActive()) return;

        if (!s_touch.active) {
            s_touch.active = true;
            s_touch.fingerCount = num;
            s_touch.elapsed = 0.f;
            s_touch.completed = false;
            s_touch.barVisible = false;
            if (num > 0) {
                s_touch.startPos = ccp(xs[0], ys[0]);
            }
            syncTouchTicking();
        } else if (!s_touch.completed) {
            s_touch.fingerCount += num;
            if (s_touch.fingerCount > 2) s_touch.fingerCount = 2;
        }
    }

    void handleTouchesMove(int num, int ids[], float xs[], float ys[], double timestamp) {
        CCEGLViewProtocol::handleTouchesMove(num, ids, xs, ys, timestamp);

        if (!s_touch.active || s_touch.completed) return;

        if (num > 0) {
            CCPoint current = ccp(xs[0], ys[0]);
            float dist = ccpDistance(current, s_touch.startPos);
            if (dist > kMoveThreshold) {
                resetTouch();
            }
        }
    }

    void handleTouchesEnd(int num, int ids[], float xs[], float ys[], double timestamp) {
        CCEGLViewProtocol::handleTouchesEnd(num, ids, xs, ys, timestamp);

        if (!s_touch.active) return;

        if (!s_touch.completed) {
            s_touch.fingerCount -= num;
            if (s_touch.fingerCount <= 0) {
                resetTouch();
            }
        } else {
            s_touch.fingerCount -= num;
            if (s_touch.fingerCount <= 0) {
                resetTouch();
            }
        }
    }

    void handleTouchesCancel(int num, int ids[], float xs[], float ys[], double timestamp) {
        CCEGLViewProtocol::handleTouchesCancel(num, ids, xs, ys, timestamp);
        resetTouch();
    }
};

#endif
