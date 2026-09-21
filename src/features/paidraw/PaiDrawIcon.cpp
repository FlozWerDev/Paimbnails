#include "PaiDrawIcon.hpp"

#include "../../utils/SpriteHelper.hpp"
#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace paidraw {

// GD's own paint frames so the icon matches vanilla art; each step degrades gracefully.
cocos2d::CCNode* createPaiDrawIcon(float targetSize) {
    auto* container = cocos2d::CCNode::create();
    container->setContentSize({targetSize, targetSize});
    container->setAnchorPoint({0.5f, 0.5f});

    if (auto* halo = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png")) {
        float maxDim = std::max(halo->getContentSize().width, halo->getContentSize().height);
        if (maxDim > 0.f) halo->setScale(targetSize / maxDim);
        halo->setOpacity(160);
        halo->setPosition({targetSize / 2.f, targetSize / 2.f});
        container->addChild(halo, 0);
    }

    if (auto* paint = paimon::SpriteHelper::safeCreateWithFrameName("GJ_paintBtn_001.png")) {
        float maxDim = std::max(paint->getContentSize().width, paint->getContentSize().height);
        if (maxDim > 0.f) paint->setScale((targetSize * 0.85f) / maxDim);
        paint->setPosition({targetSize / 2.f, targetSize / 2.f});
        container->addChild(paint, 1);
    } else if (auto* fallback = paimon::SpriteHelper::safeCreateWithFrameName("GJ_colorBtn_001.png")) {
        float maxDim = std::max(fallback->getContentSize().width, fallback->getContentSize().height);
        if (maxDim > 0.f) fallback->setScale((targetSize * 0.85f) / maxDim);
        fallback->setPosition({targetSize / 2.f, targetSize / 2.f});
        container->addChild(fallback, 1);
    } else {
        auto* label = cocos2d::CCLabelBMFont::create("P", "goldFont.fnt");
        label->setScale(targetSize / std::max(label->getContentSize().height, 1.f) * 0.7f);
        label->setPosition({targetSize / 2.f, targetSize / 2.f});
        container->addChild(label, 1);
    }
    return container;
}

} // namespace paidraw
