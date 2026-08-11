#include "CursorConfigPopup.hpp"
#include "ClickEffectTunePopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../services/CursorManager.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include "../../../utils/InfoButton.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
namespace kit = paimon::configkit;
}



CursorConfigPopup* CursorConfigPopup::create() {
    auto ret = new CursorConfigPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}


bool CursorConfigPopup::init() {
    if (!Popup::init(480.f, 310.f)) return false;

    this->setTitle("Cursor Personalizado");
    this->setMouseEnabled(true);

    auto content = m_mainLayer->getContentSize();

    int cleaned = CursorManager::get().cleanupInvalidImages();
    if (cleaned > 0) {
        log::info("[CursorConfig] Cleaned up {} invalid image files from gallery", cleaned);
    }

    m_galleryTab = CCNode::create();
    m_galleryTab->setID("cursor-gallery-tab"_spr);
    m_galleryTab->setContentSize(content);
    m_mainLayer->addChild(m_galleryTab, 5);

    m_settingsTab = CCNode::create();
    m_settingsTab->setID("cursor-settings-tab"_spr);
    m_settingsTab->setContentSize(content);
    m_settingsTab->setVisible(false);
    m_mainLayer->addChild(m_settingsTab, 5);

    m_trailTab = CCNode::create();
    m_trailTab->setID("cursor-trail-tab"_spr);
    m_trailTab->setContentSize(content);
    m_trailTab->setVisible(false);
    m_mainLayer->addChild(m_trailTab, 5);

    m_transitionTab = CCNode::create();
    m_transitionTab->setID("cursor-transition-tab"_spr);
    m_transitionTab->setContentSize(content);
    m_transitionTab->setVisible(false);
    m_mainLayer->addChild(m_transitionTab, 5);

    m_clickTab = CCNode::create();
    m_clickTab->setID("cursor-click-tab"_spr);
    m_clickTab->setContentSize(content);
    m_clickTab->setVisible(false);
    m_mainLayer->addChild(m_clickTab, 5);

    m_advancedTab = CCNode::create();
    m_advancedTab->setID("cursor-advanced-tab"_spr);
    m_advancedTab->setContentSize(content);
    m_advancedTab->setVisible(false);
    m_mainLayer->addChild(m_advancedTab, 5);

    createTabButtons();
    buildGalleryTab();
    buildShopTab();
    buildSettingsTab();
    buildTrailTab();
    buildTransitionTab();
    buildClickTab();
    buildAdvancedTab();

    this->schedule(schedule_selector(CursorConfigPopup::updateSmoothScroll));
    this->schedule(schedule_selector(CursorConfigPopup::updateTrailPreview));
    this->schedule(schedule_selector(CursorConfigPopup::updateTransitionPreview));
    this->schedule(schedule_selector(CursorConfigPopup::updateClickPreview));

    paimon::markDynamicPopup(this);
    return true;
}

void CursorConfigPopup::onExit() {
    flushTrailSave();
    flushTransitionSave();
    flushClickSave();
    if (m_shopTab) m_shopTab->shutdown();
    this->unschedule(schedule_selector(CursorConfigPopup::checkScrollPosition));
    this->unschedule(schedule_selector(CursorConfigPopup::updateSmoothScroll));
    this->unschedule(schedule_selector(CursorConfigPopup::updateTrailPreview));
    this->unschedule(schedule_selector(CursorConfigPopup::updateTransitionPreview));
    this->unschedule(schedule_selector(CursorConfigPopup::updateClickPreview));
    if (m_scrollArrow) {
        m_scrollArrow->stopAllActions();
    }
    Popup::onExit();
}

void CursorConfigPopup::scrollWheel(float x, float y) {
    if (m_currentTab == 6) {
        kit::queueWheelScroll(m_advancedScroll, x, y, m_advancedScrollTargetY, m_advancedScrollTargetSet);
        return;
    }
    if (m_currentTab == 5) {
        kit::queueWheelScroll(m_clickScroll, x, y, m_clickScrollTargetY, m_clickScrollTargetSet);
        return;
    }
    if (m_currentTab == 4) {
        kit::queueWheelScroll(m_transitionScroll, x, y, m_transitionScrollTargetY, m_transitionScrollTargetSet);
        return;
    }
    if (m_currentTab == 3) {
        kit::queueWheelScroll(m_trailScroll, x, y, m_trailScrollTargetY, m_trailScrollTargetSet);
        return;
    }
    if (m_currentTab == 2) {
        kit::queueWheelScroll(m_scrollLayer, x, y, m_settingsScrollTargetY, m_settingsScrollTargetSet);
        return;
    }
    if (m_currentTab == 1) {
        if (m_shopTab) m_shopTab->handleScrollWheel(x, y);
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
    kit::stepWheelScroll(m_trailScroll, m_trailScrollTargetY, m_trailScrollTargetSet, dt);
    kit::stepWheelScroll(m_transitionScroll, m_transitionScrollTargetY, m_transitionScrollTargetSet, dt);
    kit::stepWheelScroll(m_clickScroll, m_clickScrollTargetY, m_clickScrollTargetSet, dt);
    if (m_shopTab) m_shopTab->stepScroll(dt);
}


void CursorConfigPopup::createTabButtons() {
    auto content = m_mainLayer->getContentSize();

    auto menu = CCMenu::create();
    menu->setID("cursor-tab-buttons-menu"_spr);
    menu->setContentSize({content.width, 30.f});
    menu->setLayout(
        RowLayout::create()
            ->setGap(7.f)
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

    auto sprShop = ButtonSprite::create("Tienda");
    sprShop->setScale(0.5f);
    auto tabShop = CCMenuItemSpriteExtra::create(sprShop, this, menu_selector(CursorConfigPopup::onTabSwitch));
    tabShop->setTag(1);
    tabShop->setID("cursor-shop-tab-btn"_spr);
    menu->addChild(tabShop);
    m_tabs.push_back(tabShop);

    auto spr2 = ButtonSprite::create("Ajustes");
    spr2->setScale(0.5f);
    auto tab2 = CCMenuItemSpriteExtra::create(spr2, this, menu_selector(CursorConfigPopup::onTabSwitch));
    tab2->setTag(2);
    tab2->setID("cursor-settings-tab-btn"_spr);
    menu->addChild(tab2);
    m_tabs.push_back(tab2);

    auto spr3 = ButtonSprite::create("Estela");
    spr3->setScale(0.5f);
    auto tab3 = CCMenuItemSpriteExtra::create(spr3, this, menu_selector(CursorConfigPopup::onTabSwitch));
    tab3->setTag(3);
    tab3->setID("cursor-trail-tab-btn"_spr);
    menu->addChild(tab3);
    m_tabs.push_back(tab3);

    auto spr4 = ButtonSprite::create("Transicion");
    spr4->setScale(0.5f);
    auto tab4 = CCMenuItemSpriteExtra::create(spr4, this, menu_selector(CursorConfigPopup::onTabSwitch));
    tab4->setTag(4);
    tab4->setID("cursor-transition-tab-btn"_spr);
    menu->addChild(tab4);
    m_tabs.push_back(tab4);

    auto spr5 = ButtonSprite::create("Click");
    spr5->setScale(0.5f);
    auto tab5 = CCMenuItemSpriteExtra::create(spr5, this, menu_selector(CursorConfigPopup::onTabSwitch));
    tab5->setTag(5);
    tab5->setID("cursor-click-tab-btn"_spr);
    menu->addChild(tab5);
    m_tabs.push_back(tab5);

    auto spr6 = ButtonSprite::create("Avanzado");
    spr6->setScale(0.5f);
    auto tab6 = CCMenuItemSpriteExtra::create(spr6, this, menu_selector(CursorConfigPopup::onTabSwitch));
    tab6->setTag(6);
    tab6->setID("cursor-advanced-tab-btn"_spr);
    menu->addChild(tab6);
    m_tabs.push_back(tab6);

    menu->updateLayout();
    onTabSwitch(tab1);
}

void CursorConfigPopup::onTabSwitch(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    m_currentTab = btn->getTag();

    m_galleryTab->setVisible(m_currentTab == 0);
    if (m_shopTab)     m_shopTab->setVisible(m_currentTab == 1);
    m_settingsTab->setVisible(m_currentTab == 2);
    if (m_trailTab)    m_trailTab->setVisible(m_currentTab == 3);
    if (m_transitionTab) m_transitionTab->setVisible(m_currentTab == 4);
    if (m_clickTab)    m_clickTab->setVisible(m_currentTab == 5);
    if (m_advancedTab) m_advancedTab->setVisible(m_currentTab == 6);

    if (m_currentTab == 1 && m_shopTab) m_shopTab->onShown();
    if (m_currentTab == 3 && m_previewTrail) m_previewTrail->reset();
    if (m_currentTab == 4) replayTransitionPreview();
    if (m_currentTab == 5 && m_clickPreview) {
        m_clickPreview->reset();
        m_clickPreviewHeld = false;
        m_clickPreviewAnimTime = 999.f;
        m_clickPreviewAnimHeld = false;
    }

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

static std::string buildStatesInfo() {
    return
        "<cy>Estados del cursor</c> - asigna una imagen distinta a cada uno:\n\n"
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

    if (auto* iBtn = PaimonInfo::createInfoBtn("Estados del Cursor", buildStatesInfo(), this, 0.5f)) {
        iBtn->setPosition({content.width - 24.f, content.height - 30.f});
        menu->addChild(iBtn);
    }

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

    auto trashSpr = CCSprite::createWithSpriteFrameName("GJ_trashBtn_001.png");
    if (trashSpr) {
        trashSpr->setScale(0.45f);
        auto trashBtn = CCMenuItemSpriteExtra::create(trashSpr, this, menu_selector(CursorConfigPopup::onDeletePack));
        trashBtn->setPosition({cx + 155.f, packY});
        menu->addChild(trashBtn);
    }

    float scrollW = content.width - 30.f;
    float scrollTop = packY - 18.f;
    float scrollBottom = 46.f;
    float scrollH = scrollTop - scrollBottom;

    m_thumbScroll = ScrollLayer::create({scrollW, scrollH});
    m_thumbScroll->setPosition({(content.width - scrollW) / 2.f, scrollBottom});
    m_galleryTab->addChild(m_thumbScroll, 1);
    // refreshGallery rebuilds the cells inside Geode's contentLayer.

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

void CursorConfigPopup::buildShopTab() {
    WeakRef<CursorConfigPopup> self = this;
    m_shopTab = CursorShopTab::create(m_mainLayer->getContentSize(), [self] {
        auto locked = self.lock();
        if (!locked) return;
        auto* popup = static_cast<CursorConfigPopup*>(locked.data());
        popup->refreshPackList();
        popup->refreshGallery();
        popup->refreshTransitionPreviewSprites();
        popup->syncEnableUI(CursorManager::get().config().enabled);
    });
    if (!m_shopTab) return;

    m_shopTab->setID("cursor-shop-tab"_spr);
    m_shopTab->setVisible(m_currentTab == 1);
    m_mainLayer->addChild(m_shopTab, 5);
}

void CursorConfigPopup::refreshPackList() {
    auto& cm = CursorManager::get();
    m_packList.clear();
    m_packList.push_back("");
    for (auto const& p : cm.getPacks()) {
        m_packList.push_back(p);
    }
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

    auto* content = m_thumbScroll->m_contentLayer;
    content->removeAllChildren();

    auto& cm = CursorManager::get();
    std::string pack = currentPack();
    auto images = cm.getImagesInPack(pack);

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
        auto cell = CCNode::create();
        cell->setContentSize({cellSize, cellSize});
        cell->setAnchorPoint({0.5f, 0.5f});
        grid->addChild(cell);

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

        auto bg = paimon::SpriteHelper::createColorPanel(cellSize, cellSize, bgColor, bgOpacity);
        bg->setPosition({0.f, 0.f});
        cell->addChild(bg, 0);

        auto tex = cm.loadGalleryThumb(images[i]);
        if (tex) {
            if (auto thumbSpr = CCSprite::createWithTexture(tex)) {
                float maxDim = std::max(thumbSpr->getContentSize().width,
                                        thumbSpr->getContentSize().height);
                if (maxDim > 0) thumbSpr->setScale((cellSize - 6.f) / maxDim);
                thumbSpr->setPosition({cellSize / 2.f, cellSize / 2.f});
                cell->addChild(thumbSpr, 1);

                auto imgPath = cm.galleryDir() / paimon::assets::pathFromUtf8(images[i]);
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

        auto cellMenu = CCMenu::create();
        cellMenu->setPosition({0.f, 0.f});
        cellMenu->setContentSize({cellSize, cellSize});
        cell->addChild(cellMenu, 5);

        auto selectArea = CCSprite::create();
        selectArea->setContentSize({cellSize, cellSize});
        selectArea->setOpacity(0);
        auto selectBtn = CCMenuItemSpriteExtra::create(selectArea, this, menu_selector(CursorConfigPopup::onSelectImage));
        selectBtn->setContentSize({cellSize, cellSize});
        selectBtn->setPosition({cellSize / 2.f, cellSize / 2.f});
        selectBtn->setUserObject(CCString::create(images[i]));
        cellMenu->addChild(selectBtn, 0);

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

    // Use Geode's scrollToTop(); GD's moveToTop() leaves the content misplaced.
    m_thumbScroll->scrollToTop();
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
            p->refreshTransitionPreviewSprites();
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

    // Keep the toggle in sync when the first image enables the cursor.
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
    refreshTransitionPreviewSprites();
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
            auto* p = static_cast<CursorConfigPopup*>(popup.data());
            p->refreshGallery();
            p->refreshTransitionPreviewSprites();
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
            p->refreshTransitionPreviewSprites();
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

        CursorState slot = p->m_activeSlot;
        if (CursorManager::get().imageForState(slot).empty()) {
            CursorManager::get().setImageForState(slot, imported.front());
        }
        p->syncEnableUI(CursorManager::get().config().enabled);
        p->refreshPackList();
        p->refreshGallery();
        p->refreshTransitionPreviewSprites();
    });
}


void CursorConfigPopup::buildSettingsTab() {
    auto content = m_mainLayer->getContentSize();
    float scrollW = content.width - 24.f;
    float scrollH = content.height - 58.f;
    float innerW = kit::cardInnerWidth(scrollW);

    auto& cfg = CursorManager::get().config();
    auto save = [] { CursorManager::get().saveConfig(); };

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

    auto* trailCard = kit::makeCard(scrollW, "Estela del cursor", {255, 200, 100}, {
        kit::makeToggleRow(innerW,
            "Mostrar estela",
            "Deja un rastro al mover el cursor.",
            cfg.trailEnabled,
            [this](bool v) {
                auto& c = CursorManager::get().config();
                c.trailEnabled = v;
                applyLive();
                if (v && !c.enabled) {
                    PaimonNotify::create(
                        "Activa el Cursor personalizado: la estela va con el.",
                        NotificationIcon::Info)->show();
                }
            }),
        kit::makeButtonRow(innerW,
            "Efectos y colores",
            "18 efectos, 6 modos de color y vista previa en vivo.",
            "Abrir",
            [this] {
                for (auto* tab : m_tabs) {
                    if (tab->getTag() == 2) { onTabSwitch(tab); break; }
                }
            }),
    });

    auto* footer = kit::makeHint(scrollW,
        "Consejo: en la pestana Avanzado puedes cambiar el cursor segun lo que "
        "toques (botones, texto...) y darle movimiento con retraso.");

    m_scrollLayer = kit::makeScrollStack({scrollW, scrollH},
        {hero, lookCard, trailCard, footer});
    m_scrollLayer->setPosition({12.f, 8.f});
    m_settingsTab->addChild(m_scrollLayer, 5);

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
        kit::makeButtonRow(innerW,
            "Transiciones de estado",
            "Anima el cambio entre Normal, Boton, Click y los demas estados.",
            "Configurar",
            [this] {
                for (auto* tab : m_tabs) {
                    if (tab->getTag() == 3) { onTabSwitch(tab); break; }
                }
            }),
    });

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

    auto* footer = kit::makeHint(scrollW,
        "El cursor aparece en todas las pantallas y se oculta solo durante el gameplay.");

    m_advancedScroll = kit::makeScrollStack({scrollW, scrollH},
        {statesCard, moveCard, footer});
    m_advancedScroll->setPosition({12.f, 8.f});
    m_advancedTab->addChild(m_advancedScroll, 5);
}


void CursorConfigPopup::buildTrailTab() {
    namespace fx = paimon::cursorfx;
    auto content = m_mainLayer->getContentSize();

    constexpr float kPreviewH = 84.f;
    float areaW = content.width - 24.f;
    float top   = content.height - 52.f;

    m_previewSize = CCSizeMake(areaW, kPreviewH);
    auto* frame = CCScale9Sprite::create("square02_001.png");
    if (frame) {
        frame->setContentSize(m_previewSize);
        frame->setAnchorPoint({0.f, 0.f});
        frame->setPosition({12.f, top - kPreviewH});
        frame->setColor({8, 10, 18});
        frame->setOpacity(225);
        m_trailTab->addChild(frame, 0);
    }

    auto* stencil = PaimonDrawNode::create();
    if (stencil) {
        CCPoint quad[4] = {
            ccp(0.f, 0.f), ccp(areaW, 0.f), ccp(areaW, kPreviewH), ccp(0.f, kPreviewH)
        };
        ccColor4F white = {1.f, 1.f, 1.f, 1.f};
        stencil->drawPolygon(quad, 4, white, 0.f, white);
    }

    auto* clip = CCClippingNode::create();
    if (clip && stencil) {
        clip->setStencil(stencil);
        clip->setAlphaThreshold(0.05f);
        clip->setContentSize(m_previewSize);
        clip->setAnchorPoint({0.f, 0.f});
        clip->setPosition({12.f, top - kPreviewH});
        m_trailTab->addChild(clip, 1);
        m_previewArea = clip;

        m_previewTrail = fx::CursorTrailNode::create();
        if (m_previewTrail) {
            m_previewTrail->applySettings(CursorManager::get().config().trail);
            clip->addChild(m_previewTrail, 0);
        }

        m_previewCursor = CursorManager::get().createPreviewSprite();
        if (m_previewCursor) {
            clip->addChild(m_previewCursor, 1);
            if (m_previewTrail) m_previewTrail->setEchoSource(m_previewCursor);
        }
    }

    auto* hint = CCLabelBMFont::create("Mueve el raton por aqui", "chatFont.fnt");
    if (hint) {
        hint->setScale(0.38f);
        hint->setOpacity(110);
        hint->setAnchorPoint({1.f, 0.f});
        hint->setPosition({content.width - 18.f, top - kPreviewH + 5.f});
        m_trailTab->addChild(hint, 2);
    }

    m_trailControls = CCNode::create();
    m_trailControls->setContentSize(content);
    m_trailTab->addChild(m_trailControls, 3);
    rebuildTrailControls();
}

void CursorConfigPopup::rebuildTrailControls() {
    namespace fx = paimon::cursorfx;
    if (!m_trailControls) return;

    float keepY = m_trailScroll ? m_trailScroll->m_contentLayer->getPositionY() : 0.f;
    bool hadScroll = m_trailScroll != nullptr;

    m_trailControls->removeAllChildren();
    m_trailScroll = nullptr;
    m_trailScrollTargetSet = false;

    auto content = m_mainLayer->getContentSize();
    float scrollW = content.width - 24.f;
    float scrollH = content.height - 148.f;
    float innerW = kit::cardInnerWidth(scrollW);

    auto& cfg = CursorManager::get().config();
    auto& t = cfg.trail;

    auto touch = [this] {
        auto& c = CursorManager::get().config();
        c.trailPreset = fx::findPresetIndex(c.trail);
        updatePresetLabel();
        applyTrailLive();
    };

    std::vector<std::string> presetNames;
    presetNames.reserve(static_cast<size_t>(fx::presetCount()) + 1);
    presetNames.push_back("Personalizado");
    for (int i = 0; i < fx::presetCount(); ++i) {
        presetNames.push_back(fx::presetAt(i).name);
    }
    int presetIdx = (cfg.trailPreset >= 0 && cfg.trailPreset < fx::presetCount())
        ? cfg.trailPreset + 1 : 0;

    CCLabelBMFont* presetLabel = nullptr;
    auto* presetCard = kit::makeCard(scrollW, "Estilos listos", {255, 200, 100}, {
        kit::makeSelectRow(innerW,
            "Preset",
            fmt::format("{} combinaciones ya armadas.", fx::presetCount()).c_str(),
            presetNames, presetIdx,
            [this](int idx) {
                auto& c = CursorManager::get().config();
                if (idx <= 0) {
                    c.trailPreset = -1;
                } else {
                    CursorManager::get().applyTrailPreset(idx - 1);
                }
                applyTrailLive();
                queueRebuildTrailControls();
            },
            &presetLabel),
    });
    m_presetLabel = presetLabel;

    std::vector<std::string> effectNames;
    effectNames.reserve(fx::kEffectCount);
    for (int i = 0; i < fx::kEffectCount; ++i) {
        effectNames.push_back(fx::effectName(static_cast<fx::TrailEffect>(i)));
    }
    std::vector<std::string> modeNames;
    modeNames.reserve(fx::kColorModeCount);
    for (int i = 0; i < fx::kColorModeCount; ++i) {
        modeNames.push_back(fx::colorModeName(static_cast<fx::TrailColorMode>(i)));
    }

    auto* effectCard = kit::makeCard(scrollW, "Efecto", {150, 200, 255}, {
        kit::makeSelectRow(innerW,
            "Tipo de estela", fx::effectDesc(t.effect),
            effectNames, static_cast<int>(t.effect),
            [this, touch](int idx) {
                CursorManager::get().config().trail.effect =
                    static_cast<fx::TrailEffect>(idx);
                touch();
                queueRebuildTrailControls();
            }),
        kit::makeSelectRow(innerW,
            "Colores", fx::colorModeDesc(t.colorMode),
            modeNames, static_cast<int>(t.colorMode),
            [this, touch](int idx) {
                CursorManager::get().config().trail.colorMode =
                    static_cast<fx::TrailColorMode>(idx);
                touch();
                queueRebuildTrailControls();
            }),
    });

    std::vector<CCNode*> colorRows;
    if (fx::colorModeUsesColor1(t.colorMode)) {
        colorRows.push_back(kit::makeColorRow(innerW,
            t.colorMode == fx::TrailColorMode::Solid ? "Color" : "Color 1",
            t.colorMode == fx::TrailColorMode::Speed ? "Con el cursor quieto." : nullptr,
            t.color1,
            [touch](ccColor3B c) {
                CursorManager::get().config().trail.color1 = c;
                touch();
            }));
    }
    if (fx::colorModeUsesColor2(t.colorMode)) {
        colorRows.push_back(kit::makeColorRow(innerW,
            "Color 2",
            t.colorMode == fx::TrailColorMode::Speed ? "A toda velocidad." : "Final del degradado.",
            t.color2,
            [touch](ccColor3B c) {
                CursorManager::get().config().trail.color2 = c;
                touch();
            }));
    }
    if (colorRows.empty()) {
        colorRows.push_back(kit::makeHint(innerW,
            "Este modo genera los colores solo, no hace falta elegirlos."));
        colorRows.push_back(kit::makeSliderRow(innerW,
            "Velocidad del arcoiris", "Que tan rapido cambia el tono.",
            t.hueSpeed, fx::kHueSpeedMin, fx::kHueSpeedMax,
            [](double v) { return fmt::format("x{:.1f}", v); },
            [touch](double v) {
                CursorManager::get().config().trail.hueSpeed = static_cast<float>(v);
                touch();
            }));
    }
    auto* colorCard = kit::makeCard(scrollW, "Color", {255, 150, 200}, colorRows);

    auto* tuneCard = kit::makeCard(scrollW, "Ajuste fino", {130, 240, 170}, {
        kit::makeSliderRow(innerW,
            "Duracion", "Cuanto tarda en desaparecer.",
            t.life, fx::kLifeMin, fx::kLifeMax,
            [](double v) { return fmt::format("{:.2f}s", v); },
            [touch](double v) {
                CursorManager::get().config().trail.life = static_cast<float>(v);
                touch();
            }),
        kit::makeSliderRow(innerW,
            "Tamano", "Grosor de la cinta o de las particulas.",
            t.size, fx::kSizeMin, fx::kSizeMax,
            [](double v) { return fmt::format("{:.1f}", v); },
            [touch](double v) {
                CursorManager::get().config().trail.size = static_cast<float>(v);
                touch();
            }),
        kit::makeSliderRow(innerW,
            "Cantidad", "Mas particulas (o mas puntos y satelites).",
            t.density, fx::kDensityMin, fx::kDensityMax,
            [](double v) { return fmt::format("x{:.1f}", v); },
            [touch](double v) {
                CursorManager::get().config().trail.density = static_cast<float>(v);
                touch();
            }),
        kit::makeSliderRow(innerW,
            "Opacidad", "100% = solido, menos = transparente.",
            static_cast<double>(t.opacity), 0.0, 255.0,
            [](double v) { return fmt::format("{}%", static_cast<int>(v / 255.0 * 100.0)); },
            [touch](double v) {
                CursorManager::get().config().trail.opacity =
                    std::clamp(static_cast<int>(v), 0, 255);
                touch();
            }),
        kit::makeToggleRow(innerW,
            "Brillo", "Mezcla aditiva: los cruces brillan mas.",
            t.glow,
            [touch](bool v) {
                CursorManager::get().config().trail.glow = v;
                touch();
            }),
    });

    auto* footer = kit::makeHint(scrollW,
        "La estela se dibuja encima de todo y se apaga sola durante el gameplay.");

    m_trailScroll = kit::makeScrollStack({scrollW, scrollH},
        {presetCard, effectCard, colorCard, tuneCard, footer});
    m_trailScroll->setPosition({12.f, 8.f});
    m_trailControls->addChild(m_trailScroll, 5);

    if (hadScroll) {
        float totalH = m_trailScroll->m_contentLayer->getContentSize().height;
        float minY = std::min(0.f, -(totalH - scrollH));
        m_trailScroll->m_contentLayer->setPositionY(std::clamp(keepY, minY, 0.f));
    }
}

void CursorConfigPopup::queueRebuildTrailControls() {
    WeakRef<CursorConfigPopup> self = this;
    Loader::get()->queueInMainThread([self]() {
        auto popup = self.lock();
        if (!popup) return;
        static_cast<CursorConfigPopup*>(popup.data())->rebuildTrailControls();
    });
}

void CursorConfigPopup::applyTrailLive() {
    auto& cm = CursorManager::get();
    cm.applyTrailLive();
    m_trailDirty = true;
    m_trailSaveTimer = 0.35f;
}

void CursorConfigPopup::flushTrailSave() {
    if (!m_trailDirty) return;
    m_trailDirty = false;
    m_trailSaveTimer = 0.f;
    CursorManager::get().saveConfig();
}

void CursorConfigPopup::updateTrailPreview(float dt) {
    if (m_trailDirty) {
        m_trailSaveTimer -= dt;
        if (m_trailSaveTimer <= 0.f) flushTrailSave();
    }

    if (m_currentTab != 3 || !m_previewTrail || !m_previewArea) return;

    m_previewTrail->applySettings(CursorManager::get().config().trail);

    CCPoint local = m_previewArea->convertToNodeSpace(geode::cocos::getMousePos());
    CCRect box{0.f, 0.f, m_previewSize.width, m_previewSize.height};

    CCPoint target;
    if (box.containsPoint(local)) {
        target = local;
        m_previewDemoTime = 0.f;
    } else {
        m_previewDemoTime += dt;
        float u = m_previewDemoTime * 0.9f;
        target = ccp(
            m_previewSize.width * (0.5f + 0.36f * std::sin(u)),
            m_previewSize.height * (0.5f + 0.30f * std::sin(u * 2.f)));
    }

    if (m_previewCursor) m_previewCursor->setPosition(target);
    m_previewTrail->step(dt, target);
}

void CursorConfigPopup::buildTransitionTab() {
    auto content = m_mainLayer->getContentSize();
    constexpr float kPreviewH = 84.f;
    float areaW = content.width - 24.f;
    float top = content.height - 52.f;

    m_transitionPreviewSize = CCSizeMake(areaW, kPreviewH);
    auto* frame = CCScale9Sprite::create("square02_001.png");
    if (frame) {
        frame->setContentSize(m_transitionPreviewSize);
        frame->setAnchorPoint({0.f, 0.f});
        frame->setPosition({12.f, top - kPreviewH});
        frame->setColor({8, 10, 18});
        frame->setOpacity(225);
        m_transitionTab->addChild(frame, 0);
    }

    auto* stencil = PaimonDrawNode::create();
    if (stencil) {
        CCPoint quad[4] = {
            ccp(0.f, 0.f), ccp(areaW, 0.f), ccp(areaW, kPreviewH), ccp(0.f, kPreviewH)
        };
        ccColor4F white = {1.f, 1.f, 1.f, 1.f};
        stencil->drawPolygon(quad, 4, white, 0.f, white);
    }

    auto* clip = CCClippingNode::create();
    if (clip && stencil) {
        clip->setStencil(stencil);
        clip->setAlphaThreshold(0.05f);
        clip->setContentSize(m_transitionPreviewSize);
        clip->setAnchorPoint({0.f, 0.f});
        clip->setPosition({12.f, top - kPreviewH});
        m_transitionTab->addChild(clip, 1);
        m_transitionPreviewArea = clip;

        refreshTransitionPreviewSprites();
    }

    m_transitionStateLabel = CCLabelBMFont::create("Normal  >  Mover", "chatFont.fnt");
    if (m_transitionStateLabel) {
        m_transitionStateLabel->setScale(0.48f);
        m_transitionStateLabel->setOpacity(190);
        m_transitionStateLabel->setAnchorPoint({0.f, 1.f});
        m_transitionStateLabel->setPosition({18.f, top - 6.f});
        m_transitionTab->addChild(m_transitionStateLabel, 2);
    }

    auto* hint = CCLabelBMFont::create("Vista previa automatica", "chatFont.fnt");
    if (hint) {
        hint->setScale(0.38f);
        hint->setOpacity(110);
        hint->setAnchorPoint({1.f, 0.f});
        hint->setPosition({content.width - 18.f, top - kPreviewH + 5.f});
        m_transitionTab->addChild(hint, 2);
    }

    m_transitionControls = CCNode::create();
    m_transitionControls->setContentSize(content);
    m_transitionTab->addChild(m_transitionControls, 3);
    rebuildTransitionControls();
    replayTransitionPreview();
}

void CursorConfigPopup::rebuildTransitionControls() {
    namespace fx = paimon::cursorfx;
    if (!m_transitionControls) return;

    float keepY = m_transitionScroll ? m_transitionScroll->m_contentLayer->getPositionY() : 0.f;
    bool hadScroll = m_transitionScroll != nullptr;

    m_transitionControls->removeAllChildren();
    m_transitionScroll = nullptr;
    m_transitionScrollTargetSet = false;

    auto content = m_mainLayer->getContentSize();
    float scrollW = content.width - 24.f;
    float scrollH = content.height - 148.f;
    float innerW = kit::cardInnerWidth(scrollW);
    auto& cfg = CursorManager::get().config();
    auto& transition = cfg.transition;

    auto touch = [this] {
        auto& c = CursorManager::get().config();
        c.transitionPreset = paimon::cursorfx::findTransitionPreset(c.transition);
        updateTransitionPresetLabel();
        applyTransitionLive();
        replayTransitionPreview();
    };

    auto* enabled = kit::makeHeroToggle(scrollW,
        "Transiciones de estado",
        "Anima el cambio entre las imagenes Normal, Boton, Click, Texto y las demas.",
        cfg.transitionEnabled,
        [this](bool value) {
            paimon::modules::setEnabled("paimbnails.cursortransition.global", value);
            replayTransitionPreview();
        });

    std::vector<std::string> presetNames;
    presetNames.reserve(static_cast<size_t>(fx::transitionPresetCount()) + 1);
    presetNames.push_back("Personalizado");
    for (int i = 0; i < fx::transitionPresetCount(); ++i) {
        presetNames.push_back(fx::transitionPresetAt(i).name);
    }
    int presetIndex = cfg.transitionPreset >= 0 &&
        cfg.transitionPreset < fx::transitionPresetCount()
        ? cfg.transitionPreset + 1 : 0;

    CCLabelBMFont* presetLabel = nullptr;
    auto* presetCard = kit::makeCard(scrollW, "Estilos listos", {255, 200, 100}, {
        kit::makeSelectRow(innerW,
            "Preset",
            fmt::format("{} estilos preparados.", fx::transitionPresetCount()).c_str(),
            presetNames, presetIndex,
            [this](int index) {
                auto& c = CursorManager::get().config();
                if (index <= 0) c.transitionPreset = -1;
                else CursorManager::get().applyTransitionPreset(index - 1);
                applyTransitionLive();
                replayTransitionPreview();
                queueRebuildTransitionControls();
            },
            &presetLabel),
    });
    m_transitionPresetLabel = presetLabel;

    std::vector<std::string> effectNames;
    effectNames.reserve(fx::kTransitionEffectCount);
    for (int i = 0; i < fx::kTransitionEffectCount; ++i) {
        effectNames.push_back(fx::transitionEffectName(static_cast<fx::TransitionEffect>(i)));
    }

    std::vector<std::string> easingNames;
    easingNames.reserve(fx::kTransitionEasingCount);
    for (int i = 0; i < fx::kTransitionEasingCount; ++i) {
        easingNames.push_back(fx::transitionEasingName(static_cast<fx::TransitionEasing>(i)));
    }

    auto* effectCard = kit::makeCard(scrollW, "Movimiento", {150, 200, 255}, {
        kit::makeSelectRow(innerW,
            "Tipo de transicion", fx::transitionEffectDesc(transition.effect),
            effectNames, static_cast<int>(transition.effect),
            [this, touch](int index) {
                CursorManager::get().config().transition.effect =
                    static_cast<paimon::cursorfx::TransitionEffect>(index);
                touch();
                queueRebuildTransitionControls();
            }),
        kit::makeSelectRow(innerW,
            "Curva", fx::transitionEasingDesc(transition.easing),
            easingNames, static_cast<int>(transition.easing),
            [this, touch](int index) {
                CursorManager::get().config().transition.easing =
                    static_cast<paimon::cursorfx::TransitionEasing>(index);
                touch();
                queueRebuildTransitionControls();
            }),
    });

    auto* tuneCard = kit::makeCard(scrollW, "Ajuste fino", {130, 240, 170}, {
        kit::makeSliderRow(innerW,
            "Duracion", "Cuanto tarda en completarse cada cambio.",
            transition.duration, fx::kTransitionDurationMin, fx::kTransitionDurationMax,
            [](double value) { return fmt::format("{:.2f}s", value); },
            [touch](double value) {
                CursorManager::get().config().transition.duration = static_cast<float>(value);
                touch();
            }),
        kit::makeSliderRow(innerW,
            "Intensidad", "Distancia, giro y deformacion del efecto.",
            transition.intensity, fx::kTransitionIntensityMin, fx::kTransitionIntensityMax,
            [](double value) { return fmt::format("x{:.2f}", value); },
            [touch](double value) {
                CursorManager::get().config().transition.intensity = static_cast<float>(value);
                touch();
            }),
        kit::makeButtonRow(innerW,
            "Repetir vista previa", "Vuelve a reproducir el cambio actual.", "Probar",
            [this] { replayTransitionPreview(); }),
    });

    auto* footer = kit::makeHint(scrollW,
        "La vista previa recorre todos los estados. Si alguno no tiene imagen, usa Normal igual que el cursor real.");

    m_transitionScroll = kit::makeScrollStack(
        {scrollW, scrollH}, {enabled, presetCard, effectCard, tuneCard, footer});
    m_transitionScroll->setPosition({12.f, 8.f});
    m_transitionControls->addChild(m_transitionScroll, 5);

    if (hadScroll) {
        float totalH = m_transitionScroll->m_contentLayer->getContentSize().height;
        float minY = std::min(0.f, -(totalH - scrollH));
        m_transitionScroll->m_contentLayer->setPositionY(std::clamp(keepY, minY, 0.f));
    }
}

void CursorConfigPopup::queueRebuildTransitionControls() {
    WeakRef<CursorConfigPopup> self = this;
    Loader::get()->queueInMainThread([self]() {
        auto popup = self.lock();
        if (!popup) return;
        static_cast<CursorConfigPopup*>(popup.data())->rebuildTransitionControls();
    });
}

void CursorConfigPopup::applyTransitionLive() {
    CursorManager::get().applyTransitionLive();
    m_transitionDirty = true;
    m_transitionSaveTimer = 0.35f;
}

void CursorConfigPopup::flushTransitionSave() {
    if (!m_transitionDirty) return;
    m_transitionDirty = false;
    m_transitionSaveTimer = 0.f;
    CursorManager::get().saveConfig();
}

void CursorConfigPopup::refreshTransitionPreviewSprites() {
    if (!m_transitionPreviewArea) return;

    for (int i = 0; i < kSlotCount; ++i) {
        if (m_transitionSprites[i]) {
            m_transitionSprites[i]->removeFromParent();
            m_transitionSprites[i] = nullptr;
        }

        auto* sprite = CursorManager::get().createPreviewSprite(kSlotStates[i]);
        if (!sprite) continue;
        sprite->setVisible(false);
        m_transitionPreviewArea->addChild(sprite, 1);
        m_transitionSprites[i] = sprite;
        m_transitionBaseScales[i] = ccp(sprite->getScaleX(), sprite->getScaleY());
    }
    replayTransitionPreview();
}

void CursorConfigPopup::replayTransitionPreview() {
    m_transitionPreviewTime = 0.f;
    if (m_transitionStateLabel) {
        m_transitionStateLabel->setString(fmt::format("{}  >  {}",
            slotDisplayName(kSlotStates[m_transitionFrom]),
            slotDisplayName(kSlotStates[m_transitionTo])).c_str());
    }
}

void CursorConfigPopup::updateTransitionPreview(float dt) {
    namespace fx = paimon::cursorfx;

    if (m_transitionDirty) {
        m_transitionSaveTimer -= dt;
        if (m_transitionSaveTimer <= 0.f) flushTransitionSave();
    }

    if (m_currentTab != 4 || !m_transitionPreviewArea) return;

    auto const& settings = CursorManager::get().config().transition;
    float duration = std::clamp(
        settings.duration, fx::kTransitionDurationMin, fx::kTransitionDurationMax);
    constexpr float kPause = 0.65f;

    m_transitionPreviewTime += std::max(dt, 0.f);
    if (m_transitionPreviewTime >= duration + kPause) {
        m_transitionFrom = m_transitionTo;
        m_transitionTo = (m_transitionTo + 1) % kSlotCount;
        replayTransitionPreview();
    }

    float progress = std::min(m_transitionPreviewTime / duration, 1.f);
    for (auto* sprite : m_transitionSprites) {
        if (sprite) sprite->setVisible(false);
    }

    CCPoint center = ccp(
        m_transitionPreviewSize.width * 0.47f,
        m_transitionPreviewSize.height * 0.66f);
    auto apply = [&](int index, bool incoming) {
        auto* sprite = m_transitionSprites[index];
        if (!sprite) return;
        sprite->setVisible(true);
        fx::applyTransitionFrame(
            sprite, center, m_transitionBaseScales[index], 255,
            fx::sampleTransition(settings, progress, incoming));
    };

    apply(m_transitionFrom, false);
    apply(m_transitionTo, true);
}

void CursorConfigPopup::updateTransitionPresetLabel() {
    if (!m_transitionPresetLabel) return;
    auto const& cfg = CursorManager::get().config();
    if (cfg.transitionPreset < 0 ||
        cfg.transitionPreset >= paimon::cursorfx::transitionPresetCount()) {
        m_transitionPresetLabel->setString("Personalizado");
    } else {
        m_transitionPresetLabel->setString(
            paimon::cursorfx::transitionPresetAt(cfg.transitionPreset).name);
    }
}


void CursorConfigPopup::buildClickTab() {
    namespace fx = paimon::cursorfx;
    auto content = m_mainLayer->getContentSize();
    constexpr float kPreviewH = 84.f;
    float areaW = content.width - 24.f;
    float top = content.height - 52.f;

    m_clickPreviewSize = CCSizeMake(areaW, kPreviewH);
    auto* frame = CCScale9Sprite::create("square02_001.png");
    if (frame) {
        frame->setContentSize(m_clickPreviewSize);
        frame->setAnchorPoint({0.f, 0.f});
        frame->setPosition({12.f, top - kPreviewH});
        frame->setColor({8, 10, 18});
        frame->setOpacity(225);
        m_clickTab->addChild(frame, 0);
    }

    auto* stencil = PaimonDrawNode::create();
    if (stencil) {
        CCPoint quad[4] = {
            ccp(0.f, 0.f), ccp(areaW, 0.f), ccp(areaW, kPreviewH), ccp(0.f, kPreviewH)
        };
        ccColor4F white = {1.f, 1.f, 1.f, 1.f};
        stencil->drawPolygon(quad, 4, white, 0.f, white);
    }

    auto* clip = CCClippingNode::create();
    if (clip && stencil) {
        clip->setStencil(stencil);
        clip->setAlphaThreshold(0.05f);
        clip->setContentSize(m_clickPreviewSize);
        clip->setAnchorPoint({0.f, 0.f});
        clip->setPosition({12.f, top - kPreviewH});
        m_clickTab->addChild(clip, 1);
        m_clickPreviewArea = clip;

        m_clickPreview = fx::CursorClickNode::create();
        if (m_clickPreview) {
            m_clickPreview->applySettings(CursorManager::get().config().click);
            clip->addChild(m_clickPreview, 0);
        }

        m_clickPreviewCursor = CursorManager::get().createPreviewSprite();
        if (m_clickPreviewCursor) {
            clip->addChild(m_clickPreviewCursor, 1);
            m_clickCursorBaseScale = ccp(m_clickPreviewCursor->getScaleX(),
                                         m_clickPreviewCursor->getScaleY());
        }
    }

    m_clickHintLabel = CCLabelBMFont::create("Haz click aqui dentro para probar", "chatFont.fnt");
    if (m_clickHintLabel) {
        m_clickHintLabel->setScale(0.38f);
        m_clickHintLabel->setOpacity(110);
        m_clickHintLabel->setAnchorPoint({1.f, 0.f});
        m_clickHintLabel->setPosition({content.width - 18.f, top - kPreviewH + 5.f});
        m_clickTab->addChild(m_clickHintLabel, 2);
    }

    m_clickControls = CCNode::create();
    m_clickControls->setContentSize(content);
    m_clickTab->addChild(m_clickControls, 3);
    rebuildClickControls();
}

void CursorConfigPopup::rebuildClickControls() {
    namespace fx = paimon::cursorfx;
    if (!m_clickControls) return;

    float keepY = m_clickScroll ? m_clickScroll->m_contentLayer->getPositionY() : 0.f;
    bool hadScroll = m_clickScroll != nullptr;

    m_clickControls->removeAllChildren();
    m_clickScroll = nullptr;
    m_clickScrollTargetSet = false;

    auto content = m_mainLayer->getContentSize();
    float scrollW = content.width - 24.f;
    float scrollH = content.height - 148.f;
    float innerW = kit::cardInnerWidth(scrollW);

    auto& cfg = CursorManager::get().config();
    auto& c = cfg.click;

    auto touch = [this] {
        auto& conf = CursorManager::get().config();
        conf.clickPreset = fx::findClickPreset(conf.click);
        updateClickPresetLabel();
        applyClickLive();
    };

    std::vector<std::string> burstNames;
    burstNames.reserve(fx::kClickBurstCount);
    for (int i = 0; i < fx::kClickBurstCount; ++i) {
        burstNames.push_back(fx::clickBurstName(static_cast<fx::ClickBurst>(i)));
    }
    std::vector<std::string> holdNames;
    holdNames.reserve(fx::kClickHoldCount);
    for (int i = 0; i < fx::kClickHoldCount; ++i) {
        holdNames.push_back(fx::clickHoldName(static_cast<fx::ClickHold>(i)));
    }
    std::vector<std::string> animNames;
    animNames.reserve(fx::kClickAnimCount);
    for (int i = 0; i < fx::kClickAnimCount; ++i) {
        animNames.push_back(fx::clickAnimName(static_cast<fx::ClickAnim>(i)));
    }
    std::vector<std::string> soundNames;
    soundNames.reserve(fx::kClickSoundCount);
    for (int i = 0; i < fx::kClickSoundCount; ++i) {
        soundNames.push_back(fx::clickSoundName(static_cast<fx::ClickSound>(i)));
    }
    std::vector<std::string> modeNames;
    modeNames.reserve(fx::kColorModeCount);
    for (int i = 0; i < fx::kColorModeCount; ++i) {
        modeNames.push_back(fx::colorModeName(static_cast<fx::TrailColorMode>(i)));
    }

    auto* enabled = kit::makeHeroToggle(scrollW,
        "Efectos de click",
        "Estallidos, efectos al mantener apretado, reaccion del cursor y sonido.",
        cfg.clickFxEnabled,
        [this](bool value) {
            paimon::modules::setEnabled("paimbnails.cursorclick.global", value);
            applyClickLive();
        });

    std::vector<std::string> presetNames;
    presetNames.reserve(static_cast<size_t>(fx::clickPresetCount()) + 1);
    presetNames.push_back("Personalizado");
    for (int i = 0; i < fx::clickPresetCount(); ++i) {
        presetNames.push_back(fx::clickPresetAt(i).name);
    }
    int presetIndex = cfg.clickPreset >= 0 && cfg.clickPreset < fx::clickPresetCount()
        ? cfg.clickPreset + 1 : 0;

    CCLabelBMFont* presetLabel = nullptr;
    auto* presetCard = kit::makeCard(scrollW, "Estilos listos", {255, 200, 100}, {
        kit::makeSelectRow(innerW,
            "Preset",
            fmt::format("{} combinaciones ya armadas.", fx::clickPresetCount()).c_str(),
            presetNames, presetIndex,
            [this](int index) {
                auto& conf = CursorManager::get().config();
                if (index <= 0) conf.clickPreset = -1;
                else CursorManager::get().applyClickPreset(index - 1);
                applyClickLive();
                triggerPreviewClick();
                queueRebuildClickControls();
            },
            &presetLabel),
        kit::makeButtonRow(innerW,
            "Probar", "Lanza el efecto en la vista previa de arriba.", "Probar",
            [this] { triggerPreviewClick(); }),
    });
    m_clickPresetLabel = presetLabel;

    auto* burstCard = kit::makeCard(scrollW, "Al hacer click", {150, 200, 255}, {
        kit::makeSelectRow(innerW,
            "Estallido", fx::clickBurstDesc(c.press),
            burstNames, static_cast<int>(c.press),
            [this, touch](int index) {
                CursorManager::get().config().click.press =
                    static_cast<fx::ClickBurst>(index);
                touch();
                triggerPreviewClick();
                queueRebuildClickControls();
            },
            nullptr,
            [this] { openBurstTuning(false); }),
        kit::makeSelectRow(innerW,
            "Al soltar", fx::clickBurstDesc(c.release),
            burstNames, static_cast<int>(c.release),
            [this, touch](int index) {
                CursorManager::get().config().click.release =
                    static_cast<fx::ClickBurst>(index);
                touch();
                triggerPreviewClick();
                queueRebuildClickControls();
            },
            nullptr,
            [this] { openBurstTuning(true); }),
    });

    auto* holdCard = kit::makeCard(scrollW, "Mientras mantienes", {180, 150, 255}, {
        kit::makeSelectRow(innerW,
            "Efecto", fx::clickHoldDesc(c.hold),
            holdNames, static_cast<int>(c.hold),
            [this, touch](int index) {
                CursorManager::get().config().click.hold =
                    static_cast<fx::ClickHold>(index);
                touch();
                triggerPreviewClick();
                queueRebuildClickControls();
            },
            nullptr,
            [this] { openHoldTuning(); }),
    });

    auto* animCard = kit::makeCard(scrollW, "Reaccion del cursor", {255, 170, 120}, {
        kit::makeSelectRow(innerW,
            "Movimiento", fx::clickAnimDesc(c.anim),
            animNames, static_cast<int>(c.anim),
            [this, touch](int index) {
                CursorManager::get().config().click.anim =
                    static_cast<fx::ClickAnim>(index);
                touch();
                triggerPreviewClick();
                queueRebuildClickControls();
            }),
        kit::makeSliderRow(innerW,
            "Intensidad", "Cuanto se deforma el cursor.",
            c.animStrength, fx::kClickAnimMin, fx::kClickAnimMax,
            [](double value) { return fmt::format("x{:.2f}", value); },
            [touch](double value) {
                CursorManager::get().config().click.animStrength = static_cast<float>(value);
                touch();
            }),
        kit::makeSliderRow(innerW,
            "Duracion", "Cuanto tarda en llegar a la pose y en volver.",
            c.animDuration, fx::kClickAnimDurMin, fx::kClickAnimDurMax,
            [](double value) { return fmt::format("{:.2f}s", value); },
            [touch](double value) {
                CursorManager::get().config().click.animDuration = static_cast<float>(value);
                touch();
            }),
    });

    auto* soundCard = kit::makeCard(scrollW, "Sonido", {130, 240, 200}, {
        kit::makeSelectRow(innerW,
            "Al apretar", "Se reproduce en el momento del click.",
            soundNames, static_cast<int>(c.pressSound),
            [this, touch](int index) {
                auto& conf = CursorManager::get().config();
                conf.click.pressSound = static_cast<fx::ClickSound>(index);
                touch();
                fx::playClickSound(conf.click.pressSound, conf.click.volume,
                                   conf.click.pitch, conf.click.randomPitch);
            }),
        kit::makeSelectRow(innerW,
            "Al soltar", "Se reproduce al levantar el boton.",
            soundNames, static_cast<int>(c.releaseSound),
            [this, touch](int index) {
                auto& conf = CursorManager::get().config();
                conf.click.releaseSound = static_cast<fx::ClickSound>(index);
                touch();
                fx::playClickSound(conf.click.releaseSound, conf.click.volume,
                                   conf.click.pitch, conf.click.randomPitch);
            }),
        kit::makeSliderRow(innerW,
            "Volumen", "0% deja el sonido en silencio.",
            static_cast<double>(c.volume), 0.0, 1.0,
            [](double value) { return fmt::format("{}%", static_cast<int>(value * 100.0)); },
            [touch](double value) {
                CursorManager::get().config().click.volume = static_cast<float>(value);
                touch();
            }),
        kit::makeSliderRow(innerW,
            "Tono", "Mas alto suena agudo, mas bajo suena grave.",
            c.pitch, fx::kClickPitchMin, fx::kClickPitchMax,
            [](double value) { return fmt::format("x{:.2f}", value); },
            [touch](double value) {
                CursorManager::get().config().click.pitch = static_cast<float>(value);
                touch();
            }),
        kit::makeToggleRow(innerW,
            "Tono al azar", "Varia un poco en cada click para que no canse.",
            c.randomPitch,
            [touch](bool value) {
                CursorManager::get().config().click.randomPitch = value;
                touch();
            }),
    });

    std::vector<CCNode*> colorRows;
    colorRows.push_back(kit::makeSelectRow(innerW,
        "Modo", fx::colorModeDesc(c.colorMode),
        modeNames, static_cast<int>(c.colorMode),
        [this, touch](int index) {
            CursorManager::get().config().click.colorMode =
                static_cast<fx::TrailColorMode>(index);
            touch();
            queueRebuildClickControls();
        }));
    if (fx::colorModeUsesColor1(c.colorMode)) {
        colorRows.push_back(kit::makeColorRow(innerW,
            c.colorMode == fx::TrailColorMode::Solid ? "Color" : "Color 1",
            c.colorMode == fx::TrailColorMode::Speed ? "Apenas apretas." : nullptr,
            c.color1,
            [touch](ccColor3B value) {
                CursorManager::get().config().click.color1 = value;
                touch();
            }));
    }
    if (fx::colorModeUsesColor2(c.colorMode)) {
        colorRows.push_back(kit::makeColorRow(innerW,
            "Color 2",
            c.colorMode == fx::TrailColorMode::Speed
                ? "Tras un rato manteniendo." : "Final del degradado.",
            c.color2,
            [touch](ccColor3B value) {
                CursorManager::get().config().click.color2 = value;
                touch();
            }));
    }
    if (c.colorMode == fx::TrailColorMode::RainbowCycle ||
        c.colorMode == fx::TrailColorMode::RainbowTrail) {
        colorRows.push_back(kit::makeSliderRow(innerW,
            "Velocidad del arcoiris", "Que tan rapido cambia el tono.",
            c.hueSpeed, fx::kHueSpeedMin, fx::kHueSpeedMax,
            [](double value) { return fmt::format("x{:.1f}", value); },
            [touch](double value) {
                CursorManager::get().config().click.hueSpeed = static_cast<float>(value);
                touch();
            }));
    }
    auto* colorCard = kit::makeCard(scrollW, "Color", {255, 150, 200}, colorRows);

    auto* tuneCard = kit::makeCard(scrollW, "Ajuste fino", {130, 240, 170}, {
        kit::makeSliderRow(innerW,
            "Tamano", "Escala de las particulas, ondas y rayos.",
            c.size, fx::kClickSizeMin, fx::kClickSizeMax,
            [](double value) { return fmt::format("x{:.2f}", value); },
            [touch](double value) {
                CursorManager::get().config().click.size = static_cast<float>(value);
                touch();
            }),
        kit::makeSliderRow(innerW,
            "Cantidad", "Cuantas cosas salen en cada click.",
            c.amount, fx::kClickAmountMin, fx::kClickAmountMax,
            [](double value) { return fmt::format("x{:.2f}", value); },
            [touch](double value) {
                CursorManager::get().config().click.amount = static_cast<float>(value);
                touch();
            }),
        kit::makeSliderRow(innerW,
            "Duracion", "Cuanto tarda el estallido en apagarse.",
            c.life, fx::kClickLifeMin, fx::kClickLifeMax,
            [](double value) { return fmt::format("{:.2f}s", value); },
            [touch](double value) {
                CursorManager::get().config().click.life = static_cast<float>(value);
                touch();
            }),
        kit::makeSliderRow(innerW,
            "Fuerza", "Que tan lejos llega todo al salir.",
            c.spread, fx::kClickSpreadMin, fx::kClickSpreadMax,
            [](double value) { return fmt::format("x{:.2f}", value); },
            [touch](double value) {
                CursorManager::get().config().click.spread = static_cast<float>(value);
                touch();
            }),
        kit::makeSliderRow(innerW,
            "Opacidad", "100% = solido, menos = transparente.",
            static_cast<double>(c.opacity), 0.0, 255.0,
            [](double value) { return fmt::format("{}%", static_cast<int>(value / 255.0 * 100.0)); },
            [touch](double value) {
                CursorManager::get().config().click.opacity =
                    std::clamp(static_cast<int>(value), 0, 255);
                touch();
            }),
        kit::makeToggleRow(innerW,
            "Brillo", "Mezcla aditiva: los cruces brillan mas.",
            c.glow,
            [touch](bool value) {
                CursorManager::get().config().click.glow = value;
                touch();
            }),
    });

    auto* extrasCard = kit::makeCard(scrollW, "Extras", {200, 200, 210}, {
        kit::makeToggleRow(innerW,
            "Boton derecho", "El click derecho tambien dispara los efectos.",
            c.rightClick,
            [touch](bool value) {
                CursorManager::get().config().click.rightClick = value;
                touch();
            }),
    });

    auto* footer = kit::makeHint(scrollW,
        "Funcionan aunque el cursor personalizado este apagado, y en telefono salen donde tocas. "
        "Se dibujan encima de todo y se apagan solos durante el gameplay. "
        "El engranaje de cada efecto guarda su propio tamano y velocidad.");

    m_clickScroll = kit::makeScrollStack({scrollW, scrollH},
        {enabled, presetCard, burstCard, holdCard, animCard, soundCard, colorCard,
         tuneCard, extrasCard, footer});
    m_clickScroll->setPosition({12.f, 8.f});
    m_clickControls->addChild(m_clickScroll, 5);

    if (hadScroll) {
        float totalH = m_clickScroll->m_contentLayer->getContentSize().height;
        float minY = std::min(0.f, -(totalH - scrollH));
        m_clickScroll->m_contentLayer->setPositionY(std::clamp(keepY, minY, 0.f));
    }
}

void CursorConfigPopup::queueRebuildClickControls() {
    WeakRef<CursorConfigPopup> self = this;
    Loader::get()->queueInMainThread([self]() {
        auto popup = self.lock();
        if (!popup) return;
        static_cast<CursorConfigPopup*>(popup.data())->rebuildClickControls();
    });
}

void CursorConfigPopup::applyClickLive() {
    CursorManager::get().applyClickLive();
    m_clickDirty = true;
    m_clickSaveTimer = 0.35f;
}

void CursorConfigPopup::flushClickSave() {
    if (!m_clickDirty) return;
    m_clickDirty = false;
    m_clickSaveTimer = 0.f;
    CursorManager::get().saveConfig();
}

void CursorConfigPopup::updateClickPresetLabel() {
    if (!m_clickPresetLabel) return;
    auto const& cfg = CursorManager::get().config();
    if (cfg.clickPreset < 0 ||
        cfg.clickPreset >= paimon::cursorfx::clickPresetCount()) {
        m_clickPresetLabel->setString("Personalizado");
    } else {
        m_clickPresetLabel->setString(
            paimon::cursorfx::clickPresetAt(cfg.clickPreset).name);
    }
}

void CursorConfigPopup::openBurstTuning(bool release) {
    namespace fx = paimon::cursorfx;
    auto& click = CursorManager::get().config().click;
    auto effect = release ? click.release : click.press;
    if (effect == fx::ClickBurst::None) {
        PaimonNotify::show("Elige un efecto antes de ajustarlo.", NotificationIcon::Info);
        return;
    }

    auto index = static_cast<size_t>(effect);
    auto const& tune = click.burstTuning[index];
    auto* popup = ClickEffectTunePopup::create(
        fx::clickBurstName(effect), fx::clickBurstDesc(effect),
        tune.size, tune.speed,
        [this, index](float size, float speed) {
            auto& target = CursorManager::get().config().click.burstTuning[index];
            target.size = size;
            target.speed = speed;
            applyClickLive();
        },
        [this] { triggerPreviewClick(); });
    if (popup) popup->show();
}

void CursorConfigPopup::openHoldTuning() {
    namespace fx = paimon::cursorfx;
    auto& click = CursorManager::get().config().click;
    if (click.hold == fx::ClickHold::None) {
        PaimonNotify::show("Elige un efecto antes de ajustarlo.", NotificationIcon::Info);
        return;
    }

    auto index = static_cast<size_t>(click.hold);
    auto const& tune = click.holdTuning[index];
    auto* popup = ClickEffectTunePopup::create(
        fx::clickHoldName(click.hold), fx::clickHoldDesc(click.hold),
        tune.size, tune.speed,
        [this, index](float size, float speed) {
            auto& target = CursorManager::get().config().click.holdTuning[index];
            target.size = size;
            target.speed = speed;
            applyClickLive();
        },
        [this] { triggerPreviewClick(); });
    if (popup) popup->show();
}

void CursorConfigPopup::triggerPreviewClick() {
    namespace fx = paimon::cursorfx;
    if (!m_clickPreview) return;

    auto const& click = CursorManager::get().config().click;
    m_clickPreview->applySettings(click);
    m_clickPreview->press(ccp(m_clickPreviewSize.width * 0.5f,
                              m_clickPreviewSize.height * 0.55f));
    fx::playClickSound(click.pressSound, click.volume, click.pitch, click.randomPitch);
    m_clickPreviewAnimTime = 0.f;
    m_clickPreviewAnimHeld = true;
    m_clickTestTimer = 0.45f;
}

void CursorConfigPopup::updateClickPreview(float dt) {
    namespace fx = paimon::cursorfx;

    if (m_clickDirty) {
        m_clickSaveTimer -= dt;
        if (m_clickSaveTimer <= 0.f) flushClickSave();
    }

    if (m_currentTab != 5 || !m_clickPreview || !m_clickPreviewArea) return;

    auto const& click = CursorManager::get().config().click;
    m_clickPreview->applySettings(click);

    CCPoint local = m_clickPreviewArea->convertToNodeSpace(CursorManager::get().pointerPos());
    CCRect box{0.f, 0.f, m_clickPreviewSize.width, m_clickPreviewSize.height};
    bool inside = box.containsPoint(local);
    CCPoint target = inside
        ? local
        : ccp(m_clickPreviewSize.width * 0.5f, m_clickPreviewSize.height * 0.55f);

    auto pressPreview = [&] {
        m_clickPreview->press(target);
        fx::playClickSound(click.pressSound, click.volume, click.pitch, click.randomPitch);
        m_clickPreviewAnimTime = 0.f;
        m_clickPreviewAnimHeld = true;
    };
    auto releasePreview = [&] {
        m_clickPreview->release(target);
        fx::playClickSound(click.releaseSound, click.volume, click.pitch, click.randomPitch);
        m_clickPreviewAnimTime = 0.f;
        m_clickPreviewAnimHeld = false;
    };

    bool held = CursorManager::get().isClickFxHeld();
    bool edge = held != m_clickPreviewHeld;
    if (edge) m_clickPreviewHeld = held;

    if (m_clickTestTimer > 0.f) {
        m_clickTestTimer = edge ? 0.f : m_clickTestTimer - dt;
        if (m_clickTestTimer <= 0.f) {
            m_clickTestTimer = 0.f;
            releasePreview();
        }
    }

    if (edge) {
        if (held && inside) pressPreview();
        else if (!held && m_clickPreviewAnimHeld) releasePreview();
    }

    m_clickPreviewAnimTime += dt;
    m_clickPreview->step(dt, target, m_clickPreviewAnimHeld);

    if (m_clickPreviewCursor) {
        auto animFrame = fx::sampleClickAnim(click.anim, m_clickPreviewAnimTime,
                                             click.animDuration, click.animStrength,
                                             m_clickPreviewAnimHeld);
        fx::applyTransitionFrame(m_clickPreviewCursor, target,
                                 m_clickCursorBaseScale, 255, animFrame);
    }

    if (m_clickHintLabel) {
        m_clickHintLabel->setString(CursorManager::get().config().clickFxEnabled
            ? "Haz click aqui dentro para probar"
            : "Enciende los efectos para verlos en el juego");
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
    if (cfg.trailPreset < 0 || cfg.trailPreset >= paimon::cursorfx::presetCount()) {
        m_presetLabel->setString("Personalizado");
    } else {
        m_presetLabel->setString(paimon::cursorfx::presetAt(cfg.trailPreset).name);
    }
}
