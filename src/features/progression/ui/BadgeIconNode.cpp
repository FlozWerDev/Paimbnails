#include "BadgeIconNode.hpp"
#include "TierBadgeNode.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::progression {

namespace {

// silhouette carries rarity, face carries category.
char const* plateFile(BadgeRarity rarity) {
    switch (rarity) {
        case BadgeRarity::Common:    return "paim_progPlate1.png"_spr;
        case BadgeRarity::Uncommon:  return "paim_progPlate2.png"_spr;
        case BadgeRarity::Rare:      return "paim_progPlate3.png"_spr;
        case BadgeRarity::Epic:      return "paim_progPlate4.png"_spr;
        case BadgeRarity::Legendary: return "paim_progPlate5.png"_spr;
        case BadgeRarity::Mythic:    return "paim_progPlate6.png"_spr;
    }
    return "paim_progPlate1.png"_spr;
}

// below this the goal is unreadable, so the tile drops it.
constexpr float kGoalMinSize = 40.f;

void fitSquare(CCSprite* sprite, float size) {
    float const source = std::max(sprite->getContentSize().width, sprite->getContentSize().height);
    sprite->setScale(size / std::max(1.f, source));
}

} // namespace

BadgeIconNode* BadgeIconNode::create(BadgeDef const& badge, BadgeContext const& ctx, float size) {
    auto ret = new BadgeIconNode();
    if (ret && ret->init(badge, ctx, size)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool BadgeIconNode::init(BadgeDef const& badge, BadgeContext const& ctx, float size) {
    if (!CCNode::init()) return false;

    m_badge = &badge;
    m_size = size;
    m_unlocked = isUnlocked(badge, ctx);
    this->setContentSize({size, size});
    this->setAnchorPoint({0.5f, 0.5f});

    m_content = CCNode::create();
    m_content->setPosition({size / 2.f, size / 2.f});
    this->addChild(m_content);

    auto const accent = rarityColor(badge.rarity);
    auto const family = categoryColor(badge.category);
    bool const goal = size >= kGoalMinSize;

    if (m_unlocked && badge.rarity >= BadgeRarity::Epic) {
        if (auto* glow = makeRadialGlow(accent, size * 0.82f, 0.42f)) {
            float const base = glow->getScale();
            glow->runAction(CCRepeatForever::create(CCSequence::create(
                CCEaseSineInOut::create(CCScaleTo::create(1.7f, base * 1.14f)),
                CCEaseSineInOut::create(CCScaleTo::create(1.7f, base)),
                nullptr
            )));
            m_content->addChild(glow, -1);
        }
    }

    if (auto* face = paimon::SpriteHelper::safeCreate("paim_progPlateFace.png"_spr)) {
        fitSquare(face, size);
        face->setColor(m_unlocked ? family : ccColor3B{58, 62, 74});
        m_content->addChild(face, 0);
    }

    if (auto* plate = paimon::SpriteHelper::safeCreate(plateFile(badge.rarity))) {
        fitSquare(plate, size);
        plate->setColor(m_unlocked ? accent : ccColor3B{124, 130, 146});
        m_content->addChild(plate, 1);
    }

    if (auto* glyph = paimon::SpriteHelper::safeCreateWithFrameName(badge.glyph)) {
        float const target = size * (goal ? 0.30f : 0.38f);
        float const source = std::max(glyph->getContentSize().width, glyph->getContentSize().height);
        glyph->setScale(target / std::max(1.f, source));
        glyph->setPosition({0.f, goal ? size * 0.05f : 0.f});
        if (!m_unlocked) {
            glyph->setColor({48, 52, 64});
            glyph->setOpacity(225);
        }
        m_content->addChild(glyph, 2);
    }

    if (goal) {
        if (auto* label = CCLabelBMFont::create(badgeShortGoal(badge).c_str(), "bigFont.fnt")) {
            float const height = size * 0.13f;
            label->limitLabelWidth(
                size * 0.44f, height / std::max(1.f, label->getContentSize().height), 0.05f);
            label->setPosition({0.f, -size * 0.175f});
            label->setColor(m_unlocked ? family : ccColor3B{118, 124, 140});
            m_content->addChild(label, 3);
        }
    }

    if (!m_unlocked) {
        if (auto* lock = paimon::SpriteHelper::safeCreateWithFrameName("GJ_lock_001.png")) {
            lock->setScale(size * 0.28f / std::max(1.f, lock->getContentSize().height));
            lock->setPosition({size * 0.29f, size * 0.30f});
            m_content->addChild(lock, 4);
        }
    }

    return true;
}

void BadgeIconNode::playIntro(float delay) {
    m_content->setScale(0.f);
    m_content->runAction(CCSequence::create(
        CCDelayTime::create(delay),
        CCEaseBackOut::create(CCScaleTo::create(0.30f, 1.f)),
        nullptr
    ));
}

void BadgeIconNode::playUnlock() {
    auto const accent = rarityColor(m_badge->rarity);

    m_content->stopAllActions();
    m_content->setScale(0.3f);
    m_content->runAction(CCEaseElasticOut::create(CCScaleTo::create(0.85f, 1.f), 0.6f));

    if (auto* wave = paimon::SpriteHelper::safeCreate(plateFile(m_badge->rarity))) {
        fitSquare(wave, m_size);
        wave->setColor(accent);
        wave->setOpacity(150);
        wave->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        float const from = wave->getScale();
        wave->runAction(CCSpawn::create(
            CCEaseSineOut::create(CCScaleTo::create(0.65f, from * 2.1f)),
            CCFadeTo::create(0.65f, 0),
            nullptr
        ));
        wave->runAction(CCSequence::create(
            CCDelayTime::create(0.7f),
            CCRemoveSelf::create(),
            nullptr
        ));
        m_content->addChild(wave, -2);
    }
}

} // namespace paimon::progression
