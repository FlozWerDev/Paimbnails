#include "DynamicPanelTransitions.hpp"
#include "DynamicTransitionManager.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/Settings.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include <Geode/cocos/kazmath/include/kazmath/GL/matrix.h>
#include <algorithm>
#include <optional>
#include <vector>

using namespace geode::prelude;

namespace paimon::transitions::dynamic {
namespace {

constexpr int kMotionTag = 0x5A9E1;

enum class PanelKind { None, Popup, Dropdown, Blocking, Dialog };

struct PanelRoute {
    WeakRef<CCNode> panel;
    Rect origin;
    bool fromButton;
};

struct Plan {
    Config config;
    Rect origin;
};

struct PendingOpen {
    WeakRef<CCNode> panel;
    Plan plan;
};

std::vector<PanelRoute> s_panelRoutes;
std::vector<PendingOpen> s_pendingOpen;
std::vector<WeakRef<CCNode>> s_moving;
std::vector<WeakRef<CCNode>> s_ghosts;
// a hidden-then-removed panel must not leave a second ghost.
std::vector<WeakRef<CCNode>> s_closed;
unsigned s_openDepth = 0;

template <class T>
void prune(std::vector<T>& list) {
    std::erase_if(list, [](T const& entry) {
        if constexpr (requires { entry.panel; }) return !entry.panel.lock();
        else return !entry.lock();
    });
}

template <class T>
bool contains(std::vector<WeakRef<T>> const& list, CCNode* node) {
    return std::ranges::any_of(list, [node](auto const& entry) { return entry.lock().data() == node; });
}

CCScene* attachedScene(CCNode* node) {
    if (!node) return nullptr;
    while (node->getParent()) node = node->getParent();
    return typeinfo_cast<CCScene*>(node);
}

CCNode* sceneChildOf(CCNode* node) {
    while (node && node->getParent() && !typeinfo_cast<CCScene*>(node->getParent())) node = node->getParent();
    return node;
}

PanelKind panelKind(CCNode* node) {
    if (typeinfo_cast<FLAlertLayer*>(node)) return PanelKind::Popup;
    if (typeinfo_cast<GJDropDownLayer*>(node) || typeinfo_cast<SlideInLayer*>(node))
        return PanelKind::Dropdown;
    if (typeinfo_cast<CCBlockLayer*>(node)) return PanelKind::Blocking;
    if (typeinfo_cast<DialogLayer*>(node)) return PanelKind::Dialog;
    auto* browser = typeinfo_cast<LevelBrowserLayer*>(node);
    return browser && browser->m_isOverlay ? PanelKind::Popup : PanelKind::None;
}

bool allowsPanel(PanelKind kind, CCNode* node, CCScene* scene, Config const& config) {
    if (!config.animateOtherMods && typeinfo_cast<geode::Popup*>(node)) return false;
    if (!config.animateEditorPanels &&
        (scene->getChildByType<LevelEditorLayer>(0) || scene->getChildByType<EditorUI>(0))) return false;
    if (!config.animateGameplayPanels && scene->getChildByType<PlayLayer>(0)) return false;
    switch (kind) {
        case PanelKind::Popup: return config.animatePopups;
        case PanelKind::Dropdown: return config.animateDropdowns;
        case PanelKind::Blocking: return config.animateBlockingLayers;
        case PanelKind::Dialog: return config.animateDialogs;
        case PanelKind::None: return false;
    }
    return false;
}

// only the popup box moves; the panel itself stays put as the dimmed backdrop.
CCNode* contentOf(CCNode* panel) {
    if (auto* alert = typeinfo_cast<FLAlertLayer*>(panel)) return alert->m_mainLayer;
    if (auto* drop = typeinfo_cast<GJDropDownLayer*>(panel)) return drop->m_mainLayer;
    if (auto* dialog = typeinfo_cast<DialogLayer*>(panel)) return dialog->m_mainLayer;
    return panel->getChildByID("main-layer");
}

std::optional<Plan> planFor(CCNode* panel, CCNode* parent, bool opening) {
    if (!parent || paimon::isRuntimeShuttingDown()) return std::nullopt;
    auto kind = panelKind(panel);
    if (paimon::isManagedDynamicPopup(panel)) return std::nullopt;
    auto* director = CCDirector::get();
    auto* scene = director->getRunningScene();
    if (!scene || director->getNextScene() || typeinfo_cast<CCTransitionScene*>(scene) ||
        attachedScene(parent) != scene || scene->getChildByType<LoadingLayer>(0)) return std::nullopt;
    auto config = panelConfig(animationConfig(getConfig()));
    if (!allowsPanel(kind, panel, scene, config)) return std::nullopt;
    if (!opening && (!config.animateBack || (hasBackInput() && !config.animateKeyboardBack)))
        return std::nullopt;
    if (paimon::settings::smoothui::reducedMotion()) {
        if (config.reducedMotion == ReducedMotion::Instant) return std::nullopt;
        config = reducedMotionConfig(config);
    }
    auto size = director->getWinSize();
    if (size.width < 8.f || size.height < 8.f) return std::nullopt;

    Rect origin = fallbackOrigin(size.width, size.height, config.origin);
    bool back = false;
    bool fromButton = currentButton(scene, origin, back);
    bool remembered = false;
    prune(s_panelRoutes);
    for (auto const& route : s_panelRoutes) {
        if (route.panel.lock().data() != panel) continue;
        if (!opening || !fromButton) {
            origin = {route.origin.x * size.width, route.origin.y * size.height,
                route.origin.width * size.width, route.origin.height * size.height};
            fromButton = route.fromButton;
        }
        remembered = true;
        break;
    }
    if (config.onlyFromButton && !fromButton) return std::nullopt;
    if (config.origin != Origin::Button) origin = fallbackOrigin(size.width, size.height, config.origin);
    origin = normalizeOrigin(origin, size.width, size.height);
    if (opening && !remembered) {
        if (s_panelRoutes.size() >= 64) s_panelRoutes.erase(s_panelRoutes.begin());
        s_panelRoutes.push_back({WeakRef<CCNode>(panel), {origin.x / size.width, origin.y / size.height,
            origin.width / size.width, origin.height / size.height}, fromButton});
    }
    return Plan{config, origin};
}

struct HiddenPose {
    CCPoint offset;
    float scale = 1.f;
    bool fades = false;
};

// where the box sits when fully hidden, in world points relative to its pivot.
HiddenPose hiddenPose(Plan const& plan, CCPoint pivot, CCSize win) {
    auto const& config = plan.config;
    auto const& origin = plan.origin;
    CCPoint center{origin.x + origin.width / 2.f, origin.y + origin.height / 2.f};
    float fromButton = std::clamp(std::max(origin.width / (win.width * .6f),
        origin.height / (win.height * .6f)), .06f, .6f);
    switch (config.style) {
        case Style::App: return {center - pivot, fromButton, true};
        case Style::Reveal: return {center - pivot, .04f, true};
        case Style::Card: return {(center - pivot) * .5f, std::max(fromButton, .6f), true};
        case Style::Zoom: return {{0.f, 0.f}, .82f, true};
        case Style::Fade: return {{0.f, 0.f}, .97f, true};
        case Style::Slide:
        case Style::Push:
        case Style::Sheet: {
            Edge edge = config.style == Style::Slide ? edgeFor(config.origin, Edge::Bottom) :
                config.style == Style::Push ? edgeFor(config.origin, Edge::Right) : Edge::Bottom;
            float dx = edge == Edge::Left ? -win.width : edge == Edge::Right ? win.width : 0.f;
            float dy = edge == Edge::Bottom ? -win.height : edge == Edge::Top ? win.height : 0.f;
            return {{dx, dy}, 1.f, false};
        }
    }
    return {};
}

float visibleScale(float hidden, float shown) {
    return std::max(.01f, hidden + (1.f - hidden) * shown);
}

CCPoint pivotOf(CCNode* node) {
    return node->convertToWorldSpace(node->getAnchorPointInPoints());
}

// animates the live popup box in place, so nothing is drawn over newer popups.
class PanelMotion : public CCAction {
public:
    static PanelMotion* create(CCNode* panel, CCNode* content, Plan const& plan) {
        auto* parent = content ? content->getParent() : nullptr;
        if (!panel || (content && !parent)) return nullptr;
        auto* motion = new PanelMotion();
        motion->m_plan = plan;
        motion->m_duration = std::max(plan.config.duration, .05f);
        motion->m_content = content != nullptr;
        if (content) {
            auto win = CCDirector::get()->getWinSize();
            CCPoint pivot = pivotOf(content);
            auto pose = hiddenPose(plan, pivot, win);
            motion->m_basePos = content->getPosition();
            motion->m_baseScaleX = content->getScaleX();
            motion->m_baseScaleY = content->getScaleY();
            motion->m_offset = parent->convertToNodeSpace(pivot + pose.offset) - parent->convertToNodeSpace(pivot);
            motion->m_hiddenScale = pose.scale;
        }
        // a native fade on the backdrop wins; fighting it would flicker.
        auto* dim = typeinfo_cast<CCLayerColor*>(panel);
        if (dim && dim->getOpacity() > 0 && dim->numberOfRunningActions() == 0) {
            motion->m_dim = dim;
            motion->m_dimOpacity = dim->getOpacity();
        }
        motion->setTag(kMotionTag);
        motion->autorelease();
        return motion;
    }

    bool hasWork() { return m_content || m_dim.lock(); }

    void startWithTarget(CCNode* target) override {
        CCAction::startWithTarget(target);
        apply(0.f);
    }

    bool isDone() override { return m_done; }

    void step(float dt) override {
        if (m_done) return;
        m_elapsed += frameStep(dt, m_steps++ < 2);
        float t = m_elapsed / m_duration;
        if (t >= 1.f || paimon::isRuntimeShuttingDown()) {
            finish();
            return;
        }
        apply(t);
    }

    void finish() {
        if (m_done) return;
        m_done = true;
        if (m_content && m_pTarget) {
            m_pTarget->setPosition(m_basePos);
            m_pTarget->setScaleX(m_baseScaleX);
            m_pTarget->setScaleY(m_baseScaleY);
        }
        if (auto dim = m_dim.lock()) dim->setOpacity(m_dimOpacity);
    }

private:
    void apply(float t) {
        float shown = std::clamp(ease(t, m_plan.config.curve, m_plan.config.spring), -.3f, 1.3f);
        if (m_content && m_pTarget) {
            float scale = visibleScale(m_hiddenScale, shown);
            m_pTarget->setPosition(m_basePos + m_offset * (1.f - shown));
            m_pTarget->setScaleX(m_baseScaleX * scale);
            m_pTarget->setScaleY(m_baseScaleY * scale);
        }
        if (auto dim = m_dim.lock()) {
            dim->setOpacity(static_cast<GLubyte>(m_dimOpacity * smooth01(t)));
        }
    }

    Plan m_plan;
    WeakRef<CCLayerColor> m_dim;
    CCPoint m_basePos, m_offset;
    float m_baseScaleX = 1.f, m_baseScaleY = 1.f, m_hiddenScale = 1.f;
    float m_duration = .3f, m_elapsed = 0.f;
    GLubyte m_dimOpacity = 0;
    int m_steps = 0;
    bool m_content = false;
    bool m_done = false;
};

void stopMotion(CCNode* node) {
    if (!node) return;
    if (auto* motion = static_cast<PanelMotion*>(node->getActionByTag(kMotionTag))) {
        motion->finish();
        node->stopAction(motion);
    }
}

void stopMotions(CCNode* panel) {
    if (!panel) return;
    stopMotion(contentOf(panel));
    stopMotion(panel);
}

// renders just the popup box (transparent around it) for the close animation.
Ref<CCTexture2D> captureContent(CCNode* content, CCSize win) {
    if (!content || !content->isVisible()) return nullptr;
    Ref<CCRenderTexture> surface = CCRenderTexture::create(
        static_cast<int>(win.width), static_cast<int>(win.height),
        kCCTexture2DPixelFormat_RGBA8888, GL_DEPTH24_STENCIL8);
    if (!surface || !surface->getSprite() || !surface->getSprite()->getTexture()) return nullptr;
    surface->beginWithClear(0.f, 0.f, 0.f, 0.f, 1.f, 0);
    kmGLMatrixMode(KM_GL_MODELVIEW);
    kmGLPushMatrix();
    if (auto* parent = content->getParent()) {
        auto t = parent->nodeToWorldTransform();
        kmMat4 m{};
        m.mat[0] = t.a; m.mat[1] = t.b;
        m.mat[4] = t.c; m.mat[5] = t.d;
        m.mat[10] = 1.f;
        m.mat[12] = t.tx; m.mat[13] = t.ty;
        m.mat[15] = 1.f;
        kmGLMultMatrix(&m);
    }
    content->visit();
    kmGLPopMatrix();
    surface->end();
    Ref<CCTexture2D> texture = surface->getSprite()->getTexture();
    texture->setAntiAliasTexParameters();
    return texture;
}

// stand-in for a closed popup: lives at the popup's own z, never takes touches.
class PanelGhost : public CCNode {
public:
    static PanelGhost* create(Ref<CCTexture2D> texture, CCPoint pivot, CCLayerColor* dim, Plan const& plan) {
        auto* ghost = new PanelGhost();
        if (!ghost->init()) {
            delete ghost;
            return nullptr;
        }
        auto win = CCDirector::get()->getWinSize();
        ghost->m_plan = plan;
        ghost->m_duration = std::max(plan.config.backDuration, .05f);
        ghost->setContentSize(win);
        ghost->setID("dynamic-panel-ghost"_spr);

        if (dim && dim->getDisplayedOpacity() > 0) {
            ghost->m_dimOpacity = dim->getDisplayedOpacity();
            auto color = dim->getDisplayedColor();
            ghost->m_dim = CCLayerColor::create({color.r, color.g, color.b, ghost->m_dimOpacity},
                win.width, win.height);
            ghost->addChild(ghost->m_dim, 0);
        }
        if (texture) {
            auto pose = hiddenPose(plan, pivot, win);
            auto* sprite = CCSprite::createWithTexture(texture);
            sprite->setFlipY(true);
            // render texture output is premultiplied, so opacity must scale rgb too.
            sprite->setBlendFunc({GL_ONE, GL_ONE_MINUS_SRC_ALPHA});
            sprite->setOpacityModifyRGB(true);
            sprite->setAnchorPoint({pivot.x / std::max(win.width, 1.f), pivot.y / std::max(win.height, 1.f)});
            sprite->setPosition(pivot);
            ghost->m_box = sprite;
            ghost->m_pivot = pivot;
            ghost->m_offset = pose.offset;
            ghost->m_hiddenScale = pose.scale;
            ghost->m_fades = pose.fades;
            ghost->addChild(sprite, 1);
        }
        if (!ghost->m_dim && !ghost->m_box) {
            delete ghost;
            return nullptr;
        }
        ghost->autorelease();
        return ghost;
    }

    void onEnter() override {
        CCNode::onEnter();
        apply(0.f);
        scheduleUpdate();
    }

    void update(float dt) override {
        m_elapsed += frameStep(dt, m_steps++ < 2);
        float t = m_elapsed / m_duration;
        auto* director = CCDirector::get();
        if (t >= 1.f || paimon::isRuntimeShuttingDown() || director->getNextScene()) {
            unscheduleUpdate();
            removeFromParentAndCleanup(true);
            return;
        }
        apply(t);
    }

private:
    void apply(float t) {
        float remaining = 1.f - t;
        float shown = std::clamp(1.f - ease(t, m_plan.config.curve, m_plan.config.spring), -.3f, 1.3f);
        if (m_box) {
            m_box->setPosition(m_pivot + m_offset * (1.f - shown));
            m_box->setScale(visibleScale(m_hiddenScale, shown));
            m_box->setOpacity(m_fades ? static_cast<GLubyte>(255.f * smooth01(remaining / .35f)) : 255);
        }
        if (m_dim) m_dim->setOpacity(static_cast<GLubyte>(m_dimOpacity * smooth01(remaining)));
    }

    Plan m_plan;
    CCSprite* m_box = nullptr;
    CCLayerColor* m_dim = nullptr;
    CCPoint m_pivot, m_offset;
    float m_hiddenScale = 1.f, m_duration = .3f, m_elapsed = 0.f;
    GLubyte m_dimOpacity = 0;
    int m_steps = 0;
    bool m_fades = false;
};

bool openPanel(CCNode* panel, CCNode* parent) {
    auto plan = planFor(panel, parent, true);
    if (!plan) return false;
    auto* content = contentOf(panel);
    if (!content && !typeinfo_cast<CCLayerColor*>(panel)) return false;

    std::erase_if(s_closed, [panel](auto const& entry) {
        auto node = entry.lock();
        return !node || node.data() == panel;
    });
    // a restart must measure the resting pose, not a half-animated one.
    stopMotions(panel);

    prune(s_pendingOpen);
    for (auto& pending : s_pendingOpen) {
        if (pending.panel.lock().data() == panel) {
            pending.plan = *plan;
            return true;
        }
    }
    s_pendingOpen.push_back({WeakRef<CCNode>(panel), *plan});

    // panels attached outside any show scope still start before the next draw.
    WeakRef<CCNode> weak(panel);
    Loader::get()->queueInMainThread([weak] {
        if (auto node = weak.lock()) panelDidOpen(node.data());
    });
    return true;
}

bool closePanel(CCNode* panel, CCNode* parent) {
    std::erase_if(s_pendingOpen, [panel](PendingOpen const& pending) {
        auto node = pending.panel.lock();
        return !node || node.data() == panel;
    });
    stopMotions(panel);
    if (!panel || !panel->isVisible() || contains(s_closed, panel)) return false;

    auto plan = planFor(panel, parent, false);
    if (!plan) return false;
    auto* scene = attachedScene(parent);
    auto* anchor = sceneChildOf(panel);
    if (!scene || !anchor) return false;

    auto win = CCDirector::get()->getWinSize();
    auto* content = contentOf(panel);
    auto texture = captureContent(content, win);
    CCPoint pivot = content ? pivotOf(content) : CCPoint{win.width / 2.f, win.height / 2.f};
    Ref<PanelGhost> ghost = PanelGhost::create(texture, pivot, typeinfo_cast<CCLayerColor*>(panel), *plan);
    if (!ghost) return false;

    prune(s_closed);
    s_closed.emplace_back(panel);

    int z = anchor->getZOrder();
    WeakRef<CCScene> weakScene(scene);
    // the caller may be iterating the scene's children; attach once that's over.
    Loader::get()->queueInMainThread([ghost, weakScene, z] {
        auto scene = weakScene.lock();
        auto* director = CCDirector::get();
        if (paimon::isRuntimeShuttingDown() || !scene || scene.data() != director->getRunningScene() ||
            director->getNextScene()) return;
        scene->addChild(ghost, z);
        prune(s_ghosts);
        s_ghosts.emplace_back(ghost.data());
    });
    return true;
}

}

PanelOpenScope::PanelOpenScope(CCNode* panel, CCNode* parent)
    : m_panel(panel), m_active(panelWillChange(panel, parent, true)) {
    ++s_openDepth;
}

PanelOpenScope::~PanelOpenScope() {
    --s_openDepth;
    // an inner addChild may have queued the panel even if this scope declined it.
    panelDidOpen(m_panel);
}

PanelShowGuard::PanelShowGuard(FLAlertLayer* panel) : m_panel(panel),
    m_elasticity(panel->m_noElasticity), m_action(panel->m_noAction),
    m_scope(panel, panel->m_scene ? panel->m_scene : CCDirector::get()->getRunningScene()) {
    if (m_scope.active()) {
        panel->m_noElasticity = true;
        panel->m_noAction = true;
    }
}

PanelShowGuard::~PanelShowGuard() {
    if (m_scope.active()) {
        m_panel->m_noElasticity = m_elasticity;
        m_panel->m_noAction = m_action;
    }
}

bool panelWillChange(CCNode* panel, CCNode* parent, bool opening) {
    // Runs for every addChild/removeChild, so the cheapest rejections come first.
    if (!isEnabled() || !panel || !typeinfo_cast<CCLayer*>(panel) || panelKind(panel) == PanelKind::None)
        return false;
    return opening ? openPanel(panel, parent) : closePanel(panel, parent);
}

void panelDidOpen(CCNode* panel) {
    if (!panel || s_openDepth) return;
    auto it = std::ranges::find_if(s_pendingOpen, [panel](PendingOpen const& pending) {
        return pending.panel.lock().data() == panel;
    });
    if (it == s_pendingOpen.end()) return;
    auto plan = it->plan;
    s_pendingOpen.erase(it);

    auto* director = CCDirector::get();
    if (paimon::isRuntimeShuttingDown() || director->getNextScene() ||
        attachedScene(panel) != director->getRunningScene()) return;

    auto* content = contentOf(panel);
    // the popup's own intro is left alone instead of fought.
    if (content && content->numberOfRunningActions() > 0) content = nullptr;
    auto* host = content ? content : panel;
    stopMotion(host);
    auto* motion = PanelMotion::create(panel, content, plan);
    if (!motion || !motion->hasWork()) return;
    host->runAction(motion);
    prune(s_moving);
    s_moving.emplace_back(host);
}

void finishPanelAnimation() {
    s_pendingOpen.clear();
    for (auto const& entry : s_moving) {
        if (auto node = entry.lock()) stopMotion(node.data());
    }
    s_moving.clear();
    for (auto const& entry : s_ghosts) {
        if (auto ghost = entry.lock()) ghost->removeFromParentAndCleanup(true);
    }
    s_ghosts.clear();
}

void clearPanelHistory() {
    finishPanelAnimation();
    s_panelRoutes.clear();
    s_closed.clear();
}

}
