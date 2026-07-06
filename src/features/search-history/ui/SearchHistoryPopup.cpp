#include "SearchHistoryPopup.hpp"
#include "../SearchHistory.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"

using namespace geode::prelude;

namespace sh = paimon::searchhistory;

static constexpr float POPUP_W = 360.f;
static constexpr float POPUP_H = 280.f;
static constexpr float ROW_H = 34.f;

SearchHistoryPopup* SearchHistoryPopup::create(std::function<void(int)> callback) {
    auto ret = new SearchHistoryPopup();
    if (ret && ret->init(std::move(callback))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool SearchHistoryPopup::init(std::function<void(int)> callback) {
    if (!Popup::init(POPUP_W, POPUP_H)) return false;
    paimon::markDynamicPopup(this);
    m_callback = std::move(callback);
    this->setTitle("Search History");

    float scrollW = POPUP_W - 40.f;
    float scrollH = POPUP_H - 80.f;
    m_scroll = ScrollLayer::create({ scrollW, scrollH });
    m_scroll->setID("history-scroll"_spr);
    m_mainLayer->addChildAtPosition(m_scroll, Anchor::Center, { -scrollW / 2.f, -scrollH / 2.f + 8.f });

    // borde decorativo alrededor del scroll
    auto borders = geode::ListBorders::create();
    borders->setContentSize({ scrollW + 6.f, scrollH });
    m_mainLayer->addChildAtPosition(borders, Anchor::Center, { 0.f, 8.f });

    // boton Clear (abajo)
    auto clearSpr = ButtonSprite::create("Clear", "bigFont.fnt", "GJ_button_06.png", 0.8f);
    clearSpr->setScale(0.6f);
    auto clearBtn = CCMenuItemSpriteExtra::create(clearSpr, this, menu_selector(SearchHistoryPopup::onClear));
    clearBtn->setID("clear-button"_spr);
    m_buttonMenu->addChildAtPosition(clearBtn, Anchor::Bottom, { 0.f, 22.f });

    this->rebuild();
    return true;
}

void SearchHistoryPopup::rebuild() {
    auto content = m_scroll->m_contentLayer;
    content->removeAllChildren();

    auto& hist = sh::history;
    float w = m_scroll->getContentWidth();
    float viewH = m_scroll->getContentHeight();

    if (hist.empty()) {
        content->setContentHeight(viewH);
        auto lbl = CCLabelBMFont::create("No search history yet", "bigFont.fnt");
        lbl->setScale(0.5f);
        lbl->setOpacity(140);
        lbl->setPosition({ w / 2.f, viewH / 2.f });
        content->addChild(lbl);
        m_scroll->scrollToTop();
        return;
    }

    float totalH = std::max(ROW_H * hist.size(), viewH);
    content->setContentHeight(totalH);

    for (int i = 0; i < (int)hist.size(); i++) {
        auto& e = hist[i];
        float bottom = totalH - ROW_H * (i + 1);

        auto row = CCLayerColor::create(
            (i % 2) ? ccColor4B{ 0, 0, 0, 40 } : ccColor4B{ 0, 0, 0, 75 }, w, ROW_H);
        row->setPosition({ 0.f, bottom });
        content->addChild(row);

        auto menu = CCMenu::create();
        menu->setPosition({ 0.f, 0.f });
        menu->setContentSize({ w, ROW_H });
        row->addChild(menu);

        // Boton "buscar de nuevo"
        auto findSpr = CCSprite::createWithSpriteFrameName("gj_findBtn_001.png");
        CCNode* findIcon = findSpr;
        if (!findSpr) findIcon = ButtonSprite::create("Go", "bigFont.fnt", "GJ_button_01.png", 0.8f);
        findIcon->setScale(findSpr ? 0.6f : 0.4f);
        auto findBtn = CCMenuItemSpriteExtra::create(findIcon, this, menu_selector(SearchHistoryPopup::onSearchEntry));
        findBtn->setTag(i);
        findBtn->setPosition({ 18.f, ROW_H / 2.f });
        menu->addChild(findBtn);

        // Boton eliminar
        auto delSpr = CCSprite::createWithSpriteFrameName("GJ_deleteIcon_001.png");
        CCNode* delIcon = delSpr ? (CCNode*)delSpr : (CCNode*)ButtonSprite::create("X");
        delIcon->setScale(delSpr ? 0.6f : 0.4f);
        auto delBtn = CCMenuItemSpriteExtra::create(delIcon, this, menu_selector(SearchHistoryPopup::onRemoveEntry));
        delBtn->setTag(i);
        delBtn->setPosition({ w - 18.f, ROW_H / 2.f });
        menu->addChild(delBtn);

        // Texto principal: la query (o un placeholder)
        std::string primary = e.query.empty()
            ? (e.type == 2 ? "(any user)" : "(no query)")
            : e.query;
        auto title = CCLabelBMFont::create(primary.c_str(), "bigFont.fnt");
        title->setAnchorPoint({ 0.f, 0.5f });
        title->setScale(0.5f);
        title->limitLabelWidth(w - 90.f, 0.5f, 0.2f);
        title->setPosition({ 38.f, ROW_H * 0.68f });
        row->addChild(title);

        // Subtitulo: resumen de filtros
        auto sub = CCLabelBMFont::create(e.summary().c_str(), "chatFont.fnt");
        sub->setAnchorPoint({ 0.f, 0.5f });
        sub->setScale(0.5f);
        sub->setColor({ 170, 170, 170 });
        sub->limitLabelWidth(w - 90.f, 0.5f, 0.2f);
        sub->setPosition({ 38.f, ROW_H * 0.28f });
        row->addChild(sub);
    }

    m_scroll->scrollToTop();
}

void SearchHistoryPopup::onSearchEntry(CCObject* sender) {
    int index = sender->getTag();
    auto cb = m_callback;
    this->onClose(nullptr);
    if (cb) cb(index);
}

void SearchHistoryPopup::onRemoveEntry(CCObject* sender) {
    sh::remove(sender->getTag());
    this->rebuild();
}

void SearchHistoryPopup::onClear(CCObject*) {
    if (sh::history.empty()) return;
    geode::createQuickPopup(
        "Clear History",
        "Are you sure you want to <cr>clear</c> your entire search history?",
        "Cancel", "Clear",
        [this](auto, bool confirmed) {
            if (!confirmed) return;
            sh::clear();
            this->rebuild();
        }
    );
}
