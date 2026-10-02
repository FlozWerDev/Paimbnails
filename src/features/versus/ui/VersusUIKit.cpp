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

    if (auto* bg = paimon::ui::makeSurface(size)) {
        panel->addChild(bg, -1);
    }

    if (caption.empty()) return panel;

    auto* title = CCLabelBMFont::create(caption.c_str(), "bigFont.fnt");
    title->setAnchorPoint({0.f, 0.5f});
    title->setScale(std::min(0.4f, (size.width - 20.f) /
                                   std::max(1.f, title->getContentSize().width)));
    title->setPosition({10.f, size.height - kCaptionH / 2.f - 2.f});
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
    auto* face = paimon::ui::makeButtonFace(label.c_str(), {width, 26.f});
    return CCMenuItemSpriteExtra::create(face, target, callback);
}

void styleTab(CCMenuItemSpriteExtra* tab, bool active) {
    if (!tab) return;
    tab->setColor(active ? ccColor3B{255, 255, 255} : ccColor3B{124, 130, 148});
    tab->setOpacity(active ? 255 : 190);
}

CCMenuItemSpriteExtra* makeAction(std::string const& label, float width, char const* skin,
                                  float scale, CCObject* target, SEL_MenuHandler callback) {
    auto* face = paimon::ui::makeButtonFace(label.c_str(), {width, 30.f},
        paimon::ui::actionColor(skin), scale);
    return CCMenuItemSpriteExtra::create(face, target, callback);
}

CCMenuItemSpriteExtra* makeIconRow(char const* frameName, std::string const& label, float width,
                                   CCObject* target, SEL_MenuHandler callback) {
    float const height = 30.f;

    auto* row = CCNode::create();
    row->setContentSize({width, height});
    row->setAnchorPoint({0.5f, 0.5f});

    if (auto* bg = paimon::ui::makeSurface({width, height})) {
        row->addChild(bg, 0);
    }

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
    return CCLayerColor::create(ccColor4B{255, 255, 255, 38}, width, 1.f);
}

} // namespace paimon::versus::ui
