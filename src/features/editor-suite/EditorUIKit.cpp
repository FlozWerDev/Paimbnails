#include "EditorUIKit.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::editor::uikit {

CCSprite* frameIcon(std::initializer_list<char const*> frames, float scale) {
    CCSprite* spr = nullptr;
    for (auto* f : frames) {
        if (!f) continue;
        spr = CCSprite::createWithSpriteFrameName(f);
        if (spr) break;
    }
    if (!spr) spr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
    if (!spr) spr = CCSprite::create("square02_001.png");
    if (spr) spr->setScale(scale);
    return spr;
}

namespace {

ButtonSprite* fixedTextSprite(char const* label, char const* texture) {
    // Fixed width + fixed height: every toggle/button in a bar is the same size.
    auto* spr = ButtonSprite::create(
        label, static_cast<int>(kToggleWidth), true,
        "bigFont.fnt", texture, kToggleHeight, 0.32f
    );
    if (!spr) spr = ButtonSprite::create(label, "bigFont.fnt", texture, 0.32f);
    return spr;
}

// One visual state of a checkbox row: box sprite + label, whole thing sized.
CCNode* checkboxRowState(char const* label, float width, bool on) {
    auto* root = CCNode::create();
    root->setContentSize({width, kRowHeight});
    root->setAnchorPoint({0.5f, 0.5f});

    auto* box = frameIcon({on ? "GJ_checkOn_001.png" : "GJ_checkOff_001.png"}, 0.45f);
    box->setPosition({10.f, kRowHeight / 2.f});
    root->addChild(box);

    auto* lab = CCLabelBMFont::create(label, "bigFont.fnt");
    lab->setScale(0.3f);
    lab->setAnchorPoint({0.f, 0.5f});
    lab->setPosition({22.f, kRowHeight / 2.f});
    lab->setOpacity(on ? 255 : 165);
    lab->limitLabelWidth(width - 26.f, 0.3f, 0.1f);
    root->addChild(lab);
    return root;
}

} // namespace

CCMenuItemToggler* fixedToggle(char const* label, bool on, std::function<void(bool)> onChange) {
    auto* off = fixedTextSprite(label, "GJ_button_04.png");
    auto* onSpr = fixedTextSprite(label, "GJ_button_01.png");
    if (!off || !onSpr) return nullptr;
    auto* t = CCMenuItemExt::createToggler(
        onSpr, off,
        [cb = std::move(onChange)](CCMenuItemToggler* self) {
            if (cb) cb(!self->isToggled());
        }
    );
    if (t) t->toggle(on);
    return t;
}

CCMenuItemSpriteExtra* fixedButton(
    char const* label, char const* texture, std::function<void()> onClick
) {
    auto* spr = fixedTextSprite(label, texture);
    if (!spr) return nullptr;
    return CCMenuItemExt::createSpriteExtra(
        spr, [cb = std::move(onClick)](CCMenuItemSpriteExtra*) {
            if (cb) cb();
        }
    );
}

CCMenuItemSpriteExtra* fixedSmallButton(
    char const* label, char const* texture, std::function<void()> onClick
) {
    auto* spr = ButtonSprite::create(label, 18, true, "bigFont.fnt", texture, 18.f, 0.5f);
    if (!spr) spr = ButtonSprite::create(label, "bigFont.fnt", texture, 0.5f);
    if (!spr) return nullptr;
    return CCMenuItemExt::createSpriteExtra(
        spr, [cb = std::move(onClick)](CCMenuItemSpriteExtra*) {
            if (cb) cb();
        }
    );
}

CCMenuItemToggler* checkboxRow(
    char const* label, float width, bool on, std::function<void(bool)> onChange
) {
    auto* offState = checkboxRowState(label, width, false);
    auto* onState = checkboxRowState(label, width, true);
    auto* t = CCMenuItemExt::createToggler(
        onState, offState,
        [cb = std::move(onChange)](CCMenuItemToggler* self) {
            if (cb) cb(!self->isToggled());
        }
    );
    if (t) t->toggle(on);
    return t;
}

CCMenuItemSpriteExtra* circleIconButton(
    std::initializer_list<char const*> frames,
    float iconScale,
    CircleBaseColor color,
    std::function<void()> onClick
) {
    auto* icon = frameIcon(frames, 1.f);
    if (!icon) return nullptr;
    // Wrap in a sized container: BasedButtonSprite needs a real contentSize.
    auto* wrap = CCNode::create();
    auto sz = icon->getContentSize();
    if (sz.width < 1.f || sz.height < 1.f) sz = CCSize{20.f, 20.f};
    wrap->setContentSize(sz);
    icon->setPosition(sz / 2.f);
    wrap->addChild(icon);

    auto* base = CircleButtonSprite::create(wrap, color, CircleBaseSize::Small);
    if (!base) return nullptr;
    base->setTopRelativeScale(iconScale);
    return CCMenuItemExt::createSpriteExtra(
        base, [cb = std::move(onClick)](CCMenuItemSpriteExtra*) {
            if (cb) cb();
        }
    );
}

CCScale9Sprite* darkPanel(CCSize size, GLubyte opacity) {
    auto* bg = CCScale9Sprite::create("square02b_001.png");
    if (!bg) bg = CCScale9Sprite::create("square02_001.png");
    if (!bg) return nullptr;
    bg->setContentSize(size);
    bg->setColor({0, 0, 0});
    bg->setOpacity(opacity);
    return bg;
}

CCScale9Sprite* hudPill(CCSize size, GLubyte opacity) {
    return darkPanel(size, opacity);
}

CCLabelBMFont* caption(char const* text, float scale) {
    auto* lab = CCLabelBMFont::create(text, "bigFont.fnt");
    lab->setScale(scale);
    lab->setOpacity(220);
    return lab;
}

CCLabelBMFont* hint(char const* text, float scale) {
    auto* lab = CCLabelBMFont::create(text, "chatFont.fnt");
    lab->setScale(scale);
    lab->setOpacity(170);
    return lab;
}

} // namespace paimon::editor::uikit
