#include "LevelEntryEffects.hpp"
#include "../../gameplay-performance/GameplayPerformance.hpp"

#include <Geode/utils/cocos.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <initializer_list>
#include <unordered_set>
#include <utility>

using namespace cocos2d;
using namespace geode::prelude;

namespace {

using LevelEntryEffectsConfig = paimon::transitions::LevelEntryEffectsConfig;
using LevelEntryStyle = paimon::transitions::LevelEntryStyle;
using LevelExitMode = paimon::transitions::LevelExitMode;

std::atomic<bool> s_levelExitPending{false};
Ref<PlayLayer> s_levelExitPlayLayer;

enum class TransitionDirection {
    Enter,
    Exit,
};

struct StyleProfile {
    CCPoint ground1;
    CCPoint ground2;
    CCPoint middle;
    CCPoint objects;
    float objectRotation;
    float objectScale;
    float hudScale;
    float playerScale;
    float playerRotation;
    float backgroundDelay;
};

StyleProfile styleProfile(LevelEntryStyle style, float intensity) {
    switch (style) {
        case LevelEntryStyle::LegacyBounce:
            return {
                {0.f, -90.f * intensity}, {0.f, -90.f * intensity},
                {0.f, -180.f * intensity}, {0.f, -85.f * intensity},
                18.f * intensity, .3f, .55f, .72f, 35.f, 0.f,
            };
        case LevelEntryStyle::Soft:
            return {
                {0.f, -28.f * intensity}, {0.f, 28.f * intensity},
                {45.f * intensity, 0.f}, {22.f * intensity, -18.f * intensity},
                0.f, .88f, .94f, .9f, 0.f, 0.f,
            };
        case LevelEntryStyle::Impact:
            return {
                {-220.f * intensity, 0.f}, {220.f * intensity, 0.f},
                {-700.f * intensity, 0.f}, {145.f * intensity, 0.f},
                110.f * intensity, 1.45f, 1.4f, 1.45f, 180.f, .62f,
            };
        case LevelEntryStyle::SmoothPlus:
        default:
            return {
                {0.f, -120.f * intensity}, {0.f, 120.f * intensity},
                {500.f * intensity, 0.f}, {50.f * intensity, -100.f * intensity},
                50.f * intensity, 0.f, 0.f, .72f, 90.f, .5f,
            };
    }
}

struct NodeState {
    Ref<CCNode> node;
    CCPoint position;
    CCPoint anchor;
    CCSize contentSize;
    float scaleX;
    float scaleY;
    float rotationX;
    float rotationY;
    GLubyte opacity;
    bool hasOpacity;
    bool visible;
    bool ignoresAnchor;
};

struct TrackedAction {
    Ref<CCNode> node;
    Ref<CCAction> action;
};

int nextActionTag() {
    static std::atomic<int> tag{-1800000000};
    return tag.fetch_add(1, std::memory_order_relaxed);
}

class ActionTracker {
public:
    void run(CCNode* node, CCAction* action) {
        if (!node || !action) return;
        action->setTag(m_tag);
        node->runAction(action);
        // On level exit the PlayLayer is paused, so its objects are paused
        // targets in the action manager. A paused target also pauses newly
        // added actions, which would freeze the exit animation. Resume the
        // target so our action always advances (no-op when not paused).
        if (auto* manager = node->getActionManager()) manager->resumeTarget(node);
        m_actions.push_back({node, action});
    }

    void stop() {
        for (auto& tracked : m_actions) {
            auto* node = tracked.node.data();
            auto* action = tracked.action.data();
            if (node && action && action->getTarget() == node) {
                node->stopAction(action);
            }
        }
        m_actions.clear();
    }

private:
    int m_tag = nextActionTag();
    std::vector<TrackedAction> m_actions;
};

void setRunningRecursive(CCNode* node, bool running) {
    if (!node) return;
    node->m_bRunning = running;
    for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) {
        setRunningRecursive(child, running);
    }
}

CCActionInterval* styleEaseOut(CCActionInterval* action, LevelEntryStyle style) {
    switch (style) {
        case LevelEntryStyle::LegacyBounce: return CCEaseBounceOut::create(action);
        case LevelEntryStyle::Impact:       return CCEaseBackOut::create(action);
        case LevelEntryStyle::Soft:         return CCEaseSineOut::create(action);
        case LevelEntryStyle::SmoothPlus:
        default:                            return CCEaseExponentialOut::create(action);
    }
}

CCActionInterval* styleEaseIn(CCActionInterval* action, LevelEntryStyle style) {
    switch (style) {
        case LevelEntryStyle::LegacyBounce: return CCEaseSineIn::create(action);
        case LevelEntryStyle::Impact:       return CCEaseBackIn::create(action);
        case LevelEntryStyle::Soft:         return CCEaseSineIn::create(action);
        case LevelEntryStyle::SmoothPlus:
        default:                            return CCEaseExponentialIn::create(action);
    }
}

void moveNodes(ActionTracker& actions, CCNode* layer,
               std::initializer_list<char const*> ids, CCPoint delta, float duration) {
    if (!layer) return;
    for (auto const* id : ids) {
        if (auto* node = layer->getChildByID(id)) {
            actions.run(node, CCEaseExponentialIn::create(CCMoveBy::create(duration, delta)));
        }
    }
}

void scaleNodeOut(ActionTracker& actions, CCNode* layer, char const* id, float duration) {
    if (!layer) return;
    if (auto* node = layer->getChildByID(id)) {
        actions.run(node, CCEaseExponentialIn::create(
            CCScaleBy::create(duration, 1.5f, 1.5f)
        ));
    }
}

void centerMenu(CCNode* menu, bool useScreenCenter = true) {
    if (!menu) return;

    std::vector<CCPoint> childPositions;
    childPositions.reserve(menu->getChildrenCount());
    for (auto* child : CCArrayExt<CCNode*>(menu->getChildren())) {
        childPositions.push_back(menu->convertToWorldSpace(child->getPosition()));
    }

    menu->ignoreAnchorPointForPosition(false);
    menu->setPosition(useScreenCenter
        ? CCPoint(CCDirector::get()->getWinSize() / 2.f)
        : CCPoint{0.f, 0.f});

    size_t index = 0;
    for (auto* child : CCArrayExt<CCNode*>(menu->getChildren())) {
        child->setPosition(menu->convertToNodeSpace(childPositions[index++]));
    }
}

void animateOutScene(ActionTracker& actions, CCScene* scene, float duration, float intensity) {
    if (!scene) return;

    float distance = 150.f * intensity;
    if (auto* layer = scene->getChildByType<LevelInfoLayer>(0)) {
        moveNodes(actions, layer, {
            "back-menu", "left-side-menu", "bottom-left-art", "difficulty-sprite",
            "stars-icon", "stars-label", "diamond-icon", "diamond-label",
            "coin-icon-1", "coin-icon-2", "coin-icon-3"
        }, {-distance, 0.f}, duration);
        moveNodes(actions, layer, {
            "right-side-menu", "bottom-right-art", "downloads-icon", "downloads-label",
            "likes-icon", "likes-label", "orbs-icon", "orbs-label", "length-icon",
            "length-label", "exact-length-label"
        }, {distance, 0.f}, duration);
        moveNodes(actions, layer, {
            "copy-indicator", "title-label", "creator-info-menu", "garage-menu",
            "high-object-indicator"
        }, {0.f, distance}, duration);
        moveNodes(actions, layer, {
            "settings-menu", "custom-songs-widget", "normal-mode-bar", "practice-mode-bar",
            "normal-mode-percentage", "practice-mode-percentage", "normal-mode-label",
            "practice-mode-label"
        }, {0.f, -distance}, duration);
        scaleNodeOut(actions, layer, "play-menu", duration);
        scaleNodeOut(actions, layer, "other-menu", duration);
        centerMenu(layer->getChildByID("play-menu"));
    } else if (auto* layer = scene->getChildByType<EditLevelLayer>(0)) {
        moveNodes(actions, layer, {
            "back-menu", "description-menu", "folder-menu", "bottom-left-art"
        }, {-distance, 0.f}, duration);
        moveNodes(actions, layer, {"level-actions-menu", "bottom-right-art"}, {distance, 0.f}, duration);
        moveNodes(actions, layer, {
            "level-name-background", "description-background", "level-name-input",
            "description-input"
        }, {0.f, distance}, duration);
        moveNodes(actions, layer, {
            "level-length", "level-song", "level-verified", "version-label", "level-id-label"
        }, {0.f, -distance}, duration);
        scaleNodeOut(actions, layer, "level-edit-menu", duration);
        scaleNodeOut(actions, layer, "info-button-menu", duration);
        centerMenu(layer->getChildByID("info-button-menu"));
    } else if (auto* layer = scene->getChildByType<LevelSelectLayer>(0)) {
        moveNodes(actions, layer, {"back-menu", "bottom-left-corner"}, {-distance, 0.f}, duration);
        moveNodes(actions, layer, {"info-menu", "bottom-right-corner"}, {distance, 0.f}, duration);
        moveNodes(actions, layer, {"top-bar-sprite"}, {0.f, distance}, duration);
        moveNodes(actions, layer, {"ground-layer", "bottom-center-menu"}, {0.f, -distance}, duration);
        scaleNodeOut(actions, layer, "levels-list", duration);
        scaleNodeOut(actions, layer, "arrows-menu", duration);
        centerMenu(layer->getChildByID("arrows-menu"));
    } else if (auto* layer = scene->getChildByType<LevelAreaInnerLayer>(0)) {
        scaleNodeOut(actions, layer, "main-node", duration);
        scaleNodeOut(actions, layer, "back-menu", duration);
        centerMenu(layer->getChildByID("back-menu"));
    }
}

class LevelEffectsTransitionScene final : public CCTransitionScene {
public:
    static LevelEffectsTransitionScene* create(
        CCScene* destination,
        LevelEntryEffectsConfig config,
        TransitionDirection direction,
        PlayLayer* exitPlayLayer = nullptr
    ) {
        auto* ret = new LevelEffectsTransitionScene();
        if (ret && ret->init(destination, std::move(config), direction, exitPlayLayer)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    bool init(
        CCScene* destination,
        LevelEntryEffectsConfig config,
        TransitionDirection direction,
        PlayLayer* exitPlayLayer
    ) {
        if (!destination || !CCTransitionScene::initWithDuration(config.duration, destination)) {
            return false;
        }
        m_config = std::move(config);
        m_direction = direction;
        m_exitPlayLayer = exitPlayLayer;
        m_bIsSendCleanupToScene = false;
        return true;
    }

    void onEnter() override {
        CCScene::onEnter();
        auto* touchDispatcher = CCTouchDispatcher::get();
        m_touchDispatchWasEnabled = touchDispatcher->isDispatchEvents();
        touchDispatcher->setDispatchEvents(false);

        if (m_pOutScene) m_pOutScene->onExitTransitionDidStart();
        if (!m_pInScene) {
            finishTransition();
            return;
        }

        if (m_direction == TransitionDirection::Enter) {
            m_pInScene->resumeSchedulerAndActions();
            setRunningRecursive(m_pInScene, true);
            m_fakedIncomingRunning = true;
            m_playLayer = m_pInScene->getChildByType<PlayLayer>(0);
        } else {
            m_pInScene->onEnter();
            m_inSceneEntered = true;
            m_playLayer = m_exitPlayLayer
                ? m_exitPlayLayer.data()
                : (m_pOutScene ? m_pOutScene->getChildByType<PlayLayer>(0) : nullptr);
        }
        if (!m_playLayer) {
            if (m_direction == TransitionDirection::Exit) {
                log::info("[LevelTransitions] Exit scene has no PlayLayer");
            }
            finishTransition();
            return;
        }

        if (m_direction == TransitionDirection::Exit) {
            log::info("[LevelTransitions] Exit scene started ({:.2f}s)", m_fDuration);
        }

        m_playLayer->setVisible(true);
        animatePlayLayer();
        if (m_direction == TransitionDirection::Enter && m_config.animatePage) {
            animateOutScene(m_actions, m_pOutScene, m_fDuration, m_config.intensity);
        }

        m_playLayer->updateShaderLayer(.01f);
        scheduleUpdate();
        runAction(CCSequence::create(
            CCDelayTime::create(m_fDuration),
            CallFuncExt::create([this] { finishTransition(); }),
            nullptr
        ));

        // Entering the destination scene (esp. on exit, e.g. LevelInfoLayer) can
        // stall a frame while it loads. That stall lands as a huge dt on the next
        // tick, which would fast-forward every action here and snap the transition
        // straight to the end. Drop that accumulated time so the animation starts
        // from a clean frame.
        CCDirector::get()->setNextDeltaTimeZero(true);
    }

    void onExit() override {
        m_actions.stop();
        restoreNodes();
        if (m_fakedIncomingRunning && m_pInScene) setRunningRecursive(m_pInScene, false);
        unscheduleUpdate();
        unschedule(schedule_selector(LevelEffectsTransitionScene::switchToIncoming));
        CCScene::onExit();
        CCTouchDispatcher::get()->setDispatchEvents(m_touchDispatchWasEnabled);

        if (m_switchingToIncoming && m_pInScene && !m_inSceneEntered) {
            m_pInScene->onEnter();
            m_inSceneEntered = true;
        }
        if (m_switchingToIncoming && m_pInScene) {
            CCDirector::get()->willSwitchToScene(m_pInScene);
        }
        if (!m_switchingToIncoming && m_inSceneEntered && m_pInScene) {
            m_pInScene->onExit();
        }
        if (m_pOutScene) m_pOutScene->onExit();
        if (m_switchingToIncoming && m_pInScene) {
            m_pInScene->onEnterTransitionDidFinish();
        }
    }

    void draw() override {
        auto applyShaderBlend = [this] {
            if (m_hasShaderBlend && m_playLayer && m_playLayer->m_shaderLayer &&
                m_playLayer->m_shaderLayer->m_sprite) {
                glBlendColor(0.f, 0.f, 0.f,
                    m_playLayer->m_shaderLayer->m_sprite->getOpacity() / 255.f);
            }
        };

        if (m_direction == TransitionDirection::Exit) {
            if (m_pInScene) m_pInScene->visit();
            applyShaderBlend();
            if (m_pOutScene && m_pOutScene->isVisible()) m_pOutScene->visit();
        } else {
            if (m_pOutScene && m_pOutScene->isVisible()) m_pOutScene->visit();
            applyShaderBlend();
            if (m_pInScene) m_pInScene->visit();
        }
        if (m_hasShaderBlend) glBlendColor(0.f, 0.f, 0.f, 0.f);
    }

    void update(float) override {
        if (m_playLayer) m_playLayer->updateShaderLayer(0.f);
    }

private:
    void capture(CCNode* node) {
        if (!node || !m_captured.insert(node).second) return;

        auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node);
        m_states.push_back({
            node,
            node->getPosition(),
            node->getAnchorPoint(),
            node->getContentSize(),
            node->getScaleX(),
            node->getScaleY(),
            node->getRotationX(),
            node->getRotationY(),
            rgba ? rgba->getOpacity() : static_cast<GLubyte>(255),
            rgba != nullptr,
            node->isVisible(),
            node->isIgnoreAnchorPointForPosition(),
        });
    }

    void restoreNodes() {
        if (m_restored) return;
        m_restored = true;

        for (auto const& state : m_states) {
            auto* node = state.node.data();
            if (!node) continue;
            node->setPosition(state.position);
            node->setAnchorPoint(state.anchor);
            node->setContentSize(state.contentSize);
            node->setScaleX(state.scaleX);
            node->setScaleY(state.scaleY);
            node->setRotationX(state.rotationX);
            node->setRotationY(state.rotationY);
            node->setVisible(state.visible);
            node->ignoreAnchorPointForPosition(state.ignoresAnchor);
            if (state.hasOpacity) {
                if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node)) {
                    rgba->setOpacity(state.opacity);
                }
            }
        }

        if (m_hasBackgroundBlend && m_playLayer && m_playLayer->m_background) {
            m_playLayer->m_background->setBlendFunc(m_backgroundBlend);
        }
        if (m_hasShaderBlend && m_playLayer && m_playLayer->m_shaderLayer &&
            m_playLayer->m_shaderLayer->m_sprite) {
            m_playLayer->m_shaderLayer->m_sprite->setBlendFunc(m_shaderBlend);
        }
        m_states.clear();
        m_captured.clear();
    }

    void finishTransition() {
        if (m_finished) return;
        m_finished = true;
        m_actions.stop();
        restoreNodes();
        if (m_fakedIncomingRunning && m_pInScene) setRunningRecursive(m_pInScene, false);

        m_pInScene->setVisible(true);
        m_pInScene->setPosition({0.f, 0.f});
        m_pInScene->setScale(1.f);
        m_pInScene->setRotation(0.f);
        m_pInScene->getCamera()->restore();

        m_pOutScene->setVisible(false);
        m_pOutScene->setPosition({0.f, 0.f});
        m_pOutScene->setScale(1.f);
        m_pOutScene->setRotation(0.f);
        m_pOutScene->getCamera()->restore();

        schedule(schedule_selector(LevelEffectsTransitionScene::switchToIncoming), 0.f);
    }

    void switchToIncoming(float) {
        unschedule(schedule_selector(LevelEffectsTransitionScene::switchToIncoming));
        m_switchingToIncoming = true;
        auto* director = CCDirector::get();
        m_bIsSendCleanupToScene = director->isSendCleanupToScene();
        director->replaceScene(m_pInScene);
        m_pOutScene->setVisible(true);
    }

    void fadeNode(CCNode* node, float duration, float delay = 0.f) {
        if (!node) return;
        auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node);
        if (!rgba) return;

        capture(node);
        auto opacity = rgba->getOpacity();
        GLubyte target = 0;
        if (m_direction == TransitionDirection::Enter) {
            rgba->setOpacity(0);
            target = opacity;
        }
        auto* fade = m_direction == TransitionDirection::Enter
            ? styleEaseOut(CCFadeTo::create(duration, target), m_config.style)
            : styleEaseIn(CCFadeTo::create(duration, target), m_config.style);
        if (delay > 0.f) {
            m_actions.run(node, CCSequence::create(CCDelayTime::create(delay), fade, nullptr));
        } else {
            m_actions.run(node, fade);
        }
    }

    void animateBackground() {
        auto* background = m_playLayer->m_background;
        if (!background) return;

        capture(background);
        m_backgroundBlend = background->getBlendFunc();
        m_hasBackgroundBlend = true;
        background->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
        auto* rgba = typeinfo_cast<CCRGBAProtocol*>(background);
        auto opacity = rgba ? rgba->getOpacity() : static_cast<GLubyte>(255);
        auto profile = styleProfile(m_config.style, m_config.intensity);
        if (m_direction == TransitionDirection::Enter) {
            if (rgba) rgba->setOpacity(0);
            float delay = m_fDuration * profile.backgroundDelay;
            float fadeDuration = std::max(.01f, m_fDuration - delay);
            m_actions.run(background, CCSequence::create(
                CCDelayTime::create(delay),
                styleEaseOut(CCFadeTo::create(fadeDuration, opacity), m_config.style),
                nullptr
            ));
        } else if (rgba) {
            m_actions.run(background, styleEaseIn(
                CCFadeTo::create(m_fDuration, 0), m_config.style
            ));
        }

        if (m_playLayer->m_shaderLayer && m_playLayer->m_shaderLayer->m_sprite) {
            auto* shaderSprite = m_playLayer->m_shaderLayer->m_sprite;
            capture(shaderSprite);
            m_shaderBlend = shaderSprite->getBlendFunc();
            m_hasShaderBlend = true;
            auto opacity = shaderSprite->getOpacity();
            shaderSprite->setBlendFunc({GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA});
            if (m_direction == TransitionDirection::Enter) {
                shaderSprite->setOpacity(0);
                m_actions.run(shaderSprite, CCFadeTo::create(m_fDuration, opacity));
            } else {
                m_actions.run(shaderSprite, CCFadeTo::create(m_fDuration, 0));
            }
        }
    }

    void animateGround() {
        auto profile = styleProfile(m_config.style, m_config.intensity);
        if (auto* ground = m_playLayer->m_groundLayer) {
            capture(ground);
            if (m_direction == TransitionDirection::Enter) {
                ground->setPosition(ground->getPosition() + profile.ground1);
                m_actions.run(ground, styleEaseOut(
                    CCMoveBy::create(m_fDuration, -profile.ground1), m_config.style
                ));
            } else {
                m_actions.run(ground, styleEaseIn(
                    CCMoveBy::create(m_fDuration, profile.ground1), m_config.style
                ));
            }
        }
        if (auto* ground = m_playLayer->m_groundLayer2) {
            capture(ground);
            if (m_direction == TransitionDirection::Enter) {
                ground->setPosition(ground->getPosition() + profile.ground2);
                m_actions.run(ground, styleEaseOut(
                    CCMoveBy::create(m_fDuration, -profile.ground2), m_config.style
                ));
            } else {
                m_actions.run(ground, styleEaseIn(
                    CCMoveBy::create(m_fDuration, profile.ground2), m_config.style
                ));
            }
        }
    }

    void animateMiddleground() {
        auto* middle = m_playLayer->m_middleground;
        if (!middle) return;

        auto profile = styleProfile(m_config.style, m_config.intensity);
        std::array batches = {middle->m_mg1BatchNode, middle->m_mg2BatchNode};
        for (auto* batch : batches) {
            if (!batch) continue;
            for (auto* sprite : CCArrayExt<CCSprite*>(batch->getChildren())) {
                if (!sprite) continue;
                capture(sprite);
                auto opacity = sprite->getOpacity();
                if (m_direction == TransitionDirection::Enter) {
                    sprite->setOpacity(0);
                    sprite->setPosition(sprite->getPosition() + profile.middle);
                    m_actions.run(sprite, CCFadeTo::create(m_fDuration, opacity));
                    m_actions.run(sprite, styleEaseOut(
                        CCMoveBy::create(m_fDuration, -profile.middle), m_config.style
                    ));
                } else {
                    m_actions.run(sprite, CCFadeTo::create(m_fDuration, 0));
                    m_actions.run(sprite, styleEaseIn(
                        CCMoveBy::create(m_fDuration, profile.middle), m_config.style
                    ));
                }
            }
        }
    }

    void animateHud() {
        auto winSize = CCDirector::get()->getWinSize();
        auto profile = styleProfile(m_config.style, m_config.intensity);
        fadeNode(m_playLayer->m_attemptLabel, m_fDuration);

        if (auto* ui = m_playLayer->m_uiLayer) {
            capture(ui);
            auto scaleX = ui->getScaleX();
            auto scaleY = ui->getScaleY();
            if (m_direction == TransitionDirection::Enter) {
                ui->setScaleX(scaleX * profile.hudScale);
                ui->setScaleY(scaleY * profile.hudScale);
                m_actions.run(ui, styleEaseOut(
                    CCScaleTo::create(m_fDuration, scaleX, scaleY), m_config.style
                ));
            } else {
                m_actions.run(ui, styleEaseIn(CCScaleTo::create(
                    m_fDuration,
                    scaleX * profile.hudScale,
                    scaleY * profile.hudScale
                ), m_config.style));
            }
        }

        if (auto* triggerUi = m_playLayer->m_uiTriggerUI) {
            capture(triggerUi);
            auto scaleX = triggerUi->getScaleX();
            auto scaleY = triggerUi->getScaleY();
            triggerUi->setAnchorPoint({.5f, .5f});
            triggerUi->ignoreAnchorPointForPosition(false);
            triggerUi->setPosition(winSize / 2.f);
            triggerUi->setContentSize(winSize);
            if (m_direction == TransitionDirection::Enter) {
                triggerUi->setScaleX(scaleX * profile.hudScale);
                triggerUi->setScaleY(scaleY * profile.hudScale);
                m_actions.run(triggerUi, styleEaseOut(
                    CCScaleTo::create(m_fDuration, scaleX, scaleY), m_config.style
                ));
            } else {
                m_actions.run(triggerUi, styleEaseIn(CCScaleTo::create(
                    m_fDuration,
                    scaleX * profile.hudScale,
                    scaleY * profile.hudScale
                ), m_config.style));
            }
        }

        CCPoint hudDelta{0.f, 30.f * m_config.intensity};
        if (m_config.style == LevelEntryStyle::LegacyBounce) {
            hudDelta = CCPoint{0.f, -55.f * m_config.intensity};
        } else if (m_config.style == LevelEntryStyle::Impact) {
            hudDelta = CCPoint{110.f * m_config.intensity, 0.f};
        }
        for (auto* node : {static_cast<CCNode*>(m_playLayer->m_progressBar),
                           static_cast<CCNode*>(m_playLayer->m_percentageLabel)}) {
            if (!node) continue;
            capture(node);
            if (m_direction == TransitionDirection::Enter) {
                node->setPosition(node->getPosition() + hudDelta);
                m_actions.run(node, styleEaseOut(
                    CCMoveBy::create(m_fDuration, -hudDelta), m_config.style
                ));
            } else {
                m_actions.run(node, styleEaseIn(
                    CCMoveBy::create(m_fDuration, hudDelta), m_config.style
                ));
            }
        }

        if (auto* info = m_playLayer->m_infoLabel) {
            capture(info);
            CCPoint delta{-50.f * m_config.intensity, 0.f};
            if (m_config.style == LevelEntryStyle::Soft) {
                delta = CCPoint{0.f, 18.f * m_config.intensity};
            } else if (m_config.style == LevelEntryStyle::Impact) {
                delta = CCPoint{-140.f * m_config.intensity, 0.f};
            }
            if (m_direction == TransitionDirection::Enter) {
                info->setPosition(info->getPosition() + delta);
                m_actions.run(info, styleEaseOut(
                    CCMoveBy::create(m_fDuration, -delta), m_config.style
                ));
            } else {
                m_actions.run(info, styleEaseIn(
                    CCMoveBy::create(m_fDuration, delta), m_config.style
                ));
            }
        }
    }

    void animatePlayer(PlayerObject* player, IconType mode) {
        if (!player || !player->getParent()) return;

        auto winSize = CCDirector::get()->getWinSize();
        auto worldPos = player->getParent()->convertToWorldSpace(player->getPosition());
        if (worldPos.x < -15.f || worldPos.x > winSize.width + 15.f ||
            worldPos.y < -15.f || worldPos.y > winSize.height + 15.f) {
            return;
        }

        capture(player);
        auto profile = styleProfile(m_config.style, m_config.intensity);
        float originalX = player->getPositionX();
        float originalScaleX = player->getScaleX();
        float originalScaleY = player->getScaleY();
        float originalRotation = player->getRotation();
        float distance = 0.f;

        if (m_direction == TransitionDirection::Enter) {
            float startWorldX;
            if (m_config.style == LevelEntryStyle::Soft) {
                float offset = 55.f * m_config.intensity;
                startWorldX = worldPos.x < winSize.width * .5f
                    ? worldPos.x - offset
                    : worldPos.x + offset;
            } else {
                float offset = 134.f * m_config.intensity;
                if (m_config.style == LevelEntryStyle::Impact) offset *= 1.65f;
                startWorldX = worldPos.x < winSize.width * .5f
                    ? std::min(worldPos.x - offset, -16.f)
                    : std::max(worldPos.x + offset, winSize.width + 16.f);
            }
            float startX = player->getParent()->convertToNodeSpace({startWorldX, worldPos.y}).x;
            distance = originalX - startX;
            player->setPositionX(startX);
            m_actions.run(player, styleEaseOut(
                CCMoveBy::create(m_fDuration, {distance, 0.f}), m_config.style
            ));
        } else {
            float targetWorldX = worldPos.x < winSize.width * .5f
                ? -32.f - 90.f * m_config.intensity
                : winSize.width + 32.f + 90.f * m_config.intensity;
            float targetX = player->getParent()->convertToNodeSpace({targetWorldX, worldPos.y}).x;
            distance = targetX - originalX;
            m_actions.run(player, styleEaseIn(
                CCMoveBy::create(m_fDuration, {distance, 0.f}), m_config.style
            ));
        }

        if (m_direction == TransitionDirection::Enter &&
            mode == IconType::Robot && player->m_robotSprite) {
            player->m_robotSprite->runAnimationForced("run");
        } else if (m_direction == TransitionDirection::Enter &&
                   mode == IconType::Spider && player->m_spiderSprite) {
            player->m_spiderSprite->runAnimationForced("walk");
        }

        if (mode == IconType::Ball) {
            float turns = m_config.style == LevelEntryStyle::Impact ? 720.f : 360.f;
            float rotation = distance < 0.f ? -turns : turns;
            auto* action = CCRotateBy::create(m_fDuration, rotation);
            m_actions.run(player, m_direction == TransitionDirection::Enter
                ? styleEaseOut(action, m_config.style)
                : styleEaseIn(action, m_config.style));
            return;
        }

        float rotationOffset = distance < 0.f
            ? -profile.playerRotation
            : profile.playerRotation;
        if (m_direction == TransitionDirection::Enter) {
            player->setScaleX(originalScaleX * profile.playerScale);
            player->setScaleY(originalScaleY * profile.playerScale);
            player->setRotation(originalRotation - rotationOffset);
            m_actions.run(player, styleEaseOut(CCScaleTo::create(
                m_fDuration, originalScaleX, originalScaleY
            ), m_config.style));
            m_actions.run(player, styleEaseOut(
                CCRotateTo::create(m_fDuration, originalRotation), m_config.style
            ));
        } else {
            m_actions.run(player, styleEaseIn(CCScaleTo::create(
                m_fDuration,
                originalScaleX * profile.playerScale,
                originalScaleY * profile.playerScale
            ), m_config.style));
            m_actions.run(player, styleEaseIn(CCRotateTo::create(
                m_fDuration, originalRotation + rotationOffset
            ), m_config.style));
        }
    }

    void animatePlayers() {
        auto mode = static_cast<IconType>(m_playLayer->m_levelSettings->m_startMode);
        if (m_playLayer->m_startPosObject && m_playLayer->m_startPosObject->m_startSettings) {
            mode = static_cast<IconType>(
                m_playLayer->m_startPosObject->m_startSettings->m_startMode
            );
        }
        animatePlayer(m_playLayer->m_player1, mode);
        animatePlayer(m_playLayer->m_player2, mode);
    }

    void animateObjects() {
        auto winSize = CCDirector::get()->getWinSize();
        auto count = std::min(
            static_cast<size_t>(std::max(0, m_playLayer->m_activeObjectsCount)),
            m_playLayer->m_activeObjects.size()
        );
        auto profile = styleProfile(m_config.style, m_config.intensity);
        float motionDuration = m_config.staggerObjects ? m_fDuration * .55f : m_fDuration;
        float delayWindow = m_config.staggerObjects ? m_fDuration - motionDuration : 0.f;
        auto* objects = m_playLayer->m_objects;
        if (!objects) return;

        for (size_t i = 0; i < count; ++i) {
            auto* candidate = reinterpret_cast<CCObject*>(m_playLayer->m_activeObjects[i]);
            // Active slots outlive their objects during exit teardown; m_objects is the owning list.
            if (!candidate || !objects->containsObject(candidate)) continue;

            auto* object = typeinfo_cast<GameObject*>(candidate);
            if (!object || !object->getParent()) continue;

            capture(object);
            auto opacity = object->getOpacity();
            auto scaleX = object->getScaleX();
            auto scaleY = object->getScaleY();
            if (m_direction == TransitionDirection::Enter) {
                object->setPosition(object->getPosition() + profile.objects);
                object->setOpacity(0);
                object->setRotationX(object->getRotationX() + profile.objectRotation);
                object->setRotationY(object->getRotationY() + profile.objectRotation);
                object->setScaleX(scaleX * profile.objectScale);
                object->setScaleY(scaleY * profile.objectScale);
            }

            float worldX = object->getParent()->convertToWorldSpace(object->getPosition()).x;
            float delay = std::clamp(worldX / winSize.width, 0.f, 1.f) * delayWindow;
            CCActionInterval* reveal;
            if (m_direction == TransitionDirection::Enter) {
                reveal = CCSpawn::create(
                    styleEaseOut(CCMoveBy::create(motionDuration, -profile.objects), m_config.style),
                    styleEaseOut(CCScaleTo::create(motionDuration, scaleX, scaleY), m_config.style),
                    styleEaseOut(CCRotateBy::create(
                        motionDuration,
                        -profile.objectRotation,
                        -profile.objectRotation
                    ), m_config.style),
                    CCFadeTo::create(motionDuration, opacity),
                    nullptr
                );
            } else {
                reveal = CCSpawn::create(
                    styleEaseIn(CCMoveBy::create(motionDuration, profile.objects), m_config.style),
                    styleEaseIn(CCScaleTo::create(
                        motionDuration,
                        scaleX * profile.objectScale,
                        scaleY * profile.objectScale
                    ), m_config.style),
                    styleEaseIn(CCRotateBy::create(
                        motionDuration,
                        profile.objectRotation,
                        profile.objectRotation
                    ), m_config.style),
                    CCFadeTo::create(motionDuration, 0),
                    nullptr
                );
            }
            if (delay > 0.f) {
                m_actions.run(object, CCSequence::create(
                    CCDelayTime::create(delay), reveal, nullptr
                ));
            } else {
                m_actions.run(object, reveal);
            }
        }
    }

    void animateGradients() {
        if (!m_playLayer->m_gradientLayers) return;
        for (auto [_, gradient] : CCDictionaryExt<int, GJGradientLayer>(
                 m_playLayer->m_gradientLayers)) {
            fadeNode(gradient, m_fDuration);
        }
    }

    void animatePlayLayer() {
        if (m_config.animateBackground) animateBackground();
        if (m_config.animateGround) animateGround();
        if (m_config.animateMiddleground) animateMiddleground();
        if (m_config.animateHud) animateHud();
        if (m_config.animatePlayer) animatePlayers();
        if (m_config.animateObjects) animateObjects();
        if (m_config.animateGradients) animateGradients();
    }

    LevelEntryEffectsConfig m_config;
    TransitionDirection m_direction = TransitionDirection::Enter;
    Ref<PlayLayer> m_exitPlayLayer;
    PlayLayer* m_playLayer = nullptr;
    ActionTracker m_actions;
    std::vector<NodeState> m_states;
    std::unordered_set<CCNode*> m_captured;
    ccBlendFunc m_backgroundBlend{};
    ccBlendFunc m_shaderBlend{};
    bool m_hasBackgroundBlend = false;
    bool m_hasShaderBlend = false;
    bool m_finished = false;
    bool m_restored = false;
    bool m_switchingToIncoming = false;
    bool m_touchDispatchWasEnabled = true;
    bool m_fakedIncomingRunning = false;
    bool m_inSceneEntered = false;
};

void applyReducedMotion(LevelEntryEffectsConfig& config) {
    if (config.respectReducedMotion &&
        Mod::get()->getSavedValue<bool>("smooth-ui-reduced-motion", false)) {
        config.duration = std::min(config.duration, .25f);
        config.intensity = .2f;
        config.animatePage = false;
        config.animateGround = false;
        config.animateMiddleground = false;
        config.animateHud = false;
        config.animatePlayer = false;
        config.animateObjects = false;
    }
}

LevelEntryEffectsConfig effectiveEntryConfig() {
    auto config = paimon::transitions::getLevelEntryEffectsConfig();
    applyReducedMotion(config);
    return config;
}

LevelEntryEffectsConfig effectiveExitConfig() {
    auto config = paimon::transitions::getLevelEntryEffectsConfig();
    if (config.exitMode == LevelExitMode::Disabled) {
        config.enabled = false;
        return config;
    }
    if (config.exitMode == LevelExitMode::Separate) {
        config.style = config.exitStyle;
        config.duration = config.exitDuration;
        config.intensity = config.exitIntensity;
    }
    applyReducedMotion(config);
    return config;
}

} // namespace

namespace paimon::transitions {

std::vector<LevelEntryStyle> const& levelEntryStyles() {
    static const std::vector styles = {
        LevelEntryStyle::SmoothPlus,
        LevelEntryStyle::LegacyBounce,
        LevelEntryStyle::Soft,
        LevelEntryStyle::Impact,
    };
    return styles;
}

std::string levelEntryStyleId(LevelEntryStyle style) {
    switch (style) {
        case LevelEntryStyle::SmoothPlus: return "smooth-plus";
        case LevelEntryStyle::LegacyBounce: return "legacy-bounce";
        case LevelEntryStyle::Soft:       return "soft";
        case LevelEntryStyle::Impact:     return "impact";
    }
    return "smooth-plus";
}

std::string levelEntryStyleName(LevelEntryStyle style) {
    switch (style) {
        case LevelEntryStyle::SmoothPlus: return "Smooth+";
        case LevelEntryStyle::LegacyBounce: return "Rebote legacy";
        case LevelEntryStyle::Soft:       return "Suave";
        case LevelEntryStyle::Impact:     return "Impacto";
    }
    return "Smooth+";
}

LevelEntryStyle levelEntryStyleFromId(std::string const& id) {
    if (id == "legacy-bounce" || id == "original" || id == "shutters") {
        return LevelEntryStyle::LegacyBounce;
    }
    if (id == "soft" || id == "bloom") return LevelEntryStyle::Soft;
    if (id == "impact" || id == "swipe") return LevelEntryStyle::Impact;
    return LevelEntryStyle::SmoothPlus;
}

std::vector<LevelExitMode> const& levelExitModes() {
    static const std::vector modes = {
        LevelExitMode::Disabled,
        LevelExitMode::MatchEntry,
        LevelExitMode::Separate,
    };
    return modes;
}

std::string levelExitModeId(LevelExitMode mode) {
    switch (mode) {
        case LevelExitMode::Disabled:   return "disabled";
        case LevelExitMode::MatchEntry: return "match-entry";
        case LevelExitMode::Separate:   return "separate";
    }
    return "match-entry";
}

std::string levelExitModeName(LevelExitMode mode) {
    switch (mode) {
        case LevelExitMode::Disabled:   return "Sin animacion especial";
        case LevelExitMode::MatchEntry: return "Reflejar entrada";
        case LevelExitMode::Separate:   return "Estilo diferente";
    }
    return "Reflejar entrada";
}

LevelExitMode levelExitModeFromId(std::string const& id) {
    if (id == "disabled") return LevelExitMode::Disabled;
    if (id == "separate") return LevelExitMode::Separate;
    return LevelExitMode::MatchEntry;
}

LevelEntryEffectsConfig getLevelEntryEffectsConfig() {
    auto* mod = Mod::get();
    LevelEntryEffectsConfig config;
    config.enabled = mod->getSavedValue<bool>("level-entry-smooth-enabled", true);
    config.style = levelEntryStyleFromId(
        mod->getSavedValue<std::string>("level-entry-smooth-style", "smooth-plus")
    );
    config.duration = std::clamp(
        static_cast<float>(mod->getSavedValue<double>("level-entry-smooth-duration", 1.0)),
        .35f, 1.8f
    );
    config.intensity = std::clamp(
        static_cast<float>(mod->getSavedValue<double>("level-entry-smooth-intensity", 1.0)),
        .25f, 1.5f
    );
    config.animatePage = mod->getSavedValue<bool>("level-entry-smooth-page", true);
    config.animateBackground = mod->getSavedValue<bool>("level-entry-smooth-background", true);
    config.animateGround = mod->getSavedValue<bool>("level-entry-smooth-ground", true);
    config.animateMiddleground = mod->getSavedValue<bool>("level-entry-smooth-middle", true);
    config.animateHud = mod->getSavedValue<bool>("level-entry-smooth-hud", true);
    config.animatePlayer = mod->getSavedValue<bool>("level-entry-smooth-player", true);
    config.animateObjects = mod->getSavedValue<bool>("level-entry-smooth-objects", true);
    config.animateGradients = mod->getSavedValue<bool>("level-entry-smooth-gradients", true);
    config.staggerObjects = mod->getSavedValue<bool>("level-entry-smooth-stagger", true);
    config.respectReducedMotion = mod->getSavedValue<bool>("level-entry-smooth-reduced-motion", true);
    config.exitMode = levelExitModeFromId(
        mod->getSavedValue<std::string>("level-exit-smooth-mode", "match-entry")
    );
    config.exitStyle = levelEntryStyleFromId(
        mod->getSavedValue<std::string>("level-exit-smooth-style", "soft")
    );
    config.exitDuration = std::clamp(
        static_cast<float>(mod->getSavedValue<double>("level-exit-smooth-duration", .75)),
        .25f, 1.8f
    );
    config.exitIntensity = std::clamp(
        static_cast<float>(mod->getSavedValue<double>("level-exit-smooth-intensity", 1.0)),
        .25f, 1.5f
    );
    return config;
}

void saveLevelEntryEffectsConfig(LevelEntryEffectsConfig const& config) {
    auto* mod = Mod::get();
    mod->setSavedValue("level-entry-smooth-enabled", config.enabled);
    mod->setSavedValue("level-entry-smooth-style", levelEntryStyleId(config.style));
    mod->setSavedValue("level-entry-smooth-duration", static_cast<double>(config.duration));
    mod->setSavedValue("level-entry-smooth-intensity", static_cast<double>(config.intensity));
    mod->setSavedValue("level-entry-smooth-page", config.animatePage);
    mod->setSavedValue("level-entry-smooth-background", config.animateBackground);
    mod->setSavedValue("level-entry-smooth-ground", config.animateGround);
    mod->setSavedValue("level-entry-smooth-middle", config.animateMiddleground);
    mod->setSavedValue("level-entry-smooth-hud", config.animateHud);
    mod->setSavedValue("level-entry-smooth-player", config.animatePlayer);
    mod->setSavedValue("level-entry-smooth-objects", config.animateObjects);
    mod->setSavedValue("level-entry-smooth-gradients", config.animateGradients);
    mod->setSavedValue("level-entry-smooth-stagger", config.staggerObjects);
    mod->setSavedValue("level-entry-smooth-reduced-motion", config.respectReducedMotion);
    mod->setSavedValue("level-exit-smooth-mode", levelExitModeId(config.exitMode));
    mod->setSavedValue("level-exit-smooth-style", levelEntryStyleId(config.exitStyle));
    mod->setSavedValue("level-exit-smooth-duration", static_cast<double>(config.exitDuration));
    mod->setSavedValue("level-exit-smooth-intensity", static_cast<double>(config.exitIntensity));
}

bool shouldUseLevelEntryTransition() {
    return !paimon::gameplayperf::isOptionEnabled(
        paimon::gameplayperf::kTransitionsModuleId
    ) && getLevelEntryEffectsConfig().enabled;
}

CCTransitionScene* createLevelEntryTransition(CCScene* destination) {
    if (!destination) return nullptr;
    if (paimon::gameplayperf::isOptionEnabled(
            paimon::gameplayperf::kTransitionsModuleId)) return nullptr;
    auto config = effectiveEntryConfig();
    if (!config.enabled) return nullptr;
    return LevelEffectsTransitionScene::create(
        destination,
        std::move(config),
        TransitionDirection::Enter
    );
}

bool shouldUseLevelExitTransition() {
    auto config = getLevelEntryEffectsConfig();
    return !paimon::gameplayperf::isOptionEnabled(
        paimon::gameplayperf::kTransitionsModuleId
    ) &&
        config.enabled && config.exitMode != LevelExitMode::Disabled;
}

CCTransitionScene* createLevelExitTransition(CCScene* destination) {
    if (!destination) return nullptr;
    if (paimon::gameplayperf::isOptionEnabled(
            paimon::gameplayperf::kTransitionsModuleId)) return nullptr;
    auto config = effectiveExitConfig();
    if (!config.enabled) return nullptr;
    return LevelEffectsTransitionScene::create(
        destination,
        std::move(config),
        TransitionDirection::Exit,
        s_levelExitPlayLayer.data()
    );
}

void beginLevelExitTransition(PlayLayer* playLayer) {
    if (playLayer) s_levelExitPlayLayer = playLayer;
    s_levelExitPending = true;
}

void endLevelExitTransition() {
    s_levelExitPending = false;
    s_levelExitPlayLayer = nullptr;
}

bool isLevelExitTransitionPending() {
    return s_levelExitPending.load();
}

} // namespace paimon::transitions
