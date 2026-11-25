#include "ProgressionPopup.hpp"
#include "BadgeDetailPopup.hpp"
#include "BadgeIconNode.hpp"
#include "GDProgressBar.hpp"
#include "TierBadgeNode.hpp"
#include "XPBarNode.hpp"
#include "../data/ProgressionStats.hpp"
#include "../data/ProgressionTiers.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/ScissorClipNode.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::progression {

namespace {

constexpr float kPopupW = 448.f;
constexpr float kPopupH = 302.f;
constexpr float kPageW = 420.f;
constexpr float kPageH = 208.f;
constexpr float kPageX = 14.f;
constexpr float kPageY = 14.f;

constexpr int kTabCount = 3;
constexpr int kGridColumns = 7;
constexpr float kTileSize = 46.f;
constexpr float kTileGap = 8.f;

constexpr ccColor3B kIdleLabel = {178, 186, 206};
constexpr ccColor3B kMutedText = {182, 194, 218};

ccColor3B sourceColor(ExpSource source) {
    switch (source) {
        case ExpSource::Stars:         return {255, 220,  90};
        case ExpSource::Moons:         return {150, 190, 255};
        case ExpSource::Diamonds:      return {110, 230, 240};
        case ExpSource::UserCoins:     return {214, 218, 228};
        case ExpSource::SecretCoins:   return {255, 195,  80};
        case ExpSource::Demons:        return {235,  90,  90};
        case ExpSource::CreatorPoints: return {130, 240, 150};
        case ExpSource::Mastery:       return {190, 140, 255};
        case ExpSource::Versus:        return {255, 130, 200};
    }
    return {255, 255, 255};
}

CCLabelBMFont* makeLabel(std::string const& text, char const* font, float scale, ccColor3B color) {
    auto* label = CCLabelBMFont::create(text.c_str(), font);
    if (!label) return nullptr;
    label->setScale(scale);
    label->setColor(color);
    return label;
}

CCNode* makeTab(float width, float height, CCNode* content) {
    auto* tab = CCNode::create();
    tab->setContentSize({width, height});

    if (auto* offSkin = paimon::SpriteHelper::safeCreateScale9WithFrameName("GJ_tabOff_001.png")) {
        offSkin->setContentSize({width, height});
        offSkin->setAnchorPoint({0.f, 0.f});
        offSkin->setID("tab-off"_spr);
        tab->addChild(offSkin, 0);
    }
    if (auto* onSkin = paimon::SpriteHelper::safeCreateScale9WithFrameName("GJ_tabOn_001.png")) {
        onSkin->setContentSize({width, height});
        onSkin->setAnchorPoint({0.f, 0.f});
        onSkin->setVisible(false);
        onSkin->setID("tab-on"_spr);
        tab->addChild(onSkin, 0);
    }
    if (content) {
        content->setID("tab-label"_spr);
        content->setPosition({width / 2.f, height / 2.f + 1.f});
        tab->addChild(content, 1);
    }
    return tab;
}

} // namespace

ProgressionPopup* ProgressionPopup::create(BadgeContext const& ctx, std::string const& username) {
    auto ret = new ProgressionPopup();
    if (ret && ret->init(ctx, username)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ProgressionPopup::init(BadgeContext const& ctx, std::string const& username) {
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;

    m_ctx = ctx;

    auto& loc = Localization::get();
    auto const& tier = tierForLevel(ctx.level);

    this->addCorners(SideArtStyle::PopupGold, 0.70f);
    this->setTitle(username.empty() ? loc.getString("progression.title") : username, "goldFont.fnt", 0.72f, 18.f);
    if (m_title) m_title->setColor(tier.accent);

    using namespace exp_values;
    auto const infoBody = fmt::format(
        fmt::runtime(loc.getString("progression.info.body")),
        kStar, kMoon, kDiamond, kUserCoin, kSecretCoin,
        kDemonEasy, kDemonExtreme, kCreatorPoint
    );
    this->addInfoButton(loc.getString("progression.info.title"), infoBody);

    buildTabRow();

    if (auto* line = paimon::ui::makeDivider(kPageW, paimon::ui::palette::gold, 115)) {
        line->setPosition({kPopupW / 2.f, kPopupH - 61.f});
        m_mainLayer->addChild(line, 1);
    }

    if (auto* stencil = paimon::SpriteHelper::createRectStencil(kPageW, kPageH)) {
        if (auto* clip = paimon::ScissorClipNode::create(stencil)) {
            clip->setAlphaThreshold(0.05f);
            clip->setContentSize({kPageW, kPageH});
            clip->setPosition({kPageX, kPageY});
            m_pageHolder = clip;
            m_mainLayer->addChild(clip, 1);
        }
    }

    showTab(0, false);
    paimon::markDynamicPopup(this);
    return true;
}

void ProgressionPopup::buildTabRow() {
    auto& loc = Localization::get();

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setID("progression-tabs"_spr);
    m_mainLayer->addChild(menu, 2);

    char const* keys[kTabCount] = {
        "progression.tab.overview",
        "progression.tab.sources",
        "progression.tab.badges",
    };

    constexpr float tabW = 118.f;
    constexpr float tabH = 26.f;
    constexpr float gap = 8.f;
    float const totalW = tabW * kTabCount + gap * (kTabCount - 1);
    float const startX = (kPopupW - totalW) / 2.f + tabW / 2.f;

    for (int i = 0; i < kTabCount; ++i) {
        auto* label = makeLabel(loc.getString(keys[i]).c_str(), "bigFont.fnt", 0.38f, {255, 255, 255});
        if (label) label->limitLabelWidth(tabW - 18.f, 0.38f, 0.20f);
        auto* tab = makeTab(tabW, tabH, label);
        auto* btn = CCMenuItemSpriteExtra::create(tab, this, menu_selector(ProgressionPopup::onTab));
        if (!btn) continue;
        btn->setTag(i);
        btn->setPosition({startX + (tabW + gap) * i, kPopupH - 47.f});
        menu->addChild(btn);
        m_tabs.push_back(tab);
    }
}

void ProgressionPopup::onTab(CCObject* sender) {
    int const index = sender ? sender->getTag() : 0;
    if (index == m_tab) return;
    showTab(index, true);
}

void ProgressionPopup::showTab(int index, bool animated) {
    if (!m_pageHolder) return;

    int const previous = m_tab;
    m_tab = std::clamp(index, 0, kTabCount - 1);
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        bool const active = (i == m_tab);
        if (auto* off = m_tabs[i]->getChildByID("tab-off"_spr)) off->setVisible(!active);
        if (auto* on  = m_tabs[i]->getChildByID("tab-on"_spr))  on->setVisible(active);
        if (auto* lbl = typeinfo_cast<CCRGBAProtocol*>(m_tabs[i]->getChildByID("tab-label"_spr))) {
            lbl->setColor(active ? ccColor3B{255, 255, 255} : kIdleLabel);
        }
        if (auto* parent = m_tabs[i]->getParent()) {
            parent->setPositionY(kPopupH - (active ? 45.f : 47.f));
            if (auto* menu = parent->getParent()) {
                menu->reorderChild(parent, active ? 2 : 0);
            }
        }
    }

    CCNode* page = nullptr;
    switch (m_tab) {
        case 1: page = buildSources(); break;
        case 2: page = buildBadges(); break;
        default: page = buildOverview(); break;
    }
    if (!page) return;

    if (m_page && animated) {
        float const exit = (m_tab > previous) ? -kPageW : kPageW;
        m_page->runAction(CCSequence::create(
            CCEaseSineIn::create(CCMoveTo::create(0.18f, ccp(exit, 0.f))),
            CCRemoveSelf::create(),
            nullptr
        ));
    } else if (m_page) {
        m_page->removeFromParent();
    }

    m_page = page;
    m_pageHolder->addChild(page);

    if (animated) {
        page->setPositionX((m_tab > previous) ? kPageW : -kPageW);
        page->runAction(CCEaseBackOut::create(CCMoveTo::create(0.30f, ccp(0.f, 0.f))));
    }
}

CCNode* ProgressionPopup::buildOverview() {
    auto& loc = Localization::get();
    auto const& tier = tierForLevel(m_ctx.level);

    auto* page = CCNode::create();
    page->setContentSize({kPageW, kPageH});

    // Top Hero Panel
    constexpr float heroH = 96.f;
    auto* hero = paimon::ui::makeInset({kPageW, heroH}, 105);
    if (hero) {
        hero->setPosition({0.f, kPageH - heroH});
        page->addChild(hero, 0);
    }

    constexpr float kColX = 104.f;
    constexpr float kColRight = kPageW - 12.f;

    if (auto* badge = TierBadgeNode::create(m_ctx.level, 66.f)) {
        badge->setProgress(levelProgress(m_ctx.exp));
        badge->setPosition({50.f, kPageH - heroH * 0.5f});
        badge->playIntro(0.04f);
        page->addChild(badge, 1);
    }

    if (auto* name = makeLabel(tier.name, "goldFont.fnt", 0.48f, {255, 255, 255})) {
        name->setAnchorPoint({0.f, 0.5f});
        name->limitLabelWidth(175.f, 0.48f, 0.24f);
        name->setPosition({kColX, kPageH - 17.f});
        page->addChild(name, 1);
    }

    if (auto* tierIndex = makeLabel(
            fmt::format("{} {} / {}", loc.getString("progression.tier"),
                        tier.index + 1, kTierCount),
            "bigFont.fnt", 0.27f, kMutedText)) {
        tierIndex->setAnchorPoint({1.f, 0.5f});
        tierIndex->setPosition({kColRight, kPageH - 17.f});
        page->addChild(tierIndex, 1);
    }

    if (auto* levelWord = makeLabel(loc.getString("progression.level"),
                                    "goldFont.fnt", 0.36f, paimon::ui::palette::gold)) {
        levelWord->setAnchorPoint({0.f, 0.5f});
        levelWord->setPosition({kColX, kPageH - 42.f});
        page->addChild(levelWord, 1);
    }

    if (auto* levelValue = makeLabel(std::to_string(m_ctx.level),
                                     "bigFont.fnt", 0.78f, tier.accent)) {
        levelValue->setAnchorPoint({0.f, 0.5f});
        levelValue->setPosition({kColX + 46.f, kPageH - 42.f});
        levelValue->setScale(0.f);
        levelValue->runAction(CCSequence::create(
            CCDelayTime::create(0.10f),
            CCEaseBackOut::create(CCScaleTo::create(0.34f, 0.78f)),
            nullptr
        ));
        page->addChild(levelValue, 1);
    }

    if (auto* total = makeLabel(
            fmt::format("{}  {}", loc.getString("progression.total-xp"),
                        formatCount(m_ctx.exp)),
            "bigFont.fnt", 0.28f, {255, 230, 125})) {
        total->setAnchorPoint({1.f, 0.5f});
        total->limitLabelWidth(155.f, 0.28f, 0.16f);
        total->setPosition({kColRight, kPageH - 42.f});
        page->addChild(total, 1);
    }

    if (auto* bar = XPBarNode::create(kColRight - kColX, 17.f)) {
        bar->setTier(tier);
        bar->setExp(m_ctx.exp);
        bar->setPosition({(kColX + kColRight) / 2.f, kPageH - 66.f});
        page->addChild(bar, 1);
    }

    if (auto* hint = makeLabel(
            m_ctx.level >= kMaxLevel
                ? loc.getString("progression.maxed")
                : fmt::format(fmt::runtime(loc.getString("progression.to-level")),
                              m_ctx.level + 1),
            "bigFont.fnt", 0.24f, kMutedText)) {
        hint->setAnchorPoint({0.f, 0.5f});
        hint->setPosition({kColX, kPageH - 85.f});
        page->addChild(hint, 1);
    }

    // Bottom Split Cards
    constexpr float cardW = 206.f;
    constexpr float cardH = 104.f;

    bool const lastTier = tier.index >= kTierCount - 1;
    auto const& nextTier = tierAt(std::min(tier.index + 1, kTierCount - 1));
    int const goalLevel = nextTierLevel(tier.index);

    auto* nextCard = paimon::ui::makePanel(
        {cardW, cardH},
        loc.getString(lastTier ? "progression.max-tier" : "progression.next-tier").c_str(),
        105);
    nextCard->setPosition({0.f, 2.f});
    page->addChild(nextCard);

    if (auto* preview = TierBadgeNode::create(lastTier ? m_ctx.level : goalLevel, 40.f)) {
        preview->setPosition({34.f, 50.f});
        preview->playIntro(0.18f);
        nextCard->addChild(preview);
    }

    if (auto* nextName = makeLabel(lastTier ? tier.name : nextTier.name,
                                   "bigFont.fnt", 0.40f, nextTier.accent)) {
        nextName->setAnchorPoint({0.f, 0.5f});
        nextName->limitLabelWidth(128.f, 0.40f, 0.22f);
        nextName->setPosition({64.f, 58.f});
        nextCard->addChild(nextName);
    }

    if (!lastTier) {
        int const tierStart = tier.index * kLevelsPerTier + 1;
        float const span = static_cast<float>(goalLevel - tierStart);
        float const tierProgress = span > 0.f
            ? std::clamp((m_ctx.level - tierStart) / span, 0.f, 1.f)
            : 1.f;

        if (auto* need = makeLabel(
                fmt::format(fmt::runtime(loc.getString("progression.at-level")), goalLevel).c_str(),
                "bigFont.fnt", 0.24f, kMutedText)) {
            need->setAnchorPoint({0.f, 0.5f});
            need->setPosition({64.f, 41.f});
            nextCard->addChild(need);
        }

        if (auto* bar = GDProgressBar::create(cardW - 22.f, 11.f)) {
            bar->setFillColor(nextTier.base);
            bar->animateTo(tierProgress, 0.22f, 0.5f);
            bar->setPosition({cardW / 2.f, 15.f});
            nextCard->addChild(bar);
        }
    }

    auto* badgeCard = paimon::ui::makePanel(
        {cardW, cardH},
        loc.getString("progression.tab.badges").c_str(),
        105);
    badgeCard->setPosition({kPageW - cardW, 2.f});
    page->addChild(badgeCard);

    int const unlocked = unlockedCount(m_ctx);
    int const totalBadges = static_cast<int>(allBadges().size());

    if (auto* count = makeLabel(fmt::format("{} / {}", unlocked, totalBadges).c_str(),
                                "bigFont.fnt", 0.42f, {255, 255, 255})) {
        count->setAnchorPoint({0.f, 0.5f});
        count->setPosition({12.f, 69.f});
        badgeCard->addChild(count);
    }

    int const pct = totalBadges > 0
        ? static_cast<int>(std::lround(100.f * static_cast<float>(unlocked) / static_cast<float>(totalBadges)))
        : 0;
    if (auto* pctLbl = makeLabel(fmt::format("{}%", pct).c_str(), "bigFont.fnt", 0.28f, tier.accent)) {
        pctLbl->setAnchorPoint({1.f, 0.5f});
        pctLbl->setPosition({cardW - 12.f, 69.f});
        badgeCard->addChild(pctLbl);
    }

    if (auto* bar = GDProgressBar::create(cardW - 22.f, 11.f)) {
        bar->setFillColor(tier.base);
        bar->animateTo(totalBadges > 0
            ? static_cast<float>(unlocked) / static_cast<float>(totalBadges) : 0.f, 0.26f, 0.5f);
        bar->setPosition({cardW / 2.f, 52.f});
        badgeCard->addChild(bar);
    }

    // Four rarest unlocked as clickable showcase medallions.
    m_showcaseBadges.clear();
    for (auto const& badge : allBadges()) {
        if (isUnlocked(badge, m_ctx)) m_showcaseBadges.push_back(&badge);
    }
    std::sort(m_showcaseBadges.begin(), m_showcaseBadges.end(), [](BadgeDef const* a, BadgeDef const* b) {
        return a->rarity > b->rarity;
    });
    m_showcaseBadges.resize(std::min<size_t>(m_showcaseBadges.size(), 4));

    auto* showcaseMenu = CCMenu::create();
    showcaseMenu->setPosition({0.f, 0.f});
    badgeCard->addChild(showcaseMenu, 2);

    for (size_t i = 0; i < m_showcaseBadges.size(); ++i) {
        if (auto* icon = BadgeIconNode::create(*m_showcaseBadges[i], m_ctx, 34.f)) {
            icon->playIntro(0.28f + 0.06f * i);
            auto* btn = CCMenuItemSpriteExtra::create(
                icon, this, menu_selector(ProgressionPopup::onShowcaseBadge));
            if (btn) {
                btn->setTag(static_cast<int>(i));
                btn->setPosition({32.f + 47.f * static_cast<float>(i), 22.f});
                showcaseMenu->addChild(btn);
            }
        }
    }

    return page;
}

CCNode* ProgressionPopup::buildSources() {
    auto& loc = Localization::get();
    auto const report = computeExp(m_ctx.stats);

    auto* page = CCNode::create();
    page->setContentSize({kPageW, kPageH});

    std::vector<ExpEntry> rows(report.entries.begin(), report.entries.end());
    std::sort(rows.begin(), rows.end(), [](ExpEntry const& a, ExpEntry const& b) {
        return a.exp > b.exp;
    });

    int64_t const peak = rows.empty() ? 0 : std::max<int64_t>(1, rows.front().exp);

    if (auto* header = makeLabel(loc.getString("progression.sources.title").c_str(),
                                 "goldFont.fnt", 0.42f, paimon::ui::palette::gold)) {
        header->setAnchorPoint({0.f, 0.5f});
        header->setPosition({6.f, kPageH - 11.f});
        page->addChild(header);
    }

    if (auto* totalLbl = makeLabel(
            fmt::format("{} {}", formatCount(m_ctx.exp), "XP").c_str(),
            "bigFont.fnt", 0.28f, {255, 230, 125})) {
        totalLbl->setAnchorPoint({1.f, 0.5f});
        totalLbl->setPosition({kPageW - 6.f, kPageH - 11.f});
        page->addChild(totalLbl);
    }

    constexpr float listW = kPageW - 4.f;
    constexpr float listH = kPageH - 26.f;
    if (auto* inset = paimon::ui::makeInset({listW, listH}, 90)) {
        inset->setPosition({2.f, 2.f});
        page->addChild(inset, 0);
    }

    auto* scroll = ScrollLayer::create({listW, listH});
    if (scroll) {
        scroll->setAnchorPoint({0.f, 0.f});
        scroll->setPosition({2.f, 2.f});
        page->addChild(scroll, 1);

        constexpr float rowH = 26.f;
        float const totalH = std::max(listH, rowH * static_cast<float>(rows.size()));
        auto* content = scroll->m_contentLayer;
        content->setContentSize({listW, totalH});

        for (size_t i = 0; i < rows.size(); ++i) {
            auto const& entry = rows[i];
            float const rowBottom = totalH - rowH * static_cast<float>(i + 1);
            float const y = rowBottom + rowH * 0.5f;
            auto const color = sourceColor(entry.source);

            if (auto* stripe = paimon::ui::makeInset({listW - 4.f, rowH - 2.f}, (i % 2 == 0) ? 55 : 22)) {
                stripe->setPosition({2.f, rowBottom + 1.f});
                content->addChild(stripe, 0);
            }

            if (auto* icon = paimon::SpriteHelper::safeCreateWithFrameName(sourceIconFrame(entry.source))) {
                float const source = std::max(icon->getContentSize().width, icon->getContentSize().height);
                icon->setScale(16.f / std::max(1.f, source));
                icon->setPosition({16.f, y});
                content->addChild(icon, 1);
            }

            if (auto* name = makeLabel(
                    loc.getString(std::string("progression.source.") + sourceId(entry.source)).c_str(),
                    "bigFont.fnt", 0.32f, {240, 244, 255})) {
                name->setAnchorPoint({0.f, 0.5f});
                name->limitLabelWidth(94.f, 0.32f, 0.18f);
                name->setPosition({31.f, y + 4.f});
                content->addChild(name, 1);
            }

            if (auto* count = makeLabel(formatCount(entry.count).c_str(),
                                        "bigFont.fnt", 0.22f, kMutedText)) {
                count->setAnchorPoint({0.f, 0.5f});
                count->setPosition({31.f, y - 6.5f});
                content->addChild(count, 1);
            }

            constexpr float barW = 186.f;
            float const ratio = peak > 0
                ? std::clamp(static_cast<float>(entry.exp) / static_cast<float>(peak), 0.f, 1.f) : 0.f;

            if (auto* bar = GDProgressBar::create(barW, 11.f)) {
                bar->setFillColor(color);
                bar->animateTo(ratio, 0.05f + 0.045f * static_cast<float>(i), 0.42f);
                bar->setPosition({134.f + barW / 2.f, y});
                content->addChild(bar, 1);
            }

            if (auto* value = makeLabel(
                    fmt::format("{} XP", formatCount(entry.exp)).c_str(),
                    "bigFont.fnt", 0.30f, color)) {
                value->setAnchorPoint({1.f, 0.5f});
                value->limitLabelWidth(84.f, 0.30f, 0.18f);
                value->setPosition({listW - 10.f, y});
                content->addChild(value, 1);
            }
        }
        scroll->scrollToTop();
    }

    if (auto* borders = ListBorders::create()) {
        borders->setContentSize({listW + 4.f, listH + 2.f});
        borders->setAnchorPoint({0.f, 0.f});
        borders->setPosition({0.f, 1.f});
        page->addChild(borders, 2);
    }

    return page;
}

CCNode* ProgressionPopup::buildBadges() {
    auto* page = CCNode::create();
    page->setContentSize({kPageW, kPageH});
    m_categoryBtns.clear();

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    page->addChild(menu, 3);

    auto const& categories = allCategories();
    int const buttonCount = static_cast<int>(categories.size()) + 1;
    std::vector<CCMenuItem*> catItems;

    for (int i = 0; i < buttonCount; ++i) {
        auto* spr = ButtonSprite::create(" ", 28, true, "bigFont.fnt", "GJ_button_04.png", 30.f, 0.7f);
        if (!spr) continue;

        CCNode* content = nullptr;
        if (i == 0) {
            content = makeLabel("*", "bigFont.fnt", 0.56f, {255, 255, 255});
        } else if (auto* glyph = paimon::SpriteHelper::safeCreateWithFrameName(categories[i - 1].glyph)) {
            float const source = std::max(glyph->getContentSize().width, glyph->getContentSize().height);
            glyph->setScale(18.f / std::max(1.f, source));
            content = glyph;
        }
        if (content) {
            auto const sprSize = spr->getContentSize();
            content->setPosition({sprSize.width * 0.5f, sprSize.height * 0.5f + 1.f});
            spr->addChild(content, 5);
        }

        auto* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ProgressionPopup::onCategory));
        if (!btn) continue;
        btn->setTag(i - 1);
        btn->setScale(0.76f);
        btn->m_baseScale = 0.76f;
        menu->addChild(btn);
        m_categoryBtns.push_back(btn);
        catItems.push_back(btn);
    }

    paimon::ui::layoutButtonRow(catItems, 2.f, kPageW - 2.f, kPageH - 14.f, 5.f, paimon::ui::RowAlign::Center);

    constexpr float gridW = kPageW - 4.f;
    constexpr float gridH = kPageH - 36.f;
    if (auto* panel = paimon::ui::makeInset({gridW, gridH}, 90)) {
        panel->setPosition({2.f, 2.f});
        page->addChild(panel, 0);
    }

    m_grid = ScrollLayer::create({gridW, gridH});
    if (m_grid) {
        m_grid->setAnchorPoint({0.f, 0.f});
        m_grid->setPosition({2.f, 2.f});
        page->addChild(m_grid, 1);
    }

    if (auto* borders = ListBorders::create()) {
        borders->setContentSize({gridW + 4.f, gridH + 2.f});
        borders->setAnchorPoint({0.f, 0.f});
        borders->setPosition({0.f, 1.f});
        page->addChild(borders, 2);
    }

    rebuildBadgeGrid();
    return page;
}

void ProgressionPopup::onCategory(CCObject* sender) {
    int const index = sender ? sender->getTag() : -1;
    if (index == m_category) return;
    m_category = index;
    rebuildBadgeGrid();
}

void ProgressionPopup::rebuildBadgeGrid() {
    if (!m_grid || !m_grid->m_contentLayer) return;

    for (auto* btn : m_categoryBtns) {
        bool const active = btn && btn->getTag() == m_category;
        paimon::ui::setButtonSkin(btn, active ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    }

    auto* layer = m_grid->m_contentLayer;
    layer->removeAllChildren();

    std::vector<BadgeDef const*> shown;
    for (auto const& badge : allBadges()) {
        if (m_category >= 0) {
            auto const& category = allCategories()[m_category];
            if (std::string_view(badge.category) != category.id) continue;
        }
        shown.push_back(&badge);
    }

    auto const viewSize = m_grid->getContentSize();
    int const rows = (static_cast<int>(shown.size()) + kGridColumns - 1) / kGridColumns;
    float const gridH = std::max(viewSize.height, rows * (kTileSize + kTileGap) + kTileGap);
    layer->setContentSize({viewSize.width, gridH});

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({viewSize.width, gridH});
    menu->setLayout(
        RowLayout::create()
            ->setGap(kTileGap)
            ->setGrowCrossAxis(true)
            ->setCrossAxisOverflow(false)
            ->setAutoScale(false)
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisAlignment(AxisAlignment::End)
    );
    layer->addChild(menu);

    for (size_t i = 0; i < shown.size(); ++i) {
        auto* icon = BadgeIconNode::create(*shown[i], m_ctx, kTileSize);
        if (!icon) continue;
        auto* btn = CCMenuItemSpriteExtra::create(icon, this, menu_selector(ProgressionPopup::onBadge));
        if (!btn) continue;
        btn->setTag(static_cast<int>(i));
        // stagger only first rows; rest reveal on scroll anyway.
        if (i < kGridColumns * 3) icon->playIntro(0.02f * static_cast<float>(i));
        menu->addChild(btn);
    }

    menu->updateLayout();
    m_grid->scrollToTop();

    m_gridBadges = std::move(shown);
}

void ProgressionPopup::onBadge(CCObject* sender) {
    if (!sender) return;
    int const index = sender->getTag();
    if (index < 0 || index >= static_cast<int>(m_gridBadges.size())) return;

    if (auto* popup = BadgeDetailPopup::create(*m_gridBadges[index], m_ctx)) {
        popup->show();
    }
}

void ProgressionPopup::onShowcaseBadge(CCObject* sender) {
    if (!sender) return;
    int const index = sender->getTag();
    if (index < 0 || index >= static_cast<int>(m_showcaseBadges.size())) return;

    if (auto* popup = BadgeDetailPopup::create(*m_showcaseBadges[index], m_ctx)) {
        popup->show();
    }
}

} // namespace paimon::progression
