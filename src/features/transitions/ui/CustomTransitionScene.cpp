#include "CustomTransitionScene.hpp"
#include "TransitionObjectVisual.hpp"
#include "../../../features/cursor/services/CursorManager.hpp"
#include <algorithm>
#include <cmath>
using namespace geode::prelude;
using namespace paimon::transitions;

bool CustomTransitionScene::isActive() {
    auto* director = CCDirector::get();
    return typeinfo_cast<CustomTransitionScene*>(director->getRunningScene()) ||
        typeinfo_cast<CustomTransitionScene*>(director->getNextScene());
}
CustomTransitionScene* CustomTransitionScene::create(CCScene* from, CCScene* to,
    std::vector<TransitionCommand> const& commands, bool, float cutPoint, float minimumDuration) {
    // ccdirector owns push/replace/pop; never touch its scene pointers.
    if (!from || from == to || from != CCDirector::get()->getRunningScene()) return nullptr;
    auto* result = new CustomTransitionScene();
    result->m_sceneCut = cutPoint;
    if (result->initialize(to, commands, minimumDuration)) { result->autorelease(); return result; }
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
bool CustomTransitionScene::initialize(CCScene* to, std::vector<TransitionCommand> commands, float minimumDuration) {
    if (!to || commands.empty()) return false;
    TransitionManager::sanitizeCommands(commands);
    m_commands = std::move(commands);
    m_evaluator.configure(m_commands, minimumDuration);
    m_timeline = m_evaluator.timeline();
    if (m_timeline.clips.empty() || m_timeline.duration > 30.f) return false;
    m_objBase.assign(m_timeline.clips.size(), 0.f);
    m_clipOfCommand.assign(m_commands.size(), -1);
    m_overlays.resize(m_timeline.clips.size());
    m_media.resize(m_timeline.clips.size());
    for (std::size_t i = 0; i < m_timeline.clips.size(); ++i) {
        auto const& command = m_commands[m_timeline.clips[i].command];
        m_clipOfCommand[m_timeline.clips[i].command] = static_cast<int>(i);
        if (command.action == CommandAction::Image && !command.solidObject) {
            m_media[i] = findTransitionMedia(command.imagePath);
            if (!m_media[i]) { prepareTransitionMedia(command.imagePath); return false; }
        }
    }
    return initWithDuration(m_timeline.duration, to);
}
bool CustomTransitionScene::capture(CCScene* scene, Ref<CCRenderTexture>& surface, CCLayerRGBA*& container) {
    auto size = CCDirector::get()->getWinSize();
    surface = CCRenderTexture::create(static_cast<int>(size.width), static_cast<int>(size.height));
    if (!surface || !surface->getSprite()) return false;
    // scenes stay where gd put them; only their pixels enter here, so
    // commands can't touch gameplay.
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
        m_wash = CCLayerColor::create({0, 0, 0, 0}, size.width, size.height);
        addChild(m_wash, 9);
        // objects exist (hidden) from the start so triggers can animate them
        // before/after their own clip plays.
        for (std::size_t c = 0; c < m_commands.size(); ++c)
            if (m_commands[c].action == CommandAction::Image) objectSprite((int)c);
    }
    update(0.f);
    if (!m_finished) scheduleUpdate();
}
void CustomTransitionScene::draw() {
    if (m_stinger) {
        // cut ignores overlay length, like an obs stinger.
        auto* scene = m_elapsed < m_fDuration * m_cutPoint ? m_pOutScene : m_pInScene;
        if (scene) scene->visit();
    } else if (!m_captured) CCTransitionScene::draw();
}
void CustomTransitionScene::onExit() {
    // navigation may interrupt the scene; cancel the native finish callback too.
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
        applyFrame();
    }
    if (m_elapsed >= m_fDuration) complete();
}
void CustomTransitionScene::complete() {
    if (m_finished) return;
    m_finished = true;
    unscheduleUpdate();
    // native finish keeps the director handoff and stack cleanup.
    finish();
}
CCSprite* CustomTransitionScene::objectSprite(int commandIndex) {
    if (commandIndex < 0 || commandIndex >= static_cast<int>(m_clipOfCommand.size())) return nullptr;
    int clip = m_clipOfCommand[commandIndex];
    if (clip < 0) return nullptr;
    auto const& cmd = m_commands[commandIndex];
    if (!cmd.solidObject && !m_media[clip]) return nullptr;
    if (!m_overlays[clip]) {
        auto size = CCDirector::get()->getWinSize();
        auto* sprite = cmd.solidObject ? createSolidTransitionSprite() : CCSprite::create();
        if (!sprite) return nullptr;
        if (cmd.solidObject) {
            sprite->setTextureRect({0.f, 0.f, size.width, size.height});
            m_objBase[clip] = cmd.objScale;
        } else {
            m_media[clip]->apply(sprite, 0);
            m_objBase[clip] = std::max(size.width / std::max(1, m_media[clip]->width),
                size.height / std::max(1, m_media[clip]->height)) * cmd.objScale;
        }
        sprite->setVisible(false);
        addChild(sprite, 10 + commandIndex);
        m_overlays[clip] = sprite;
    }
    return m_overlays[clip];
}

void CustomTransitionScene::applyFrame() {
    auto size = CCDirector::get()->getWinSize();
#if defined(GEODE_IS_MOBILE)
    auto ptr = CursorManager::get().pointerPos();
    auto mouse = (ptr.x >= 0.f && ptr.y >= 0.f) ? ptr : ccp(size.width * 0.5f, size.height * 0.5f);
#else
    auto mouse = geode::cocos::getMousePos();
#endif
    auto frame = m_evaluator.sample(m_elapsed, {size.width, size.height},
        {std::clamp(mouse.x, 0.f, size.width), std::clamp(mouse.y, 0.f, size.height)}, m_sceneCut);
    applyTransitionVisual(m_from, frame.sceneA);
    applyTransitionVisual(m_to, frame.sceneB);
    m_wash->setColor({static_cast<GLubyte>(frame.wash.r), static_cast<GLubyte>(frame.wash.g), static_cast<GLubyte>(frame.wash.b)});
    m_wash->setOpacity(static_cast<GLubyte>(std::clamp(frame.wash.opacity, 0.f, 255.f)));
    for (std::size_t i = 0; i < m_timeline.clips.size(); ++i) {
        auto commandIndex = m_timeline.clips[i].command;
        auto const& command = m_commands[commandIndex];
        if (command.action != CommandAction::Image) continue;
        auto* sprite = objectSprite(static_cast<int>(commandIndex));
        if (!sprite) continue;
        auto const& visual = frame.objects[commandIndex];
        if (m_media[i] && visual.visible) {
            float time = visual.mediaElapsed;
            if (command.stretchMedia) time *= m_media[i]->duration() / m_timeline.clips[i].duration;
            m_media[i]->apply(sprite, time);
        }
        applyTransitionVisual(sprite, visual, m_objBase[i]);
    }
}
