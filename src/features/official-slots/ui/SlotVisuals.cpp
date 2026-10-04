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
    // tier via updatefeaturestate: hand-mounted coins doubled the glow in the request list.
    auto* face = GJDifficultySprite::create(difficultyFace(difficulty), GJDifficultyName::Short);
    if (!face) return nullptr;

    face->updateFeatureState(featureStateOf(tier));
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
