#include "ProgressionToast.hpp"
#include "TierBadgeNode.hpp"
#include "XPBarNode.hpp"
#include "BadgeIconNode.hpp"
#include "../data/ProgressionStats.hpp"
#include "../data/ProgressionTiers.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/FMODAudioEngine.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::progression {

namespace {

constexpr int kToastZOrder = 1000;
constexpr size_t kMaxBadgeCards = 3;
constexpr float kTopMargin = 10.f;
constexpr float kProgressHeight = 68.f;
constexpr float kProgressTallHeight = 92.f;
constexpr float kBadgeHeight = 56.f;

CCLabelBMFont* label(std::string const& text, char const* font, float scale, ccColor3B color) {
    auto* node = CCLabelBMFont::create(text.c_str(), font);
    if (!node) return nullptr;
    node->setScale(scale);
    node->setColor(color);
    return node;
}

void playNativeAchievementSfx() {
    if (auto* audio = FMODAudioEngine::sharedEngine()) {
        audio->playEffect("achievement_01.ogg");
    }
}

void burst(CCNode* parent, CCPoint const& at, ccColor3B color) {
    if (auto* glow = makeRadialGlow(color, 58.f, 0.78f)) {
        glow->setPosition(at);
        glow->setScale(0.15f);
        glow->runAction(CCSpawn::create(
            CCEaseSineOut::create(CCScaleTo::create(0.75f, 1.35f)),
            CCSequence::create(CCFadeTo::create(0.2f, 220), CCFadeTo::create(0.6f, 0), nullptr),
            nullptr
        ));
        glow->runAction(CCSequence::create(
            CCDelayTime::create(0.85f), CCRemoveSelf::create(), nullptr));
        parent->addChild(glow, -1);
    }

    for (int i = 0; i < 8; ++i) {
        auto* spark = paimon::SpriteHelper::safeCreate("paim_progSpark.png"_spr);
        if (!spark) break;
        float const angle = static_cast<float>(i) * 6.2831853f / 8.f;
        spark->setPosition(at);
        spark->setScale(0.32f);
        spark->setColor(color);
        spark->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        spark->runAction(CCSpawn::create(
            CCEaseSineOut::create(CCMoveBy::create(
                0.6f, ccp(std::cos(angle) * 44.f, std::sin(angle) * 44.f))),
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

    auto const win = CCDirector::sharedDirector()->getWinSize();
    float const progH = delta.leveledUp() ? kProgressTallHeight : kProgressHeight;
    float const progTargetY = win.height - kTopMargin - progH * 0.5f;

    if (auto* card = createProgress(delta)) {
        card->setPosition({win.width * 0.5f, win.height + progH * 0.5f + 12.f});
        card->slideDown(0.15f, delta.leveledUp() ? 3.4f : 2.4f, progTargetY);
        scene->addChild(card, kToastZOrder);
    }

    // Stack badge unlock banners cleanly below the XP card
    float stackTopY = win.height - kTopMargin - progH - 8.f;
    size_t const shown = std::min(delta.newBadges.size(), kMaxBadgeCards);
    for (size_t i = 0; i < shown; ++i) {
        auto* card = createBadge(*delta.newBadges[i], ctx);
        if (!card) continue;
        float const targetY = stackTopY - kBadgeHeight * 0.5f;
        float const delay = 1.6f + 0.55f * static_cast<float>(i);
        card->setPosition({win.width * 0.5f, win.height + kBadgeHeight * 0.5f + 12.f});
        card->slideDown(delay, 2.6f, targetY);
        scene->addChild(card, kToastZOrder);
        stackTopY -= (kBadgeHeight + 6.f);
    }

    if (shown > 0) {
        paimon::scheduleMainThreadDelay(1.65f, []() {
            if (!paimon::isRuntimeShuttingDown()) playNativeAchievementSfx();
        });
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

void ProgressionToast::buildCard(float width, float height, ccColor3B accent, bool badgeCard) {
    m_width = width;
    m_height = height;

    this->setContentSize({width, height});
    this->setAnchorPoint({0.5f, 0.5f});

    m_card = CCNode::create();
    m_card->setContentSize({width, height});
    m_card->setPosition({0.f, 0.f});
    this->addChild(m_card);

    // Classic GD beveled AchievementBar window frame (GJ_square01 brown / GJ_square02 blue)
    char const* frameTex = badgeCard ? "GJ_square02.png" : "GJ_square01.png";
    if (auto* panel = geode::NineSlice::create(frameTex)) {
        panel->setScaleMultiplier(0.58f);
        panel->setContentSize({width, height});
        panel->setAnchorPoint({0.f, 0.f});
        panel->setPosition({0.f, 0.f});
        m_card->addChild(panel, -3);
    }

    // Recessed dark well inside the GD border
    if (auto* inset = paimon::ui::makeInset({width - 14.f, height - 14.f}, 115)) {
        inset->setPosition({7.f, 7.f});
        m_card->addChild(inset, -2);
    }

    // Subtle accent bottom trim
    if (auto* div = paimon::ui::makeDivider(width - 28.f, accent, 150)) {
        div->setPosition({width * 0.5f, 8.f});
        m_card->addChild(div, -1);
    }
}

void ProgressionToast::slideDown(float delay, float hold, float targetY) {
    float const startY = this->getPositionY();
    float const cx = this->getPositionX();
    this->runAction(CCSequence::create(
        CCDelayTime::create(delay),
        CCEaseBackOut::create(CCMoveTo::create(0.45f, ccp(cx, targetY))),
        CCDelayTime::create(hold),
        CCEaseBackIn::create(CCMoveTo::create(0.35f, ccp(cx, startY))),
        CCRemoveSelf::create(),
        nullptr
    ));
}

bool ProgressionToast::initProgress(ProgressDelta const& delta) {
    if (!CCNode::init()) return false;

    auto& loc = Localization::get();
    auto const& tier = tierForLevel(delta.toLevel);
    bool const levelUp = delta.leveledUp();

    buildCard(278.f, levelUp ? kProgressTallHeight : kProgressHeight, tier.base, false);

    auto* badge = TierBadgeNode::create(delta.fromLevel, 48.f);
    if (badge) {
        badge->setPosition({40.f, m_height * 0.5f});
        badge->playIntro(0.45f);
        m_card->addChild(badge, 2);
    }

    float const textX = 76.f;
    float const textW = m_width - textX - 16.f;

    auto* headline = label(
        levelUp ? loc.getString("progression.levelup.title")
                : fmt::format(fmt::runtime(loc.getString("progression.level-n")), delta.fromLevel),
        levelUp ? "goldFont.fnt" : "bigFont.fnt",
        levelUp ? 0.48f : 0.36f,
        levelUp ? ccColor3B{255, 255, 255} : tier.accent
    );
    if (headline) {
        headline->setAnchorPoint({0.f, 0.5f});
        headline->limitLabelWidth(textW - 64.f, levelUp ? 0.48f : 0.36f, 0.18f);
        headline->setPosition({textX, m_height - 20.f});
        m_card->addChild(headline, 2);
        if (levelUp) {
            float const targetScale = headline->getScale();
            headline->setScale(0.f);
            headline->runAction(CCSequence::create(
                CCDelayTime::create(0.55f),
                CCEaseElasticOut::create(CCScaleTo::create(0.7f, targetScale), 0.6f),
                nullptr
            ));
        }
    }

    auto* gained = label(
        fmt::format(fmt::runtime(loc.getString("progression.gain")), formatCount(delta.gainedExp)),
        "bigFont.fnt", 0.32f, paimon::ui::palette::success
    );
    if (gained) {
        gained->setAnchorPoint({1.f, 0.5f});
        gained->setPosition({m_width - 14.f, m_height - 24.f});
        gained->setOpacity(0);
        gained->runAction(CCSequence::create(
            CCDelayTime::create(0.5f),
            CCSpawn::create(
                CCFadeTo::create(0.3f, 255),
                CCEaseSineOut::create(CCMoveBy::create(0.3f, ccp(0.f, 4.f))),
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
        bar->setPosition({textX + textW * 0.5f, m_height - 43.f});
        m_card->addChild(bar, 2);

        Ref<TierBadgeNode> badgeRef = badge;
        Ref<CCLabelBMFont> headlineRef = headline;
        // weak: bar is a card child, strong ref back would leak the toast.
        WeakRef<ProgressionToast> weakSelf = this;
        bar->setLevelUpCallback([badgeRef, headlineRef, weakSelf](int reached) mutable {
            if (paimon::isRuntimeShuttingDown()) return;
            playNativeAchievementSfx();
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
            "bigFont.fnt", 0.25f, {210, 222, 244}
        );
        if (subtitle) {
            subtitle->setAnchorPoint({0.f, 0.5f});
            subtitle->limitLabelWidth(textW, 0.25f, 0.16f);
            subtitle->setPosition({textX, 17.f});
            m_card->addChild(subtitle, 2);
        }
    }

    return true;
}

bool ProgressionToast::initBadge(BadgeDef const& badge, BadgeContext const& ctx) {
    if (!CCNode::init()) return false;

    auto& loc = Localization::get();
    auto const accent = rarityColor(badge.rarity);

    buildCard(256.f, kBadgeHeight, accent, true);

    if (auto* icon = BadgeIconNode::create(badge, ctx, 38.f)) {
        icon->setPosition({36.f, m_height * 0.5f});
        icon->playUnlock();
        m_card->addChild(icon, 2);
    }

    if (auto* header = label(loc.getString("progression.new-badge"), "goldFont.fnt", 0.36f, paimon::ui::palette::gold)) {
        header->setAnchorPoint({0.f, 0.5f});
        header->setPosition({64.f, m_height - 18.f});
        m_card->addChild(header, 2);
    }

    if (auto* name = label(badge.name, "bigFont.fnt", 0.36f, {255, 255, 255})) {
        name->setAnchorPoint({0.f, 0.5f});
        name->limitLabelWidth(m_width - 80.f, 0.36f, 0.20f);
        name->setPosition({64.f, 19.f});
        m_card->addChild(name, 2);
    }

    return true;
}

void ProgressionToast::onStartFill() {
    if (!m_pendingBar) return;
    m_pendingBar->animateTo(m_pendingExp, 1.5f);
}

} // namespace paimon::progression
