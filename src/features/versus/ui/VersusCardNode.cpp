#include "VersusCardNode.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::versus {

namespace {

// The plate art is 0.66 wide by 0.92 tall inside its square canvas.
constexpr float kAspect = 1.36f;

void fitWidth(CCSprite* sprite, float width) {
    sprite->setScale(width / std::max(1.f, sprite->getContentSize().width));
}

} // namespace

VersusCardNode* VersusCardNode::create(CardId id, float width) {
    auto ret = new VersusCardNode();
    if (ret && ret->init(id, width, false)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool VersusCardNode::init(CardId id, float width, bool faceDown) {
    if (!CCNode::init()) return false;

    m_card = id;
    m_width = width;
    m_faceDown = faceDown;
    this->setContentSize({width, width * kAspect});
    this->setAnchorPoint({0.5f, 0.5f});

    m_content = CCNode::create();
    m_content->setPosition({width / 2.f, width * kAspect / 2.f});
    this->addChild(m_content);

    rebuild();
    return true;
}

void VersusCardNode::setCard(CardId id) {
    if (id == m_card && !m_faceDown) return;
    m_card = id;
    m_faceDown = false;
    rebuild();
}

void VersusCardNode::rebuild() {
    m_content->removeAllChildren();

    // The plate art lives in a square canvas, so it is scaled by that canvas
    // width and not by the visible card width.
    float const canvas = m_width / 0.66f;

    if (m_faceDown) {
        if (auto* back = paimon::SpriteHelper::safeCreate(cardBackSprite().c_str())) {
            fitWidth(back, canvas);
            back->setColor({120, 130, 165});
            m_content->addChild(back, 0);
        }
        return;
    }

    auto const& def = cardAt(m_card);

    if (auto* plate = paimon::SpriteHelper::safeCreate(cardPlateSprite().c_str())) {
        fitWidth(plate, canvas);
        plate->setColor(rarityBody(def.rarity));
        m_content->addChild(plate, 0);
    }

    if (auto* ring = paimon::SpriteHelper::safeCreate(cardRingSprite().c_str())) {
        fitWidth(ring, canvas);
        ring->setColor(rarityRim(def.rarity));
        m_content->addChild(ring, 1);
    }

    if (auto* glyph = paimon::SpriteHelper::safeCreate(cardGlyphSprite(def).c_str())) {
        glyph->setScale(m_width * 0.52f / std::max(1.f, glyph->getContentSize().width));
        glyph->setPositionY(m_width * 0.20f);
        m_content->addChild(glyph, 2);
    }

    auto* name = CCLabelBMFont::create(def.name, "bigFont.fnt");
    name->setScale(std::min(m_width * 0.0075f, m_width * 0.80f / std::max(1.f, name->getContentSize().width)));
    name->setPositionY(-m_width * 0.50f);
    m_content->addChild(name, 3);
}

void VersusCardNode::playDraw(float delay) {
    m_content->setScale(0.f);
    m_content->setRotation(-18.f);
    m_content->runAction(CCSequence::create(
        CCDelayTime::create(delay),
        CCSpawn::create(
            CCEaseBackOut::create(CCScaleTo::create(0.28f, 1.f)),
            CCEaseSineOut::create(CCRotateTo::create(0.28f, 0.f)),
            nullptr),
        nullptr));
}

} // namespace paimon::versus
