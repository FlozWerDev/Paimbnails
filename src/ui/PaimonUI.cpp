#include "PaimonUI.hpp"
#include "../core/Settings.hpp"
#include "../utils/InfoButton.hpp"

#include <Geode/ui/General.hpp>
#include <algorithm>
#include <cmath>
#include <string_view>

using namespace cocos2d;
using namespace geode::prelude;

namespace paimon::ui {

char const* buttonTexture(Btn skin) {
    switch (skin) {
        case Btn::Green: return "GJ_button_01.png";
        case Btn::Cyan:  return "GJ_button_02.png";
        case Btn::Pink:  return "GJ_button_03.png";
        case Btn::Gray:  return "GJ_button_04.png";
        case Btn::Blue:  return "GJ_button_05.png";
        case Btn::Red:   return "GJ_button_06.png";
    }
    return "GJ_button_01.png";
}

char const* popupTexture(Bg bg) {
    switch (bg) {
        case Bg::Brown:  return "GJ_square01.png";
        case Bg::Blue:   return "GJ_square02.png";
        case Bg::Green:  return "GJ_square03.png";
        case Bg::Purple: return "GJ_square04.png";
        case Bg::Dark:   return "GJ_square05.png";
        case Bg::Light:  return "GJ_square06.png";
        case Bg::White:  return "GJ_square07.png";
    }
    return "GJ_square01.png";
}

bool motionEnabled() {
    return settings::smoothui::enabled() && !settings::smoothui::reducedMotion();
}

float motionDuration(float seconds) {
    auto speed = settings::smoothui::globalSpeed();
    if (!std::isfinite(speed)) speed = 1.0;
    return seconds / static_cast<float>(std::clamp(speed, 0.35, 2.5));
}

NineSlice* makeInset(CCSize size, GLubyte opacity, ccColor3B color) {
    // half-size corners keep short rows from squashing the rounded edge
    auto* panel = NineSlice::create("square02b_001.png");
    panel->setScaleMultiplier(0.5f);
    panel->setContentSize(size);
    panel->setAnchorPoint({0.f, 0.f});
    panel->setColor(color);
    panel->setOpacity(opacity);
    return panel;
}

namespace {

// bigFont glyphs read much heavier than goldFont at the same scale.
float buttonLabelScale(char const* font) {
    return std::string_view(font) == "bigFont.fnt" ? 0.65f : 0.8f;
}

void fitButtonLabel(ButtonSprite* sprite) {
    if (!sprite || !sprite->m_label) return;
    float const inner = std::max(8.f, sprite->getContentSize().width - 14.f);
    sprite->m_label->limitLabelWidth(inner, sprite->m_scale, 0.1f);
}

}

ButtonSprite* makeButtonSprite(char const* text, char const* texture, float width, float scale, char const* font) {
    scale = std::max(0.1f, scale);
    if (!texture || texture[0] == '\0') texture = "GJ_button_01.png";
    if (!font || font[0] == '\0') font = "goldFont.fnt";
    char const* caption = text ? text : "";
    float const labelScale = buttonLabelScale(font);
    ButtonSprite* sprite = nullptr;
    if (width > 0.f) {
        int const target = std::max(20, static_cast<int>(std::round(width / scale)));
        sprite = ButtonSprite::create(caption, target, true, font, texture, 30.f, labelScale);
        // the frame adds padding around m_width, so rows of fixed-width buttons overlapped.
        int const pad = sprite ? static_cast<int>(std::round(sprite->getContentSize().width)) - target : 0;
        if (pad > 0 && target - pad >= 16) {
            sprite = ButtonSprite::create(caption, target - pad, true, font, texture, 30.f, labelScale);
        }
        fitButtonLabel(sprite);
    } else {
        sprite = ButtonSprite::create(caption, font, texture, labelScale);
    }
    if (sprite) sprite->setScale(scale);
    return sprite;
}

ButtonSprite* makeButtonSprite(char const* text, Btn skin, float width, float scale, char const* font) {
    return makeButtonSprite(text, buttonTexture(skin), width, scale, font);
}

CCMenuItemSpriteExtra* makeButton(char const* text, std::function<void()> onPress,
    char const* texture, float width, float scale, char const* font) {
    return CCMenuItemExt::createSpriteExtra(makeButtonSprite(text, texture, width, scale, font),
        [callback = std::move(onPress)](CCMenuItemSpriteExtra*) {
            if (callback) callback();
        });
}

CCMenuItemSpriteExtra* makeButton(char const* text, std::function<void()> onPress,
    Btn skin, float width, float scale, char const* font) {
    return makeButton(text, std::move(onPress), buttonTexture(skin), width, scale, font);
}

void setButtonSkin(CCMenuItemSpriteExtra* button, Btn skin) {
    if (!button) return;
    if (auto* sprite = typeinfo_cast<ButtonSprite*>(button->getNormalImage())) {
        sprite->updateBGImage(buttonTexture(skin));
        // updateBGImage relayouts the label, which can undo the fit from makeButtonSprite
        if (sprite->m_absolute) fitButtonLabel(sprite);
    }
}

void matchButtonLabels(std::vector<CCMenuItemSpriteExtra*> const& buttons) {
    std::vector<CCLabelBMFont*> labels;
    float smallest = 0.f;
    for (auto* button : buttons) {
        auto* sprite = button ? typeinfo_cast<ButtonSprite*>(button->getNormalImage()) : nullptr;
        if (!sprite || !sprite->m_label) continue;
        float const scale = sprite->m_label->getScale();
        smallest = labels.empty() ? scale : std::min(smallest, scale);
        labels.push_back(sprite->m_label);
    }
    for (auto* label : labels) label->setScale(smallest);
}

CircleButtonSprite* makeCircleSprite(char const* frame, CircleBaseColor color,
    CircleBaseSize size, float topScale) {
    return CircleButtonSprite::createWithSpriteFrameName(frame, topScale, color, size);
}

CCMenuItemSpriteExtra* makeCircleButton(char const* frame, std::function<void()> onPress,
    CircleBaseColor color, CircleBaseSize size, float topScale) {
    return CCMenuItemExt::createSpriteExtra(makeCircleSprite(frame, color, size, topScale),
        [callback = std::move(onPress)](CCMenuItemSpriteExtra*) {
            if (callback) callback();
        });
}

CCMenuItemSpriteExtra* makeFrameButton(char const* frame, float scale, std::function<void()> onPress) {
    return CCMenuItemExt::createSpriteExtraWithFrameName(frame, scale,
        [callback = std::move(onPress)](CCMenuItemSpriteExtra*) {
            if (callback) callback();
        });
}

CCMenuItemToggler* makeSwitch(CCObject* target, SEL_MenuHandler callback, bool value, float scale) {
    auto* toggle = CCMenuItemToggler::createWithStandardSprites(target, callback, scale);
    toggle->toggle(value);
    return toggle;
}

CCMenuItemToggler* makeToggle(bool value, std::function<void(bool)> onChange, float scale) {
    // the callback fires before the toggler flips its state
    auto* toggle = CCMenuItemExt::createTogglerWithStandardSprites(scale,
        [callback = std::move(onChange)](CCMenuItemToggler* item) {
            if (callback) callback(!item->isToggled());
        });
    toggle->toggle(value);
    return toggle;
}

CCMenuItemSpriteExtra* makeInfoButton(std::string const& title, std::string const& body, float scale) {
    return PaimonInfo::createInfoBtn(title, body, nullptr, scale);
}

CCLabelBMFont* makeTitle(char const* text, float maxWidth, float scale) {
    auto* label = CCLabelBMFont::create(text ? text : "", "goldFont.fnt");
    label->limitLabelWidth(std::max(1.f, maxWidth), scale, 0.1f);
    return label;
}

CCLabelBMFont* makeLabel(char const* text, float maxWidth, float scale, ccColor3B color) {
    auto* label = CCLabelBMFont::create(text ? text : "", "bigFont.fnt");
    label->limitLabelWidth(std::max(1.f, maxWidth), scale, 0.1f);
    label->setColor(color);
    return label;
}

CCLabelBMFont* makeText(char const* text, float wrapWidth, float scale, ccColor3B color,
    CCTextAlignment align) {
    auto* label = CCLabelBMFont::create(text ? text : "", "chatFont.fnt",
        std::max(1.f, wrapWidth) / scale, align);
    label->setScale(scale);
    label->setColor(color);
    return label;
}

CCSprite* makeDivider(float width, ccColor3B color, GLubyte opacity) {
    auto* line = CCSprite::createWithSpriteFrameName("floorLine_001.png");
    line->setScaleX(std::max(1.f, width) / std::max(1.f, line->getContentSize().width));
    line->setScaleY(0.6f);
    line->setColor(color);
    line->setOpacity(opacity);
    return line;
}

CCNode* makePanel(CCSize size, char const* title, GLubyte opacity) {
    auto* panel = CCNode::create();
    panel->setAnchorPoint({0.f, 0.f});
    panel->setContentSize(size);
    panel->addChild(makeInset(size, opacity), -1);
    if (title && title[0] != '\0') {
        auto* heading = makeTitle(title, size.width - 20.f, 0.5f);
        heading->setAnchorPoint({0.f, 0.5f});
        heading->setPosition({10.f, size.height - 12.f});
        panel->addChild(heading);
        auto* line = makeDivider(size.width - 16.f, palette::gold, 90);
        line->setPosition({size.width / 2.f, size.height - kPanelHeader + 1.f});
        panel->addChild(line);
    }
    return panel;
}

void addCorners(CCNode* to, CCSize size, SideArtStyle style, float scale, bool top) {
    if (!to) return;
    char const* frame = "dailyLevelCorner_001.png";
    switch (style) {
        case SideArtStyle::PopupBlue: frame = "rewardCorner_001.png"; break;
        case SideArtStyle::Layer:     frame = "GJ_sideArt_001.png"; break;
        case SideArtStyle::LayerGray: frame = "gauntletCorner_001.png"; break;
        default: break;
    }
    constexpr float kPad = 3.f;
    struct Spot { bool right; bool upper; };
    for (auto spot : {Spot{false, false}, Spot{true, false}, Spot{false, true}, Spot{true, true}}) {
        if (spot.upper && !top) continue;
        auto* corner = CCSprite::createWithSpriteFrameName(frame);
        corner->setScale(scale);
        corner->setFlipX(spot.right);
        corner->setFlipY(spot.upper);
        corner->setAnchorPoint({spot.right ? 1.f : 0.f, spot.upper ? 1.f : 0.f});
        corner->setPosition({spot.right ? size.width - kPad : kPad, spot.upper ? size.height - kPad : kPad});
        corner->setID(std::string("paimon-corner"_spr) + (spot.upper ? "-top" : "-bottom")
            + (spot.right ? "-right" : "-left"));
        to->addChild(corner);
    }
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

CCSprite* decorateScene(CCNode* parent, ccColor3B tint, bool sideArt) {
    auto* bg = geode::createLayerBG();
    bg->setColor(tint);
    bg->setID("paimon-scene-background"_spr);
    parent->addChild(bg, -10);
    if (sideArt) {
        auto const win = CCDirector::get()->getWinSize();
        for (bool right : {false, true}) {
            auto* art = CCSprite::createWithSpriteFrameName("GJ_sideArt_001.png");
            art->setFlipX(right);
            art->setAnchorPoint({right ? 1.f : 0.f, 0.f});
            art->setPosition({right ? win.width : 0.f, 0.f});
            art->setID(right ? "paimon-side-art-right"_spr : "paimon-side-art-left"_spr);
            parent->addChild(art, -9);
        }
    }
    return bg;
}

CCLabelBMFont* addSceneTitle(CCNode* parent, char const* text, float maxWidth) {
    auto const win = CCDirector::get()->getWinSize();
    auto* title = makeTitle(text, maxWidth, 0.8f);
    title->setPosition({win.width / 2.f, win.height - 22.f});
    title->setID("paimon-scene-title"_spr);
    parent->addChild(title, 5);
    return title;
}

CCMenuItemSpriteExtra* makeBackButton(std::function<void()> onPress) {
    auto* button = makeFrameButton("GJ_arrow_01_001.png", 1.f, std::move(onPress));
    button->setID("back-button"_spr);
    return button;
}

}
