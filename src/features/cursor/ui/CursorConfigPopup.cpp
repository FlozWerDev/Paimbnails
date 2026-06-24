#include "CursorConfigPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../services/CursorManager.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/InfoButton.hpp"
#include <Geode/binding/ButtonSprite.hpp>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
// Inicia/actualiza el destino de scroll suave para la rueda del raton.
// Devuelve true si el cursor esta sobre el area de scroll (consume el evento).
bool queueSmoothScroll(ScrollLayer* scrollLayer, float x, float y,
                       float& targetY, bool& targetSet) {
#if !defined(GEODE_IS_WINDOWS) && !defined(GEODE_IS_MACOS)
    return false;
#else
    if (!scrollLayer) return false;

    CCPoint mousePos = geode::cocos::getMousePos();
    CCRect scrollRect = scrollLayer->boundingBox();
    scrollRect.origin = scrollLayer->getParent()->convertToWorldSpace(scrollRect.origin);
    if (!scrollRect.containsPoint(mousePos)) return false;

    auto* contentLayer = scrollLayer->m_contentLayer;
    if (!contentLayer) return false;

    float scrollAmount = y;
    if (std::abs(scrollAmount) < 0.001f) {
        scrollAmount = -x;
    }

    float minY = scrollLayer->getContentSize().height - contentLayer->getContentSize().height;
    float maxY = 0.f;
    if (minY > maxY) minY = maxY;

    // Si aun no hay destino, partir de la posicion actual.
    if (!targetSet) {
        targetY = contentLayer->getPositionY();
        targetSet = true;
    }
    targetY = std::max(minY, std::min(maxY, targetY - scrollAmount * 16.f));
    return true;
#endif
}

// Aproxima suavemente el contentLayer hacia targetY (lerp por frame).
void stepSmoothScroll(ScrollLayer* scrollLayer, float& targetY, bool& targetSet, float dt) {
    if (!targetSet || !scrollLayer) return;
    auto* contentLayer = scrollLayer->m_contentLayer;
    if (!contentLayer) { targetSet = false; return; }

    float cur = contentLayer->getPositionY();
    float diff = targetY - cur;
    if (std::abs(diff) < 0.5f) {
        contentLayer->setPositionY(targetY);
        targetSet = false;
        return;
    }
    // factor de suavizado independiente del framerate
    float t = 1.f - std::pow(0.001f, dt);
    contentLayer->setPositionY(cur + diff * t);
}
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

    this->setTitle("Custom Cursor");
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

    createTabButtons();
    buildGalleryTab();
    buildSettingsTab();

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
    if (m_currentTab == 1) {
        queueSmoothScroll(m_scrollLayer, x, y, m_settingsScrollTargetY, m_settingsScrollTargetSet);
        return;
    }
    if (m_currentTab == 0) {
        queueSmoothScroll(m_thumbScroll, x, y, m_thumbScrollTargetY, m_thumbScrollTargetSet);
        return;
    }
}

void CursorConfigPopup::updateSmoothScroll(float dt) {
    stepSmoothScroll(m_thumbScroll, m_thumbScrollTargetY, m_thumbScrollTargetSet, dt);
    stepSmoothScroll(m_scrollLayer, m_settingsScrollTargetY, m_settingsScrollTargetSet, dt);
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

    auto spr1 = ButtonSprite::create("Gallery");
    spr1->setScale(0.5f);
    auto tab1 = CCMenuItemSpriteExtra::create(spr1, this, menu_selector(CursorConfigPopup::onTabSwitch));
    tab1->setTag(0);
    tab1->setID("cursor-gallery-tab-btn"_spr);
    menu->addChild(tab1);
    m_tabs.push_back(tab1);

    auto spr2 = ButtonSprite::create("Settings");
    spr2->setScale(0.5f);
    auto tab2 = CCMenuItemSpriteExtra::create(spr2, this, menu_selector(CursorConfigPopup::onTabSwitch));
    tab2->setTag(1);
    tab2->setID("cursor-settings-tab-btn"_spr);
    menu->addChild(tab2);
    m_tabs.push_back(tab2);

    menu->updateLayout();
    onTabSwitch(tab1);
}

void CursorConfigPopup::onTabSwitch(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    m_currentTab = btn->getTag();

    m_galleryTab->setVisible(m_currentTab == 0);
    m_settingsTab->setVisible(m_currentTab == 1);

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
        case CursorState::Move:     return "Move";
        case CursorState::Hover:    return "Hover";
        case CursorState::Click:    return "Click";
        case CursorState::Text:     return "Text";
        case CursorState::Disabled: return "Disabled";
        case CursorState::Idle:
        default:                    return "Idle";
    }
}

std::string CursorConfigPopup::currentPack() const {
    if (m_currentPackIdx < 0 || m_currentPackIdx >= (int)m_packList.size()) return "";
    return m_packList[m_currentPackIdx];
}

// States info text shown by the "i" button
static std::string buildStatesInfo() {
    return
        "<cy>Cursor states</c> — assign a different image to each one:\n\n"
        "<cg>Idle</c>: at rest (the default arrow).\n"
        "<cb>Move</c>: while the mouse is moving.\n"
        "<co>Hover</c>: over a clickable button.\n"
        "<cp>Click</c>: holding the left mouse button.\n"
        "<cl>Text</c>: over a text field (I-beam).\n"
        "<cr>Disabled</c>: over a disabled button.\n\n"
        "Tap a state, then tap an image below to assign it.\n"
        "Any state with no image falls back to <cg>Idle</c>.";
}

void CursorConfigPopup::buildGalleryTab() {
    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    auto menu = CCMenu::create();
    menu->setID("cursor-gallery-fixed-menu"_spr);
    menu->setPosition({0, 0});
    m_galleryTab->addChild(menu, 10);

    // slots: 6 estados en UNA sola fila compacta (deja mucho mas espacio
    // libre abajo para la rejilla de miniaturas)
    float slotSize = 30.f;
    float colStep  = 58.f;
    float slotRowY = content.height - 62.f;

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
    if (auto* iBtn = PaimonInfo::createInfoBtn("Cursor States", buildStatesInfo(), this, 0.5f)) {
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

    m_packLabel = CCLabelBMFont::create("All loose images", "bigFont.fnt");
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
        "No cursors here yet.\nUse + Add to import images, .cur/.ani cursors, or a .zip pack.",
        "bigFont.fnt"
    );
    if (m_emptyGalleryLabel) {
        m_emptyGalleryLabel->setScale(0.28f);
        m_emptyGalleryLabel->setOpacity(170);
        m_emptyGalleryLabel->setAlignment(kCCTextAlignmentCenter);
        m_emptyGalleryLabel->setPosition({cx, scrollBottom + scrollH / 2.f});
        m_galleryTab->addChild(m_emptyGalleryLabel, 2);
    }

    // botones inferiores: + Add / Delete All
    auto addSpr = ButtonSprite::create("+ Add", "goldFont.fnt", "GJ_button_01.png", 0.7f);
    addSpr->setScale(0.5f);
    auto addBtn = CCMenuItemSpriteExtra::create(addSpr, this, menu_selector(CursorConfigPopup::onAddImage));
    addBtn->setPosition({cx - 55.f, 22.f});
    menu->addChild(addBtn);

    auto delAllSpr = ButtonSprite::create("Delete All", "goldFont.fnt", "GJ_button_06.png", 0.7f);
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
            m_packLabel->setString(fmt::format("Loose images ({})", images.size()).c_str());
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
    float slotRowY = content.height - 62.f;
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
        PaimonNotify::create("Pick a pack to delete (loose images can't be deleted as a pack).",
            NotificationIcon::Info)->show();
        return;
    }

    WeakRef<CursorConfigPopup> self = this;
    geode::createQuickPopup(
        "Delete Pack",
        fmt::format("Delete the whole pack <cy>{}</c> and all its cursors?", pack),
        "Cancel", "Delete",
        [self, pack](auto*, bool confirmed) {
            if (!confirmed) return;
            auto popup = self.lock();
            if (!popup || !popup->getParent()) return;
            CursorManager::get().removePack(pack);
            PaimonNotify::create("Pack deleted", NotificationIcon::Success)->show();
            auto* p = static_cast<CursorConfigPopup*>(popup.data());
            p->m_currentPackIdx = 0;
            p->refreshPackList();
            p->refreshGallery();
        }
    );
}

void CursorConfigPopup::onSelectImage(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto nameObj = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!nameObj) return;

    std::string filename = nameObj->getCString();

    CursorManager::get().setImageForState(m_activeSlot, filename);
    PaimonNotify::create(
        fmt::format("{} cursor set!", slotDisplayName(m_activeSlot)),
        NotificationIcon::Success
    )->show();
    refreshGallery();
}

void CursorConfigPopup::onDeleteImage(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto nameObj = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!nameObj) return;

    std::string filename = nameObj->getCString();

    WeakRef<CursorConfigPopup> self = this;
    geode::createQuickPopup(
        "Delete Cursor Image",
        "Are you sure you want to <cr>delete</c> this image?",
        "Cancel", "Delete",
        [self, filename](auto*, bool confirmed) {
            if (!confirmed) return;
            auto popup = self.lock();
            if (!popup || !popup->getParent()) return;
            CursorManager::get().removeFromGallery(filename);
            PaimonNotify::create("Image removed", NotificationIcon::Info)->show();
            static_cast<CursorConfigPopup*>(popup.data())->refreshGallery();
        }
    );
}

void CursorConfigPopup::onDeleteAllImages(CCObject*) {
    auto images = CursorManager::get().getGalleryImages();
    if (images.empty()) {
        PaimonNotify::create("Gallery is already empty", NotificationIcon::Info)->show();
        return;
    }

    std::string msg = fmt::format(
        "Are you sure you want to <cr>delete ALL</c> {} cursors and packs?\nThis cannot be undone!",
        images.size()
    );

    WeakRef<CursorConfigPopup> self = this;
    geode::createQuickPopup(
        "Delete All Cursors",
        msg,
        "Cancel", "Delete All",
        [self](auto*, bool confirmed) {
            if (!confirmed) return;
            auto popup = self.lock();
            if (!popup || !popup->getParent()) return;

            int cleaned = CursorManager::get().cleanupInvalidImages();
            CursorManager::get().removeAllFromGallery();

            std::string note = "All cursors deleted!";
            if (cleaned > 0) {
                note += fmt::format(" ({} corrupted files removed)", cleaned);
            }
            PaimonNotify::create(note, NotificationIcon::Success)->show();
            auto* p = static_cast<CursorConfigPopup*>(popup.data());
            p->m_currentPackIdx = 0;
            p->refreshPackList();
            p->refreshGallery();
        }
    );
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
                reason = "Supported: images, .cur/.ani/.ico and .zip packs.";
            }
            PaimonNotify::create(reason, NotificationIcon::Error)->show();
            return;
        }

        auto* p = static_cast<CursorConfigPopup*>(popup.data());

        if (imported.size() == 1) {
            PaimonNotify::create("Cursor added!", NotificationIcon::Success)->show();
        } else {
            PaimonNotify::create(
                fmt::format("Imported {} cursors into a new pack!", imported.size()),
                NotificationIcon::Success
            )->show();
        }

        // Auto-asignar al slot activo si esta vacio (usa el primer importado).
        CursorState slot = p->m_activeSlot;
        if (CursorManager::get().imageForState(slot).empty()) {
            CursorManager::get().setImageForState(slot, imported.front());
        }
        // Navega al pack recien creado (refreshPackList lo detecta) o refresca.
        p->refreshPackList();
        p->refreshGallery();
    });
}

// settings tab

static float readSlider(Slider* s, float minV, float maxV) {
    float v = s->getThumb()->getValue();
    return minV + v * (maxV - minV);
}

void CursorConfigPopup::buildSettingsTab() {
    auto content = m_mainLayer->getContentSize();
    float scrollW = content.width - 16.f;
    float scrollH = content.height - 52.f;
    float totalH = 760.f;

    m_scrollLayer = ScrollLayer::create({scrollW, scrollH});
    m_scrollLayer->setPosition({8.f, 8.f});
    m_settingsTab->addChild(m_scrollLayer, 5);

    CCNode* sc = m_scrollLayer->m_contentLayer;
    sc->setContentSize({scrollW, totalH});

    auto scrollContent = CCLayer::create();
    scrollContent->setContentSize({scrollW, totalH});
    sc->addChild(scrollContent);
    sc = scrollContent;

    auto navMenu = CCMenu::create();
    navMenu->setPosition({0, 0});
    scrollContent->addChild(navMenu, 10);

    float cx = scrollW / 2.f;
    float y = totalH - 8.f;

    auto& cfg = CursorManager::get().config();

    // helpers
    auto addTitle = [&](char const* text, char const* info = nullptr) {
        auto label = CCLabelBMFont::create(text, "goldFont.fnt");
        label->setScale(0.4f);
        label->setPosition({cx, y});
        sc->addChild(label);

        if (info) {
            auto btn = PaimonInfo::createInfoBtn(text, info, this, 0.45f);
            if (btn) {
                float halfW = label->getContentSize().width * 0.4f / 2.f;
                btn->setPosition({cx + halfW + 10.f, y});
                navMenu->addChild(btn);
            }
        }
    };

    auto addSlider = [&](Slider*& slider, CCLabelBMFont*& label,
                         float val, float minV, float maxV,
                         SEL_MenuHandler cb, char const* fmt_str) {
        float norm = (maxV > minV) ? (val - minV) / (maxV - minV) : 0.f;
        slider = Slider::create(this, cb, 0.65f);
        slider->setPosition({cx, y});
        slider->setValue(norm);
        sc->addChild(slider);

        label = CCLabelBMFont::create(fmt::format(fmt::runtime(fmt_str), val).c_str(), "bigFont.fnt");
        label->setScale(0.35f);
        label->setPosition({cx + 95.f, y});
        sc->addChild(label);
    };

    auto addToggle = [&](char const* text, CCMenuItemToggler*& toggle,
                         bool value, SEL_MenuHandler cb, char const* info = nullptr) {
        auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(0.35f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({cx - 85.f, y});
        sc->addChild(lbl);

        if (info) {
            auto iBtn = PaimonInfo::createInfoBtn(text, info, this, 0.35f);
            if (iBtn) {
                float lblW = lbl->getContentSize().width * 0.35f;
                iBtn->setPosition({cx - 85.f + lblW + 7.f, y});
                navMenu->addChild(iBtn);
            }
        }

        toggle = CCMenuItemToggler::createWithStandardSprites(this, cb, 0.35f);
        toggle->setPosition({cx + 85.f, y});
        toggle->toggle(value);
        navMenu->addChild(toggle);
    };

    // General
    addTitle("General",
        "<cy>Enable Cursor</c>: turns the custom cursor <cg>ON</c> or <cr>OFF</c>.\n"
        "The OS cursor is only hidden when at least one custom image can actually be rendered.\n"
        "<cy>Scale</c>: size of the cursor sprite (0.10 = tiny, 3.0 = huge).\n"
        "<cy>Opacity</c>: transparency of the cursor (0 = invisible, 255 = fully opaque).");
    y -= 18.f;

    addToggle("Enable Cursor", m_enableToggle, cfg.enabled,
        menu_selector(CursorConfigPopup::onEnableToggled),
        "Turns the custom cursor <cg>ON</c> or <cr>OFF</c>.\nThe OS cursor stays visible until an idle or move image is assigned.");
    y -= 22.f;

    addSlider(m_scaleSlider, m_scaleLabel, cfg.scale, CURSOR_SCALE_MIN, CURSOR_SCALE_MAX,
        menu_selector(CursorConfigPopup::onScaleChanged), "{:.2f}");
    y -= 18.f;

    addSlider(m_opacitySlider, m_opacityLabel, static_cast<float>(cfg.opacity), 0.f, 255.f,
        menu_selector(CursorConfigPopup::onOpacityChanged), "{:.0f}");
    y -= 24.f;

    // Cursor States (Ecuet-inspired)
    addTitle("Cursor States",
        "The cursor reacts to context using the images you assign in the Gallery tab.\n"
        "<cy>Idle</c>: at rest. <cy>Move</c>: while moving.\n"
        "<co>Hover</c>: over a button. <cp>Click</c>: holding left click.\n"
        "<cl>Text</c>: over a text field. <cr>Disabled</c>: over a disabled button.\n"
        "Any state without its own image falls back to Idle.");
    y -= 18.f;

    addToggle("Hover State", m_hoverToggle, cfg.hoverEnabled,
        menu_selector(CursorConfigPopup::onHoverToggled),
        "Switch to the <co>Hover</c> image while the cursor is over a button.\nNeeds a Hover image assigned in the Gallery.");
    y -= 22.f;

    addToggle("Click State", m_clickToggle, cfg.clickEnabled,
        menu_selector(CursorConfigPopup::onClickToggled),
        "Switch to the <cp>Click</c> image while holding the left mouse button.\nNeeds a Click image assigned in the Gallery.");
    y -= 22.f;

    addToggle("Text State", m_textToggle, cfg.textEnabled,
        menu_selector(CursorConfigPopup::onTextToggled),
        "Switch to the <cl>Text</c> image while the cursor is over a text field.\nNeeds a Text image assigned in the Gallery.");
    y -= 22.f;

    addToggle("Disabled State", m_disabledToggle, cfg.disabledEnabled,
        menu_selector(CursorConfigPopup::onDisabledToggled),
        "Switch to the <cr>Disabled</c> image while the cursor is over a disabled button.\nNeeds a Disabled image assigned in the Gallery.");
    y -= 24.f;

    // Follow Delay
    addTitle("Follow Delay",
        "Makes the cursor follow your mouse with a smooth delay.\n"
        "<cy>Enable Follow Delay</c>: turns the delay effect <cg>ON</c> or <cr>OFF</c>.\n"
        "<cy>Delay Amount</c>: how slow the cursor follows (0 = instant, 1 = very slow).");
    y -= 18.f;

    addToggle("Enable Follow Delay", m_followDelayToggle, cfg.followDelayEnabled,
        menu_selector(CursorConfigPopup::onFollowDelayToggled),
        "Adds a smooth delay to cursor movement.\nThe cursor will lerp towards the actual mouse position.");
    y -= 22.f;

    addSlider(m_followDelaySlider, m_followDelayLabel, cfg.followDelay, 0.f, 1.f,
        menu_selector(CursorConfigPopup::onFollowDelayChanged), "{:.2f}");
    y -= 24.f;

    // Trail
    addTitle("Trail Effect",
        "Leaves a glowing trail behind the cursor as it moves.\n"
        "<cy>Enable Trail</c>: shows/hides the trail.\n"
        "<cy>Presets</c>: 10 built-in trail styles with different colors and behavior.\n"
        "Use <cg>Edit Trail</c> to switch to custom mode and tweak values manually.");
    y -= 18.f;

    addToggle("Enable Trail", m_trailToggle, cfg.trailEnabled,
        menu_selector(CursorConfigPopup::onTrailToggled),
        "Shows a <cy>CCMotionStreak</c> trail behind the cursor.\nRequires cursor to be enabled.");
    y -= 22.f;

    // preset picker row
    {
        auto lbl = CCLabelBMFont::create("Trail Preset", "bigFont.fnt");
        lbl->setScale(0.35f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({cx - 85.f, y});
        sc->addChild(lbl);

        // prev arrow
        auto prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        if (prevSpr) {
            prevSpr->setScale(0.35f);
            auto prevBtn = CCMenuItemSpriteExtra::create(prevSpr, this, menu_selector(CursorConfigPopup::onPresetPrev));
            prevBtn->setPosition({cx + 30.f, y});
            navMenu->addChild(prevBtn);
        }

        // preset name label
        m_presetLabel = CCLabelBMFont::create("", "bigFont.fnt");
        m_presetLabel->setScale(0.22f);
        m_presetLabel->setPosition({cx + 60.f, y});
        sc->addChild(m_presetLabel);
        updatePresetLabel();

        // next arrow
        auto nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        if (nextSpr) {
            nextSpr->setFlipX(true);
            nextSpr->setScale(0.35f);
            auto nextBtn = CCMenuItemSpriteExtra::create(nextSpr, this, menu_selector(CursorConfigPopup::onPresetNext));
            nextBtn->setPosition({cx + 90.f, y});
            navMenu->addChild(nextBtn);
        }
    }
    y -= 22.f;

    // edit trail (custom mode) button
    {
        auto editSpr = ButtonSprite::create("Edit Trail", "bigFont.fnt", "GJ_button_04.png", 0.6f);
        editSpr->setScale(0.45f);
        auto editBtn = CCMenuItemSpriteExtra::create(editSpr, this, menu_selector(CursorConfigPopup::onEditTrail));
        editBtn->setPosition({cx, y});
        navMenu->addChild(editBtn);
    }
    y -= 26.f;

    // Visibility
    addTitle("Visibility",
        "The custom cursor appears on <cg>all screens</c> automatically.\n"
        "It hides automatically during gameplay if you disable the native cursor.");
    y -= 16.f;

    m_scrollLayer->moveToTop();

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

// slider callbacks

void CursorConfigPopup::onScaleChanged(CCObject*) {
    if (!m_scaleSlider) return;
    auto& c = CursorManager::get().config();
    c.scale = readSlider(m_scaleSlider, CURSOR_SCALE_MIN, CURSOR_SCALE_MAX);
    if (m_scaleLabel) m_scaleLabel->setString(fmt::format("{:.2f}", c.scale).c_str());
    applyLive();
}

void CursorConfigPopup::onOpacityChanged(CCObject*) {
    if (!m_opacitySlider) return;
    float v = m_opacitySlider->getThumb()->getValue();
    auto& cfg = CursorManager::get().config();
    cfg.opacity = static_cast<int>(v * 255.f);
    cfg.opacity = std::max(0, std::min(255, cfg.opacity));
    if (m_opacityLabel) m_opacityLabel->setString(fmt::format("{}", cfg.opacity).c_str());
    applyLive();
}

// toggle callbacks

void CursorConfigPopup::onEnableToggled(CCObject*) {
    auto& cfg = CursorManager::get().config();
    cfg.enabled = !m_enableToggle->isToggled();
    applyLive();

    if (cfg.enabled && cfg.idleImage.empty() && cfg.moveImage.empty()) {
        PaimonNotify::create(
            "Pick an idle or move image first. The system cursor will stay visible until then.",
            NotificationIcon::Info
        )->show();
    }
}

void CursorConfigPopup::onFollowDelayToggled(CCObject*) {
    auto& cfg = CursorManager::get().config();
    cfg.followDelayEnabled = !m_followDelayToggle->isToggled();
    CursorManager::get().saveConfig();
}

void CursorConfigPopup::onFollowDelayChanged(CCObject*) {
    if (!m_followDelaySlider) return;
    auto& cfg = CursorManager::get().config();
    cfg.followDelay = readSlider(m_followDelaySlider, 0.f, 1.f);
    if (m_followDelayLabel) m_followDelayLabel->setString(fmt::format("{:.2f}", cfg.followDelay).c_str());
    CursorManager::get().saveConfig();
}

void CursorConfigPopup::onTrailToggled(CCObject*) {
    CursorManager::get().config().trailEnabled = !m_trailToggle->isToggled();
    applyLive();
}

void CursorConfigPopup::onHoverToggled(CCObject*) {
    auto& cfg = CursorManager::get().config();
    cfg.hoverEnabled = !m_hoverToggle->isToggled();
    CursorManager::get().saveConfig();

    if (cfg.hoverEnabled && cfg.hoverImage.empty()) {
        PaimonNotify::create(
            "Assign a Hover image in the Gallery tab to see this state.",
            NotificationIcon::Info
        )->show();
    }
}

void CursorConfigPopup::onClickToggled(CCObject*) {
    auto& cfg = CursorManager::get().config();
    cfg.clickEnabled = !m_clickToggle->isToggled();
    CursorManager::get().saveConfig();

    if (cfg.clickEnabled && cfg.clickImage.empty()) {
        PaimonNotify::create(
            "Assign a Click image in the Gallery tab to see this state.",
            NotificationIcon::Info
        )->show();
    }
}

void CursorConfigPopup::onTextToggled(CCObject*) {
    auto& cfg = CursorManager::get().config();
    cfg.textEnabled = !m_textToggle->isToggled();
    CursorManager::get().saveConfig();

    if (cfg.textEnabled && cfg.textImage.empty()) {
        PaimonNotify::create(
            "Assign a Text image in the Gallery tab to see this state.",
            NotificationIcon::Info
        )->show();
    }
}

void CursorConfigPopup::onDisabledToggled(CCObject*) {
    auto& cfg = CursorManager::get().config();
    cfg.disabledEnabled = !m_disabledToggle->isToggled();
    CursorManager::get().saveConfig();

    if (cfg.disabledEnabled && cfg.disabledImage.empty()) {
        PaimonNotify::create(
            "Assign a Disabled image in the Gallery tab to see this state.",
            NotificationIcon::Info
        )->show();
    }
}

// trail preset callbacks

void CursorConfigPopup::updatePresetLabel() {
    if (!m_presetLabel) return;
    auto& cfg = CursorManager::get().config();
    if (cfg.trailPreset < 0 || cfg.trailPreset >= CursorManager::TRAIL_PRESET_COUNT) {
        m_presetLabel->setString("Custom");
    } else {
        m_presetLabel->setString(CursorManager::TRAIL_PRESETS[cfg.trailPreset].name);
    }
}

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

void CursorConfigPopup::onPresetPrev(CCObject*) {
    auto& cfg = CursorManager::get().config();
    cfg.trailPreset--;
    if (cfg.trailPreset < -1) cfg.trailPreset = CursorManager::TRAIL_PRESET_COUNT - 1;
    if (cfg.trailPreset >= 0) applyPresetToConfig(cfg);
    updatePresetLabel();
    applyLive();
}

void CursorConfigPopup::onPresetNext(CCObject*) {
    auto& cfg = CursorManager::get().config();
    cfg.trailPreset++;
    if (cfg.trailPreset >= CursorManager::TRAIL_PRESET_COUNT) cfg.trailPreset = -1;
    if (cfg.trailPreset >= 0) applyPresetToConfig(cfg);
    updatePresetLabel();
    applyLive();
}

void CursorConfigPopup::onEditTrail(CCObject*) {
    auto& cfg = CursorManager::get().config();
    cfg.trailPreset = -1;
    updatePresetLabel();

    std::string info = fmt::format(
        "<cy>Current Trail Values</c>\n\n"
        "Color: R={} G={} B={}\n"
        "Length: {:.0f}  Width: {:.1f}\n"
        "Fade Type: {}\n"
        "Opacity: {}\n\n"
        "Modify these values by editing <cy>cursor_config.json</c>\n"
        "in your mod save directory.",
        cfg.trailR, cfg.trailG, cfg.trailB,
        cfg.trailLength, cfg.trailWidth,
        cfg.trailFadeType == 0 ? "Linear" : (cfg.trailFadeType == 1 ? "Sine" : "None"),
        cfg.trailOpacity
    );

    FLAlertLayer::create(
        nullptr,
        "Custom Trail",
        info,
        "OK", nullptr, 360.f
    )->show();

    applyLive();
}

// layer visibility toggle

void CursorConfigPopup::onLayerToggled(CCObject* sender) {
    auto toggle = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggle) return;
    auto* nameStr = typeinfo_cast<CCString*>(toggle->getUserObject());
    if (!nameStr) return;

    std::string layerName = nameStr->getCString();
    bool nowOn = !toggle->isToggled();

    auto& layers = CursorManager::get().config().visibleLayers;
    if (nowOn) {
        layers.insert(layerName);
    } else {
        layers.erase(layerName);
    }
    applyLive();
}
