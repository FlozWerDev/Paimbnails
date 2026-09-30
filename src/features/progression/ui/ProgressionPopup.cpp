#include "ProgressionPopup.hpp"
#include "BadgeDetailPopup.hpp"
#include "BadgeIconNode.hpp"
#include "GDProgressBar.hpp"
#include "TierBadgeNode.hpp"
#include "XPBarNode.hpp"
#include "../data/ProgressionStats.hpp"
#include "../data/ProgressionTiers.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/ScissorClipNode.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::progression {

namespace {

constexpr float kPopupW = 420.f;
constexpr float kPopupH = 285.f;
constexpr float kPageW = 400.f;
constexpr float kPageH = 200.f;
constexpr float kPageX = 10.f;
constexpr float kPageY = 14.f;

constexpr int kTabCount = 3;
constexpr int kGridColumns = 7;
constexpr float kTileSize = 44.f;
constexpr float kTileGap = 7.f;

constexpr ccColor3B kIdleTint = {104, 108, 122};
constexpr ccColor3B kMutedText = {168, 178, 198};

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

CCNode* makeCard(float width, float height) {
    auto* card = CCNode::create();
    card->setContentSize({width, height});
    if (auto* panel = paimon::SpriteHelper::safeCreateScale9("GJ_square05.png", CCRectMake(12.f, 12.f, 56.f, 56.f))) {
        panel->setContentSize({width, height});
        panel->setAnchorPoint({0.f, 0.f});
        panel->setColor({0, 0, 0});
        panel->setOpacity(85);
        card->addChild(panel, -1);
    }
    return card;
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

    if (auto* skin = paimon::SpriteHelper::safeCreateScale9WithFrameName("GJ_tabOff_001.png")) {
        skin->setContentSize({width, height});
        skin->setAnchorPoint({0.f, 0.f});
        skin->setID("tab-skin"_spr);
        tab->addChild(skin, 0);
    }
    if (content) {
        content->setPosition({width / 2.f, height / 2.f});
        tab->addChild(content, 1);
    }
    return tab;
}

CCNode* makeChip(float size, CCNode* content) {
    auto* chip = CCNode::create();
    chip->setContentSize({size, size});

    auto place = [&](CCSprite* sprite, char const* id, int z) {
        if (!sprite) return;
        sprite->setScale(size / std::max(1.f, sprite->getContentSize().width));
        sprite->setPosition({size / 2.f, size / 2.f});
        sprite->setID(id);
        chip->addChild(sprite, z);
    };
    place(paimon::SpriteHelper::safeCreate("paim_progPlateFace.png"_spr), "chip-face"_spr, 0);
    place(paimon::SpriteHelper::safeCreate("paim_progPlate1.png"_spr), "chip-plate"_spr, 1);

    if (content) {
        content->setPosition({size / 2.f, size / 2.f});
        chip->addChild(content, 2);
    }
    return chip;
}

ccColor3B dim(ccColor3B const& color, float factor) {
    return {
        static_cast<GLubyte>(color.r * factor),
        static_cast<GLubyte>(color.g * factor),
        static_cast<GLubyte>(color.b * factor),
    };
}

void setTinted(CCNode* holder, char const* childId, ccColor3B color) {
    if (!holder) return;
    if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(holder->getChildByID(childId))) {
        rgba->setColor(color);
    }
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
    if (!Popup::init(kPopupW, kPopupH)) return false;

    m_ctx = ctx;

    auto& loc = Localization::get();
    auto const& tier = tierForLevel(ctx.level);

    this->setTitle(username.empty() ? loc.getString("progression.title") : username);
    if (m_title) m_title->setColor(tier.accent);

    buildTabRow();

    if (auto* stencil = paimon::SpriteHelper::createRectStencil(kPageW, kPageH)) {
        if (auto* clip = paimon::ScissorClipNode::create(stencil)) {
            clip->setAlphaThreshold(0.05f);
            clip->setContentSize({kPageW, kPageH});
            clip->setPosition({kPageX, kPageY});
            m_pageHolder = clip;
            m_mainLayer->addChild(clip, 1);
        }
    }

    if (auto* infoSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png")) {
        infoSprite->setScale(0.7f);
        if (auto* btn = CCMenuItemSpriteExtra::create(
                infoSprite, this, menu_selector(ProgressionPopup::onFormulaInfo))) {
            btn->setID("progression-formula-info"_spr);
            btn->setPosition({kPopupW - 20.f, 20.f});
            m_buttonMenu->addChild(btn);
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

    constexpr float tabW = 112.f;
    constexpr float tabH = 26.f;
    constexpr float gap = 8.f;
    float const totalW = tabW * kTabCount + gap * (kTabCount - 1);
    float const startX = (kPopupW - totalW) / 2.f + tabW / 2.f;

    for (int i = 0; i < kTabCount; ++i) {
        auto* label = makeLabel(loc.getString(keys[i]).c_str(), "bigFont.fnt", 0.4f, {255, 255, 255});
        if (label) label->limitLabelWidth(tabW - 16.f, 0.4f, 0.2f);
        auto* tab = makeTab(tabW, tabH, label);
        auto* btn = CCMenuItemSpriteExtra::create(tab, this, menu_selector(ProgressionPopup::onTab));
        if (!btn) continue;
        btn->setTag(i);
        btn->setPosition({startX + (tabW + gap) * i, kPopupH - 46.f});
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

    auto const& tier = tierForLevel(m_ctx.level);
    int const previous = m_tab;
    m_tab = std::clamp(index, 0, kTabCount - 1);
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        setTinted(m_tabs[i], "tab-skin", i == m_tab ? tier.base : kIdleTint);
        if (auto* parent = m_tabs[i]->getParent()) {
            parent->setPositionY(kPopupH - (i == m_tab ? 44.f : 46.f));
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

    // above y=110: the two cards underneath stay clear.
    constexpr float kColX = 108.f;
    constexpr float kColRight = kPageW - 8.f;

    if (auto* badge = TierBadgeNode::create(m_ctx.level, 72.f)) {
        badge->setProgress(levelProgress(m_ctx.exp));
        badge->setPosition({52.f, 152.f});
        badge->playIntro(0.04f);
        page->addChild(badge);
    }

    if (auto* name = makeLabel(tier.name, "goldFont.fnt", 0.46f, {255, 255, 255})) {
        name->setAnchorPoint({0.f, 0.5f});
        name->limitLabelWidth(150.f, 0.46f, 0.24f);
        name->setPosition({kColX, 188.f});
        page->addChild(name);
    }

    if (auto* tierIndex = makeLabel(
            fmt::format("{} {} / {}", loc.getString("progression.tier"),
                        tier.index + 1, kTierCount),
            "chatFont.fnt", 0.38f, kMutedText)) {
        tierIndex->setAnchorPoint({1.f, 0.5f});
        tierIndex->setPosition({kColRight, 188.f});
        page->addChild(tierIndex);
    }

    if (auto* levelWord = makeLabel(loc.getString("progression.level"),
                                    "chatFont.fnt", 0.44f, kMutedText)) {
        levelWord->setAnchorPoint({0.f, 0.5f});
        levelWord->setPosition({kColX, 163.f});
        page->addChild(levelWord);
    }

    if (auto* levelValue = makeLabel(std::to_string(m_ctx.level),
                                     "bigFont.fnt", 0.85f, tier.accent)) {
        levelValue->setAnchorPoint({0.f, 0.5f});
        levelValue->setPosition({kColX + 36.f, 162.f});
        levelValue->setScale(0.f);
        levelValue->runAction(CCSequence::create(
            CCDelayTime::create(0.10f),
            CCEaseBackOut::create(CCScaleTo::create(0.34f, 0.85f)),
            nullptr
        ));
        page->addChild(levelValue);
    }

    if (auto* total = makeLabel(
            fmt::format("{}  {}", loc.getString("progression.total-xp"),
                        formatCount(m_ctx.exp)),
            "chatFont.fnt", 0.4f, {215, 224, 240})) {
        total->setAnchorPoint({1.f, 0.5f});
        total->setPosition({kColRight, 163.f});
        page->addChild(total);
    }

    if (auto* bar = XPBarNode::create(kColRight - kColX, 18.f)) {
        bar->setTier(tier);
        bar->setExp(m_ctx.exp);
        bar->setPosition({(kColX + kColRight) / 2.f, 138.f});
        page->addChild(bar);
    }

    if (auto* hint = makeLabel(
            m_ctx.level >= kMaxLevel
                ? loc.getString("progression.maxed")
                : fmt::format(fmt::runtime(loc.getString("progression.to-level")),
                              m_ctx.level + 1),
            "chatFont.fnt", 0.36f, {150, 162, 185})) {
        hint->setAnchorPoint({0.f, 0.5f});
        hint->setPosition({kColX, 119.f});
        page->addChild(hint);
    }

    constexpr float cardW = 196.f;
    constexpr float cardH = 100.f;

    auto* nextCard = makeCard(cardW, cardH);
    nextCard->setPosition({0.f, 4.f});
    page->addChild(nextCard);

    bool const lastTier = tier.index >= kTierCount - 1;
    auto const& nextTier = tierAt(std::min(tier.index + 1, kTierCount - 1));
    int const goalLevel = nextTierLevel(tier.index);

    if (auto* header = makeLabel(
            loc.getString(lastTier ? "progression.max-tier" : "progression.next-tier").c_str(),
            "chatFont.fnt", 0.38f, kMutedText)) {
        header->setAnchorPoint({0.f, 0.5f});
        header->setPosition({12.f, cardH - 14.f});
        nextCard->addChild(header);
    }

    if (auto* preview = TierBadgeNode::create(lastTier ? m_ctx.level : goalLevel, 42.f)) {
        preview->setPosition({36.f, 46.f});
        preview->playIntro(0.18f);
        nextCard->addChild(preview);
    }

    if (auto* nextName = makeLabel(lastTier ? tier.name : nextTier.name,
                                   "bigFont.fnt", 0.46f, nextTier.accent)) {
        nextName->setAnchorPoint({0.f, 0.5f});
        nextName->limitLabelWidth(120.f, 0.46f, 0.24f);
        nextName->setPosition({66.f, 58.f});
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
                "chatFont.fnt", 0.36f, {180, 192, 214})) {
            need->setAnchorPoint({0.f, 0.5f});
            need->setPosition({66.f, 42.f});
            nextCard->addChild(need);
        }

        if (auto* bar = GDProgressBar::create(cardW - 24.f, 10.f)) {
            bar->setFillColor(nextTier.base);
            bar->animateTo(tierProgress, 0.22f, 0.5f);
            bar->setPosition({cardW / 2.f, 16.f});
            nextCard->addChild(bar);
        }
    }

    auto* badgeCard = makeCard(cardW, cardH);
    badgeCard->setPosition({kPageW - cardW, 4.f});
    page->addChild(badgeCard);

    int const unlocked = unlockedCount(m_ctx);
    int const totalBadges = static_cast<int>(allBadges().size());

    if (auto* header = makeLabel(loc.getString("progression.tab.badges").c_str(),
                                 "chatFont.fnt", 0.38f, kMutedText)) {
        header->setAnchorPoint({0.f, 0.5f});
        header->setPosition({12.f, cardH - 14.f});
        badgeCard->addChild(header);
    }

    if (auto* count = makeLabel(fmt::format("{} / {}", unlocked, totalBadges).c_str(),
                                "bigFont.fnt", 0.58f, {255, 255, 255})) {
        count->setAnchorPoint({0.f, 0.5f});
        count->setPosition({12.f, cardH - 34.f});
        badgeCard->addChild(count);
    }

    if (auto* bar = GDProgressBar::create(cardW - 24.f, 10.f)) {
        bar->setFillColor(tier.base);
        bar->animateTo(totalBadges > 0
            ? static_cast<float>(unlocked) / static_cast<float>(totalBadges) : 0.f, 0.26f, 0.5f);
        bar->setPosition({cardW / 2.f, cardH - 54.f});
        badgeCard->addChild(bar);
    }

    // four rarest unlocked as a teaser row.
    std::vector<BadgeDef const*> showcase;
    for (auto const& badge : allBadges()) {
        if (isUnlocked(badge, m_ctx)) showcase.push_back(&badge);
    }
    std::sort(showcase.begin(), showcase.end(), [](BadgeDef const* a, BadgeDef const* b) {
        return a->rarity > b->rarity;
    });
    showcase.resize(std::min<size_t>(showcase.size(), 4));

    for (size_t i = 0; i < showcase.size(); ++i) {
        if (auto* icon = BadgeIconNode::create(*showcase[i], m_ctx, 32.f)) {
            icon->setPosition({28.f + 44.f * i, 22.f});
            icon->playIntro(0.28f + 0.06f * i);
            badgeCard->addChild(icon);
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
                                 "chatFont.fnt", 0.4f, kMutedText)) {
        header->setAnchorPoint({0.f, 0.5f});
        header->setPosition({8.f, kPageH - 10.f});
        page->addChild(header);
    }

    constexpr float rowH = 23.f;
    float const top = kPageH - 27.f;

    for (size_t i = 0; i < rows.size(); ++i) {
        auto const& entry = rows[i];
        float const y = top - rowH * static_cast<float>(i);
        auto const color = sourceColor(entry.source);

        if (auto* icon = paimon::SpriteHelper::safeCreateWithFrameName(sourceIconFrame(entry.source))) {
            float const source = std::max(icon->getContentSize().width, icon->getContentSize().height);
            icon->setScale(15.f / std::max(1.f, source));
            icon->setPosition({16.f, y});
            page->addChild(icon);
        }

        if (auto* name = makeLabel(
                loc.getString(std::string("progression.source.") + sourceId(entry.source)).c_str(),
                "bigFont.fnt", 0.36f, {235, 240, 250})) {
            name->setAnchorPoint({0.f, 0.5f});
            name->limitLabelWidth(84.f, 0.36f, 0.2f);
            name->setPosition({30.f, y + 4.f});
            page->addChild(name);
        }

        if (auto* count = makeLabel(formatCount(entry.count).c_str(),
                                    "chatFont.fnt", 0.32f, {140, 152, 176})) {
            count->setAnchorPoint({0.f, 0.5f});
            count->setPosition({30.f, y - 7.f});
            page->addChild(count);
        }

        constexpr float barW = 190.f;
        float const ratio = peak > 0
            ? std::clamp(static_cast<float>(entry.exp) / static_cast<float>(peak), 0.f, 1.f) : 0.f;

        if (auto* bar = GDProgressBar::create(barW, 10.f)) {
            bar->setFillColor(color);
            bar->animateTo(ratio, 0.05f + 0.045f * static_cast<float>(i), 0.42f);
            bar->setPosition({122.f + barW / 2.f, y});
            page->addChild(bar);
        }

        if (auto* value = makeLabel(
                fmt::format("{} XP", formatCount(entry.exp)).c_str(),
                "bigFont.fnt", 0.34f, color)) {
            value->setAnchorPoint({1.f, 0.5f});
            value->limitLabelWidth(78.f, 0.34f, 0.2f);
            value->setPosition({kPageW - 8.f, y});
            page->addChild(value);
        }
    }

    return page;
}

CCNode* ProgressionPopup::buildBadges() {
    auto* page = CCNode::create();
    page->setContentSize({kPageW, kPageH});
    m_categoryChips.clear();

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    page->addChild(menu, 2);

    auto const& categories = allCategories();
    int const buttonCount = static_cast<int>(categories.size()) + 1;
    constexpr float chipSize = 26.f;
    float const gap = (kPageW - 12.f - chipSize * buttonCount) / (buttonCount - 1);
    float const startX = 6.f + chipSize / 2.f;
    float const rowY = kPageH - 16.f;

    for (int i = 0; i < buttonCount; ++i) {
        CCNode* content = nullptr;
        if (i == 0) {
            content = makeLabel("*", "bigFont.fnt", 0.5f, {255, 255, 255});
        } else if (auto* glyph = paimon::SpriteHelper::safeCreateWithFrameName(categories[i - 1].glyph)) {
            float const source = std::max(glyph->getContentSize().width, glyph->getContentSize().height);
            glyph->setScale(15.f / std::max(1.f, source));
            content = glyph;
        }

        auto* chip = makeChip(chipSize, content);
        auto* btn = CCMenuItemSpriteExtra::create(chip, this, menu_selector(ProgressionPopup::onCategory));
        if (!btn) continue;
        btn->setTag(i - 1);
        btn->setPosition({startX + (chipSize + gap) * i, rowY});
        menu->addChild(btn);
        m_categoryChips.push_back(chip);
    }

    float const gridH = kPageH - 36.f;
    if (auto* panel = paimon::SpriteHelper::safeCreateScale9("GJ_square05.png", CCRectMake(12.f, 12.f, 56.f, 56.f))) {
        panel->setContentSize({kPageW, gridH});
        panel->setAnchorPoint({0.f, 0.f});
        panel->setColor({0, 0, 0});
        panel->setOpacity(75);
        page->addChild(panel, 0);
    }

    m_grid = ScrollLayer::create({kPageW, gridH});
    m_grid->setPosition({0.f, 0.f});
    page->addChild(m_grid, 1);

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

    auto const& tier = tierForLevel(m_ctx.level);
    auto const& categories = allCategories();
    for (int i = 0; i < static_cast<int>(m_categoryChips.size()); ++i) {
        bool const active = (i - 1) == m_category;
        auto const accent = i == 0 ? tier.base : categories[i - 1].color;
        setTinted(m_categoryChips[i], "chip-plate"_spr, active ? accent : kIdleTint);
        // idle face keeps a trace of category colour so rows stay distinct.
        setTinted(m_categoryChips[i], "chip-face"_spr, active ? accent : dim(accent, 0.42f));
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

void ProgressionPopup::onFormulaInfo(CCObject*) {
    auto& loc = Localization::get();
    using namespace exp_values;

    auto const body = fmt::format(
        fmt::runtime(loc.getString("progression.info.body")),
        kStar, kMoon, kDiamond, kUserCoin, kSecretCoin,
        kDemonEasy, kDemonExtreme, kCreatorPoint
    );
    PopupManager::get().alert(loc.getString("progression.info.title"), body).showInstant();
}

} // namespace paimon::progression
