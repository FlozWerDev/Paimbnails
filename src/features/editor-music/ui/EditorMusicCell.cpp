#include "EditorMusicCell.hpp"
#include "../services/EditorMusicPlayer.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/ui/Layout.hpp>
#include <Geode/utils/cocos.hpp>

using namespace geode::prelude;

namespace paimon::editormusic {

EditorMusicCell* EditorMusicCell::create() {
    auto ret = new EditorMusicCell();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool EditorMusicCell::init() {
    if (!CCNode::init()) return false;

    const float w = 232.f, h = 66.f;
    this->setContentSize({w, h});
    this->setAnchorPoint({0.f, 1.f});

    if (auto bg = CCScale9Sprite::create("GJ_square02.png")) {
        bg->setContentSize({w, h});
        bg->setAnchorPoint({0.f, 0.f});
        bg->setPosition({0.f, 0.f});
        bg->setOpacity(225);
        this->addChild(bg, -1);
    }

    m_trackLabel = CCLabelBMFont::create("Editor Music", "bigFont.fnt");
    m_trackLabel->setAnchorPoint({0.f, 0.5f});
    m_trackLabel->setPosition({10.f, h - 12.f});
    m_trackLabel->limitLabelWidth(w - 20.f, 0.45f, 0.1f);
    this->addChild(m_trackLabel);

    m_modeLabel = CCLabelBMFont::create("Library", "chatFont.fnt");
    m_modeLabel->setAnchorPoint({0.f, 0.5f});
    m_modeLabel->setScale(0.6f);
    m_modeLabel->setColor({180, 180, 180});
    m_modeLabel->setPosition({10.f, h - 28.f});
    this->addChild(m_modeLabel);

    auto menu = CCMenu::create();
    menu->setContentSize({w - 16.f, 26.f});
    menu->setPosition({w / 2.f, 15.f});

    auto iconBtn = [&](const char* frame, bool flipX, SEL_MenuHandler sel) -> CCMenuItemSpriteExtra* {
        CCSprite* spr = CCSprite::createWithSpriteFrameName(frame);
        if (!spr) { spr = CCSprite::create(); if (spr) spr->setContentSize({18.f, 18.f}); }
        if (!spr) return nullptr;
        spr->setFlipX(flipX);
        spr->setScale(0.6f);
        return CCMenuItemSpriteExtra::create(spr, this, sel);
    };
    auto textBtn = [&](const char* txt, SEL_MenuHandler sel) -> CCMenuItemSpriteExtra* {
        auto spr = ButtonSprite::create(txt, "bigFont.fnt", "GJ_button_05.png", 0.7f);
        if (!spr) return nullptr;
        spr->setScale(0.5f);
        return CCMenuItemSpriteExtra::create(spr, this, sel);
    };

    if (auto b = iconBtn("GJ_arrow_02_001.png", false, menu_selector(EditorMusicCell::onPrev)))
        menu->addChild(b);

    m_playSprite = CCSprite::createWithSpriteFrameName("GJ_playBtn2_001.png");
    if (!m_playSprite) { m_playSprite = CCSprite::create(); m_playSprite->setContentSize({18.f, 18.f}); }
    m_playSprite->setScale(0.7f);
    if (auto b = CCMenuItemSpriteExtra::create(m_playSprite, this, menu_selector(EditorMusicCell::onPlayPause)))
        menu->addChild(b);

    if (auto b = iconBtn("GJ_arrow_02_001.png", true, menu_selector(EditorMusicCell::onNext)))
        menu->addChild(b);
    if (auto b = textBtn("Mode", menu_selector(EditorMusicCell::onMode)))
        menu->addChild(b);
    if (auto b = textBtn("List", menu_selector(EditorMusicCell::onPlaylist)))
        menu->addChild(b);

    menu->setLayout(RowLayout::create()
        ->setGap(8.f)
        ->setAxisAlignment(AxisAlignment::Center)
        ->setDefaultScaleLimits(0.4f, 1.0f));
    menu->updateLayout();
    this->addChild(menu);

    this->schedule(schedule_selector(EditorMusicCell::tickMonitor), 0.3f);
    refresh();
    return true;
}

void EditorMusicCell::onExit() {
    this->unschedule(schedule_selector(EditorMusicCell::tickMonitor));
    CCNode::onExit();
}

void EditorMusicCell::refresh() {
    auto& p = EditorMusicPlayer::get();
    if (m_trackLabel) {
        m_trackLabel->setString(p.currentDisplayName().c_str());
        m_trackLabel->limitLabelWidth(232.f - 20.f, 0.45f, 0.1f);
    }
    if (m_modeLabel) m_modeLabel->setString(p.modeLabel().c_str());
    if (m_playSprite) {
        const char* f = p.isPlaying() ? "GJ_pauseBtn_001.png" : "GJ_playBtn2_001.png";
        if (auto* frame = CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(f))
            m_playSprite->setDisplayFrame(frame);
    }
}

void EditorMusicCell::animateIn() {
    auto target = this->getPosition();
    this->setScale(0.6f);
    this->setPosition(target + ccp(-24.f, 0.f));
    this->stopAllActions();
    this->runAction(CCEaseBackOut::create(CCScaleTo::create(0.32f, 1.f)));
    this->runAction(CCEaseOut::create(CCMoveTo::create(0.32f, target), 2.f));
}

void EditorMusicCell::animateOut() {
    this->stopAllActions();
    this->runAction(CCEaseBackIn::create(CCScaleTo::create(0.26f, 0.55f)));
    this->runAction(CCSequence::create(
        CCEaseIn::create(CCMoveBy::create(0.26f, ccp(-24.f, 0.f)), 2.f),
        CallFuncExt::create([this]() {
            EditorMusicPlayer::get().stop();
            this->removeFromParent();
        }),
        nullptr));
}

void EditorMusicCell::tickMonitor(float) {
    bool mainPlaying = false;
    if (auto* fmod = FMODAudioEngine::sharedEngine(); fmod && fmod->m_backgroundMusicChannel) {
        fmod->m_backgroundMusicChannel->isPlaying(&mainPlaying);
    }
    EditorMusicPlayer::get().setMainChannelActive(mainPlaying);
}

void EditorMusicCell::onPrev(CCObject*) { EditorMusicPlayer::get().playPrevious(); refresh(); }
void EditorMusicCell::onPlayPause(CCObject*) { EditorMusicPlayer::get().togglePause(); refresh(); }
void EditorMusicCell::onNext(CCObject*) { EditorMusicPlayer::get().playNext(); refresh(); }

void EditorMusicCell::onMode(CCObject*) {
    auto& p = EditorMusicPlayer::get();
    p.cycleMode();
    p.playNext();
    refresh();
}

void EditorMusicCell::onPlaylist(CCObject*) {
    auto& p = EditorMusicPlayer::get();
    p.cyclePlaylist();
    p.playNext();
    refresh();
}

} // namespace paimon::editormusic
