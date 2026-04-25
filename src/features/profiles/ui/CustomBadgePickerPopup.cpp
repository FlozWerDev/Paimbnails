#include "CustomBadgePickerPopup.hpp"
#include "../services/CustomBadgeService.hpp"
#include "../../../features/emotes/services/EmoteService.hpp"
#include "../../../features/emotes/services/EmoteCache.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include <Geode/loader/Log.hpp>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::emotes;

// Constantes de layout
static constexpr float POP_W     = 340.f;
static constexpr float POP_H     = 260.f;
static constexpr float CELL_SIZE = 34.f;
static constexpr float CELL_GAP  = 4.f;

// Paleta de colores
static constexpr ccColor4F COL_DARK_INTERIOR   = {0.00f, 0.00f, 0.03f, 0.93f};
// Fondo del scroll
static constexpr ccColor4F COL_SCROLL_BG       = {0.00f, 0.00f, 0.00f, 0.45f};
// Celda normal
static constexpr ccColor4F COL_CELL_BG         = {0.00f, 0.00f, 0.00f, 0.65f};
static constexpr ccColor4F COL_CELL_BORDER     = {0.26f, 0.26f, 0.26f, 0.72f};
// Celda seleccionada
static constexpr ccColor4F COL_CELL_SEL_BG     = {0.18f, 0.13f, 0.00f, 0.78f};
static constexpr ccColor4F COL_CELL_SEL_BORDER = {1.00f, 0.82f, 0.00f, 0.90f};
// Separador
static constexpr ccColor4F COL_SEPARATOR       = {0.30f, 0.30f, 0.30f, 0.38f};

// ─────────────────────────────────────────────────────────────────────────────

CustomBadgePickerPopup* CustomBadgePickerPopup::create(
        int accountID, std::string const& currentBadge) {
    auto ret = new CustomBadgePickerPopup();
    if (ret && ret->init(accountID, currentBadge)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool CustomBadgePickerPopup::init(int accountID, std::string const& currentBadge) {
    if (!Popup::init(POP_W, POP_H)) return false;

    m_accountID    = accountID;
    m_currentBadge = currentBadge;

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    // Fondo oscuro sobre el popup original
    auto darkBg = paimon::SpriteHelper::createRoundedRect(
        content.width  - 8.f,
        content.height - 8.f,
        4.f, COL_DARK_INTERIOR);
    darkBg->setPosition({4.f, 4.f});
    m_mainLayer->addChild(darkBg, 0);   // above m_bgSprite (-1), below everything else

    // ── Title ─────────────────────────────────────────────────────────────────
    this->setTitle("Custom Badge");

    // Separador debajo del titulo
    auto titleSep = paimon::SpriteHelper::createRoundedRect(
        content.width - 28.f, 1.f, 0.5f, COL_SEPARATOR);
    titleSep->setPosition({14.f, content.height - 48.f});
    m_mainLayer->addChild(titleSep, 1);

    // Badge actual
    {
        std::string txt = currentBadge.empty()
            ? "Current: none"
            : ("Current: :" + currentBadge + ":");
        m_currentLabel = CCLabelBMFont::create(txt.c_str(), "bigFont.fnt");
        m_currentLabel->setScale(0.28f);
        m_currentLabel->setColor({210, 210, 210});
        m_currentLabel->setPosition({cx, content.height - 40.f});
        m_mainLayer->addChild(m_currentLabel, 2);
    }

    // Estado y mensajes
    m_statusLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_statusLabel->setScale(0.27f);
    m_statusLabel->setPosition({cx, 24.f});
    m_statusLabel->setColor({255, 210, 60});
    m_mainLayer->addChild(m_statusLabel, 2);

    // Menu inferior
    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    m_mainLayer->addChild(menu, 3);

    {
        auto spr = ButtonSprite::create(
            "Clear Badge", "bigFont.fnt", "GJ_button_06.png", 0.6f);
        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(CustomBadgePickerPopup::onClearBadge));
        btn->setPosition({cx, 24.f});
        btn->setID("clear-badge-btn"_spr);
        menu->addChild(btn);
    }

    // Area de scroll
    float gridBot = 44.f;
    float gridTop = content.height - 54.f;
    float gridH   = gridTop - gridBot;
    float gridW   = content.width - 16.f;

    // Fondo oscuro del grid
    auto scrollBg = paimon::SpriteHelper::createRoundedRect(
        gridW, gridH, 6.f, COL_SCROLL_BG);
    scrollBg->setPosition({8.f, gridBot});
    m_mainLayer->addChild(scrollBg, 1);

    m_scroll = geode::ScrollLayer::create({gridW, gridH});
    m_scroll->setPosition({8.f, gridBot});
    m_mainLayer->addChild(m_scroll, 2);

    m_contentNode = CCNode::create();
    m_contentNode->setContentSize({gridW, gridH});
    m_scroll->m_contentLayer->setContentSize({gridW, gridH});
    m_scroll->m_contentLayer->addChild(m_contentNode);

    // Carga inicial de emotes
    auto& service = EmoteService::get();
    if (service.isLoaded()) {
        buildEmoteGrid(service.getStaticEmotes());
    } else {
        m_statusLabel->setString("Loading...");
        WeakRef<CustomBadgePickerPopup> safeRef = this;
        service.fetchAllEmotes([safeRef](bool) {
            Loader::get()->queueInMainThread([safeRef]() {
                auto self = safeRef.lock();
                if (!self || !self->getParent()) return;
                self->m_statusLabel->setString("");
                self->buildEmoteGrid(EmoteService::get().getStaticEmotes());
            });
        });
    }

    this->setZOrder(10600);
    this->setID("custom-badge-picker-popup"_spr);
    paimon::markDynamicPopup(this);
    return true;
}

// buildEmoteGrid: crea el grid de emotes
void CustomBadgePickerPopup::buildEmoteGrid(std::vector<EmoteInfo> const& emotes) {
    m_contentNode->removeAllChildren();

    if (emotes.empty()) {
        m_statusLabel->setString("No emotes available");
        return;
    }

    float gridW    = m_scroll->getContentSize().width;
    int   cols     = std::max(1, (int)((gridW + CELL_GAP) / (CELL_SIZE + CELL_GAP)));
    int   rows     = ((int)emotes.size() + cols - 1) / cols;
    float contentH = (float)rows * (CELL_SIZE + CELL_GAP);
    float scrollH  = m_scroll->getContentSize().height;
    float totalH   = std::max(contentH, scrollH);

    m_contentNode->setContentSize({gridW, totalH});
    m_scroll->m_contentLayer->setContentSize({gridW, totalH});

    auto gridMenu = CCMenu::create();
    gridMenu->setPosition({0, 0});
    gridMenu->setContentSize({gridW, totalH});
    m_contentNode->addChild(gridMenu);

    for (size_t i = 0; i < emotes.size(); ++i) {
        int   col = (int)(i % (size_t)cols);
        int   row = (int)(i / (size_t)cols);
        float x   = (float)col * (CELL_SIZE + CELL_GAP) + CELL_SIZE / 2.f;
        float y   = totalH - ((float)row * (CELL_SIZE + CELL_GAP) + CELL_SIZE / 2.f);

        bool isSelected = (emotes[i].name == m_currentBadge);

        // Contenedor de celda
        auto container = CCNode::create();
        container->setContentSize({CELL_SIZE, CELL_SIZE});

        // Borde
        ccColor4F borderCol = isSelected ? COL_CELL_SEL_BORDER : COL_CELL_BORDER;
        auto cellBorder = paimon::SpriteHelper::createRoundedRect(
            CELL_SIZE, CELL_SIZE, 5.f, borderCol);
        cellBorder->setPosition({0.f, 0.f});
        container->addChild(cellBorder, 0);

        // Relleno interior
        ccColor4F bgCol = isSelected ? COL_CELL_SEL_BG : COL_CELL_BG;
        auto cellBg = paimon::SpriteHelper::createRoundedRect(
            CELL_SIZE - 2.f, CELL_SIZE - 2.f, 4.f, bgCol);
        cellBg->setPosition({1.f, 1.f});
        container->addChild(cellBg, 1);

        // Placeholder de carga
        auto ph = CCLabelBMFont::create("...", "chatFont.fnt");
        ph->setScale(0.28f);
        ph->setPosition({CELL_SIZE / 2.f, CELL_SIZE / 2.f});
        ph->setTag(99);
        container->addChild(ph, 2);

        // Boton
        auto btn = CCMenuItemSpriteExtra::create(
            container, this,
            menu_selector(CustomBadgePickerPopup::onEmoteSelected));
        btn->setPosition({x, y});
        btn->setTag((int)i);
        btn->setSizeMult(0.97f);
        gridMenu->addChild(btn);

        // Carga la textura asincronamente
        EmoteInfo emoteCopy = emotes[i];
        Ref<CCMenuItemSpriteExtra> btnRef = btn;

        EmoteCache::get().loadEmote(emoteCopy,
            [btnRef](CCTexture2D* tex, bool /*isGif*/,
                     std::vector<uint8_t> const& /*gifData*/) {
                Loader::get()->queueInMainThread([btnRef, tex]() {
                    if (!btnRef || !btnRef->getParent()) return;
                    auto* cont = btnRef->getNormalImage();
                    if (!cont) return;

                    if (tex) {
                        auto* spr = CCSprite::createWithTexture(tex);
                        float maxD = std::max(
                            spr->getContentSize().width,
                            spr->getContentSize().height);
                        if (maxD > 0.f)
                            spr->setScale((CELL_SIZE - 8.f) / maxD);
                        spr->setPosition({CELL_SIZE / 2.f, CELL_SIZE / 2.f});
                        cont->addChild(spr, 3);
                    }

                    if (auto* p = cont->getChildByTag(99))
                        p->setVisible(false);
                });
            });
    }

    m_scroll->moveToTop();
}

// ─────────────────────────────────────────────────────────────────────────────
// Callbacks
// ─────────────────────────────────────────────────────────────────────────────

void CustomBadgePickerPopup::onEmoteSelected(CCObject* sender) {
    auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;

    auto emotes = EmoteService::get().getStaticEmotes();
    int  idx    = btn->getTag();
    if (idx < 0 || idx >= (int)emotes.size()) return;

    onSaveBadge(emotes[idx].name);
}

void CustomBadgePickerPopup::onClearBadge(CCObject*) {
    onSaveBadge("");
}

void CustomBadgePickerPopup::onSaveBadge(std::string const& emoteName) {
    m_statusLabel->setString("Saving...");
    int accountID = m_accountID;

    if (emoteName.empty()) {
        Ref<CustomBadgePickerPopup> self = this;
        CustomBadgeService::get().clearBadge(accountID,
            [self](bool success, std::string const& msg) {
                if (!self->getParent()) return;
                if (success) {
                    self->m_currentBadge = "";
                    self->m_currentLabel->setString("Current: none");
                    self->m_statusLabel->setString("Badge cleared!");
                    if (self->m_onSelect) self->m_onSelect("");
                    self->buildEmoteGrid(EmoteService::get().getStaticEmotes());
                } else {
                    self->m_statusLabel->setString("Error!");
                    log::warn("[CustomBadge] clear failed: {}", msg);
                }
            });
    } else {
        Ref<CustomBadgePickerPopup> self = this;
        CustomBadgeService::get().setBadge(accountID, emoteName,
            [self, emoteName](bool success, std::string const& msg) {
                if (!self->getParent()) return;
                if (success) {
                    self->m_currentBadge = emoteName;
                    self->m_currentLabel->setString(
                        ("Current: :" + emoteName + ":").c_str());
                    self->m_statusLabel->setString("Saved!");
                    if (self->m_onSelect) self->m_onSelect(emoteName);
                    self->buildEmoteGrid(EmoteService::get().getStaticEmotes());
                } else {
                    self->m_statusLabel->setString("Error saving!");
                    log::warn("[CustomBadge] set failed: {}", msg);
                }
            });
    }
}