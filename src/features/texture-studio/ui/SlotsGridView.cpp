#include "SlotsGridView.hpp"

#include "../persist/SlotStore.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/ScrollLayer.hpp>

#include <ctime>
#include <cstdio>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

constexpr float kCardW = 104.f;
constexpr float kCardH = 88.f;
constexpr float kCardGap = 8.f;

std::string formatRelativeTime(std::int64_t ms) {
    if (ms <= 0) return "never";
    auto now = static_cast<std::int64_t>(std::time(nullptr)) * 1000;
    auto diff = now - ms;
    if (diff < 0) return "just now";
    auto secs = diff / 1000;
    if (secs < 60)    return std::to_string(secs) + "s ago";
    auto mins = secs / 60;
    if (mins < 60)    return std::to_string(mins) + "m ago";
    auto hours = mins / 60;
    if (hours < 24)   return std::to_string(hours) + "h ago";
    auto days = hours / 24;
    if (days < 30)    return std::to_string(days) + "d ago";
    return "long ago";
}

}  // anonymous namespace

SlotsGridView* SlotsGridView::create(float width, float height,
                                     SlotActionCallback onApply,
                                     SlotActionCallback onEdit,
                                     SlotActionCallback onDelete,
                                     std::function<void()> onNewPack) {
    auto* ret = new SlotsGridView();
    if (ret->init(width, height,
                  std::move(onApply),
                  std::move(onEdit),
                  std::move(onDelete),
                  std::move(onNewPack))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool SlotsGridView::init(float width, float height,
                         SlotActionCallback onApply,
                         SlotActionCallback onEdit,
                         SlotActionCallback onDelete,
                         std::function<void()> onNewPack) {
    if (!CCNode::init()) return false;
    m_onApply    = std::move(onApply);
    m_onEdit     = std::move(onEdit);
    m_onDelete   = std::move(onDelete);
    m_onNewPack  = std::move(onNewPack);
    m_widgetWidth  = width;
    m_widgetHeight = height;
    this->setContentSize({width, height});

    auto* scroll = ScrollLayer::create({width, height});
    if (!scroll) return false;
    scroll->setAnchorPoint({0.f, 0.f});
    scroll->setPosition({0.f, 0.f});
    this->addChild(scroll);
    m_contentLayer = scroll->m_contentLayer;
    if (!m_contentLayer) return false;

    refresh();
    return true;
}

void SlotsGridView::refresh() {
    if (!m_contentLayer) return;
    m_contentLayer->removeAllChildren();

    SlotStore::get().loadIndex();
    auto const& list = SlotStore::get().list();

    int columns = std::max(1, static_cast<int>((m_widgetWidth + kCardGap) / (kCardW + kCardGap)));
    int rowsRequired = (static_cast<int>(list.size()) + 1 + columns - 1) / columns;
    float totalH = std::max(m_widgetHeight, rowsRequired * (kCardH + kCardGap) + kCardGap);
    m_contentLayer->setContentHeight(totalH);

    auto placeCard = [&](CCNode* card, int idx) {
        if (!card) return;
        int col = idx % columns;
        int row = idx / columns;
        float x = kCardGap + col * (kCardW + kCardGap) + kCardW / 2.f;
        float y = totalH - kCardGap - row * (kCardH + kCardGap) - kCardH / 2.f;
        card->setAnchorPoint({0.5f, 0.5f});
        card->setPosition({x, y});
        m_contentLayer->addChild(card);
    };

    int i = 0;
    for (auto const& entry : list) {
        if (auto* card = makeSlotCard(entry.id, entry.name, entry.modifiedAt, entry.hasBuiltOnce)) {
            placeCard(card, i++);
        }
    }
    if (auto* card = makeNewPackCard()) {
        placeCard(card, i);
    }
}

CCNode* SlotsGridView::makeSlotCard(std::string const& id,
                                    std::string const& name,
                                    std::int64_t modifiedAt,
                                    bool hasBuiltOnce) {
    auto* card = CCNode::create();
    if (!card) return nullptr;
    card->setContentSize({kCardW, kCardH});

    if (auto* bg = CCScale9Sprite::create("GJ_square01.png")) {
        bg->setContentSize({kCardW, kCardH});
        bg->setColor({40, 40, 40});
        card->addChildAtPosition(bg, Anchor::Center);
    }

    if (auto* nameLbl = CCLabelBMFont::create(name.empty() ? "(unnamed)" : name.c_str(), "bigFont.fnt")) {
        nameLbl->setScale(0.4f);
        nameLbl->limitLabelWidth(kCardW - 10.f, 0.4f, 0.2f);
        card->addChildAtPosition(nameLbl, Anchor::Top, {0.f, -12.f});
    }

    if (auto* metaLbl = CCLabelBMFont::create(
            (formatRelativeTime(modifiedAt) + (hasBuiltOnce ? " · built" : " · draft")).c_str(),
            "bigFont.fnt")) {
        metaLbl->setScale(0.26f);
        card->addChildAtPosition(metaLbl, Anchor::Top, {0.f, -25.f});
    }

    // Action buttons row at the bottom.
    auto* menu = CCMenu::create();
    if (!menu) return card;
    menu->setContentSize({kCardW, 30.f});

    auto makeMini = [&](char const* label, ccColor3B color, std::function<void()> action) -> CCMenuItemSpriteExtra* {
        auto* spr = ButtonSprite::create(label, "bigFont.fnt", "GJ_button_05.png", 0.5f);
        if (!spr) return nullptr;
        spr->setColor(color);
        return CCMenuItemExt::createSpriteExtra(spr,
            [action = std::move(action)](CCMenuItemSpriteExtra*) { if (action) action(); });
    };

    auto* applyBtn = makeMini("Apply", {120, 220, 120}, [this, id]() { if (m_onApply) m_onApply(id); });
    auto* editBtn  = makeMini("Edit",  {220, 220, 220}, [this, id]() { if (m_onEdit)  m_onEdit(id);  });
    auto* delBtn   = makeMini("X",     {220, 80, 80},   [this, id]() { if (m_onDelete) m_onDelete(id); });

    if (applyBtn) menu->addChildAtPosition(applyBtn, Anchor::Center, {-32.f, 0.f});
    if (editBtn)  menu->addChildAtPosition(editBtn,  Anchor::Center, { 0.f,  0.f});
    if (delBtn)   menu->addChildAtPosition(delBtn,   Anchor::Center, { 32.f, 0.f});

    card->addChildAtPosition(menu, Anchor::Bottom, {0.f, 13.f});

    return card;
}

CCNode* SlotsGridView::makeNewPackCard() {
    auto* card = CCNode::create();
    if (!card) return nullptr;
    card->setContentSize({kCardW, kCardH});

    if (auto* bg = CCScale9Sprite::create("GJ_square01.png")) {
        bg->setContentSize({kCardW, kCardH});
        bg->setColor({30, 50, 30});
        card->addChildAtPosition(bg, Anchor::Center);
    }

    if (auto* plusLbl = CCLabelBMFont::create("+", "bigFont.fnt")) {
        plusLbl->setScale(1.0f);
        plusLbl->setColor({150, 220, 150});
        card->addChildAtPosition(plusLbl, Anchor::Center, {0.f, 10.f});
    }

    if (auto* tlbl = CCLabelBMFont::create("New Pack", "bigFont.fnt")) {
        tlbl->setScale(0.36f);
        card->addChildAtPosition(tlbl, Anchor::Center, {0.f, -10.f});
    }

    auto* menu = CCMenu::create();
    if (!menu) return card;
    menu->setContentSize({kCardW, kCardH});

    if (auto* btnSpr = ButtonSprite::create("Click", "bigFont.fnt", "GJ_button_04.png", 0.5f)) {
        if (auto* btn = CCMenuItemExt::createSpriteExtra(btnSpr,
                [this](CCMenuItemSpriteExtra*) { if (m_onNewPack) m_onNewPack(); })) {
            menu->addChildAtPosition(btn, Anchor::Bottom, {0.f, 13.f});
        }
    }
    card->addChild(menu);

    return card;
}

}  // namespace paimon::texture_studio
