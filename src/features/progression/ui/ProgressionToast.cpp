#include "ProgressionToast.hpp"
#include "GDProgressBar.hpp"
#include "TierBadgeNode.hpp"
#include "XPBarNode.hpp"
#include "BadgeIconNode.hpp"
#include "../data/ProgressionStats.hpp"
#include "../data/ProgressionTiers.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::progression {

namespace {

constexpr int kToastZOrder = 1000;
constexpr size_t kMaxBadgeCards = 3;
constexpr float kMargin = 10.f;
constexpr float kProgressHeight = 64.f;
constexpr float kProgressTallHeight = 92.f;
constexpr float kBadgeHeight = 50.f;

CCLabelBMFont* label(std::string const& text, char const* font, float scale, ccColor3B color) {
    auto* node = CCLabelBMFont::create(text.c_str(), font);
    if (!node) return nullptr;
    node->setScale(scale);
    node->setColor(color);
    return node;
}

void burst(CCNode* parent, CCPoint const& at, ccColor3B color) {
    if (auto* glow = makeRadialGlow(color, 60.f, 0.75f)) {
        glow->setPosition(at);
        glow->setScale(0.15f);
        glow->runAction(CCSpawn::create(
            CCEaseSineOut::create(CCScaleTo::create(0.75f, 1.4f)),
            CCSequence::create(CCFadeTo::create(0.2f, 220), CCFadeTo::create(0.6f, 0), nullptr),
            nullptr
        ));
        glow->runAction(CCSequence::create(
            CCDelayTime::create(0.85f), CCRemoveSelf::create(), nullptr));
        parent->addChild(glow, -1);
    }

    for (int i = 0; i < 9; ++i) {
        auto* spark = paimon::SpriteHelper::safeCreate("paim_progSpark.png"_spr);
        if (!spark) break;
        float const angle = static_cast<float>(i) * 6.2831853f / 9.f;
        spark->setPosition(at);
        spark->setScale(0.32f);
        spark->setColor(color);
        spark->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        spark->runAction(CCSpawn::create(
            CCEaseSineOut::create(CCMoveBy::create(
                0.6f, ccp(std::cos(angle) * 46.f, std::sin(angle) * 46.f))),
            CCScaleTo::create(0.6f, 0.04f),
            CCFadeTo::create(0.6f, 0),
            nullptr
        ));
        spark->runAction(CCSequence::create(
            CCDelayTime::create(0.65f), CCRemoveSelf::create(), nullptr));
        parent->addChild(spark, 3);
    }
}

} // namespace

void ProgressionToast::present(ProgressDelta const& delta, BadgeContext const& ctx) {
    auto* scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return;

    if (auto* card = createProgress(delta)) {
        scene->addChild(card, kToastZOrder);
    }

    // Stack above the progress card so a level-up never gets covered.
    float stackY = kMargin + (delta.leveledUp() ? kProgressTallHeight : kProgressHeight) + 8.f;
    size_t const shown = std::min(delta.newBadges.size(), kMaxBadgeCards);
    for (size_t i = 0; i < shown; ++i) {
        auto* card = createBadge(*delta.newBadges[i], ctx);
        if (!card) continue;
        card->setPositionY(stackY + kBadgeHeight / 2.f);
        card->slideIn(1.6f + 0.55f * static_cast<float>(i), 2.6f);
        scene->addChild(card, kToastZOrder);
        stackY += kBadgeHeight + 6.f;
    }
}

ProgressionToast* ProgressionToast::createProgress(ProgressDelta const& delta) {
    auto ret = new ProgressionToast();
    if (ret && ret->initProgress(delta)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

ProgressionToast* ProgressionToast::createBadge(BadgeDef const& badge, BadgeContext const& ctx) {
    auto ret = new ProgressionToast();
    if (ret && ret->initBadge(badge, ctx)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void ProgressionToast::buildCard(float width, float height, ccColor3B accent) {
    m_width = width;
    m_height = height;

    this->setContentSize({width, height});
    this->setAnchorPoint({0.5f, 0.5f});

    this->setPosition({-width, kMargin + height / 2.f});

    m_card = CCNode::create();
    m_card->setContentSize({width, height});
    m_card->setPosition({-width / 2.f, -height / 2.f});
    this->addChild(m_card);

    if (auto* panel = paimon::SpriteHelper::safeCreateScale9("GJ_square05.png", CCRectMake(12.f, 12.f, 56.f, 56.f))) {
        panel->setContentSize({width, height});
        panel->setAnchorPoint({0.f, 0.f});
        panel->setColor({22, 22, 28});
        panel->setOpacity(240);
        m_card->addChild(panel, -2);
    }
    // Accent underline, the only decoration that survives at this size.
    if (auto* strip = GDProgressBar::makeCapsule()) {
        strip->setContentSize({width - 16.f, 20.f});
        strip->setScaleY(0.34f);
        strip->setAnchorPoint({0.f, 0.5f});
        strip->setColor(accent);
        strip->setPosition({8.f, 5.f});
        m_card->addChild(strip, -1);
    }
}

void ProgressionToast::slideIn(float delay, float hold) {
    float const targetX = kMargin + m_width / 2.f;
    this->runAction(CCSequence::create(
        CCDelayTime::create(delay),
        CCEaseBackOut::create(CCMoveTo::create(0.45f, ccp(targetX, this->getPositionY()))),
        CCDelayTime::create(hold),
        CCEaseBackIn::create(CCMoveTo::create(0.35f, ccp(-m_width, this->getPositionY()))),
        CCRemoveSelf::create(),
        nullptr
    ));
}

bool ProgressionToast::initProgress(ProgressDelta const& delta) {
    if (!CCNode::init()) return false;

    auto& loc = Localization::get();
    auto const& tier = tierForLevel(delta.toLevel);
    bool const levelUp = delta.leveledUp();

    buildCard(252.f, levelUp ? kProgressTallHeight : kProgressHeight, tier.base);

    auto* badge = TierBadgeNode::create(delta.fromLevel, 52.f);
    if (badge) {
        badge->setPosition({40.f, m_height / 2.f});
        badge->playIntro(0.45f);
        m_card->addChild(badge, 2);
    }

    float const textX = 74.f;
    float const textW = m_width - textX - 12.f;

    auto* headline = label(
        levelUp ? loc.getString("progression.levelup.title")
                : fmt::format(fmt::runtime(loc.getString("progression.level-n")), delta.fromLevel),
        levelUp ? "goldFont.fnt" : "bigFont.fnt",
        levelUp ? 0.52f : 0.42f,
        levelUp ? ccColor3B{255, 255, 255} : tier.accent
    );
    if (headline) {
        headline->setAnchorPoint({0.f, 0.5f});
        headline->setPosition({textX, m_height - 18.f});
        m_card->addChild(headline, 2);
        if (levelUp) {
            headline->setScale(0.f);
            headline->runAction(CCSequence::create(
                CCDelayTime::create(0.55f),
                CCEaseElasticOut::create(CCScaleTo::create(0.7f, 0.52f), 0.6f),
                nullptr
            ));
        }
    }

    auto* gained = label(
        fmt::format(fmt::runtime(loc.getString("progression.gain")), formatCount(delta.gainedExp)),
        "bigFont.fnt", 0.38f, {150, 245, 170}
    );
    if (gained) {
        gained->setAnchorPoint({1.f, 0.5f});
        gained->setPosition({m_width - 12.f, m_height - 18.f});
        gained->setOpacity(0);
        gained->runAction(CCSequence::create(
            CCDelayTime::create(0.5f),
            CCSpawn::create(
                CCFadeTo::create(0.3f, 255),
                CCEaseSineOut::create(CCMoveBy::create(0.3f, ccp(0.f, 6.f))),
                nullptr
            ),
            nullptr
        ));
        m_card->addChild(gained, 2);
    }

    auto* bar = XPBarNode::create(textW, 14.f);
    if (bar) {
        bar->setTier(tier);
        bar->setExp(delta.totalExp - delta.gainedExp);
        bar->setPosition({textX + textW / 2.f, m_height - 40.f});
        m_card->addChild(bar, 2);

        Ref<TierBadgeNode> badgeRef = badge;
        Ref<CCLabelBMFont> headlineRef = headline;
        // Weak: bar is a card child, strong ref back would leak the toast.
        WeakRef<ProgressionToast> weakSelf = this;
        bar->setLevelUpCallback([badgeRef, headlineRef, weakSelf](int reached) mutable {
            if (paimon::isRuntimeShuttingDown()) return;
            auto self = weakSelf.lock();
            if (badgeRef) {
                badgeRef->setLevel(reached);
                badgeRef->playLevelUp();
                if (self && self->m_card) {
                    burst(self->m_card, badgeRef->getPosition(), tierForLevel(reached).accent);
                }
            }
            if (headlineRef) {
                headlineRef->setString(
                    Localization::get().getString("progression.levelup.title").c_str());
            }
        });

        // Fill starts once the card settles: delayed call, not straight after create.
        m_pendingBar = bar;
        m_pendingExp = delta.totalExp;
        this->runAction(CCSequence::create(
            CCDelayTime::create(0.62f),
            CCCallFunc::create(this, callfunc_selector(ProgressionToast::onStartFill)),
            nullptr
        ));
    }

    if (levelUp) {
        auto* subtitle = label(
            fmt::format(fmt::runtime(loc.getString("progression.level-n")), delta.toLevel)
                + (delta.tierChanged() ? std::string("  -  ") + tier.name : std::string()),
            "chatFont.fnt", 0.38f, {200, 212, 235}
        );
        if (subtitle) {
            subtitle->setAnchorPoint({0.f, 0.5f});
            subtitle->setPosition({textX, 14.f});
            m_card->addChild(subtitle, 2);
        }
    }

    slideIn(0.15f, levelUp ? 3.4f : 2.4f);
    return true;
}

bool ProgressionToast::initBadge(BadgeDef const& badge, BadgeContext const& ctx) {
    if (!CCNode::init()) return false;

    auto& loc = Localization::get();
    auto const accent = rarityColor(badge.rarity);

    buildCard(232.f, kBadgeHeight, accent);

    if (auto* icon = BadgeIconNode::create(badge, ctx, 36.f)) {
        icon->setPosition({34.f, m_height / 2.f});
        icon->playUnlock();
        m_card->addChild(icon, 2);
    }

    if (auto* header = label(loc.getString("progression.new-badge"), "chatFont.fnt", 0.36f, accent)) {
        header->setAnchorPoint({0.f, 0.5f});
        header->setPosition({60.f, m_height - 15.f});
        m_card->addChild(header, 2);
    }

    if (auto* name = label(badge.name, "bigFont.fnt", 0.44f, {255, 255, 255})) {
        name->setAnchorPoint({0.f, 0.5f});
        name->limitLabelWidth(m_width - 72.f, 0.44f, 0.22f);
        name->setPosition({60.f, 17.f});
        m_card->addChild(name, 2);
    }

    return true;
}

void ProgressionToast::onStartFill() {
    if (!m_pendingBar) return;
    m_pendingBar->animateTo(m_pendingExp, 1.5f);
}

} // namespace paimon::progression
