#include <Geode/ui/PopupManager.hpp>
#include "SearchHistoryPopup.hpp"
#include "../SearchHistory.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"

#include <ctime>

using namespace geode::prelude;

namespace sh = paimon::searchhistory;

static constexpr float POPUP_W = 320.f;
static constexpr float POPUP_H = 240.f;
static constexpr float ROW_H = 30.f;

// Icono de dificultad segun la entrada (nullptr = usar la lupa generica).
static const char* difficultyFrameFor(const sh::Entry& e) {
    if (e.type != 0 && e.type != 1) return nullptr;
    if (e.difficulties.empty()) return nullptr;
    switch (e.difficulties.front()) {
        case -1: return "difficulty_auto_btn_001.png";
        case -3: return "difficulty_00_btn_001.png";
        case -2:
            switch (e.demonFilter) {
                case 1: return "difficulty_07_btn_001.png"; // easy demon
                case 2: return "difficulty_08_btn_001.png"; // medium demon
                case 4: return "difficulty_09_btn_001.png"; // insane demon
                case 5: return "difficulty_10_btn_001.png"; // extreme demon
                default: return "difficulty_06_btn_001.png";
            }
        case 1: return "difficulty_01_btn_001.png";
        case 2: return "difficulty_02_btn_001.png";
        case 3: return "difficulty_03_btn_001.png";
        case 4: return "difficulty_04_btn_001.png";
        case 5: return "difficulty_05_btn_001.png";
        default: return nullptr;
    }
}

// Fecha relativa corta para la esquina de cada fila.
static std::string timeAgo(int64_t t) {
    if (t <= 0) return "";
    int64_t diff = std::time(nullptr) - t;
    if (diff < 0) diff = 0;
    int64_t days = diff / 86400;
    if (days < 1) return "Today";
    if (days < 7) return fmt::format("{}d ago", days);
    if (days < 30) return fmt::format("{}w ago", days / 7);
    return fmt::format("{}mo ago", days / 30);
}

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
    this->setTitle("Search History", "goldFont.fnt", 0.75f);

    // Relojitos flanqueando el titulo (mismo icono que el boton que abre esto).
    if (m_title) {
        float halfW = m_title->getScaledContentWidth() / 2.f;
        auto addTitleIcon = [this, halfW](float side) {
            if (auto icon = CCSprite::createWithSpriteFrameName("GJ_timeIcon_001.png")) {
                icon->setScale(0.55f);
                icon->setPosition(m_title->getPosition() + CCPoint{ side * (halfW + 14.f), 0.f });
                m_mainLayer->addChild(icon);
            }
        };
        addTitleIcon(-1.f);
        addTitleIcon(1.f);
    }

    float scrollW = POPUP_W - 44.f;
    float scrollH = POPUP_H - 88.f;

    // Fondo oscuro detras de la lista.
    auto listBg = CCScale9Sprite::create("square02b_001.png");
    listBg->setContentSize({ scrollW + 8.f, scrollH + 8.f });
    listBg->setColor({ 0, 0, 0 });
    listBg->setOpacity(90);
    m_mainLayer->addChildAtPosition(listBg, Anchor::Center, { 0.f, 4.f });

    m_scroll = ScrollLayer::create({ scrollW, scrollH });
    m_scroll->setID("history-scroll"_spr);
    m_mainLayer->addChildAtPosition(m_scroll, Anchor::Center, { -scrollW / 2.f, -scrollH / 2.f + 4.f });

    // Borde decorativo alrededor del scroll.
    auto borders = geode::ListBorders::create();
    borders->setContentSize({ scrollW + 6.f, scrollH });
    m_mainLayer->addChildAtPosition(borders, Anchor::Center, { 0.f, 4.f });

    // Contador de busquedas guardadas (abajo a la izquierda).
    m_countLabel = CCLabelBMFont::create("", "goldFont.fnt");
    m_countLabel->setScale(0.4f);
    m_countLabel->setAnchorPoint({ 0.f, 0.5f });
    m_countLabel->setOpacity(190);
    m_mainLayer->addChildAtPosition(m_countLabel, Anchor::BottomLeft, { 26.f, 22.f });

    // Boton Clear (abajo).
    auto clearSpr = ButtonSprite::create("Clear", "bigFont.fnt", "GJ_button_06.png", 0.8f);
    clearSpr->setScale(0.55f);
    auto clearBtn = CCMenuItemSpriteExtra::create(clearSpr, this, menu_selector(SearchHistoryPopup::onClear));
    clearBtn->setID("clear-button"_spr);
    m_buttonMenu->addChildAtPosition(clearBtn, Anchor::Bottom, { 0.f, 21.f });

    this->rebuild();
    return true;
}

void SearchHistoryPopup::rebuild() {
    auto content = m_scroll->m_contentLayer;
    content->removeAllChildren();

    auto& hist = sh::history;
    float w = m_scroll->getContentWidth();
    float viewH = m_scroll->getContentHeight();

    m_countLabel->setString(
        hist.empty() ? "" : fmt::format("{} saved", hist.size()).c_str());

    if (hist.empty()) {
        content->setContentHeight(viewH);
        if (auto icon = CCSprite::createWithSpriteFrameName("gj_findBtn_001.png")) {
            icon->setScale(1.1f);
            icon->setOpacity(90);
            icon->setPosition({ w / 2.f, viewH / 2.f + 22.f });
            content->addChild(icon);
        }
        auto lbl = CCLabelBMFont::create("No search history yet", "bigFont.fnt");
        lbl->setScale(0.45f);
        lbl->setOpacity(140);
        lbl->setPosition({ w / 2.f, viewH / 2.f - 14.f });
        content->addChild(lbl);
        auto hint = CCLabelBMFont::create("Your searches will show up here", "chatFont.fnt");
        hint->setScale(0.5f);
        hint->setOpacity(110);
        hint->setPosition({ w / 2.f, viewH / 2.f - 30.f });
        content->addChild(hint);
        m_scroll->scrollToTop();
        return;
    }

    float totalH = std::max(ROW_H * hist.size(), viewH);
    content->setContentHeight(totalH);

    float rowW = w - 26.f; // deja hueco a la derecha para la X

    for (int i = 0; i < (int)hist.size(); i++) {
        auto& e = hist[i];
        float bottom = totalH - ROW_H * (i + 1);

        auto menu = CCMenu::create();
        menu->setPosition({ 0.f, bottom });
        menu->setContentSize({ w, ROW_H });
        content->addChild(menu);

        // Fondo redondeado de la fila; toda la fila es el boton de buscar.
        auto rowBg = CCScale9Sprite::create("square02b_001.png");
        rowBg->setContentSize({ rowW, ROW_H - 4.f });
        rowBg->setColor({ 0, 0, 0 });
        rowBg->setOpacity((i % 2) ? 50 : 85);

        // Icono: dificultad de la busqueda, o lupa si no aplica.
        CCSprite* icon = nullptr;
        if (auto frame = difficultyFrameFor(e)) {
            icon = CCSprite::createWithSpriteFrameName(frame);
        }
        if (!icon) icon = CCSprite::createWithSpriteFrameName("gj_findBtn_001.png");
        if (icon) {
            float h = icon->getContentSize().height;
            if (h > 0.f) icon->setScale(20.f / h);
            icon->setPosition({ 16.f, (ROW_H - 4.f) / 2.f });
            rowBg->addChild(icon);
        }

        // Query (o placeholder).
        std::string primary = e.query.empty()
            ? (e.type == 2 ? "(any user)" : "(no query)")
            : e.query;
        auto title = CCLabelBMFont::create(primary.c_str(), "bigFont.fnt");
        title->setAnchorPoint({ 0.f, 0.5f });
        title->limitLabelWidth(rowW - 90.f, 0.42f, 0.2f);
        title->setPosition({ 32.f, (ROW_H - 4.f) * 0.68f });
        rowBg->addChild(title);

        // Resumen de filtros.
        auto sub = CCLabelBMFont::create(e.summary().c_str(), "chatFont.fnt");
        sub->setAnchorPoint({ 0.f, 0.5f });
        sub->setColor({ 255, 218, 150 });
        sub->setOpacity(200);
        sub->limitLabelWidth(rowW - 90.f, 0.45f, 0.2f);
        sub->setPosition({ 32.f, (ROW_H - 4.f) * 0.28f });
        rowBg->addChild(sub);

        // Fecha relativa (arriba a la derecha de la fila).
        auto when = CCLabelBMFont::create(timeAgo(e.time).c_str(), "chatFont.fnt");
        when->setAnchorPoint({ 1.f, 0.5f });
        when->setScale(0.4f);
        when->setColor({ 160, 160, 160 });
        when->setPosition({ rowW - 6.f, (ROW_H - 4.f) * 0.68f });
        rowBg->addChild(when);

        auto rowBtn = CCMenuItemSpriteExtra::create(rowBg, this, menu_selector(SearchHistoryPopup::onSearchEntry));
        rowBtn->setTag(i);
        rowBtn->m_scaleMultiplier = 1.04f;
        rowBtn->setPosition({ rowW / 2.f, ROW_H / 2.f });
        menu->addChild(rowBtn);

        // Boton eliminar, fuera de la fila para no chocar con el boton grande.
        auto delSpr = CCSprite::createWithSpriteFrameName("GJ_deleteIcon_001.png");
        CCNode* delIcon = delSpr ? (CCNode*)delSpr : (CCNode*)ButtonSprite::create("X");
        delIcon->setScale(delSpr ? 0.55f : 0.35f);
        auto delBtn = CCMenuItemSpriteExtra::create(delIcon, this, menu_selector(SearchHistoryPopup::onRemoveEntry));
        delBtn->setTag(i);
        delBtn->setPosition({ w - 11.f, ROW_H / 2.f });
        menu->addChild(delBtn);
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
    PopupManager::get().quickPopup(
        "Clear History",
        "Are you sure you want to <cr>clear</c> your entire search history?",
        "Cancel", "Clear",
        [this](auto, bool confirmed) {
            if (!confirmed) return;
            sh::clear();
            this->rebuild();
        }
    ).showInstant();
}
