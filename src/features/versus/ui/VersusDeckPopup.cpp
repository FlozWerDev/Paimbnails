#include "VersusDeckPopup.hpp"
#include "VersusCardNode.hpp"
#include "VersusUIKit.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::versus {

namespace {

constexpr float kPopupW = 430.f;
constexpr float kPopupH = 290.f;
constexpr float kListW = 404.f;
constexpr float kListH = 186.f;
constexpr float kRowH = 62.f;
constexpr float kCardW = 34.f;

constexpr int kFilterTag = 5200;

} // namespace

VersusDeckPopup* VersusDeckPopup::create() {
    auto ret = new VersusDeckPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool VersusDeckPopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;

    this->setTitle(Localization::get().getString("versus.deck.title"));
    paimon::markDynamicPopup(this);

    buildFilters();

    auto* hint = ui::makeText(Localization::get().getString("versus.deck.hint"), "chatFont.fnt",
                              0.42f, {kPopupW / 2.f, kPopupH - 78.f});
    hint->setOpacity(170);
    m_mainLayer->addChild(hint, 3);

    m_scroll = ScrollLayer::create({kListW, kListH});
    m_scroll->setPosition({(kPopupW - kListW) / 2.f, 14.f});
    m_mainLayer->addChild(m_scroll, 1);

    auto* borders = ListBorders::create();
    borders->setContentSize({kListW, kListH});
    borders->setPosition({kPopupW / 2.f, 14.f + kListH / 2.f});
    m_mainLayer->addChild(borders, 4);

    rebuildGrid();
    return true;
}

void VersusDeckPopup::buildFilters() {
    auto* menu = CCMenu::create();
    menu->setPosition({kPopupW / 2.f, kPopupH - 52.f});
    m_mainLayer->addChild(menu, 3);

    char const* keys[] = {"versus.filter.all", "versus.rarity.common", "versus.rarity.rare",
                          "versus.rarity.epic", "versus.rarity.legendary"};

    float const step = 82.f;
    for (int i = 0; i < 5; i++) {
        auto* btn = ui::makeTab(Localization::get().getString(keys[i]), 74.f, this,
                                menu_selector(VersusDeckPopup::onFilter));
        btn->setTag(kFilterTag + i - 1);
        btn->setPosition({(i - 2) * step, 0.f});
        menu->addChild(btn);
        m_filterButtons.push_back(btn);
    }
}

void VersusDeckPopup::onFilter(CCObject* sender) {
    m_filter = sender->getTag() - kFilterTag;
    rebuildGrid();
}

void VersusDeckPopup::rebuildGrid() {
    for (size_t i = 0; i < m_filterButtons.size(); i++) {
        ui::styleTab(m_filterButtons[i], static_cast<int>(i) - 1 == m_filter);
    }

    m_scroll->m_contentLayer->removeAllChildren();

    std::vector<CardDef const*> shown;
    for (auto const& def : allCards()) {
        if (m_filter >= 0 && static_cast<int>(def.rarity) != m_filter) continue;
        shown.push_back(&def);
    }

    float const height = std::max(kListH, shown.size() * kRowH);
    m_scroll->m_contentLayer->setContentSize({kListW, height});

    auto* rowMenu = CCMenu::create();
    rowMenu->setPosition({0.f, 0.f});
    m_scroll->m_contentLayer->addChild(rowMenu, 2);

    for (size_t i = 0; i < shown.size(); i++) {
        auto const* def = shown[i];
        float const y = height - kRowH * (i + 0.5f);

        auto* row = CCLayerColor::create(
            i % 2 ? ccColor4B{0, 0, 0, 60} : ccColor4B{0, 0, 0, 95}, kListW, kRowH - 3.f);
        row->setPosition({0.f, y - (kRowH - 3.f) / 2.f});
        m_scroll->m_contentLayer->addChild(row, 0);

        if (auto* card = VersusCardNode::create(def->id, kCardW)) {
            card->setPosition({34.f, y});
            m_scroll->m_contentLayer->addChild(card, 1);
        }

        auto* name = CCLabelBMFont::create(def->name, "bigFont.fnt");
        name->setAnchorPoint({0.f, 0.5f});
        name->setScale(0.46f);
        name->setPosition({72.f, y + 12.f});
        name->setColor(rarityRim(def->rarity));
        m_scroll->m_contentLayer->addChild(name, 1);

        auto* effect = CCLabelBMFont::create(cardEffectText(*def).c_str(), "chatFont.fnt");
        effect->setAnchorPoint({0.f, 0.5f});
        effect->setScale(0.48f);
        effect->setPosition({72.f, y - 6.f});
        effect->setOpacity(200);
        m_scroll->m_contentLayer->addChild(effect, 1);

        auto* tag = CCLabelBMFont::create(rarityName(def->rarity).c_str(), "goldFont.fnt");
        tag->setAnchorPoint({1.f, 0.5f});
        tag->setScale(0.36f);
        tag->setPosition({kListW - 10.f, y + 12.f});
        m_scroll->m_contentLayer->addChild(tag, 1);

        // cards that only exist in one mode say so, or the odds table looks
        // wrong the first time a platformer card never shows up in classic.
        if (def->modes != ModeAny) {
            auto* only = CCLabelBMFont::create(
                Localization::get().getString(def->modes & ModePlatformer
                    ? "versus.only-platformer" : "versus.only-classic").c_str(),
                "chatFont.fnt");
            only->setAnchorPoint({1.f, 0.5f});
            only->setScale(0.4f);
            only->setPosition({kListW - 10.f, y - 6.f});
            only->setOpacity(160);
            m_scroll->m_contentLayer->addChild(only, 1);
        }
    }

    m_scroll->moveToTop();
}

} // namespace paimon::versus
