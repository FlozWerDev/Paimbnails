#include "QuickHubManager.hpp"
#include "../ui/QuickHubRadial.hpp"
#include "../../gestures/GestureManager.hpp"
#include "../../main-menu-layout/ui/MainMenuLayoutEditor.hpp"
#include "../../main-menu-layout/services/MainMenuLayoutManager.hpp"
#include "../../main-menu-layout/hooks/LayoutEditorKeybind.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

using namespace geode::prelude;
using namespace cocos2d;

// Polished mobile touch hold & gesture bridge (point-space, per-touch IDs).

#if defined(GEODE_IS_MOBILE)

namespace {

struct TouchHoldState {
    bool active = false;
    int fingerCount = 0;
    int peakFingers = 0;
    float elapsed = 0.f;
    bool barVisible = false;
    bool completed = false;
    CCPoint startPos = CCPointZero;
    CCScene* startScene = nullptr;
    std::vector<std::pair<int, CCPoint>> touches;
    std::string activeLabel;
    WeakRef<CCNode> progressBar;
    WeakRef<CCNode> progressFill;
    WeakRef<CCLabelBMFont> progressLabel;
};

static TouchHoldState s_touch;

CCPoint touchesCentroid(std::vector<std::pair<int, CCPoint>> const& list) {
    if (list.empty()) return CCPointZero;
    float sx = 0.f, sy = 0.f;
    for (auto const& [id, p] : list) {
        sx += p.x;
        sy += p.y;
    }
    float const inv = 1.f / static_cast<float>(list.size());
    return {sx * inv, sy * inv};
}

std::optional<paimon::gestures::GestureActionId> holdActionForFingers(int fingers) {
    using namespace paimon::gestures;
    GestureKind kind = GestureKind::None;
    if (fingers >= 3) kind = GestureKind::Hold3F;
    else if (fingers == 2) kind = GestureKind::Hold2F;
    else if (fingers == 1) kind = GestureKind::Hold1F;
    if (kind == GestureKind::None) return std::nullopt;
    return GestureManager::get().actionForKind(kind);
}

void cleanupBar() {
    if (auto bar = s_touch.progressBar.lock()) bar->removeFromParent();
    s_touch.progressBar = nullptr;
    s_touch.progressFill = nullptr;
    s_touch.progressLabel = nullptr;
    s_touch.barVisible = false;
}

void syncTouchTicking();

void resetTouch() {
    s_touch.active = false;
    s_touch.fingerCount = 0;
    s_touch.peakFingers = 0;
    s_touch.elapsed = 0.f;
    s_touch.completed = false;
    s_touch.startScene = nullptr;
    s_touch.touches.clear();
    s_touch.activeLabel.clear();
    cleanupBar();
    syncTouchTicking();
}

// cocos runs first: a tracking menu means an intentional ui touch.
bool hasTrackingMenu(CCNode* node) {
    if (!node || !node->isVisible()) return false;

    if (auto* menu = typeinfo_cast<CCMenu*>(node)) {
        if (menu->isEnabled() &&
            (menu->m_eState == kCCMenuStateTrackingTouch || menu->m_pSelectedItem)) {
            return true;
        }
    }

    auto* children = node->getChildren();
    if (!children) return false;
    for (auto* object : CCArrayExt<CCObject*>(children)) {
        if (hasTrackingMenu(typeinfo_cast<CCNode*>(object))) return true;
    }
    return false;
}

bool gestureContextIsStillValid() {
    auto* director = CCDirector::get();
    auto* scene = director ? director->getRunningScene() : nullptr;
    return scene && scene == s_touch.startScene &&
           paimon::quickhub::QuickHubManager::canOpenInCurrentContext();
}

void createBar(std::string const& labelText) {
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return;

    auto winSize = CCDirector::get()->getWinSize();

    auto container = CCNode::create();
    container->setPosition({winSize.width / 2.f, winSize.height - 14.f});
    container->setContentSize({188.f, 12.f});
    container->setAnchorPoint({0.5f, 0.5f});
    container->setZOrder(99999);
    scene->addChild(container);

    auto bg = CCLayerColor::create({20, 22, 32, 200});
    bg->setContentSize({188.f, 12.f});
    bg->setPosition({-94.f, -6.f});
    container->addChild(bg, 0);

    auto fill = CCLayerColor::create({120, 210, 255, 225});
    fill->setContentSize({0.f, 4.f});
    fill->setPosition({-92.f, -5.f});
    container->addChild(fill, 1);

    auto* lbl = CCLabelBMFont::create(labelText.c_str(), "chatFont.fnt");
    lbl->setScale(0.36f);
    lbl->setPosition({0.f, 2.5f});
    lbl->setColor({235, 240, 255});
    lbl->limitLabelWidth(176.f, 0.36f, 0.18f);
    container->addChild(lbl, 2);

    s_touch.progressBar = container;
    s_touch.progressFill = fill;
    s_touch.progressLabel = lbl;
    s_touch.barVisible = true;
}

void updateBar(float progress, std::string const& labelText) {
    auto fill = s_touch.progressFill.lock();
    if (!fill) return;
    float w = 184.f * std::clamp(progress, 0.f, 1.f);
    fill->setContentSize({w, 4.f});
    if (auto lbl = s_touch.progressLabel.lock()) {
        if (s_touch.activeLabel != labelText) {
            s_touch.activeLabel = labelText;
            lbl->setString(labelText.c_str());
            lbl->limitLabelWidth(176.f, 0.36f, 0.18f);
        }
    }
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

        if (!gestureContextIsStillValid()) {
            resetTouch();
            return;
        }

        int const fingers = std::max(s_touch.fingerCount, s_touch.peakFingers);
        auto actOpt = holdActionForFingers(fingers);
        if (!actOpt) {
            resetTouch();
            return;
        }

        s_touch.elapsed += dt;

        auto const comf = paimon::gestures::GestureManager::get().comfort();
        float total = comf.holdSeconds;
        float dead = total * 0.28f;

        if (s_touch.elapsed < dead) return;

        auto const* meta = paimon::gestures::GestureManager::findAction(*actOpt);
        std::string label = meta ? std::string(meta->name) + "..." : "Mantener...";

        if (comf.hudFeedback) {
            if (!s_touch.barVisible) {
                s_touch.activeLabel = label;
                createBar(label);
            }
            float fillProgress = (s_touch.elapsed - dead) / std::max(0.05f, total - dead);
            updateBar(fillProgress, label);
        }

        if (s_touch.elapsed >= total) {
            cleanupBar();
            s_touch.completed = true;
            syncTouchTicking();
            paimon::gestures::GestureManager::get().triggerAction(*actOpt);
        }
    }

private:
    bool m_ticking = false;
};

void syncTouchTicking() {
    TouchHoldScheduler::setTicking(s_touch.active && !s_touch.completed);
}

} // namespace

// handletouches* lives on cceglviewprotocol, not cceglview.
#include <Geode/modify/CCEGLViewProtocol.hpp>

class $modify(TouchHoldView, CCEGLViewProtocol) {
    static void onModify(auto& self) {
        // gestures first, pet clicks after.
        (void)self.setHookPriorityPost("cocos2d::CCEGLViewProtocol::handleTouchesBegin", geode::Priority::Normal);
        (void)self.setHookPriorityPost("cocos2d::CCEGLViewProtocol::handleTouchesMove", geode::Priority::Normal);
        (void)self.setHookPriorityPost("cocos2d::CCEGLViewProtocol::handleTouchesEnd", geode::Priority::Normal);
        (void)self.setHookPriorityPost("cocos2d::CCEGLViewProtocol::handleTouchesCancel", geode::Priority::Normal);
    }

    $override
    void handleTouchesBegin(int num, int ids[], float xs[], float ys[], double timestamp) {
        CCEGLViewProtocol::handleTouchesBegin(num, ids, xs, ys, timestamp);
        if (num <= 0 || !ids || !xs || !ys) return;

        auto* director = CCDirector::get();
        auto* scene = director ? director->getRunningScene() : nullptr;
        bool const canQuickHub = scene && paimon::quickhub::QuickHubManager::canOpenInCurrentContext();
        // Avoid walking the entire scene graph on every jump tap during active gameplay when QuickHub cannot open.
        bool const isUiInteraction = (canQuickHub && scene && hasTrackingMenu(scene));

        paimon::gestures::GestureManager::get().onTouchesBegan(
            num, ids, xs, ys, timestamp, isUiInteraction);

        if (paimon::quickhub::QuickHubRadial::isOpen()) return;
        if (paimon::menu_layout::MainMenuLayoutEditor::isActive()) return;

        if (!canQuickHub || isUiInteraction) {
            if (s_touch.active) resetTouch();
            return;
        }

        for (int i = 0; i < num; ++i) {
            CCPoint pt = paimon::gestures::GestureManager::viewPixelsToPoint(xs[i], ys[i]);
            auto it = std::find_if(s_touch.touches.begin(), s_touch.touches.end(),
                [id = ids[i]](auto const& e) { return e.first == id; });
            if (it != s_touch.touches.end()) it->second = pt;
            else s_touch.touches.push_back({ids[i], pt});
        }

        int const count = static_cast<int>(s_touch.touches.size());
        if (!s_touch.active) {
            if (!holdActionForFingers(count).has_value() &&
                !holdActionForFingers(2).has_value() &&
                !holdActionForFingers(3).has_value()) {
                return;
            }
            s_touch.active = true;
            s_touch.fingerCount = count;
            s_touch.peakFingers = count;
            s_touch.elapsed = 0.f;
            s_touch.completed = false;
            s_touch.barVisible = false;
            s_touch.startScene = scene;
            s_touch.startPos = touchesCentroid(s_touch.touches);
            syncTouchTicking();
        } else if (!s_touch.completed) {
            s_touch.fingerCount = count;
            if (count > s_touch.peakFingers) s_touch.peakFingers = count;
            s_touch.startPos = touchesCentroid(s_touch.touches);
        }
    }

    $override
    void handleTouchesMove(int num, int ids[], float xs[], float ys[], double timestamp) {
        CCEGLViewProtocol::handleTouchesMove(num, ids, xs, ys, timestamp);
        if (num <= 0 || !ids || !xs || !ys) return;

        paimon::gestures::GestureManager::get().onTouchesMoved(num, ids, xs, ys, timestamp);

        if (!s_touch.active || s_touch.completed) return;

        for (int i = 0; i < num; ++i) {
            CCPoint pt = paimon::gestures::GestureManager::viewPixelsToPoint(xs[i], ys[i]);
            for (auto& [id, p] : s_touch.touches) {
                if (id == ids[i]) { p = pt; break; }
            }
        }

        if (!s_touch.touches.empty()) {
            CCPoint current = touchesCentroid(s_touch.touches);
            float const threshold = paimon::gestures::GestureManager::get().comfort().moveTolerancePt;
            if (ccpDistance(current, s_touch.startPos) > threshold) {
                resetTouch();
            }
        }
    }

    $override
    void handleTouchesEnd(int num, int ids[], float xs[], float ys[], double timestamp) {
        CCEGLViewProtocol::handleTouchesEnd(num, ids, xs, ys, timestamp);

        paimon::gestures::GestureManager::get().onTouchesEnd(num, ids, xs, ys, timestamp, false);

        if (!s_touch.active) return;

        for (int i = 0; i < num; ++i) {
            std::erase_if(s_touch.touches, [id = ids[i]](auto const& e) { return e.first == id; });
        }
        s_touch.fingerCount = static_cast<int>(s_touch.touches.size());
        if (s_touch.fingerCount <= 0) {
            resetTouch();
        }
    }

    $override
    void handleTouchesCancel(int num, int ids[], float xs[], float ys[], double timestamp) {
        CCEGLViewProtocol::handleTouchesCancel(num, ids, xs, ys, timestamp);

        paimon::gestures::GestureManager::get().onTouchesEnd(num, ids, xs, ys, timestamp, true);

        resetTouch();
    }
};

#endif
