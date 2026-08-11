#include "InfoBlocks.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <algorithm>

using namespace geode::prelude;

namespace paimon::info::blocks {

CCNode* makeBlock(float width, float height, GLubyte opacity) {
    auto node = CCNode::create();
    node->setContentSize({width, height});

    if (auto bg = paimon::SpriteHelper::safeCreateScale9("square02_001.png")) {
        bg->setContentSize({width, height});
        bg->setColor({0, 0, 0});
        bg->setOpacity(opacity);
        bg->setAnchorPoint({0.f, 0.f});
        node->addChild(bg, -1);
    }
    return node;
}

CCLabelBMFont* addText(CCNode* parent, char const* text, char const* font, float scale,
                       ccColor3B color, CCPoint pos, CCPoint anchor, float maxWidth) {
    auto label = CCLabelBMFont::create(text, font);
    if (!label) return nullptr;
    label->setScale(scale);
    label->setColor(color);
    label->setAnchorPoint(anchor);
    label->setPosition(pos);
    if (maxWidth > 0.f) label->limitLabelWidth(maxWidth, scale, scale * 0.42f);
    parent->addChild(label);
    return label;
}

CCSprite* firstFrame(std::vector<char const*> const& candidates) {
    for (auto const* name : candidates) {
        if (auto sprite = paimon::SpriteHelper::safeCreateWithFrameName(name)) return sprite;
    }
    return nullptr;
}

void addBar(CCNode* parent, float x, float y, float width, float height,
            float fill, ccColor3B color) {
    auto track = CCLayerColor::create(ccc4(0, 0, 0, 140));
    track->setContentSize({width, height});
    track->setPosition({x, y});
    parent->addChild(track);

    float filled = width * std::clamp(fill, 0.f, 1.f);
    if (filled < 1.f) return;

    auto bar = CCLayerColor::create(ccc4(color.r, color.g, color.b, 235));
    bar->setContentSize({filled, height});
    bar->setPosition({x, y});
    parent->addChild(bar, 1);
}

CCNode* makeBackdrop(CCSprite* blurred, CCSize const& area, GLubyte darkness, bool fadeIn) {
    if (!blurred) return nullptr;

    // Just enough radius to follow the frame without leaving brown corners.
    auto* stencil = paimon::SpriteHelper::createRoundedRectStencil(area.width, area.height, 5.f);
    auto* clip = CCClippingNode::create();
    if (!stencil || !clip) return nullptr;

    auto size = blurred->getContentSize();
    if (size.width > 0.f && size.height > 0.f) {
        blurred->setScale(std::max(area.width / size.width, area.height / size.height));
    }
    blurred->setAnchorPoint({0.5f, 0.5f});
    blurred->setPosition({area.width / 2.f, area.height / 2.f});
    if (fadeIn) {
        blurred->setOpacity(0);
        blurred->runAction(CCFadeTo::create(0.25f, 255));
    }

    clip->setStencil(stencil);
    clip->setContentSize(area);
    clip->setAnchorPoint({0.5f, 0.5f});
    clip->addChild(blurred);

    auto dark = CCLayerColor::create(ccc4(0, 0, 0, darkness));
    dark->setContentSize(area);
    clip->addChild(dark, 1);
    return clip;
}

} // namespace paimon::info::blocks
