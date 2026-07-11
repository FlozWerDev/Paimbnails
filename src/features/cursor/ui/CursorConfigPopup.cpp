#include "CursorConfigPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../services/CursorManager.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/InfoButton.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/PopupManager.hpp>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
namespace kit = paimon::configkit;
}


// create

CursorConfigPopup* CursorConfigPopup::create() {
    auto ret = new CursorConfigPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

// init

bool CursorConfigPopup::init() {
    if (!Popup::init(480.f, 310.f)) return false;

    this->setTitle("Cursor Personalizado");
    this->setMouseEnabled(true);

    auto content = m_mainLayer->getContentSize();

    // cleanup invalid images on open
    int cleaned = CursorManager::get().cleanupInvalidImages();
    if (cleaned > 0) {
        log::info("[CursorConfig] Cleaned up {} invalid image files from gallery", cleaned);
    }

    // tab layers
    m_galleryTab = CCNode::create();
    m_galleryTab->setID("cursor-gallery-tab"_spr);
    m_galleryTab->setContentSize(content);
    m_mainLayer->addChild(m_galleryTab, 5);

    m_settingsTab = CCNode::create();
    m_settingsTab->setID("cursor-settings-tab"_spr);
    m_settingsTab->setContentSize(content);
    m_settingsTab->setVisible(false);
    m_mainLayer->addChild(m_settingsTab, 5);

    m_advancedTab = CCNode::create();
    m_advancedTab->setID("cursor-advanced-tab"_spr);
    m_advancedTab->setContentSize(content);
    m_advancedTab->setVisible(false);
    m_mainLayer->addChild(m_advancedTab, 5);

    createTabButtons();
    buildGalleryTab();
    buildSettingsTab();
    buildAdvancedTab();

    // Updater de scroll suave (rueda del raton con glide).
    this->schedule(schedule_selector(CursorConfigPopup::updateSmoothScroll));

    paimon::markDynamicPopup(this);
    return true;
}

void CursorConfigPopup::onExit() {
    this->unschedule(schedule_selector(CursorConfigPopup::checkScrollPosition));
    this->unschedule(schedule_selector(CursorConfigPopup::updateSmoothScroll));
    if (m_scrollArrow) {
        m_scrollArrow->stopAllActions();
    }
    Popup::onExit();
}

void CursorConfigPopup::scrollWheel(float x, float y) {
    if (m_currentTab == 2) {
        kit::queueWheelScroll(m_advancedScroll, x, y, m_advancedScrollTargetY, m_advancedScrollTargetSet);
        return;
    }
    if (m_currentTab == 1) {
        kit::queueWheelScroll(m_scrollLayer, x, y, m_settingsScrollTargetY, m_settingsScrollTargetSet);
        return;
    }
    if (m_currentTab == 0) {
        kit::queueWheelScroll(m_thumbScroll, x, y, m_thumbScrollTargetY, m_thumbScrollTargetSet);
        return;
    }
}

void CursorConfigPopup::updateSmoothScroll(float dt) {
    kit::stepWheelScroll(m_thumbScroll, m_thumbScrollTargetY, m_thumbScrollTargetSet, dt);
    kit::stepWheelScroll(m_scrollLayer, m_settingsScrollTargetY, m_settingsScrollTargetSet, dt);
    kit::stepWheelScroll(m_advancedScroll, m_advancedScrollTargetY, m_advancedScrollTargetSet, dt);
}

// tabs

void CursorConfigPopup::createTabButtons() {
    auto content = m_mainLayer->getContentSize();

    auto menu = CCMenu::create();
    menu->setID("cursor-tab-buttons-menu"_spr);
    menu->setContentSize({content.width, 30.f});
    menu->setLayout(
        RowLayout::create()
            ->setGap(12.f)
            ->setAxisAlignment(AxisAlignment::Center)
    );
    m_mainLayer->addChildAtPosition(menu, Anchor::Top, {0.f, -38.f});
    menu->setZOrder(10);

    auto spr1 = ButtonSprite::create("Galeria");
    spr1->setScale(0.5f);
    auto tab1 = CCMenuItemSpriteExtra::create(spr1, this, menu_selector(CursorConfigPopup::onTabSwitch));
    tab1->setTag(0);
    tab1->setID("cursor-gallery-tab-btn"_spr);
    menu->addChild(tab1);
    m_tabs.push_back(tab1);

    auto spr2 = ButtonSprite::create("Ajustes");
    spr2->setScale(0.5f);
    auto tab2 = CCMenuItemSpriteExtra::create(spr2, this, menu_selector(CursorConfigPopup::onTabSwitch));
    tab2->setTag(1);
    tab2->setID("cursor-settings-tab-btn"_spr);
    menu->addChild(tab2);
    m_tabs.push_back(tab2);

    auto spr3 = ButtonSprite::create("Avanzado");
    spr3->setScale(0.5f);
    auto tab3 = CCMenuItemSpriteExtra::create(spr3, this, menu_selector(CursorConfigPopup::onTabSwitch));
    tab3->setTag(2);
    tab3->setID("cursor-advanced-tab-btn"_spr);
    menu->addChild(tab3);
    m_tabs.push_back(tab3);

    menu->updateLayout();
    onTabSwitch(tab1);
}

void CursorConfigPopup::onTabSwitch(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    m_currentTab = btn->getTag();

    m_galleryTab->setVisible(m_currentTab == 0);
    m_settingsTab->setVisible(m_currentTab == 1);
    if (m_advancedTab) m_advancedTab->setVisible(m_currentTab == 2);

    for (auto* tab : m_tabs) {
        auto spr = typeinfo_cast<ButtonSprite*>(tab->getNormalImage());
        if (!spr) continue;
        if (tab->getTag() == m_currentTab) {
            spr->setColor({0, 255, 0});
            spr->setOpacity(255);
        } else {
            spr->setColor({255, 255, 255});
            spr->setOpacity(150);
        }
    }
}

// gallery tab

char const* CursorConfigPopup::slotDisplayName(CursorState state) {
    switch (state) {
        case CursorState::Move:     return "Mover";
        case CursorState::Hover:    return "Boton";
        case CursorState::Click:    return "Click";
        case CursorState::Text:     return "Texto";
        case CursorState::Disabled: return "Bloqueado";
        case CursorState::Idle:
        default:                    return "Normal";
    }
}

std::string CursorConfigPopup::currentPack() const {
    if (m_currentPackIdx < 0 || m_currentPackIdx >= (int)m_packList.size()) return "";
    return m_packList[m_currentPackIdx];
}

// States info text shown by the "i" button
static std::string buildStatesInfo() {
    return
        "<cy>Estados del cursor</c> — asigna una imagen distinta a cada uno:\n\n"
        "<cg>Normal</c>: en reposo (la flecha por defecto).\n"
        "<cb>Mover</c>: mientras mueves el raton.\n"
        "<co>Boton</c>: encima de un boton clickeable.\n"
        "<cp>Click</c>: manteniendo el click izquierdo.\n"
        "<cl>Texto</c>: sobre un campo de texto.\n"
        "<cr>Bloqueado</c>: sobre un boton desactivado.\n\n"
        "Toca un estado y luego toca una imagen de abajo para asignarla.\n"
        "Todo estado sin imagen usa la de <cg>Normal</c>.";
}

void CursorConfigPopup::buildGalleryTab() {
    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    auto menu = CCMenu::create();
    menu->setID("cursor-gallery-fixed-menu"_spr);
    menu->setPosition({0, 0});
    m_galleryTab->addChild(menu, 10);

    // Paso 1: elige el estado (una sola fila compacta)
    auto stepLbl = CCLabelBMFont::create("1. Toca un estado   2. Toca una imagen para asignarla", "chatFont.fnt");
    stepLbl->setScale(0.48f);
    stepLbl->setColor(kit::kDescColor);
    stepLbl->setPosition({cx, content.height - 44.f});
    m_galleryTab->addChild(stepLbl);

    float slotSize = 30.f;
    float colStep  = 58.f;
    float slotRowY = content.height - 72.f;

    for (int i = 0; i < kSlotCount; ++i) {
        CursorState state = kSlotStates[i];
        float slotX = cx + (static_cast<float>(i) - 2.5f) * colStep;
        float slotY = slotRowY;

        auto bg = CCLayerColor::create(ccc4(80, 80, 80, 120), slotSize, slotSize);
        bg->setPosition({slotX - slotSize / 2.f, slotY - slotSize / 2.f});
        m_galleryTab->addChild(bg);
        m_slots[i].bg = bg;

        auto label = CCLabelBMFont::create(slotDisplayName(state), "bigFont.fnt");
        label->setScale(0.18f);
        label->setPosition({slotX, slotY - slotSize / 2.f - 5.f});
        m_galleryTab->addChild(label);
        m_slots[i].label = label;

        auto area = CCSprite::create();
        area->setContentSize({slotSize, slotSize});
        area->setOpacity(0);
        auto btn = CCMenuItemSpriteExtra::create(area, this, menu_selector(CursorConfigPopup::onActivateSlot));
        btn->setContentSize({slotSize, slotSize});
        btn->setPosition({slotX, slotY});
        btn->setTag(static_cast<int>(state));
        menu->addChild(btn);
    }

    // "i" info button para explicar los estados (arriba a la derecha del grid)
    if (auto* iBtn = PaimonInfo::createInfoBtn("Estados del Cursor", buildStatesInfo(), this, 0.5f)) {
        iBtn->setPosition({content.width - 24.f, content.height - 30.f});
        menu->addChild(iBtn);
    }

    // barra de pack (prev / nombre / next + borrar pack)
    float packY = slotRowY - slotSize / 2.f - 22.f;

    auto prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
    if (prevSpr) {
        prevSpr->setScale(0.5f);
        auto prevBtn = CCMenuItemSpriteExtra::create(prevSpr, this, menu_selector(CursorConfigPopup::onPackPrev));
        prevBtn->setPosition({cx - 120.f, packY});
        menu->addChild(prevBtn);
    }
    auto nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
    if (nextSpr) {
        nextSpr->setScale(0.5f);
        nextSpr->setFlipX(true);
        auto nextBtn = CCMenuItemSpriteExtra::create(nextSpr, this, menu_selector(CursorConfigPopup::onPackNext));
        nextBtn->setPosition({cx + 120.f, packY});
        menu->addChild(nextBtn);
    }

    m_packLabel = CCLabelBMFont::create("Imagenes sueltas", "bigFont.fnt");
    m_packLabel->setScale(0.4f);
    m_packLabel->setPosition({cx, packY});
    m_galleryTab->addChild(m_packLabel);

    // boton borrar pack (papelera, a la derecha del label)
    auto trashSpr = CCSprite::createWithSpriteFrameName("GJ_trashBtn_001.png");
    if (trashSpr) {
        trashSpr->setScale(0.45f);
        auto trashBtn = CCMenuItemSpriteExtra::create(trashSpr, this, menu_selector(CursorConfigPopup::onDeletePack));
        trashBtn->setPosition({cx + 155.f, packY});
        menu->addChild(trashBtn);
    }

    // scroll de miniaturas del pack actual
    float scrollW = content.width - 30.f;
    float scrollTop = packY - 18.f;
    float scrollBottom = 46.f;
    float scrollH = scrollTop - scrollBottom;

    m_thumbScroll = ScrollLayer::create({scrollW, scrollH});
    m_thumbScroll->setPosition({(content.width - scrollW) / 2.f, scrollBottom});
    m_galleryTab->addChild(m_thumbScroll, 1);
    // El contenido del grid (celdas con fondo + miniatura + botones) se crea y
    // se reconstruye por completo en refreshGallery, dentro del contentLayer
    // del ScrollLayer de Geode.

    m_emptyGalleryLabel = CCLabelBMFont::create(
        "Aun no hay cursores aqui.\nUsa + Anadir para importar imagenes, cursores .cur/.ani o un pack .zip.",
        "bigFont.fnt"
    );
    if (m_emptyGalleryLabel) {
        m_emptyGalleryLabel->setScale(0.28f);
        m_emptyGalleryLabel->setOpacity(170);
        m_emptyGalleryLabel->setAlignment(kCCTextAlignmentCenter);
        m_emptyGalleryLabel->setPosition({cx, scrollBottom + scrollH / 2.f});
        m_galleryTab->addChild(m_emptyGalleryLabel, 2);
    }

    // botones inferiores: + Anadir / Borrar todo
    auto addSpr = ButtonSprite::create("+ Anadir", "goldFont.fnt", "GJ_button_01.png", 0.7f);
    addSpr->setScale(0.5f);
    auto addBtn = CCMenuItemSpriteExtra::create(addSpr, this, menu_selector(CursorConfigPopup::onAddImage));
    addBtn->setPosition({cx - 55.f, 22.f});
    menu->addChild(addBtn);

    auto delAllSpr = ButtonSprite::create("Borrar todo", "goldFont.fnt", "GJ_button_06.png", 0.7f);
    delAllSpr->setScale(0.5f);
    auto delAllBtn = CCMenuItemSpriteExtra::create(delAllSpr, this, menu_selector(CursorConfigPopup::onDeleteAllImages));
    delAllBtn->setPosition({cx + 55.f, 22.f});
    menu->addChild(delAllBtn);

    refreshPackList();
    refreshGallery();
}

void CursorConfigPopup::refreshPackList() {
    auto& cm = CursorManager::get();
    m_packList.clear();
    m_packList.push_back("");                  // index 0 = sueltas
    for (auto const& p : cm.getPacks()) {
        m_packList.push_back(p);
    }
    // Si el ultimo import creo un pack, saltar a el.
    auto last = cm.lastImportedPack();
    if (!last.empty()) {
        for (int i = 0; i < (int)m_packList.size(); ++i) {
            if (m_packList[i] == last) { m_currentPackIdx = i; break; }
        }
    }
    if (m_currentPackIdx >= (int)m_packList.size()) m_currentPackIdx = 0;
}

void CursorConfigPopup::refreshGallery() {
    if (!m_thumbScroll || !m_thumbScroll->m_contentLayer) return;

    // Patron probado (igual que RadialConfigPopup)
    // El contentLayer del ScrollLayer de Geode es la unica fuente de verdad.
    // Each cell owns its visuals and controls. The wrapping RowLayout handles
    // grid placement while the ScrollLayer content node handles scrolling.
    auto* content = m_thumbScroll->m_contentLayer;
    content->removeAllChildren();

    auto& cm = CursorManager::get();
    std::string pack = currentPack();
    auto images = cm.getImagesInPack(pack);

    // Etiqueta del pack actual.
    if (m_packLabel) {
        if (pack.empty()) {
            m_packLabel->setString(fmt::format("Imagenes sueltas ({})", images.size()).c_str());
        } else {
            m_packLabel->setString(fmt::format("{} ({})", pack, images.size()).c_str());
        }
        float maxW = 200.f;
        float w = m_packLabel->getContentSize().width * 0.4f;
        m_packLabel->setScale(w > maxW ? 0.4f * maxW / w : 0.4f);
    }

    if (m_emptyGalleryLabel) m_emptyGalleryLabel->setVisible(images.empty());

    float scrollW = m_thumbScroll->getContentSize().width;
    float scrollViewH = m_thumbScroll->getContentSize().height;
    float cellSize = 46.f;
    float padding = 7.f;
    int cols = std::max(1, static_cast<int>((scrollW - padding) / (cellSize + padding)));
    int rows = (static_cast<int>(images.size()) + cols - 1) / cols;

    // El contentLayer debe medir AL MENOS el alto visible; si hay mas filas,
    // crece para permitir scroll. Su altura define el sistema de coordenadas
    // (origen abajo-izquierda), por eso la primera fila va arriba del todo.
    float gridH = std::max(scrollViewH, rows * (cellSize + padding) + padding);
    content->setContentSize({scrollW, gridH});

    auto grid = CCNode::create();
    grid->setContentSize({scrollW, gridH});
    grid->setLayout(
        RowLayout::create()
            ->setGap(padding)
            ->setGrowCrossAxis(true)
            ->setCrossAxisOverflow(false)
            ->setAutoScale(false)
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisAlignment(AxisAlignment::End)
    );
    content->addChild(grid);

    for (int i = 0; i < (int)images.size(); i++) {
        // Self-contained cell; the parent layout determines its grid position.
        auto cell = CCNode::create();
        cell->setContentSize({cellSize, cellSize});
        cell->setAnchorPoint({0.5f, 0.5f});
        grid->addChild(cell);

        // Color del fondo segun a que estados esta asignada la imagen.
        int assignedCount = 0;
        ccColor3B singleColor = ccc3(0, 200, 0);
        for (auto state : kSlotStates) {
            if (cm.imageForState(state) == images[i]) {
                assignedCount++;
                switch (state) {
                    case CursorState::Idle:     singleColor = ccc3(0, 200, 0);   break;
                    case CursorState::Move:     singleColor = ccc3(0, 120, 255); break;
                    case CursorState::Hover:    singleColor = ccc3(255, 140, 0); break;
                    case CursorState::Click:    singleColor = ccc3(200, 60, 255);break;
                    case CursorState::Text:     singleColor = ccc3(0, 220, 220); break;
                    case CursorState::Disabled: singleColor = ccc3(255, 70, 70); break;
                }
            }
        }
        ccColor3B bgColor = ccc3(50, 50, 50);
        GLubyte bgOpacity = 100;
        if (assignedCount > 1)      { bgColor = ccc3(255, 200, 0); bgOpacity = 190; }
        else if (assignedCount == 1){ bgColor = singleColor; bgOpacity = 190; }

        // fondo (createColorPanel ancla en (0,0))
        auto bg = paimon::SpriteHelper::createColorPanel(cellSize, cellSize, bgColor, bgOpacity);
        bg->setPosition({0.f, 0.f});
        cell->addChild(bg, 0);

        // miniatura centrada en la celda
        auto tex = cm.loadGalleryThumb(images[i]);
        if (tex) {
            if (auto thumbSpr = CCSprite::createWithTexture(tex)) {
                float maxDim = std::max(thumbSpr->getContentSize().width,
                                        thumbSpr->getContentSize().height);
                if (maxDim > 0) thumbSpr->setScale((cellSize - 6.f) / maxDim);
                thumbSpr->setPosition({cellSize / 2.f, cellSize / 2.f});
                cell->addChild(thumbSpr, 1);

                auto imgPath = cm.galleryDir() / images[i];
                if (ImageLoadHelper::isAnimatedImage(imgPath)) {
                    if (auto* gifLabel = CCLabelBMFont::create("GIF", "bigFont.fnt")) {
                        gifLabel->setScale(0.22f);
                        gifLabel->setOpacity(220);
                        gifLabel->setColor({255, 100, 100});
                        gifLabel->setPosition({cellSize - 8.f, 5.f});
                        cell->addChild(gifLabel, 2);
                    }
                }
            }
            tex->release();
        }

        // CCMenu propio de la celda (coordenadas locales a la celda).
        auto cellMenu = CCMenu::create();
        cellMenu->setPosition({0.f, 0.f});
        cellMenu->setContentSize({cellSize, cellSize});
        cell->addChild(cellMenu, 5);

        // area de seleccion (cubre toda la celda) — se anade primero
        auto selectArea = CCSprite::create();
        selectArea->setContentSize({cellSize, cellSize});
        selectArea->setOpacity(0);
        auto selectBtn = CCMenuItemSpriteExtra::create(selectArea, this, menu_selector(CursorConfigPopup::onSelectImage));
        selectBtn->setContentSize({cellSize, cellSize});
        selectBtn->setPosition({cellSize / 2.f, cellSize / 2.f});
        selectBtn->setUserObject(CCString::create(images[i]));
        cellMenu->addChild(selectBtn, 0);

        // boton borrar (X) en la esquina superior derecha — z mayor para que
        // gane el toque sobre el area de seleccion en su esquina.
        if (auto xSpr = CCSprite::createWithSpriteFrameName("GJ_deleteIcon_001.png")) {
            xSpr->setScale(0.32f);
            auto xHit = CCSprite::create();
            xHit->setContentSize({18.f, 18.f});
            xHit->setOpacity(0);
            xHit->addChild(xSpr);
            xSpr->setPosition({9.f, 9.f});
            auto xBtn = CCMenuItemSpriteExtra::create(xHit, this, menu_selector(CursorConfigPopup::onDeleteImage));
            xBtn->setPosition({cellSize - 7.f, cellSize - 7.f});
            xBtn->setUserObject(CCString::create(images[i]));
            cellMenu->addChild(xBtn, 10);
        }
    }

    grid->updateLayout();

    // Geode: scrollToTop() reposiciona el contentLayer correctamente al inicio
    // (moveToTop() es el metodo crudo de GD y deja la lista mal colocada).
    m_thumbScroll->scrollToTop();
    // Cancelar cualquier destino de scroll suave pendiente.
    m_thumbScrollTargetSet = false;
    updateSlotPreviews();
}

void CursorConfigPopup::updateSlotPreviews() {
    auto& cm = CursorManager::get();
    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;
    float colStep  = 58.f;
    float slotRowY = content.height - 72.f;
    float maxThumb = 24.f;

    auto stateColor = [](CursorState state) -> ccColor3B {
        switch (state) {
            case CursorState::Idle:     return ccc3(0, 200, 0);
            case CursorState::Move:     return ccc3(0, 120, 255);
            case CursorState::Hover:    return ccc3(255, 140, 0);
            case CursorState::Click:    return ccc3(200, 60, 255);
            case CursorState::Text:     return ccc3(0, 220, 220);
            case CursorState::Disabled: return ccc3(255, 70, 70);
        }
        return ccc3(0, 200, 0);
    };

    for (int i = 0; i < kSlotCount; ++i) {
        CursorState state = kSlotStates[i];
        auto& slot = m_slots[i];
        float slotX = cx + (static_cast<float>(i) - 2.5f) * colStep;
        float slotY = slotRowY;

        if (slot.preview) {
            slot.preview->removeFromParent();
            slot.preview = nullptr;
        }
        std::string filename = cm.imageForState(state);
        if (!filename.empty()) {
            auto tex = cm.loadGalleryThumb(filename);
            if (tex) {
                slot.preview = CCSprite::createWithTexture(tex);
                if (slot.preview) {
                    float maxDim = std::max(slot.preview->getContentSize().width,
                                            slot.preview->getContentSize().height);
                    if (maxDim > 0) slot.preview->setScale(maxThumb / maxDim);
                    slot.preview->setPosition({slotX, slotY});
                    m_galleryTab->addChild(slot.preview, 5);
                }
                tex->release();
            }
            if (slot.label) slot.label->setString(slotDisplayName(state));
        } else {
            if (slot.label) slot.label->setString(slotDisplayName(state));
        }

        if (slot.bg) {
            if (state == m_activeSlot) {
                slot.bg->setColor(stateColor(state));
                slot.bg->setOpacity(180);
            } else {
                slot.bg->setColor(ccc3(80, 80, 80));
                slot.bg->setOpacity(120);
            }
        }
    }
}

void CursorConfigPopup::onActivateSlot(CCObject* sender) {
    auto* btn = typeinfo_cast<CCNode*>(sender);
    if (!btn) return;
    m_activeSlot = static_cast<CursorState>(btn->getTag());
    updateSlotPreviews();
}

void CursorConfigPopup::onPackPrev(CCObject*) {
    if (m_packList.size() <= 1) return;
    m_currentPackIdx--;
    if (m_currentPackIdx < 0) m_currentPackIdx = (int)m_packList.size() - 1;
    refreshGallery();
}

void CursorConfigPopup::onPackNext(CCObject*) {
    if (m_packList.size() <= 1) return;
    m_currentPackIdx++;
    if (m_currentPackIdx >= (int)m_packList.size()) m_currentPackIdx = 0;
    refreshGallery();
}

void CursorConfigPopup::onDeletePack(CCObject*) {
    std::string pack = currentPack();
    if (pack.empty()) {
        PaimonNotify::create("Elige un pack para borrar (las imagenes sueltas no se borran como pack).",
            NotificationIcon::Info)->show();
        return;
    }

    WeakRef<CursorConfigPopup> self = this;
    PopupManager::get().quickPopup(
        "Borrar Pack",
        fmt::format("Borrar el pack <cy>{}</c> completo y todos sus cursores?", pack),
        "Cancelar", "Borrar",
        [self, pack](auto*, bool confirmed) {
            if (!confirmed) return;
            auto popup = self.lock();
            if (!popup || !popup->getParent()) return;
            CursorManager::get().removePack(pack);
            PaimonNotify::create("Pack borrado", NotificationIcon::Success)->show();
            auto* p = static_cast<CursorConfigPopup*>(popup.data());
            p->m_currentPackIdx = 0;
            p->refreshPackList();
            p->refreshGallery();
        }
    ).showInstant();
}

void CursorConfigPopup::syncEnableUI(bool enabled) {
    if (m_enableToggle) m_enableToggle->toggle(enabled);
    kit::setHeroStateLabel(m_enableStateLabel, enabled);
}

void CursorConfigPopup::onSelectImage(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto nameObj = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!nameObj) return;

    std::string filename = nameObj->getCString();

    bool wasEnabled = CursorManager::get().config().enabled;
    CursorManager::get().setImageForState(m_activeSlot, filename);

    // setImageForState auto-enables the cursor on first assignment; keep the
    // settings toggle in sync so it doesn't look out of date.
    bool nowEnabled = CursorManager::get().config().enabled;
    syncEnableUI(nowEnabled);

    if (!wasEnabled && nowEnabled) {
        PaimonNotify::create(
            fmt::format("Cursor {} asignado y activado!", slotDisplayName(m_activeSlot)),
            NotificationIcon::Success
        )->show();
    } else {
        PaimonNotify::create(
            fmt::format("Cursor {} asignado!", slotDisplayName(m_activeSlot)),
            NotificationIcon::Success
        )->show();
    }
    refreshGallery();
}

void CursorConfigPopup::onDeleteImage(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto nameObj = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!nameObj) return;

    std::string filename = nameObj->getCString();

    WeakRef<CursorConfigPopup> self = this;
    PopupManager::get().quickPopup(
        "Borrar Imagen",
        "Seguro que quieres <cr>borrar</c> esta imagen?",
        "Cancelar", "Borrar",
        [self, filename](auto*, bool confirmed) {
            if (!confirmed) return;
            auto popup = self.lock();
            if (!popup || !popup->getParent()) return;
            CursorManager::get().removeFromGallery(filename);
            PaimonNotify::create("Imagen eliminada", NotificationIcon::Info)->show();
            static_cast<CursorConfigPopup*>(popup.data())->refreshGallery();
        }
    ).showInstant();
}

void CursorConfigPopup::onDeleteAllImages(CCObject*) {
    auto images = CursorManager::get().getGalleryImages();
    if (images.empty()) {
        PaimonNotify::create("La galeria ya esta vacia", NotificationIcon::Info)->show();
        return;
    }

    std::string msg = fmt::format(
        "Seguro que quieres <cr>borrar TODOS</c> los {} cursores y packs?\nEsto no se puede deshacer!",
        images.size()
    );

    WeakRef<CursorConfigPopup> self = this;
    PopupManager::get().quickPopup(
        "Borrar Todos",
        msg,
        "Cancelar", "Borrar todo",
        [self](auto*, bool confirmed) {
            if (!confirmed) return;
            auto popup = self.lock();
            if (!popup || !popup->getParent()) return;

            int cleaned = CursorManager::get().cleanupInvalidImages();
            CursorManager::get().removeAllFromGallery();

            std::string note = "Todos los cursores borrados!";
            if (cleaned > 0) {
                note += fmt::format(" ({} archivos corruptos eliminados)", cleaned);
            }
            PaimonNotify::create(note, NotificationIcon::Success)->show();
            auto* p = static_cast<CursorConfigPopup*>(popup.data());
            p->m_currentPackIdx = 0;
            p->refreshPackList();
            p->refreshGallery();
        }
    ).showInstant();
}

void CursorConfigPopup::onAddImage(CCObject*) {
    WeakRef<CursorConfigPopup> self = this;
    pt::pickCursorAsset([self](geode::Result<std::optional<std::filesystem::path>> result) {
        auto popup = self.lock();
        if (!popup) return;
        auto pathOpt = std::move(result).unwrapOr(std::nullopt);
        if (!pathOpt || pathOpt->empty()) return;

        auto imported = CursorManager::get().importFromFile(*pathOpt);
        if (imported.empty()) {
            auto reason = CursorManager::get().lastImportError();
            if (reason.empty()) {
                reason = "Formatos validos: imagenes, .cur/.ani/.ico y packs .zip.";
            }
            PaimonNotify::create(reason, NotificationIcon::Error)->show();
            return;
        }

        auto* p = static_cast<CursorConfigPopup*>(popup.data());

        if (imported.size() == 1) {
            PaimonNotify::create("Cursor anadido!", NotificationIcon::Success)->show();
        } else {
            PaimonNotify::create(
                fmt::format("Se importaron {} cursores en un pack nuevo!", imported.size()),
                NotificationIcon::Success
            )->show();
        }

        // Auto-asignar al slot activo si esta vacio (usa el primer importado).
        CursorState slot = p->m_activeSlot;
        if (CursorManager::get().imageForState(slot).empty()) {
            CursorManager::get().setImageForState(slot, imported.front());
        }
        // setImageForState may have auto-enabled the cursor; sync the toggle.
        p->syncEnableUI(CursorManager::get().config().enabled);
        // Navega al pack recien creado (refreshPackList lo detecta) o refresca.
        p->refreshPackList();
        p->refreshGallery();
    });
}

// settings tab (PaiConfigKit)

static void applyPresetToConfig(CursorConfig& cfg) {
    if (cfg.trailPreset < 0 || cfg.trailPreset >= CursorManager::TRAIL_PRESET_COUNT) return;
    auto const& p = CursorManager::TRAIL_PRESETS[cfg.trailPreset];
    cfg.trailR        = p.color.r;
    cfg.trailG        = p.color.g;
    cfg.trailB        = p.color.b;
    cfg.trailLength   = p.length;
    cfg.trailWidth    = p.width;
    cfg.trailFadeType = p.fadeType;
    cfg.trailOpacity  = p.opacity;
}

void CursorConfigPopup::buildSettingsTab() {
    auto content = m_mainLayer->getContentSize();
    float scrollW = content.width - 24.f;
    float scrollH = content.height - 58.f;
    float innerW = kit::cardInnerWidth(scrollW);

    auto& cfg = CursorManager::get().config();
    auto save = [] { CursorManager::get().saveConfig(); };

    // Interruptor principal
    auto* hero = kit::makeHeroToggle(scrollW,
        "Cursor personalizado",
        "Reemplaza el cursor del sistema con tus imagenes de la galeria.",
        cfg.enabled,
        [this](bool v) {
            auto& c = CursorManager::get().config();
            c.enabled = v;
            applyLive();
            if (c.enabled && c.idleImage.empty() && c.moveImage.empty()) {
                PaimonNotify::create(
                    "Elige primero una imagen Normal o Mover en la Galeria. Hasta entonces se vera el cursor del sistema.",
                    NotificationIcon::Info
                )->show();
            }
        },
        &m_enableToggle, &m_enableStateLabel);

    // Apariencia
    auto* lookCard = kit::makeCard(scrollW, "Apariencia", {120, 210, 255}, {
        kit::makeSliderRow(innerW,
            "Tamano", "Que tan grande se ve el cursor.",
            cfg.scale, CURSOR_SCALE_MIN, CURSOR_SCALE_MAX,
            [](double v) { return fmt::format("x{:.2f}", v); },
            [this](double v) {
                CursorManager::get().config().scale = static_cast<float>(v);
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Opacidad", "100% = solido, menos = transparente.",
            static_cast<double>(cfg.opacity), 0.0, 255.0,
            [](double v) { return fmt::format("{}%", static_cast<int>(v / 255.0 * 100.0)); },
            [this](double v) {
                auto& c = CursorManager::get().config();
                c.opacity = std::max(0, std::min(255, static_cast<int>(v)));
                applyLive();
            }),
    });

    // Estela (lo esencial: encender y elegir estilo)
    std::vector<std::string> presetNames;
    presetNames.push_back("Personalizado");
    for (int i = 0; i < CursorManager::TRAIL_PRESET_COUNT; ++i) {
        presetNames.push_back(CursorManager::TRAIL_PRESETS[i].name);
    }
    int presetIdx = (cfg.trailPreset >= 0 && cfg.trailPreset < CursorManager::TRAIL_PRESET_COUNT)
        ? cfg.trailPreset + 1 : 0;

    CCLabelBMFont* presetLabel = nullptr;
    auto* presetRow = kit::makeSelectRow(innerW,
        "Estilo de estela", "Elige entre 10 estilos listos.",
        presetNames, presetIdx,
        [this](int idx) {
            auto& c = CursorManager::get().config();
            c.trailPreset = idx - 1; // 0 = Personalizado -> -1
            if (c.trailPreset >= 0) applyPresetToConfig(c);
            CursorManager::get().saveConfig();
            applyLive();
        },
        &presetLabel);
    m_presetLabel = presetLabel;

    auto* trailCard = kit::makeCard(scrollW, "Estela del cursor", {255, 200, 100}, {
        kit::makeToggleRow(innerW,
            "Mostrar estela",
            "Deja un rastro brillante al mover el cursor.",
            cfg.trailEnabled,
            [this](bool v) {
                CursorManager::get().config().trailEnabled = v;
                applyLive();
            }),
        presetRow,
    });

    auto* footer = kit::makeHint(scrollW,
        "Consejo: en la pestana Avanzado puedes cambiar el cursor segun lo que "
        "toques (botones, texto...) y darle movimiento con retraso.");

    m_scrollLayer = kit::makeScrollStack({scrollW, scrollH},
        {hero, lookCard, trailCard, footer});
    m_scrollLayer->setPosition({12.f, 8.f});
    m_settingsTab->addChild(m_scrollLayer, 5);

    // scroll arrow indicator
    auto scrollArrow = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    if (scrollArrow) {
        scrollArrow->setRotation(-90.f);
        scrollArrow->setScale(0.3f);
        scrollArrow->setOpacity(150);
        scrollArrow->setPosition({content.width / 2.f, 16.f});
        scrollArrow->setID("cursor-scroll-arrow"_spr);
        m_settingsTab->addChild(scrollArrow, 20);

        auto bounce = CCRepeatForever::create(CCSequence::create(
            CCMoveBy::create(0.5f, {0, 3.f}),
            CCMoveBy::create(0.5f, {0, -3.f}), nullptr));
        scrollArrow->runAction(bounce);
        m_scrollArrow = scrollArrow;
        this->unschedule(schedule_selector(CursorConfigPopup::checkScrollPosition));
        this->schedule(schedule_selector(CursorConfigPopup::checkScrollPosition), 0.2f);
    }
}

void CursorConfigPopup::buildAdvancedTab() {
    auto content = m_mainLayer->getContentSize();
    float scrollW = content.width - 24.f;
    float scrollH = content.height - 58.f;
    float innerW = kit::cardInnerWidth(scrollW);

    auto& cfg = CursorManager::get().config();
    auto save = [] { CursorManager::get().saveConfig(); };

    // Estados (las imagenes se asignan en la Galeria)
    auto* statesCard = kit::makeCard(scrollW, "Estados del cursor", {255, 140, 220}, {
        kit::makeHint(innerW,
            "Cada estado usa la imagen que le asignes en la pestana Galeria. "
            "Si un estado no tiene imagen, se usa la de Normal."),
        kit::makeToggleRow(innerW,
            "Cambiar sobre botones",
            "Usa la imagen 'Boton' al pasar sobre algo clickeable.",
            cfg.hoverEnabled,
            [this, save](bool v) {
                auto& c = CursorManager::get().config();
                c.hoverEnabled = v; save();
                if (v && c.hoverImage.empty()) {
                    PaimonNotify::create("Asigna una imagen 'Boton' en la Galeria para ver este estado.",
                        NotificationIcon::Info)->show();
                }
            }),
        kit::makeToggleRow(innerW,
            "Cambiar al hacer click",
            "Usa la imagen 'Click' mientras mantienes el boton izquierdo.",
            cfg.clickEnabled,
            [this, save](bool v) {
                auto& c = CursorManager::get().config();
                c.clickEnabled = v; save();
                if (v && c.clickImage.empty()) {
                    PaimonNotify::create("Asigna una imagen 'Click' en la Galeria para ver este estado.",
                        NotificationIcon::Info)->show();
                }
            }),
        kit::makeToggleRow(innerW,
            "Cambiar sobre texto",
            "Usa la imagen 'Texto' sobre campos de escritura.",
            cfg.textEnabled,
            [this, save](bool v) {
                auto& c = CursorManager::get().config();
                c.textEnabled = v; save();
                if (v && c.textImage.empty()) {
                    PaimonNotify::create("Asigna una imagen 'Texto' en la Galeria para ver este estado.",
                        NotificationIcon::Info)->show();
                }
            }),
        kit::makeToggleRow(innerW,
            "Cambiar sobre botones bloqueados",
            "Usa la imagen 'Bloqueado' sobre botones desactivados.",
            cfg.disabledEnabled,
            [this, save](bool v) {
                auto& c = CursorManager::get().config();
                c.disabledEnabled = v; save();
                if (v && c.disabledImage.empty()) {
                    PaimonNotify::create("Asigna una imagen 'Bloqueado' en la Galeria para ver este estado.",
                        NotificationIcon::Info)->show();
                }
            }),
    });

    // Movimiento
    auto* moveCard = kit::makeCard(scrollW, "Movimiento", {130, 240, 170}, {
        kit::makeToggleRow(innerW,
            "Seguir con retraso",
            "El cursor persigue al raton con un movimiento suave.",
            cfg.followDelayEnabled,
            [save](bool v) {
                CursorManager::get().config().followDelayEnabled = v;
                save();
            }),
        kit::makeSliderRow(innerW,
            "Cantidad de retraso", "0 = instantaneo, 1 = muy lento.",
            cfg.followDelay, 0.0, 1.0,
            [](double v) { return fmt::format("{:.2f}", v); },
            [save](double v) {
                CursorManager::get().config().followDelay = static_cast<float>(v);
                save();
            }),
    });

    // Detalles de la estela (solo lectura + edicion manual)
    auto* trailDetailCard = kit::makeCard(scrollW, "Detalles de la estela", {255, 200, 100}, {
        kit::makeButtonRow(innerW,
            "Valores actuales",
            "Consulta el color, largo y opacidad de la estela.",
            "Ver",
            [this] {
                auto& c = CursorManager::get().config();
                std::string info = fmt::format(
                    "<cy>Valores actuales de la estela</c>\n\n"
                    "Color: R={} G={} B={}\n"
                    "Largo: {:.0f}  Grosor: {:.1f}\n"
                    "Desvanecido: {}\n"
                    "Opacidad: {}\n\n"
                    "Para valores propios elige el estilo <cy>Personalizado</c> y edita\n"
                    "<cy>cursor_config.json</c> en la carpeta de guardado del mod.",
                    c.trailR, c.trailG, c.trailB,
                    c.trailLength, c.trailWidth,
                    c.trailFadeType == 0 ? "Lineal" : (c.trailFadeType == 1 ? "Seno" : "Ninguno"),
                    c.trailOpacity
                );
                PopupManager::get().alert("Estela", info, "OK", nullptr, 360.f).showInstant();
            }),
    });

    auto* footer = kit::makeHint(scrollW,
        "El cursor aparece en todas las pantallas y se oculta solo durante el gameplay.");

    m_advancedScroll = kit::makeScrollStack({scrollW, scrollH},
        {statesCard, moveCard, trailDetailCard, footer});
    m_advancedScroll->setPosition({12.f, 8.f});
    m_advancedTab->addChild(m_advancedScroll, 5);
}

void CursorConfigPopup::checkScrollPosition(float dt) {
    if (!m_scrollArrow || !m_scrollLayer) return;
    float totalH = m_scrollLayer->m_contentLayer->getContentSize().height;
    float viewH = m_scrollLayer->getContentSize().height;
    float curY = m_scrollLayer->m_contentLayer->getPositionY();
    bool nearBottom = (curY <= -(totalH - viewH) + 20.f);

    if (nearBottom && m_scrollArrow->getOpacity() > 0) {
        m_scrollArrow->stopAllActions();
        m_scrollArrow->runAction(CCFadeTo::create(0.3f, 0));
    } else if (!nearBottom && m_scrollArrow->getOpacity() < 150) {
        m_scrollArrow->stopAllActions();
        m_scrollArrow->runAction(CCFadeTo::create(0.3f, 150));
        m_scrollArrow->runAction(CCRepeatForever::create(CCSequence::create(
            CCMoveBy::create(0.5f, {0, 3.f}),
            CCMoveBy::create(0.5f, {0, -3.f}), nullptr)));
    }
}

// apply live

void CursorConfigPopup::applyLive() {
    auto& cm = CursorManager::get();
    cm.applyConfigLive();

    if (cm.config().enabled) {
        if (!cm.isAttached()) {
            cm.attachToOverlay();
        }
    } else {
        cm.detachFromScene();
    }
}

void CursorConfigPopup::updatePresetLabel() {
    if (!m_presetLabel) return;
    auto& cfg = CursorManager::get().config();
    if (cfg.trailPreset < 0 || cfg.trailPreset >= CursorManager::TRAIL_PRESET_COUNT) {
        m_presetLabel->setString("Personalizado");
    } else {
        m_presetLabel->setString(CursorManager::TRAIL_PRESETS[cfg.trailPreset].name);
    }
}
