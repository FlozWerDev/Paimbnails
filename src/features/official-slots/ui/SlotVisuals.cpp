#include "SlotVisuals.hpp"

#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/Localization.hpp"

#include <Geode/binding/GJDifficultySprite.hpp>
#include <Geode/binding/GameLevelManager.hpp>

#include <fmt/format.h>

#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::officialslots::ui {

namespace {

GJFeatureState featureStateOf(Tier tier) {
    switch (tier) {
        case Tier::Featured:  return GJFeatureState::Featured;
        case Tier::Epic:      return GJFeatureState::Epic;
        case Tier::Legendary: return GJFeatureState::Legendary;
        case Tier::Mythic:    return GJFeatureState::Mythic;
        case Tier::None:      return GJFeatureState::None;
    }
    return GJFeatureState::None;
}

} // namespace

std::string officialName(int officialId) {
    if (auto* glm = GameLevelManager::get()) {
        if (auto* main = glm->getMainLevel(officialId, true)) {
            std::string name = main->m_levelName.c_str();
            if (!name.empty()) return name;
        }
    }
    return fmt::format("#{}", officialId);
}

std::string slotDisplayName(Slot const& slot) {
    if (!slot.name.empty()) return slot.name;
    if (slot.source == Source::LevelId && slot.levelId > 0) {
        return fmt::format("Level #{}", slot.levelId);
    }
    return Localization::get().getString("slot.level.unnamed");
}

CCNode* createDifficultyBadge(Difficulty difficulty, Tier tier, float scale) {
    // the difficulty_* frames include the name beneath the face; diffIcon_* are face-only.
    auto const frameName = difficulty == Difficulty::Auto ? std::string("diffIcon_auto_btn_001.png")
        : fmt::format("diffIcon_{:02d}_btn_001.png", difficultyFace(difficulty));
    auto* frame = CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(frameName.c_str());
    if (!frame || frame->isUsingFallback()) return nullptr;
    auto* face = GJDifficultySprite::create(difficultyFace(difficulty), GJDifficultyName::Short);
    if (!face) return nullptr;
    float const labeledHeight = face->getContentHeight();
    face->setDisplayFrame(frame);
    face->updateFeatureState(featureStateOf(tier));
    // native flame textures include the space reserved for the difficulty name.
    // align their opening with the bare face after removing that space.
    float const offset = (face->getContentHeight() - labeledHeight) / 2.f;
    if (auto* children = face->getChildren()) {
        for (auto* effect : CCArrayExt<CCNode*>(children)) {
            effect->setPositionY(effect->getPositionY() + offset);
        }
    }
    face->setScale(scale);
    return face;
}

CCNode* createStarBadge(int stars, float scale) {
    auto* icon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png");
    if (!icon) return nullptr;

    auto* node = CCNode::create();
    auto* label = CCLabelBMFont::create(fmt::format("{}", stars).c_str(), "bigFont.fnt");
    label->setScale(scale * 0.5f);

    icon->setScale(scale * 0.8f);

    constexpr float kGap = 3.f;
    float const labelW = label->getScaledContentSize().width;
    float const iconW = icon->getScaledContentSize().width;
    float const height = std::max(label->getScaledContentSize().height,
                                 icon->getScaledContentSize().height);

    node->setContentSize({labelW + kGap + iconW, height});
    node->setAnchorPoint({0.5f, 0.5f});

    label->setAnchorPoint({0.f, 0.5f});
    label->setPosition({0.f, height / 2.f});
    node->addChild(label);

    icon->setPosition({labelW + kGap + iconW / 2.f, height / 2.f});
    node->addChild(icon);
    return node;
}

void updatePageStars(CCLabelBMFont* label, CCSprite* icon, int stars) {
    if (label) {
        float const right = label->boundingBox().getMaxX();
        label->setString(fmt::format("{}", stars).c_str());
        label->setPositionX(label->getPositionX() + right - label->boundingBox().getMaxX());
        label->setVisible(true);
    }
    if (icon) icon->setVisible(true);
}

void updatePageOrbs(CCLabelBMFont* label, CCSprite* icon, int normalPercent) {
    constexpr int kDecorativeOrbTotal = 500;
    int const shown = std::clamp(normalPercent, 0, 100) * kDecorativeOrbTotal / 100;
    if (label) {
        auto const nativeBounds = label->boundingBox();
        label->setString(fmt::format("{}/{}", shown, kDecorativeOrbTotal).c_str());
        // LevelPage anchors the text on the card's left, then places the orb
        // icon after its width. Preserve that inset and the native icon gap.
        label->setPositionX(label->getPositionX() + nativeBounds.getMinX() - label->boundingBox().getMinX());
        if (icon) {
            icon->setPositionX(icon->getPositionX() + label->boundingBox().getMaxX() - nativeBounds.getMaxX());
        }
        label->setVisible(true);
    }
    if (icon) icon->setVisible(true);
}

CCNode* createCoinRow(float scale) {
    auto* node = CCNode::create();

    constexpr float kGap = 2.f;
    float width = 0.f;
    float height = 0.f;

    for (int i = 0; i < 3; i++) {
        auto* coin = paimon::SpriteHelper::safeCreateWithFrameName("secretCoin_001.png");
        if (!coin) continue;

        coin->setScale(scale);
        auto const size = coin->getScaledContentSize();
        coin->setPosition({width + size.width / 2.f, size.height / 2.f});
        node->addChild(coin);

        width += size.width + (i < 2 ? kGap : 0.f);
        height = std::max(height, size.height);
    }

    if (width <= 0.f) return nullptr;

    node->setContentSize({width, height});
    node->setAnchorPoint({0.5f, 0.5f});
    return node;
}

CCNode* createCardBackground(CCSize size) {
    auto* panel = paimon::SpriteHelper::createRoundedRect(
        size.width, size.height, 6.f, ccc4f(0.f, 0.f, 0.f, 0.42f),
        ccc4f(1.f, 1.f, 1.f, 0.14f), 0.75f
    );
    if (!panel) return nullptr;

    panel->setContentSize(size);
    return panel;
}

} // namespace paimon::officialslots::ui
