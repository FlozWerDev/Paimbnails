#include "PaimonUI.hpp"
#include "../core/Settings.hpp"
#include "../utils/SpriteHelper.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace cocos2d;
using namespace geode::prelude;

namespace paimon::ui {

bool motionEnabled() {
    return settings::smoothui::enabled() && !settings::smoothui::reducedMotion();
}

float motionDuration(float seconds) {
    auto speed = settings::smoothui::globalSpeed();
    if (!std::isfinite(speed)) speed = 1.0;
    return seconds / static_cast<float>(std::clamp(speed, 0.35, 2.5));
}

ccColor3B actionColor(char const* skin) {
    if (skin && std::strstr(skin, "06")) return {88, 43, 62};
    if (skin && std::strstr(skin, "01")) return {34, 84, 79};
    if (skin && std::strstr(skin, "02")) return {36, 75, 106};
    return palette::raised;
}

CCNodeRGBA* makeSurface(CCSize size, ccColor3B color, GLubyte opacity, float radius) {
    if (auto* panel = SpriteHelper::safeCreateNineSliceFromFile("square02b_001.png")) {
        panel->setContentSize(size);
        panel->setAnchorPoint({0.f, 0.f});
        panel->setColor(color);
        panel->setOpacity(opacity);
        return panel;
    }
    return SpriteHelper::createRoundedRect(size.width, size.height, radius,
        {color.r / 255.f, color.g / 255.f, color.b / 255.f, opacity / 255.f},
        {palette::border.r / 255.f, palette::border.g / 255.f,
            palette::border.b / 255.f, opacity / 255.f * 0.50f}, 0.65f);
}

CCSprite* makeButtonFace(char const* text, CCSize size, ccColor3B color, float textScale) {
    auto* face = CCSprite::create();
    face->setContentSize(size);
    face->setCascadeOpacityEnabled(true);
    face->setCascadeColorEnabled(true);
    if (auto* panel = makeSurface(size, color, 255, 6.f)) {
        panel->setID("paimon-button-surface"_spr);
        face->addChild(panel, -1);
    }
    auto* label = CCLabelBMFont::create(text ? text : "", "bigFont.fnt");
    label->setColor(palette::text);
    label->limitLabelWidth(std::max(1.f, size.width - 16.f), textScale, 0.12f);
    label->setPosition(size / 2.f);
    label->setID("paimon-button-label"_spr);
    face->addChild(label);
    return face;
}

CCMenuItemSpriteExtra* makeButton(char const* text, CCSize size,
    std::function<void()> onPress, ccColor3B color) {
    auto* button = CCMenuItemExt::createSpriteExtra(makeButtonFace(text, size, color),
        [callback = std::move(onPress)](CCMenuItemSpriteExtra*) {
            if (callback) callback();
        });
    button->m_scaleMultiplier = 1.035f;
    return button;
}

namespace {
CCSprite* switchFace(bool on) {
    auto* face = CCSprite::create();
    face->setContentSize({42.f, 24.f});
    face->setCascadeOpacityEnabled(true);
    auto* track = makeSurface({38.f, 20.f}, on ? ccColor3B{37, 104, 102} : palette::raised,
        255, 10.f);
    track->setPosition({2.f, 2.f});
    face->addChild(track);
    auto* thumb = SpriteHelper::createColorPanel(14.f, 14.f,
        on ? palette::success : palette::muted, 255, 7.f);
    thumb->setPosition({on ? 23.f : 5.f, 5.f});
    face->addChild(thumb);
    auto* state = CCLabelBMFont::create(on ? "I" : "O", "chatFont.fnt");
    state->setScale(0.28f);
    state->setColor(on ? palette::success : palette::muted);
    state->setPosition({on ? 12.f : 30.f, 12.f});
    face->addChild(state);
    return face;
}
}

CCMenuItemToggler* makeSwitch(CCObject* target, SEL_MenuHandler callback, bool value, float scale) {
    auto* toggle = CCMenuItemToggler::create(switchFace(false), switchFace(true), target, callback);
    toggle->setScale(scale);
    toggle->toggle(value);
    return toggle;
}

void animateIn(CCNode* node, float delay, float distance) {
    if (!node || !motionEnabled()) return;
    constexpr int tag = 0x5041494e;
    auto const destination = node->getPosition();
    node->stopActionByTag(tag);
    node->setPosition(destination - CCPoint{0.f, distance});
    auto* action = CCSequence::create(CCDelayTime::create(motionDuration(std::min(delay, 0.12f))),
        CCEaseSineOut::create(CCMoveTo::create(motionDuration(0.20f), destination)), nullptr);
    action->setTag(tag);
    node->runAction(action);
}

void decorateScene(CCNode* parent) {
    auto size = CCDirector::get()->getWinSize();
    auto* bg = CCLayerGradient::create({11, 16, 29, 255}, {24, 36, 57, 255});
    bg->setContentSize(size);
    bg->setVector({0.6f, -1.f});
    bg->setID("paimon-scene-background"_spr);
    parent->addChild(bg, -10);
    auto* line = CCLayerColor::create({116, 204, 255, 65}, size.width - 32.f, 1.f);
    line->setPosition({16.f, size.height - 42.f});
    parent->addChild(line, -9);
}

}
