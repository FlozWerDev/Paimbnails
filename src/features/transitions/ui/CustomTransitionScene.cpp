#include "CustomTransitionScene.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include <Geode/Geode.hpp>
#include <exception>
#include <filesystem>

using namespace cocos2d;
using namespace geode::prelude;

// ════════════════════════════════════════════════════════════
// CustomTransitionScene — ejecuta transiciones DSL
// ════════════════════════════════════════════════════════════

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

    // Validate destination scene - required for transition
    if (!destScene) {
        log::warn("[CustomTransitionScene] init failed: destScene is null");
        return false;
    }

    m_commands = commands;
    m_isPush = isPush;
    m_destScene = destScene;
    m_destScene->retain();

    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // ── Container for origin scene ──
    // Position at {0,0} with default layer behavior so children keep their
    // original screen coordinates when reparented.
    m_fromContainer = CCLayerColor::create({0, 0, 0, 255}, winSize.width, winSize.height);
    m_fromContainer->setPosition({0, 0});
    this->addChild(m_fromContainer, 0);

    // Move children from fromScene, saving their original state
    bool hasFromContent = false;
    if (fromScene) {
        CCArray* children = fromScene->getChildren();
        if (children && children->count() > 0) {
            auto copy = CCArray::createWithCapacity(children->count());
            for (unsigned i = 0; i < children->count(); ++i) {
                auto* obj = children->objectAtIndex(i);
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

    // If fromScene had no content, add a placeholder so transition isn't empty
    if (!hasFromContent) {
        auto* placeholder = CCLayerColor::create({30, 30, 30, 255}, winSize.width, winSize.height);
        if (placeholder) {
            m_fromContainer->addChild(placeholder, -1);
        }
    }

    // ── Container for destination scene (starts transparent) ──
    m_toContainer = CCLayerColor::create({0, 0, 0, 0}, winSize.width, winSize.height);
    m_toContainer->setPosition({0, 0});
    this->addChild(m_toContainer, 1);

    // Move children from destScene, saving state
    bool hasToContent = false;
    if (destScene) {
        CCArray* children = destScene->getChildren();
        if (children && children->count() > 0) {
            auto copy = CCArray::createWithCapacity(children->count());
            for (unsigned i = 0; i < children->count(); ++i) {
                auto* obj = children->objectAtIndex(i);
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

    // If destScene had no content, add a placeholder
    if (!hasToContent) {
        auto* placeholder = CCLayerColor::create({40, 60, 100, 255}, winSize.width, winSize.height);
        if (placeholder) {
            m_toContainer->addChild(placeholder, -1);
        }
    }

    // Initial state: destination invisible
    m_toContainer->setVisible(true);
    m_toContainer->setOpacity(0);

    // Default fallback if no commands
    if (m_commands.empty()) {
        m_commands.push_back({CommandAction::FadeOut, "from", 0.15f, 0,0,0,0, 255.f, 0.f});
        m_commands.push_back({CommandAction::FadeIn, "to", 0.15f, 0,0,0,0, 0.f, 255.f});
    }

    // Sanitize all commands: ensure minimum duration
    for (auto& cmd : m_commands) {
        if (cmd.duration < 0.001f && cmd.action != CommandAction::Color)
            cmd.duration = 0.001f;
    }

    // Calculate total duration for safety timeout
    m_totalDuration = 0.f;
    for (auto const& cmd : m_commands) {
        m_totalDuration += cmd.duration;
    }
    m_totalDuration += 0.5f; // safety margin

    return true;
}

CustomTransitionScene::~CustomTransitionScene() {
    CC_SAFE_RELEASE_NULL(m_destScene);
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
    CCScene::onEnter();
    this->scheduleUpdate();

    // Begin first command
    if (m_currentCommandIdx < static_cast<int>(m_commands.size())) {
        if (!beginCommandSafe(m_commands[m_currentCommandIdx])) {
            triggerSafeFallback("onEnter.beginCommand");
        }
    }
}

void CustomTransitionScene::onExit() {
    this->unscheduleUpdate();
    CCScene::onExit();
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

    // Spawn: run next N commands in parallel (handled in update)
    if (cmd.action == CommandAction::Spawn) return;

    // Image: create a sprite overlay
    if (cmd.action == CommandAction::Image) {
        if (auto* existing = this->getChildByTag(8888)) {
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
                auto winSize = CCDirector::sharedDirector()->getWinSize();
                spr->setPosition({winSize.width / 2, winSize.height / 2});
                spr->setOpacity(0);
                spr->setTag(8888);
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
    // Spawn is handled at the executor level, not here
    if (cmd.action == CommandAction::Spawn) return;

    auto* target = getTarget(cmd.target);

    // Easing curves
    float t = progress;
    if (cmd.action == CommandAction::EaseIn) {
        t = progress * progress * progress; // cubic ease in
    } else if (cmd.action == CommandAction::EaseOut) {
        float inv = 1.f - progress;
        t = 1.f - (inv * inv * inv); // cubic ease out
    } else if (cmd.action == CommandAction::Bounce) {
        // Bounce easing out
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
        // Fade in the image overlay
        auto* imgNode = this->getChildByTag(8888);
        if (auto* imgSpr = typeinfo_cast<CCSprite*>(imgNode)) {
            float opacity = 255.f * t;
            imgSpr->setOpacity(static_cast<GLubyte>(std::clamp(opacity, 0.f, 255.f)));
        }
        return;
    }

    if (cmd.action == CommandAction::Shake && target) {
        // Camera shake: random offset based on intensity
        float amp = cmd.intensity * (1.f - t); // shake decays over time
        float offX = (static_cast<float>(rand() % 200) / 100.f - 1.f) * amp;
        float offY = (static_cast<float>(rand() % 200) / 100.f - 1.f) * amp;
        auto winSize = CCDirector::sharedDirector()->getWinSize();
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

    // Reset shake position on finish
    if (cmd.action == CommandAction::Shake) {
        auto* target = getTarget(cmd.target);
        if (target) {
            auto winSize = CCDirector::sharedDirector()->getWinSize();
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

    // Move children from toContainer back to destScene, restoring their original state
    if (m_destScene && m_toContainer) {
        CCArray* children = m_toContainer->getChildren();
        if (children && children->count() > 0) {
            auto copy = CCArray::createWithCapacity(children->count());
            for (unsigned i = 0; i < children->count(); ++i) {
                auto* obj = children->objectAtIndex(i);
                if (obj) copy->addObject(obj);
            }
            for (auto* node : CCArrayExt<CCNode*>(copy)) {
                if (!node) continue;
                node->retain();
                node->removeFromParentAndCleanup(false);

                // Restore original state if we saved it
                auto it = m_originalStates.find(node);
                if (it != m_originalStates.end()) {
                    auto const& state = it->second;
                    node->setPosition(state.position);
                    node->setScale(state.scale);
                    node->setRotation(state.rotation);
                    node->setVisible(state.visible);
                    if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node)) {
                        rgba->setOpacity(state.opacity);
                    }
                    m_destScene->addChild(node, state.zOrder);
                } else {
                    m_destScene->addChild(node, node->getZOrder());
                }
                node->release();
            }
        }
    }

    // Clean up from container
    if (m_fromContainer) {
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
    CCDirector::sharedDirector()->replaceScene(m_destScene);
}

CCLayerColor* CustomTransitionScene::getTarget(std::string const& targetName) {
    if (targetName == "to") return m_toContainer;
    return m_fromContainer;
}
