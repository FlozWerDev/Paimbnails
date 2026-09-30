#include "DynamicPanelTransitions.hpp"
#include "DynamicTransitionManager.hpp"
#include "../ui/DynamicTransitionScene.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/Settings.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include <limits>
#include <vector>

using namespace geode::prelude;

namespace paimon::transitions::dynamic {
namespace {

class PanelOverlay : public CCLayer {
public:
    static PanelOverlay* create(CCRenderTexture* surface, CCSize size) {
        auto* overlay = new PanelOverlay();
        if (!overlay->init() || !surface || !surface->getSprite()) {
            delete overlay;
            return nullptr;
        }
        auto* original = surface->getSprite();
        auto* image = CCSprite::createWithTexture(original->getTexture(), original->getTextureRect());
        if (!image) { delete overlay; return nullptr; }
        overlay->m_size = size;
        overlay->m_surface = surface;
        image->setFlipY(true);
        image->setPosition(size / 2.f);
        image->setScaleX(size.width / image->getContentSize().width);
        image->setScaleY(size.height / image->getContentSize().height);
        overlay->setContentSize(size);
        overlay->setID("dynamic-panel-transition"_spr);
        overlay->addChild(image);
        overlay->setTouchEnabled(true);
        overlay->scheduleUpdate();
        overlay->autorelease();
        return overlay;
    }

    void registerWithTouchDispatcher() override {
        auto* dispatcher = CCDirector::get()->getTouchDispatcher();
        dispatcher->addTargetedDelegate(this, dispatcher->getTargetPrio() - 20, true);
    }

    bool ccTouchBegan(CCTouch*, CCEvent*) override { return true; }

    void start(Visual* visual, float duration) {
        removeAllChildrenWithCleanup(true);
        m_surface = nullptr;
        m_visual = visual;
        m_duration = duration;
        m_elapsed = 0.f;
        addChild(visual);
    }

    void startFade(CCRenderTexture* destination) {
        auto* original = destination->getSprite();
        auto* image = CCSprite::createWithTexture(original->getTexture(), original->getTextureRect());
        if (!image) { removeFromParentAndCleanup(true); return; }
        image->setFlipY(true);
        image->setPosition(m_size / 2.f);
        image->setScaleX(m_size.width / image->getContentSize().width);
        image->setScaleY(m_size.height / image->getContentSize().height);
        image->setOpacity(0);
        m_destination = destination;
        m_fadeImage = image;
        m_duration = .14f;
        m_elapsed = 0.f;
        addChild(image, 1);
    }

    void update(float dt) override {
        m_elapsed += limit(dt, 0.f, 0.f, 2.f);
        auto* director = CCDirector::get();
        if (paimon::isRuntimeShuttingDown() || !isEnabled() || director->getNextScene() ||
            !director->getWinSize().equals(m_size) ||
            m_elapsed >= (m_duration > 0.f ? m_duration : .5f)) {
            removeFromParentAndCleanup(true);
            return;
        }
        if (m_visual) m_visual->setProgress(m_elapsed / m_duration);
        if (m_fadeImage) m_fadeImage->setOpacity(static_cast<GLubyte>(255.f * m_elapsed / m_duration));
    }

    void onExit() override {
        unscheduleAllSelectors();
        CCLayer::onExit();
    }

private:
    CCSize m_size;
    Ref<CCRenderTexture> m_surface, m_destination;
    Visual* m_visual = nullptr;
    CCSprite* m_fadeImage = nullptr;
    float m_elapsed = 0.f, m_duration = 0.f;
};

struct PanelRoute {
    WeakRef<CCNode> panel;
    Rect origin;
    bool fromButton;
};

struct Pending {
    WeakRef<CCScene> scene;
    WeakRef<CCNode> panel;
    Ref<CCRenderTexture> before;
    CCSize size;
    Config config;
    Rect origin;
    bool opening = false;
    bool morphButton = false;
};

WeakRef<PanelOverlay> s_overlay;
Pending s_pending;
std::vector<PanelRoute> s_panelRoutes;
uint64_t s_generation = 0;
unsigned s_captureDepth = 0;

struct CaptureGuard {
    CaptureGuard() { ++s_captureDepth; }
    ~CaptureGuard() { --s_captureDepth; }
};

CCScene* attachedScene(CCNode* node) {
    if (!node) return nullptr;
    while (node->getParent()) node = node->getParent();
    return typeinfo_cast<CCScene*>(node);
}

bool allowsPanel(CCNode* node, CCScene* scene, Config const& config) {
    if (paimon::isManagedDynamicPopup(node)) return false;
    if (!config.animateOtherMods && typeinfo_cast<geode::Popup*>(node)) return false;
    if (!config.animateEditorPanels &&
        (scene->getChildByType<LevelEditorLayer>(0) || scene->getChildByType<EditorUI>(0))) return false;
    if (!config.animateGameplayPanels && scene->getChildByType<PlayLayer>(0)) return false;
    if (typeinfo_cast<FLAlertLayer*>(node)) return config.animatePopups;
    if (typeinfo_cast<GJDropDownLayer*>(node) || typeinfo_cast<SlideInLayer*>(node))
        return config.animateDropdowns;
    if (typeinfo_cast<CCBlockLayer*>(node)) return config.animateBlockingLayers;
    if (typeinfo_cast<DialogLayer*>(node)) return config.animateDialogs;
    auto* browser = typeinfo_cast<LevelBrowserLayer*>(node);
    return browser && browser->m_isOverlay && config.animatePopups;
}

Ref<CCRenderTexture> captureScene(CCScene* scene, CCSize size, Config config) {
    CaptureGuard guard;
    auto overlay = s_overlay.lock();
    bool visible = overlay && overlay->isVisible();
    if (overlay) overlay->setVisible(false);
    auto surface = Visual::captureSurface(scene, size, config);
    if (overlay) overlay->setVisible(visible);
    return surface;
}

void commit(uint64_t generation) {
    if (generation != s_generation || !s_pending.before) return;
    auto pending = s_pending;
    s_pending.before = nullptr;
    s_pending = {};
    auto scene = pending.scene.lock();
    auto overlay = s_overlay.lock();
    auto panel = pending.panel.lock();
    auto* director = CCDirector::get();
    if (paimon::isRuntimeShuttingDown() || !isEnabled() || !scene || !overlay ||
        !overlay->getParent() || scene.data() != director->getRunningScene() || director->getNextScene() ||
        !pending.size.equals(director->getWinSize()) ||
        (pending.opening && (!panel || attachedScene(panel.data()) != scene.data()))) {
        finishPanelAnimation();
        return;
    }
    auto after = captureScene(scene.data(), pending.size, pending.config);
    if (!after) { finishPanelAnimation(); return; }
    scene->reorderChild(overlay.data(), std::numeric_limits<int>::max());
    if (paimon::settings::smoothui::reducedMotion()) {
        if (pending.config.reducedMotion == ReducedMotion::Instant) finishPanelAnimation();
        else overlay->startFade(after.data());
        return;
    }
    auto* visual = Visual::createFromSnapshots(pending.before, after, pending.size,
        pending.config, pending.origin, !pending.opening, pending.morphButton);
    if (!visual) { finishPanelAnimation(); return; }
    overlay->start(visual, pending.opening ? pending.config.duration : pending.config.backDuration);
}

}

PanelShowGuard::PanelShowGuard(FLAlertLayer* panel) : m_panel(panel),
    m_elasticity(panel->m_noElasticity), m_action(panel->m_noAction),
    m_active(panelWillChange(panel, panel->m_scene ? panel->m_scene : CCDirector::get()->getRunningScene(), true)) {
    if (m_active) {
        panel->m_noElasticity = true;
        panel->m_noAction = true;
    }
}

PanelShowGuard::~PanelShowGuard() {
    if (m_active) {
        m_panel->m_noElasticity = m_elasticity;
        m_panel->m_noAction = m_action;
    }
}

bool panelWillChange(CCNode* panel, CCNode* parent, bool opening) {
    if (s_captureDepth || paimon::isRuntimeShuttingDown() || !panel || !parent ||
        !typeinfo_cast<CCLayer*>(panel) || !isEnabled()) return false;
    auto* director = CCDirector::get();
    auto* scene = director->getRunningScene();
    if (!scene || attachedScene(parent) != scene || director->getNextScene() ||
        typeinfo_cast<CCTransitionScene*>(scene) || scene->getChildByType<LoadingLayer>(0)) return false;
    auto config = animationConfig(getConfig());
    if (!allowsPanel(panel, scene, config)) return false;
    if (!opening && (!config.animateBack || (hasBackInput() && !config.animateKeyboardBack))) return false;
    if (paimon::settings::smoothui::reducedMotion() && config.reducedMotion == ReducedMotion::Instant)
        return false;
    auto size = director->getWinSize();
    if (size.width < 8.f || size.height < 8.f) return false;
    Rect origin = fallbackOrigin(size.width, size.height, config.origin);
    bool back = false;
    bool fromButton = currentButton(scene, origin, back);
    bool remembered = false;
    std::erase_if(s_panelRoutes, [](PanelRoute const& route) { return !route.panel.lock(); });
    for (auto const& route : s_panelRoutes) {
        auto existing = route.panel.lock();
        if (existing.data() != panel) continue;
        if (!opening || !fromButton) {
            origin = {route.origin.x * size.width, route.origin.y * size.height,
                route.origin.width * size.width, route.origin.height * size.height};
            fromButton = route.fromButton;
        }
        remembered = true;
        break;
    }
    if (config.onlyFromButton && !fromButton) return false;
    if (config.origin != Origin::Button) origin = fallbackOrigin(size.width, size.height, config.origin);
    origin = normalizeOrigin(origin, size.width, size.height);
    if (opening && !remembered) {
        if (s_panelRoutes.size() >= 64) s_panelRoutes.erase(s_panelRoutes.begin());
        s_panelRoutes.push_back({WeakRef<CCNode>(panel), {origin.x / size.width, origin.y / size.height,
            origin.width / size.width, origin.height / size.height}, fromButton});
    }
    auto pendingScene = s_pending.scene.lock();
    if (pendingScene.data() == scene && s_pending.before) {
        // A close followed by a confirmation popup becomes a single visual change.
        if (opening || !s_pending.opening) {
            s_pending.panel = panel;
            s_pending.origin = origin;
            s_pending.opening = opening;
            s_pending.morphButton = fromButton && config.origin == Origin::Button;
        }
        return true;
    }
    finishPanelAnimation();
    auto before = captureScene(scene, size, config);
    if (!before) return false;
    auto* overlay = PanelOverlay::create(before.data(), size);
    if (!overlay) return false;
    s_pending = {WeakRef<CCScene>(scene), WeakRef<CCNode>(panel), before, size, config,
        origin, opening, fromButton && config.origin == Origin::Button};
    s_overlay = overlay;
    {
        CaptureGuard guard;
        scene->addChild(overlay, std::numeric_limits<int>::max());
    }
    auto generation = ++s_generation;
    // Let native zero-duration entrance actions settle before taking the endpoint.
    Loader::get()->queueInMainThread([generation] {
        if (generation != s_generation) return;
        Loader::get()->queueInMainThread([generation] { commit(generation); });
    });
    return true;
}

void finishPanelAnimation() {
    ++s_generation;
    s_pending.before = nullptr;
    s_pending = {};
    auto overlay = s_overlay.lock();
    s_overlay = nullptr;
    if (overlay) {
        CaptureGuard guard;
        overlay->removeFromParentAndCleanup(true);
    }
}

void clearPanelHistory() {
    finishPanelAnimation();
    s_panelRoutes.clear();
}

}
