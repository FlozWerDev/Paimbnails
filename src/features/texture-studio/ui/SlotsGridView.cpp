#include "SlotsGridView.hpp"

#include "../persist/SlotStore.hpp"

#include <Geode/Geode.hpp>

#include <ctime>
#include <cstdio>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

constexpr float kCardW = 174.f;
constexpr float kCardH = 110.f;
constexpr float kCardGap = 12.f;

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
    int generation = m_thumbnailGeneration->fetch_add(1, std::memory_order_acq_rel) + 1;
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
    std::vector<std::pair<int, TextureProject>> thumbnailJobs;
    for (auto const& entry : list) {
        if (auto* card = makeSlotCard(entry.id, entry.name, entry.modifiedAt)) {
            int cardTag = 1000 + i;
            card->setTag(cardTag);
            placeCard(card, i++);
            auto loaded = SlotStore::get().loadSlot(entry.id);
            if (loaded) {
                auto project = loaded.unwrap();
                thumbnailJobs.emplace_back(cardTag, std::move(project));
            }
        }
    }
    if (auto* card = makeNewPackCard()) {
        placeCard(card, i);
    }
    requestThumbnails(std::move(thumbnailJobs), generation);
}

CCNode* SlotsGridView::makeSlotCard(std::string const& id,
                                    std::string const& name,
                                    std::int64_t modifiedAt) {
    auto* card = CCNode::create();
    if (!card) return nullptr;
    card->setContentSize({kCardW, kCardH});

    if (auto* bg = CCScale9Sprite::create("GJ_square01.png")) {
        bg->setContentSize({kCardW, kCardH});
        bg->setColor({46, 46, 54});
        card->addChildAtPosition(bg, Anchor::Center);
    }

    if (auto* nameLbl = CCLabelBMFont::create(name.empty() ? "(unnamed)" : name.c_str(), "bigFont.fnt")) {
        nameLbl->setScale(0.5f);
        nameLbl->limitLabelWidth(kCardW - 58.f, 0.5f, 0.25f);
        card->addChildAtPosition(nameLbl, Anchor::Top, {18.f, -16.f});
    }

    if (auto* metaLbl = CCLabelBMFont::create(
            (formatRelativeTime(modifiedAt) + (SlotStore::get().activeSlotId() == id ? "   Active" : "   Saved")).c_str(),
            "bigFont.fnt")) {
        metaLbl->setScale(0.32f);
        metaLbl->setColor(SlotStore::get().activeSlotId() == id ? ccColor3B{120, 210, 130} : ccColor3B{170, 170, 178});
        card->addChildAtPosition(metaLbl, Anchor::Top, {18.f, -34.f});
    }

    auto* previewHost = CCNode::create();
    previewHost->setContentSize({38.f, 38.f});
    previewHost->setTag(200);
    card->addChildAtPosition(previewHost, Anchor::TopLeft, {24.f, -27.f});
    if (auto* placeholder = CCSprite::create("square.png")) {
        placeholder->setColor({78, 80, 90});
        placeholder->setOpacity(150);
        placeholder->setScale(32.f / std::max(1.f, placeholder->getContentSize().width));
        placeholder->setTag(201);
        previewHost->addChildAtPosition(placeholder, Anchor::Center);
    }

    // ccmenu ignores anchor: position is the menu centre, children are offsets. no addchildatposition (anchorlayout piles items).
    auto* menu = CCMenu::create();
    if (!menu) return card;
    menu->setPosition({kCardW * 0.5f, kCardH * 0.5f});

    // local y of the card's bottom edge, relative to the menu centre.
    constexpr float kBottom = -kCardH * 0.5f;

    if (auto* applySpr = ButtonSprite::create(SlotStore::get().activeSlotId() == id ? "Disable" : "Apply", "goldFont.fnt", "GJ_button_01.png", 0.42f)) {
        if (auto* applyBtn = CCMenuItemExt::createSpriteExtra(applySpr,
                [this, id](CCMenuItemSpriteExtra*) { if (m_onApply) m_onApply(id); })) {
            applyBtn->setPosition({0.f, kBottom + 50.f});
            menu->addChild(applyBtn);
        }
    }

    auto makeMini = [&](char const* label, char const* sprite,
                        std::function<void()> action) -> CCMenuItemSpriteExtra* {
        auto* spr = ButtonSprite::create(label, "bigFont.fnt", sprite, 0.34f);
        if (!spr) return nullptr;
        return CCMenuItemExt::createSpriteExtra(spr,
            [action = std::move(action)](CCMenuItemSpriteExtra*) { if (action) action(); });
    };

    // bottom side-by-side; ~100px apart so sprites don't touch.
    if (auto* editBtn = makeMini("Edit", "GJ_button_04.png",
            [this, id]() { if (m_onEdit) m_onEdit(id); })) {
        editBtn->setPosition({-50.f, kBottom + 18.f});
        menu->addChild(editBtn);
    }
    if (auto* delBtn = makeMini("Delete", "GJ_button_06.png",
            [this, id]() { if (m_onDelete) m_onDelete(id); })) {
        delBtn->setPosition({50.f, kBottom + 18.f});
        menu->addChild(delBtn);
    }

    card->addChild(menu);
    return card;
}

void SlotsGridView::requestThumbnails(
    std::vector<std::pair<int, TextureProject>> jobs, int generation) {
    if (!m_contentLayer || generation != m_thumbnailGeneration->load()) return;
    for (auto const& [cardTag, project] : jobs) {
        auto* card = m_contentLayer->getChildByTag(cardTag);
        auto* host = card ? card->getChildByTag(200) : nullptr;
        if (!host) continue;
        host->removeAllChildren();
        ccColor3B colors[] = {project.color1, project.color2, project.colorGlow, project.colorDetail};
        for (int i = 0; i < 4; ++i) {
            auto* swatch = CCLayerColor::create({colors[i].r, colors[i].g, colors[i].b, 255}, 15.f, 15.f);
            swatch->setPosition({3.f + (i % 2) * 17.f, 3.f + (i / 2) * 17.f});
            host->addChild(swatch);
        }
    }
}

CCNode* SlotsGridView::makeNewPackCard() {
    auto* card = CCNode::create();
    if (!card) return nullptr;
    card->setContentSize({kCardW, kCardH});

    auto* menu = CCMenu::create();
    if (!menu) return card;
    menu->setContentSize({kCardW, kCardH});

    if (auto* bg = CCScale9Sprite::create("GJ_square01.png")) {
        bg->setContentSize({kCardW, kCardH});
        bg->setColor({34, 58, 38});
        if (auto* btn = CCMenuItemExt::createSpriteExtra(bg,
                [this](CCMenuItemSpriteExtra*) { if (m_onNewPack) m_onNewPack(); })) {
            menu->addChildAtPosition(btn, Anchor::Center);
        }
    }
    card->addChildAtPosition(menu, Anchor::Center);

    if (auto* plusLbl = CCLabelBMFont::create("+", "bigFont.fnt")) {
        plusLbl->setScale(1.2f);
        plusLbl->setColor({150, 220, 150});
        card->addChildAtPosition(plusLbl, Anchor::Center, {0.f, 14.f});
    }
    if (auto* tlbl = CCLabelBMFont::create("New Slot", "bigFont.fnt")) {
        tlbl->setScale(0.42f);
        tlbl->setColor({170, 230, 170});
        card->addChildAtPosition(tlbl, Anchor::Center, {0.f, -24.f});
    }

    return card;
}

}  // namespace paimon::texture_studio
