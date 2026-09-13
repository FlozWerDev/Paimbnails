#include "CustomTransitionScene.hpp"
#include <cmath>
using namespace geode::prelude;
using namespace paimon::transitions;

bool CustomTransitionScene::isActive() {
    auto* director = CCDirector::get();
    return typeinfo_cast<CustomTransitionScene*>(director->getRunningScene()) ||
        typeinfo_cast<CustomTransitionScene*>(director->getNextScene());
}
CustomTransitionScene* CustomTransitionScene::create(CCScene* from, CCScene* to,
    std::vector<TransitionCommand> const& commands, bool) {
    // CCDirector owns push/replace/pop semantics; never edit its scene pointers.
    if (!from || from == to || from != CCDirector::get()->getRunningScene()) return nullptr;
    auto* result = new CustomTransitionScene();
    if (result->initialize(to, commands)) { result->autorelease(); return result; }
    delete result; return nullptr;
}
CustomTransitionScene* CustomTransitionScene::createStinger(CCScene* to,
    std::shared_ptr<TransitionMedia> media, float duration, float cutPoint) {
    if (!to || !media || to == CCDirector::get()->getRunningScene()) return nullptr;
    auto* result = new CustomTransitionScene();
    result->m_stinger = std::move(media);
    result->m_cutPoint = bounded(cutPoint, .5f, 0.f, 1.f);
    if (result->initWithDuration(bounded(duration, 1.f, .05f, 30.f), to)) {
        result->autorelease(); return result;
    }
    delete result; return nullptr;
}
bool CustomTransitionScene::initialize(CCScene* to, std::vector<TransitionCommand> commands) {
    if (!to || commands.empty()) return false;
    TransitionManager::sanitizeCommands(commands);
    m_commands = std::move(commands);
    m_timeline = compileTimeline(m_commands, [](auto const& c) { return c.action == CommandAction::Spawn; });
    if (m_timeline.clips.empty() || m_timeline.duration > 30.f) return false;
    m_done.resize(m_timeline.clips.size());
    m_started.resize(m_timeline.clips.size());
    m_origins.resize(m_timeline.clips.size());
    m_overlays.resize(m_timeline.clips.size());
    m_media.resize(m_timeline.clips.size());
    for (std::size_t i = 0; i < m_timeline.clips.size(); ++i) {
        auto const& command = m_commands[m_timeline.clips[i].command];
        if (command.action == CommandAction::Image) {
            m_media[i] = findTransitionMedia(command.imagePath);
            // No synchronous decode or invisible partial transition.
            if (!m_media[i]) { prepareTransitionMedia(command.imagePath); return false; }
        }
    }
    return initWithDuration(m_timeline.duration, to);
}
bool CustomTransitionScene::capture(CCScene* scene, Ref<CCRenderTexture>& surface, CCLayerRGBA*& container) {
    auto size = CCDirector::get()->getWinSize();
    surface = CCRenderTexture::create(static_cast<int>(size.width), static_cast<int>(size.height));
    if (!surface || !surface->getSprite()) return false;
    // Both scenes stay attached exactly where GD put them. Only their pixels
    // enter this hierarchy; transform/opacity commands cannot affect gameplay.
    surface->beginWithClear(0, 0, 0, 1);
    scene->visit();
    surface->end();
    auto* sprite = CCSprite::createWithTexture(surface->getSprite()->getTexture(), surface->getSprite()->getTextureRect());
    if (!sprite) return false;
    sprite->setFlipY(true);
    sprite->setPosition(size / 2);
    sprite->setScaleX(size.width / sprite->getContentSize().width);
    sprite->setScaleY(size.height / sprite->getContentSize().height);
    container = CCLayerRGBA::create();
    if (!container) return false;
    container->setContentSize(size);
    container->setAnchorPoint({.5f, .5f});
    container->ignoreAnchorPointForPosition(false);
    container->setPosition(size / 2);
    container->setCascadeOpacityEnabled(true);
    container->setCascadeColorEnabled(true);
    container->addChild(sprite);
    addChild(container);
    return true;
}
void CustomTransitionScene::onEnter() {
    CCTransitionScene::onEnter();
    auto size = CCDirector::get()->getWinSize();
    if (m_stinger) {
        m_stingerSprite = CCSprite::create();
        if (!m_stingerSprite) { complete(); return; }
        m_stinger->apply(m_stingerSprite, 0);
        m_stingerSprite->setPosition(size / 2);
        m_stingerSprite->setScaleX(size.width / m_stinger->width);
        m_stingerSprite->setScaleY(size.height / m_stinger->height);
        addChild(m_stingerSprite, 10);
    } else {
        if (auto* backdrop = CCLayerColor::create({0, 0, 0, 255}, size.width, size.height)) addChild(backdrop, -100);
        m_captured = capture(m_pOutScene, m_fromSurface, m_from) && capture(m_pInScene, m_toSurface, m_to);
        if (!m_captured) {
            if (m_from) m_from->setVisible(false);
            if (m_to) m_to->setVisible(false);
            complete(); return;
        }
        m_to->setOpacity(0);
    }
    update(0.f);
    if (!m_finished) scheduleUpdate();
}
void CustomTransitionScene::draw() {
    if (m_stinger) {
        // Cut is independent of overlay playback length, as in an OBS stinger.
        auto* scene = m_elapsed < m_fDuration * m_cutPoint ? m_pOutScene : m_pInScene;
        if (scene) scene->visit();
    } else if (!m_captured) CCTransitionScene::draw();
}
void CustomTransitionScene::onExit() {
    // Also cancel a native finish callback if navigation interrupted the scene.
    unscheduleAllSelectors();
    CCTransitionScene::onExit();
}
void CustomTransitionScene::update(float dt) {
    if (m_finished) return;
    m_elapsed = std::min(m_fDuration, m_elapsed + bounded(dt, 0.f, 0.f, 30.f));
    if (m_stinger) {
        float sourceTime = m_elapsed / m_fDuration * m_stinger->duration();
        m_stinger->apply(m_stingerSprite, sourceTime);
    } else {
        for (std::size_t i = 0; i < m_timeline.clips.size(); ++i) {
            auto const& clip = m_timeline.clips[i];
            if (m_done[i] || m_elapsed < clip.start) continue;
            float elapsed = m_elapsed - clip.start;
            float progress = std::min(1.f, elapsed / clip.duration);
            apply(i, progress, elapsed);
            m_done[i] = progress >= 1.f;
        }
    }
    if (m_elapsed >= m_fDuration) complete();
}
void CustomTransitionScene::complete() {
    if (m_finished) return;
    m_finished = true;
    unscheduleUpdate();
    // Native finish schedules the director handoff and preserves stack cleanup.
    finish();
}
void CustomTransitionScene::apply(std::size_t i, float t, float elapsed) {
    auto const& command = m_commands[m_timeline.clips[i].command];
    auto* target = command.target == "to" ? m_to : m_from;
    if (command.action == CommandAction::Image) {
        if (!m_overlays[i]) {
            m_overlays[i] = CCSprite::create();
            m_media[i]->apply(m_overlays[i], 0);
            m_overlays[i]->setPosition(CCDirector::get()->getWinSize() / 2);
            addChild(m_overlays[i], 10);
        }
        m_media[i]->apply(m_overlays[i], elapsed);
        m_overlays[i]->setOpacity(static_cast<GLubyte>(255 * t));
        return;
    }
    if (!target) return;
    if (!m_started[i]) { m_origins[i] = target->getPosition(); m_started[i] = true; }
    float ease = t;
    if (command.action == CommandAction::EaseIn) ease = t * t * t;
    if (command.action == CommandAction::EaseOut) ease = 1.f - std::pow(1.f - t, 3.f);
    if (command.action == CommandAction::Bounce) {
        if (t < 1.f / 2.75f) ease = 7.5625f * t * t;
        else if (t < 2.f / 2.75f) { float p = t - 1.5f / 2.75f; ease = 7.5625f * p * p + .75f; }
        else if (t < 2.5f / 2.75f) { float p = t - 2.25f / 2.75f; ease = 7.5625f * p * p + .9375f; }
        else { float p = t - 2.625f / 2.75f; ease = 7.5625f * p * p + .984375f; }
    }
    float value = command.fromVal + (command.toVal - command.fromVal) * ease;
    switch (command.action) {
        case CommandAction::FadeIn:
        case CommandAction::FadeOut:
        case CommandAction::EaseIn:
        case CommandAction::EaseOut:
        case CommandAction::Bounce:
            target->setOpacity(static_cast<GLubyte>(std::clamp(value, 0.f, 255.f))); break;
        case CommandAction::Move:
            target->setPosition({command.fromX + (command.toX - command.fromX) * t,
                command.fromY + (command.toY - command.fromY) * t}); break;
        case CommandAction::Scale: target->setScale(std::clamp(value, .01f, 10.f)); break;
        case CommandAction::Rotate: target->setRotation(value); break;
        case CommandAction::Color:
            target->setColor({static_cast<GLubyte>(command.r), static_cast<GLubyte>(command.g), static_cast<GLubyte>(command.b)}); break;
        case CommandAction::Shake: {
            // Deterministic offsets, zero at both endpoints, independent of rand().
            float amplitude = command.intensity * std::sin(t * 3.14159265f);
            target->setPosition({m_origins[i].x + amplitude * std::sin(elapsed * 91.f),
                m_origins[i].y + amplitude * std::sin(elapsed * 113.f)}); break;
        }
        default: break;
    }
}
