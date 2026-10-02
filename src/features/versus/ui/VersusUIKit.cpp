#include "VersusUIKit.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::versus::ui {

CCNode* makePanel(CCSize size, std::string const& caption) {
    auto* panel = CCNode::create();
    panel->setContentSize(size);
    panel->setAnchorPoint({0.5f, 0.5f});

    panel->addChild(paimon::ui::makeInset(size, 85), -1);

    if (caption.empty()) return panel;

    auto* title = paimon::ui::makeTitle(caption.c_str(), size.width - 20.f, 0.5f);
    title->setAnchorPoint({0.f, 0.5f});
    title->setPosition({10.f, size.height - kCaptionH / 2.f - 1.f});
    panel->addChild(title, 1);

    if (auto* rule = makeDivider(size.width - 16.f)) {
        rule->setPosition({8.f, size.height - kCaptionH});
        panel->addChild(rule, 1);
    }
    return panel;
}

CCRect panelBody(CCSize size) {
    return {6.f, 6.f, size.width - 12.f, size.height - kCaptionH - 10.f};
}

CCLabelBMFont* makeText(std::string const& text, char const* font, float scale,
                        CCPoint const& pos) {
    auto* label = CCLabelBMFont::create(text.c_str(), font);
    label->setScale(scale);
    label->setPosition(pos);
    return label;
}

CCMenuItemSpriteExtra* makeTab(std::string const& label, float width, CCObject* target,
                               SEL_MenuHandler callback) {
    auto* face = paimon::ui::makeButtonSprite(label.c_str(), paimon::ui::Btn::Gray, width, 0.8f);
    return CCMenuItemSpriteExtra::create(face, target, callback);
}

void styleTab(CCMenuItemSpriteExtra* tab, bool active) {
    paimon::ui::setButtonSkin(tab, active ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
}

CCMenuItemSpriteExtra* makeAction(std::string const& label, float width, char const* skin,
                                  float scale, CCObject* target, SEL_MenuHandler callback) {
    auto* face = paimon::ui::makeButtonSprite(label.c_str(), skin, width, std::clamp(scale / 0.6f, 0.6f, 1.f));
    return CCMenuItemSpriteExtra::create(face, target, callback);
}

CCMenuItemSpriteExtra* makeIconRow(char const* frameName, std::string const& label, float width,
                                   CCObject* target, SEL_MenuHandler callback) {
    float const height = 30.f;

    auto* row = CCNode::create();
    row->setContentSize({width, height});
    row->setAnchorPoint({0.5f, 0.5f});

    row->addChild(paimon::ui::makeInset({width, height}, 80), 0);

    float textX = 10.f;
    auto* icon = paimon::SpriteHelper::safeCreateWithFrameName(frameName);
    if (!icon) icon = paimon::SpriteHelper::safeCreate(frameName);
    if (icon) {
        icon->setScale(20.f / std::max(1.f, icon->getContentSize().height));
        icon->setPosition({19.f, height / 2.f});
        row->addChild(icon, 1);
        textX = 33.f;
    }

    auto* text = CCLabelBMFont::create(label.c_str(), "bigFont.fnt");
    text->setAnchorPoint({0.f, 0.5f});
    text->setScale(std::min(0.38f, (width - textX - 8.f) /
                                   std::max(1.f, text->getContentSize().width)));
    text->setPosition({textX, height / 2.f});
    row->addChild(text, 1);

    return CCMenuItemSpriteExtra::create(row, target, callback);
}

CCNode* makeDivider(float width) {
    auto* line = paimon::ui::makeDivider(width, paimon::ui::palette::gold, 110);
    line->setAnchorPoint({0.f, 0.5f});
    return line;
}

} // namespace paimon::versus::ui
