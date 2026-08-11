#include "CustomTransitionScene.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <Geode/Geode.hpp>
#include <exception>
#include <filesystem>

using namespace cocos2d;
using namespace geode::prelude;

bool CustomTransitionScene::isActive() {
    auto* director = CCDirector::get();
    return typeinfo_cast<CustomTransitionScene*>(director->getRunningScene()) ||
        typeinfo_cast<CustomTransitionScene*>(director->getNextScene());
}

CustomTransitionScene* CustomTransitionScene::create(
    CCScene* fromScene,
    CCScene* destScene,
    std::vector<TransitionCommand> const& commands,
    bool isPush)
{
    auto ret = new CustomTransitionScene();
    if (ret && ret->initWithScenes(fromScene, destScene, commands, isPush)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool CustomTransitionScene::initWithScenes(
    CCScene* fromScene,
    CCScene* destScene,
    std::vector<TransitionCommand> const& commands,
    bool isPush)
{
    if (!CCScene::init()) return false;

    if (!destScene) {
        log::warn("[CustomTransitionScene] init failed: destScene is null");
        return false;
    }

    m_commands = commands;
    m_isPush = isPush;
    m_fromScene = fromScene;
    m_destScene = destScene;

    auto winSize = CCDirector::get()->getWinSize();

    // GD-blue gradient backdrop behind both containers: when the scenes move,
    // rotate or fade during a command, the exposed area shows this instead of
    // the raw (often white) GL clear color.
    {
        CCNode* backdrop = nullptr;
        if (auto grad = paimon::SpriteHelper::safeCreate("GJ_gradientBG.png")) {
            grad->setAnchorPoint({0.f, 0.f});
            grad->setScaleX(winSize.width / grad->getContentSize().width);
            grad->setScaleY(winSize.height / grad->getContentSize().height);
            grad->setColor({0, 102, 255});
            backdrop = grad;
        } else {
            auto flat = CCLayerColor::create({0, 102, 255, 255}, winSize.width, winSize.height);
            backdrop = flat;
        }
        if (backdrop) {
            backdrop->setID("paimon-transition-backdrop"_spr);
            this->addChild(backdrop, -10);
        }
    }

    m_fromContainer = CCLayerColor::create({0, 0, 0, 255}, winSize.width, winSize.height);
    m_fromContainer->setPosition({0, 0});
    this->addChild(m_fromContainer, 0);

    // Save and reparent children from origin scene
    bool hasFromContent = false;
    if (fromScene) {
        CCArray* children = fromScene->getChildren();
        if (children && children->count() > 0) {
            auto copy = CCArray::createWithCapacity(children->count());
            for (auto* obj : CCArrayExt<CCNode*>(children)) {
                if (obj) copy->addObject(obj);
            }
            for (auto* node : CCArrayExt<CCNode*>(copy)) {
                if (!node) continue;
                NodeState state;
                state.position = node->getPosition();
                state.scale = node->getScale();
                state.rotation = node->getRotation();
                if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node)) {
                    state.opacity = rgba->getOpacity();
                } else {
                    state.opacity = 255;
                }
                state.zOrder = node->getZOrder();
                state.visible = node->isVisible();
                m_originalStates[node] = state;

                node->retain();
                node->removeFromParentAndCleanup(false);
                m_fromContainer->addChild(node, node->getZOrder());
                node->release();
                hasFromContent = true;
            }
        }
    }

    // Fallback if origin had no children
    if (!hasFromContent) {
        auto* placeholder = CCLayerColor::create({30, 30, 30, 255}, winSize.width, winSize.height);
        if (placeholder) {
            m_fromContainer->addChild(placeholder, -1);
        }
    }

    m_toContainer = CCLayerColor::create({0, 0, 0, 0}, winSize.width, winSize.height);
    m_toContainer->setPosition({0, 0});
    this->addChild(m_toContainer, 1);

    // Save and reparent children from destination scene
    bool hasToContent = false;
    if (destScene) {
        CCArray* children = destScene->getChildren();
        if (children && children->count() > 0) {
            auto copy = CCArray::createWithCapacity(children->count());
            for (auto* obj : CCArrayExt<CCNode*>(children)) {
                if (obj) copy->addObject(obj);
            }
            for (auto* node : CCArrayExt<CCNode*>(copy)) {
                if (!node) continue;
                NodeState state;
                state.position = node->getPosition();
                state.scale = node->getScale();
                state.rotation = node->getRotation();
                if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node)) {
                    state.opacity = rgba->getOpacity();
                } else {
                    state.opacity = 255;
                }
                state.zOrder = node->getZOrder();
                state.visible = node->isVisible();
                m_originalStates[node] = state;

                node->retain();
                node->removeFromParentAndCleanup(false);
                m_toContainer->addChild(node, node->getZOrder());
                node->release();
                hasToContent = true;
            }
        }
    }

    // Fallback if destination had no children
    if (!hasToContent) {
        auto* placeholder = CCLayerColor::create({40, 60, 100, 255}, winSize.width, winSize.height);
        if (placeholder) {
            m_toContainer->addChild(placeholder, -1);
        }
    }

    m_toContainer->setOpacity(0);

    // Default transition if no commands provided
    if (m_commands.empty()) {
        m_commands.push_back({CommandAction::FadeOut, "from", 0.15f, 0,0,0,0, 255.f, 0.f});
        m_commands.push_back({CommandAction::FadeIn, "to", 0.15f, 0,0,0,0, 0.f, 255.f});
    }

    for (auto& cmd : m_commands) {
        if (cmd.duration < 0.001f && cmd.action != CommandAction::Color)
            cmd.duration = 0.001f;
    }

    // Safety timeout
    m_totalDuration = 0.f;
    for (auto const& cmd : m_commands) {
        m_totalDuration += cmd.duration;
    }
    m_totalDuration += 0.5f; // safety margin

    return true;
}

CustomTransitionScene::~CustomTransitionScene() {
    if (!m_started) {
        restoreSceneChildren(m_fromContainer, m_fromScene);
        restoreSceneChildren(m_toContainer, m_destScene);
    } else if (m_isPush) {
        restoreSceneChildren(m_fromContainer, m_fromScene);
    }
    restoreTouchDispatch();
}

void CustomTransitionScene::triggerSafeFallback(char const* where, char const* reason) {
    if (reason) {
        log::warn("[CustomTransition] Runtime error at {}: {}", where, reason);
        std::string msg = where ? where : "unknown";
        msg += ": ";
        msg += reason;
        TransitionManager::get().tripCustomSafeMode(msg);
    } else {
        log::warn("[CustomTransition] Runtime error at {}", where);
        TransitionManager::get().tripCustomSafeMode(where ? where : "unknown");
    }

    if (!m_finished) {
        finishTransition();
    }
}

void CustomTransitionScene::onEnter() {
    m_started = true;
    CCScene::onEnter();

    if (auto* dispatcher = CCDirector::get()->getTouchDispatcher()) {
        dispatcher->setDispatchEvents(false);
        m_touchDispatchDisabled = true;
    }

    this->scheduleUpdate();

    if (m_currentCommandIdx < static_cast<int>(m_commands.size())) {
        if (!beginCommandSafe(m_commands[m_currentCommandIdx])) {
            triggerSafeFallback("onEnter.beginCommand");
        }
    }
}

void CustomTransitionScene::onExit() {
    this->unscheduleUpdate();
    this->unschedule(schedule_selector(CustomTransitionScene::onTransitionFinished));

    if (m_isPush && !m_finished) {
        restoreSceneChildren(m_fromContainer, m_fromScene);
    }

    CCScene::onExit();
    restoreTouchDispatch();
}

void CustomTransitionScene::update(float dt) {
    if (m_finished) return;

    // Safety timeout: if transition takes too long, force finish
    m_globalElapsed += dt;
    if (m_globalElapsed > m_totalDuration) {
        log::warn("[CustomTransition] Safety timeout reached ({:.1f}s), forcing finish", m_globalElapsed);
        finishTransition();
        return;
    }

    if (m_currentCommandIdx >= static_cast<int>(m_commands.size())) {
        finishTransition();
        return;
    }

    auto& cmd = m_commands[m_currentCommandIdx];
    m_commandElapsed += dt;

    float duration = std::max(cmd.duration, 0.001f);
    float progress = std::min(m_commandElapsed / duration, 1.0f);

    if (!updateCommandSafe(cmd, progress)) {
        triggerSafeFallback("update");
        return;
    }

    if (progress >= 1.0f) {
        finishCurrentCommand();
    }
}

bool CustomTransitionScene::beginCommandSafe(TransitionCommand const& cmd) {
    if (m_finished) return false;
    if (cmd.duration < 0.f) {
        log::warn("[CustomTransition] Invalid command duration: {:.3f}", cmd.duration);
        return false;
    }
    beginCommand(cmd);
    return true;
}

bool CustomTransitionScene::updateCommandSafe(TransitionCommand const& cmd, float progress) {
    if (m_finished) return false;
    if (progress < 0.f || progress > 1.01f) {
        log::warn("[CustomTransition] progress out of range: {:.3f}", progress);
        return false;
    }
    updateCommand(cmd, progress);
    return true;
}

void CustomTransitionScene::beginCommand(TransitionCommand const& cmd) {
    m_commandElapsed = 0.f;

    if (cmd.action == CommandAction::Spawn) return;

    if (cmd.action == CommandAction::Image) {
        if (auto* existing = this->getChildByID("paimon-transition-image"_spr)) {
            existing->removeFromParent();
        }

        if (!cmd.imagePath.empty()) {
            auto fsPath = paimon::assets::normalizePath(std::filesystem::path(cmd.imagePath));
            std::error_code ec;
            if (!std::filesystem::exists(fsPath, ec) || ec) {
                log::warn("[CustomTransitionScene] Image overlay not found: {}", cmd.imagePath);
                return;
            }

            auto* spr = ImageLoadHelper::loadAnimatedOrStatic(fsPath, 16,
                [](std::string const& path) -> CCSprite* {
                    return AnimatedGIFSprite::create(path);
                });

            if (spr) {
                auto winSize = CCDirector::get()->getWinSize();
                spr->setPosition({winSize.width / 2, winSize.height / 2});
                spr->setOpacity(0);
                spr->setID("paimon-transition-image"_spr);
                this->addChild(spr, 10);
            } else {
                log::warn("[CustomTransitionScene] Failed to load image overlay: {}", cmd.imagePath);
            }
        }
        return;
    }

    auto* target = getTarget(cmd.target);
    if (!target) return;

    switch (cmd.action) {
        case CommandAction::FadeOut:
            target->setOpacity(static_cast<GLubyte>(std::clamp(cmd.fromVal, 0.f, 255.f)));
            break;
        case CommandAction::FadeIn:
            target->setVisible(true);
            target->setOpacity(static_cast<GLubyte>(std::clamp(cmd.fromVal, 0.f, 255.f)));
            break;
        case CommandAction::Move:
            target->setPosition({cmd.fromX, cmd.fromY});
            break;
        case CommandAction::Scale:
            target->setScale(std::clamp(cmd.fromVal, 0.01f, 10.f));
            break;
        case CommandAction::Rotate:
            target->setRotation(cmd.fromVal);
            break;
        case CommandAction::Color:
            target->setColor({
                static_cast<GLubyte>(std::clamp(cmd.r, 0, 255)),
                static_cast<GLubyte>(std::clamp(cmd.g, 0, 255)),
                static_cast<GLubyte>(std::clamp(cmd.b, 0, 255))
            });
            break;
        case CommandAction::Wait:
        case CommandAction::Shake:
            break;
        case CommandAction::EaseIn:
        case CommandAction::EaseOut:
        case CommandAction::Bounce:
            target->setOpacity(static_cast<GLubyte>(std::clamp(cmd.fromVal, 0.f, 255.f)));
            break;
        default:
            break;
    }
}

void CustomTransitionScene::updateCommand(TransitionCommand const& cmd, float progress) {
    if (cmd.action == CommandAction::Spawn) return;

    auto* target = getTarget(cmd.target);

    float t = progress;
    if (cmd.action == CommandAction::EaseIn) {
        t = progress * progress * progress;
    } else if (cmd.action == CommandAction::EaseOut) {
        float inv = 1.f - progress;
        t = 1.f - (inv * inv * inv);
    } else if (cmd.action == CommandAction::Bounce) {
        if (progress < 1.f / 2.75f) {
            t = 7.5625f * progress * progress;
        } else if (progress < 2.f / 2.75f) {
            float p = progress - 1.5f / 2.75f;
            t = 7.5625f * p * p + 0.75f;
        } else if (progress < 2.5f / 2.75f) {
            float p = progress - 2.25f / 2.75f;
            t = 7.5625f * p * p + 0.9375f;
        } else {
            float p = progress - 2.625f / 2.75f;
            t = 7.5625f * p * p + 0.984375f;
        }
    }

    if (cmd.action == CommandAction::Image) {
        auto* imgNode = this->getChildByID("paimon-transition-image"_spr);
        if (auto* imgSpr = typeinfo_cast<CCSprite*>(imgNode)) {
            float opacity = 255.f * t;
            imgSpr->setOpacity(static_cast<GLubyte>(std::clamp(opacity, 0.f, 255.f)));
        }
        return;
    }

    if (cmd.action == CommandAction::Shake && target) {
        float amp = cmd.intensity * (1.f - t); // shake decays over time
        float offX = (static_cast<float>(rand() % 200) / 100.f - 1.f) * amp;
        float offY = (static_cast<float>(rand() % 200) / 100.f - 1.f) * amp;
        auto winSize = CCDirector::get()->getWinSize();
        target->setPosition({winSize.width / 2 + offX, winSize.height / 2 + offY});
        return;
    }

    if (!target && cmd.action != CommandAction::Wait) return;

    switch (cmd.action) {
        case CommandAction::FadeOut:
        case CommandAction::FadeIn:
        case CommandAction::EaseIn:
        case CommandAction::EaseOut:
        case CommandAction::Bounce: {
            float val = cmd.fromVal + (cmd.toVal - cmd.fromVal) * t;
            target->setOpacity(static_cast<GLubyte>(std::clamp(val, 0.f, 255.f)));
            break;
        }
        case CommandAction::Move: {
            float x = cmd.fromX + (cmd.toX - cmd.fromX) * t;
            float y = cmd.fromY + (cmd.toY - cmd.fromY) * t;
            target->setPosition({x, y});
            break;
        }
        case CommandAction::Scale: {
            float val = cmd.fromVal + (cmd.toVal - cmd.fromVal) * t;
            target->setScale(std::clamp(val, 0.01f, 10.f));
            break;
        }
        case CommandAction::Rotate: {
            float val = cmd.fromVal + (cmd.toVal - cmd.fromVal) * t;
            target->setRotation(val);
            break;
        }
        case CommandAction::Color:
        case CommandAction::Wait:
        default:
            break;
    }
}

void CustomTransitionScene::finishCurrentCommand() {
    auto& cmd = m_commands[m_currentCommandIdx];
    updateCommand(cmd, 1.0f);

    if (cmd.action == CommandAction::Shake) {
        auto* target = getTarget(cmd.target);
        if (target) {
            auto winSize = CCDirector::get()->getWinSize();
            target->setPosition({winSize.width / 2, winSize.height / 2});
        }
    }

    m_currentCommandIdx++;
    m_commandElapsed = 0.f;

    if (m_currentCommandIdx < static_cast<int>(m_commands.size())) {
        beginCommand(m_commands[m_currentCommandIdx]);
    }
}

void CustomTransitionScene::finishTransition() {
    if (m_finished) return;
    m_finished = true;
    this->unscheduleUpdate();

    restoreSceneChildren(m_toContainer, m_destScene);

    if (m_isPush) {
        restoreSceneChildren(m_fromContainer, m_fromScene);
    } else if (m_fromContainer) {
        m_fromContainer->removeAllChildrenWithCleanup(true);
    }
    if (m_toContainer) {
        m_toContainer->removeAllChildrenWithCleanup(true);
    }
    m_originalStates.clear();

    // Replace with dest scene in next frame
    if (this->isRunning()) {
        this->scheduleOnce(schedule_selector(CustomTransitionScene::onTransitionFinished), 0.f);
    } else {
        onTransitionFinished(0.f);
    }
}

void CustomTransitionScene::onTransitionFinished(float) {
    if (!m_destScene) return;
    CCDirector::get()->replaceScene(m_destScene);
}

void CustomTransitionScene::restoreSceneChildren(CCLayerColor* container, CCScene* scene) {
    if (!container || !scene) return;

    auto* children = container->getChildren();
    if (!children || children->count() == 0) return;

    auto copy = CCArray::createWithCapacity(children->count());
    for (auto* obj : CCArrayExt<CCNode*>(children)) {
        if (obj) copy->addObject(obj);
    }

    for (auto* node : CCArrayExt<CCNode*>(copy)) {
        if (!node) continue;
        auto it = m_originalStates.find(node);
        if (it == m_originalStates.end()) continue;

        auto const& state = it->second;
        node->retain();
        node->removeFromParentAndCleanup(false);
        node->setPosition(state.position);
        node->setScale(state.scale);
        node->setRotation(state.rotation);
        node->setVisible(state.visible);
        if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node)) {
            rgba->setOpacity(state.opacity);
        }
        scene->addChild(node, state.zOrder);
        node->release();
    }
}

void CustomTransitionScene::restoreTouchDispatch() {
    if (!m_touchDispatchDisabled) return;

    if (auto* dispatcher = CCDirector::get()->getTouchDispatcher()) {
        dispatcher->setDispatchEvents(true);
    }
    m_touchDispatchDisabled = false;
}

CCLayerColor* CustomTransitionScene::getTarget(std::string const& targetName) {
    if (targetName == "to") return m_toContainer;
    return m_fromContainer;
}
