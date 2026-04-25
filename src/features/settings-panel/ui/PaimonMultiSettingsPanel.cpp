#include "PaimonMultiSettingsPanel.hpp"
#include "SettingsCategoryBuilder.hpp"
#include "SettingsControls.hpp"
#include "../services/SettingsPanelManager.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>

using namespace cocos2d;
using namespace geode::prelude;

PaimonMultiSettingsPanel* PaimonMultiSettingsPanel::create(CCSprite* blurBg) {
    auto ret = new PaimonMultiSettingsPanel();
    if (ret && ret->init(blurBg)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool PaimonMultiSettingsPanel::init(CCSprite* blurBg) {
    if (!CCLayer::init()) return false;

    this->setID("paimon-multisettings-panel"_spr);
    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // 1. fondo blur (fullscreen)
    if (blurBg) {
        m_blurBg = blurBg;
        m_blurBg->setPosition(winSize * 0.5f);
        this->addChild(m_blurBg, -2);
    }

    // 2. overlay oscuro
    m_darkOverlay = CCLayerColor::create(ccc4(0, 0, 0, 0));
    m_darkOverlay->setContentSize(winSize);
    this->addChild(m_darkOverlay, -1);

    // 3. contenedor del panel (draggable)
    m_panelContainer = CCNode::create();
    m_panelContainer->setContentSize({PANEL_W, PANEL_H});
    m_panelContainer->setAnchorPoint({0.5f, 0.5f});
    m_panelContainer->setPosition(winSize * 0.5f);
    this->addChild(m_panelContainer, 1);

    // 4. fondo del panel (redondeado oscuro)
    ccColor4F panelColor = {0.08f, 0.08f, 0.1f, 0.95f};
    m_panelBg = paimon::SpriteHelper::createRoundedRect(PANEL_W, PANEL_H, CORNER_RADIUS, panelColor);
    if (m_panelBg) {
        m_panelBg->setPosition({0.f, 0.f});
        m_panelContainer->addChild(m_panelBg, 0);
    }

    // 5. construir UI interna
    buildTitleBar();
    buildSidebar();
    buildContentArea();

    // 6. seleccionar primera categoria
    selectCategory(0);

    // 7. touch handling
    // Usar getTargetPrio() para participar en el force priority system de GD.
    // Si hay un popup activo debajo, respetamos su prioridad.
    // Si no hay popup, getTargetPrio() retorna 0 y usamos -1 (por encima de menus normales).
    auto* dispatcher = CCDirector::sharedDirector()->getTouchDispatcher();
    int basePrio = dispatcher->getTargetPrio();
    m_touchPrio = basePrio - 1;
    m_childTouchPrio = basePrio - 2;

    this->setTouchEnabled(true);
    this->setTouchMode(kCCTouchesOneByOne);
    this->setTouchPriority(m_touchPrio);
    this->setKeypadEnabled(true);

    // 8. animacion de entrada
    runEntryAnimation();

    return true;
}

void PaimonMultiSettingsPanel::buildTitleBar() {
    // barra de titulo (mas oscura)
    ccColor4F titleColor = {0.05f, 0.05f, 0.07f, 1.0f};
    m_titleBarBg = paimon::SpriteHelper::createRoundedRect(PANEL_W, TITLE_BAR_H, CORNER_RADIUS, titleColor);
    if (m_titleBarBg) {
        m_titleBarBg->setPosition({0.f, PANEL_H - TITLE_BAR_H});
        m_panelContainer->addChild(m_titleBarBg, 1);
    }

    // tapar esquinas redondeadas inferiores de la title bar
    ccColor4F titleColorSolid = {0.05f, 0.05f, 0.07f, 1.0f};
    auto patchBottom = paimon::SpriteHelper::createRoundedRect(PANEL_W, CORNER_RADIUS + 2.f, 0.f, titleColorSolid);
    if (patchBottom) {
        patchBottom->setPosition({0.f, PANEL_H - TITLE_BAR_H});
        m_panelContainer->addChild(patchBottom, 1);
    }

    // titulo texto
    m_titleLabel = CCLabelBMFont::create("Paimon Settings", "goldFont.fnt");
    m_titleLabel->setScale(0.4f);
    m_titleLabel->setAnchorPoint({0.f, 0.5f});
    m_titleLabel->setPosition({12.f, PANEL_H - TITLE_BAR_H / 2.f});
    m_panelContainer->addChild(m_titleLabel, 2);

    // search bar
    m_searchInput = geode::TextInput::create(160.f, "Search...");
    m_searchInput->setScale(0.55f);
    m_searchInput->setAnchorPoint({0.5f, 0.5f});
    m_searchInput->setPosition({PANEL_W / 2.f + 50.f, PANEL_H - TITLE_BAR_H / 2.f});
    m_searchInput->setCallback([this](std::string const& text) {
        onSearchChanged(text);
    });
    m_panelContainer->addChild(m_searchInput, 2);

    // boton cerrar [X]
    auto closeMenu = CCMenu::create();
    closeMenu->setPosition({0.f, 0.f});
    closeMenu->setTouchPriority(m_childTouchPrio);
    m_panelContainer->addChild(closeMenu, 2);

    auto closeSpr = CCSprite::createWithSpriteFrameName("GJ_deleteIcon_001.png");
    if (!closeSpr) closeSpr = CCSprite::create();
    closeSpr->setScale(0.5f);
    auto closeBtn = CCMenuItemSpriteExtra::create(closeSpr, this, menu_selector(PaimonMultiSettingsPanel::onClose));
    closeBtn->setPosition({PANEL_W - 18.f, PANEL_H - TITLE_BAR_H / 2.f});
    closeMenu->addChild(closeBtn);
}

void PaimonMultiSettingsPanel::buildSidebar() {
    // fondo sidebar
    ccColor4F sidebarColor = {0.06f, 0.06f, 0.08f, 1.0f};
    m_sidebarBg = paimon::SpriteHelper::createRoundedRect(SIDEBAR_W, CONTENT_H, 0.f, sidebarColor);
    if (m_sidebarBg) {
        m_sidebarBg->setPosition({0.f, 0.f});
        m_panelContainer->addChild(m_sidebarBg, 1);
    }

    // acento de seleccion (barra izquierda)
    m_sidebarAccent = paimon::SpriteHelper::createRoundedRect(3.f, 20.f, 1.5f, {0.94f, 0.76f, 0.22f, 1.0f});
    if (m_sidebarAccent) {
        m_sidebarAccent->setPosition({0.f, 0.f});
        m_panelContainer->addChild(m_sidebarAccent, 3);
    }

    m_sidebarMenu = CCMenu::create();
    m_sidebarMenu->setPosition({0.f, 0.f});
    m_sidebarMenu->setTouchPriority(m_childTouchPrio);
    m_panelContainer->addChild(m_sidebarMenu, 2);

    auto const& groups = paimon::settings_ui::getAllGroups();
    float startY = CONTENT_H - 24.f;
    float spacing = 36.f;

    // colores para 6 grupos
    static const ccColor3B catColors[] = {
        {180, 180, 180}, // General
        {100, 200, 255}, // Thumbnails
        {100, 255, 200}, // Level Info
        {255, 100, 200}, // Audio
        {100, 180, 255}, // Backgrounds
        {200, 255, 150}, // Pet & More
    };

    for (size_t i = 0; i < groups.size(); i++) {
        float y = startY - static_cast<float>(i) * spacing;

        // crear icono (circulo de color)
        auto iconDraw = PaimonDrawNode::create();
        ccColor3B c = catColors[i % 6];
        ccColor4F fill = {c.r / 255.f, c.g / 255.f, c.b / 255.f, 0.8f};
        constexpr int segs = 16;
        constexpr float r = 10.f;
        std::vector<CCPoint> pts;
        pts.reserve(segs);
        for (int s = 0; s < segs; s++) {
            float a = static_cast<float>(M_PI) * 2.f * (static_cast<float>(s) / static_cast<float>(segs));
            pts.push_back(ccp(r + cosf(a) * r, r + sinf(a) * r));
        }
        iconDraw->drawPolygon(pts.data(), segs, fill, 0.f, {0, 0, 0, 0});
        iconDraw->setContentSize({r * 2.f, r * 2.f});

        // primera letra del grupo
        auto letterLabel = CCLabelBMFont::create(
            groups[i].name.substr(0, 1).c_str(), "bigFont.fnt"
        );
        letterLabel->setScale(0.28f);
        letterLabel->setPosition({r, r});
        iconDraw->addChild(letterLabel, 1);

        auto btn = CCMenuItemExt::createSpriteExtra(iconDraw, [this, idx = static_cast<int>(i)](CCMenuItemSpriteExtra*) {
            selectCategory(idx);
        });
        btn->setPosition({SIDEBAR_W / 2.f, y});

        m_sidebarMenu->addChild(btn);
        m_sidebarButtons.push_back(btn);
    }
}

void PaimonMultiSettingsPanel::buildContentArea() {
    float scrollW = CONTENT_W;
    float scrollH = CONTENT_H;

    m_scrollLayer = geode::ScrollLayer::create({scrollW, scrollH});
    m_scrollLayer->setPosition({SIDEBAR_W, 0.f});
    m_scrollLayer->setTouchEnabled(true);
    m_scrollLayer->setTouchPriority(m_childTouchPrio);
    m_panelContainer->addChild(m_scrollLayer, 2);
}

void PaimonMultiSettingsPanel::selectCategory(int index) {
    auto const& groups = paimon::settings_ui::getAllGroups();
    if (index < 0 || index >= static_cast<int>(groups.size())) return;

    m_selectedCategory = index;

    // limpiar busqueda al cambiar categoria
    if (m_searchInput) m_searchInput->setString("");
    m_searchQuery.clear();
    m_isSearchActive = false;

    updateSidebarAccent();
    rebuildContent();
}

void PaimonMultiSettingsPanel::rebuildContent() {
    if (!m_scrollLayer) return;

    auto contentLayer = m_scrollLayer->m_contentLayer;
    contentLayer->removeAllChildren();

    auto const& groups = paimon::settings_ui::getAllGroups();
    if (m_selectedCategory < 0 || m_selectedCategory >= static_cast<int>(groups.size())) return;

    auto const& group = groups[m_selectedCategory];

    // construir subcategorias colapsables
    std::vector<CCNode*> allRows;

    for (auto const& sub : group.subcategories) {
        // construir contenido de la subcategoria
        auto subContainer = CCNode::create();
        subContainer->setAnchorPoint({0.f, 0.f});
        sub.buildContent(subContainer, CONTENT_W);

        // calcular altura del contenido
        float subH = 0.f;
        if (auto children = subContainer->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                subH += child->getContentSize().height;
            }
        }
        subContainer->setContentSize({CONTENT_W, subH});

        // posicionar rows dentro del subContainer top-to-bottom
        float yPos = subH;
        if (auto children = subContainer->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                yPos -= child->getContentSize().height;
                child->setPosition({0.f, yPos});
            }
        }

        // cabecera colapsable
        auto header = paimon::settings_ui::createCollapsibleHeader(
            sub.name.c_str(), CONTENT_W, subContainer, true,
            [this]() { relayoutContent(); }
        );

        allRows.push_back(header);
        allRows.push_back(subContainer);
    }

    // calcular altura total y posicionar
    float totalH = 0.f;
    for (auto* row : allRows) {
        if (row->isVisible()) {
            totalH += row->getContentSize().height;
        }
    }

    float contentH = std::max(totalH, CONTENT_H);
    contentLayer->setContentSize({CONTENT_W, contentH});

    float currentY = contentH;
    for (auto* row : allRows) {
        contentLayer->addChild(row, 0);
        if (row->isVisible()) {
            float h = row->getContentSize().height;
            currentY -= h;
            row->setPosition({0.f, currentY});
        }
    }

    m_scrollLayer->moveToTop();
}

void PaimonMultiSettingsPanel::relayoutContent() {
    if (!m_scrollLayer) return;

    auto contentLayer = m_scrollLayer->m_contentLayer;
    auto children = contentLayer->getChildren();
    if (!children) return;

    // calcular altura total visible
    float totalH = 0.f;
    for (auto* child : CCArrayExt<CCNode*>(children)) {
        if (child->isVisible()) {
            totalH += child->getContentSize().height;
        }
    }

    float contentH = std::max(totalH, CONTENT_H);
    contentLayer->setContentSize({CONTENT_W, contentH});

    // reposicionar solo los visibles
    float currentY = contentH;
    for (auto* child : CCArrayExt<CCNode*>(children)) {
        if (child->isVisible()) {
            float h = child->getContentSize().height;
            currentY -= h;
            child->setPosition({0.f, currentY});
        }
    }
}

void PaimonMultiSettingsPanel::relayoutScrollContent() {
    relayoutContent();
}

void PaimonMultiSettingsPanel::updateSidebarAccent() {
    if (!m_sidebarAccent) return;
    if (m_selectedCategory < 0 || m_selectedCategory >= static_cast<int>(m_sidebarButtons.size())) return;

    auto btn = m_sidebarButtons[m_selectedCategory];
    float y = btn->getPositionY() - 10.f;
    m_sidebarAccent->setPosition({0.f, y});
    m_sidebarAccent->setVisible(true);

    for (size_t i = 0; i < m_sidebarButtons.size(); i++) {
        m_sidebarButtons[i]->setOpacity(i == static_cast<size_t>(m_selectedCategory) ? 255 : 150);
    }
}

// ── Search ────────────────────────────────────────────────────────────

void PaimonMultiSettingsPanel::onSearchChanged(std::string const& query) {
    m_searchQuery = query;

    std::string lowerQuery = geode::utils::string::toLower(query);

    if (lowerQuery.empty()) {
        m_isSearchActive = false;
        rebuildContent();
        return;
    }

    m_isSearchActive = true;
    buildSearchResults(lowerQuery);
}

void PaimonMultiSettingsPanel::buildSearchResults(std::string const& query) {
    if (!m_scrollLayer) return;

    auto contentLayer = m_scrollLayer->m_contentLayer;
    contentLayer->removeAllChildren();

    std::vector<CCNode*> matchingRows;
    auto const& groups = paimon::settings_ui::getAllGroups();

    for (auto const& group : groups) {
        for (auto const& sub : group.subcategories) {
            auto tempContainer = CCNode::create();
            sub.buildContent(tempContainer, CONTENT_W);

            auto children = tempContainer->getChildren();
            if (!children) continue;

            std::vector<CCNode*> toExtract;
            for (auto* child : CCArrayExt<CCNode*>(children)) {

                // buscar CCLabelBMFont en los hijos del row
                bool matches = false;
                auto rowChildren = child->getChildren();
                if (rowChildren) {
                    for (auto* subChild : CCArrayExt<CCNode*>(rowChildren)) {
                        auto label = typeinfo_cast<CCLabelBMFont*>(subChild);
                        if (label) {
                            std::string labelText = geode::utils::string::toLower(label->getString());
                            if (labelText.find(query) != std::string::npos) {
                                matches = true;
                                break;
                            }
                        }
                    }
                }

                if (matches) {
                    toExtract.push_back(child);
                }
            }

            for (auto* row : toExtract) {
                row->retain();
                row->removeFromParent();
                matchingRows.push_back(row);
            }
        }
    }

    // layout matching rows
    float totalH = 0.f;
    for (auto* row : matchingRows) totalH += row->getContentSize().height;

    float contentH = std::max(totalH, CONTENT_H);
    contentLayer->setContentSize({CONTENT_W, contentH});

    float currentY = contentH;
    for (auto* row : matchingRows) {
        float h = row->getContentSize().height;
        currentY -= h;
        row->setPosition({0.f, currentY});
        contentLayer->addChild(row, 0);
        row->release();
    }

    m_scrollLayer->moveToTop();
}

// ── Animations ────────────────────────────────────────────────────────

void PaimonMultiSettingsPanel::runEntryAnimation() {
    m_darkOverlay->runAction(CCFadeTo::create(0.2f, 150));

    m_panelContainer->setScale(0.92f);
    auto scaleAction = CCEaseExponentialOut::create(CCScaleTo::create(0.25f, 1.0f));
    m_panelContainer->runAction(scaleAction);
}

void PaimonMultiSettingsPanel::animateClose() {
    if (m_isClosing) return;
    m_isClosing = true;

    this->setTouchEnabled(false);

    if (m_darkOverlay) {
        m_darkOverlay->runAction(CCFadeTo::create(0.15f, 0));
    }

    if (m_panelContainer) {
        auto scaleAction = CCEaseExponentialIn::create(CCScaleTo::create(0.15f, 0.92f));
        auto callback = CCCallFunc::create(this, callfunc_selector(PaimonMultiSettingsPanel::onCloseFinished));
        auto seq = CCSequence::create(scaleAction, callback, nullptr);
        m_panelContainer->runAction(seq);
    } else {
        onCloseFinished();
    }
}

void PaimonMultiSettingsPanel::onCloseFinished() {
    SettingsPanelManager::get().notifyPanelRemoved();
    this->removeFromParent();
}

void PaimonMultiSettingsPanel::onClose(CCObject*) {
    animateClose();
}

void PaimonMultiSettingsPanel::onExit() {
    // Clear the manager's pointer so it doesn't dangle after scene transitions
    SettingsPanelManager::get().notifyPanelRemoved();
    CCLayer::onExit();
}

// ── Touch handling ─────────────────────────────────────────────────────

bool PaimonMultiSettingsPanel::isTouchInTitleBar(CCPoint const& worldPos) {
    if (!m_panelContainer) return false;
    auto panelWorldPos = m_panelContainer->convertToWorldSpace({0.f, PANEL_H - TITLE_BAR_H});
    CCRect titleRect(panelWorldPos.x, panelWorldPos.y, PANEL_W * m_panelContainer->getScaleX(), TITLE_BAR_H * m_panelContainer->getScaleY());
    return titleRect.containsPoint(worldPos);
}

bool PaimonMultiSettingsPanel::isTouchInPanel(CCPoint const& worldPos) {
    if (!m_panelContainer) return false;
    auto panelWorldPos = m_panelContainer->convertToWorldSpace({0.f, 0.f});
    float scX = m_panelContainer->getScaleX();
    float scY = m_panelContainer->getScaleY();
    CCRect panelRect(panelWorldPos.x, panelWorldPos.y, PANEL_W * scX, PANEL_H * scY);
    return panelRect.containsPoint(worldPos);
}

bool PaimonMultiSettingsPanel::isTouchInSearchInput(CCPoint const& worldPos) const {
    if (!m_searchInput || !m_searchInput->isVisible() || !m_panelContainer) return false;
    auto localPos = m_panelContainer->convertToNodeSpace(worldPos);
    return m_searchInput->boundingBox().containsPoint(localPos);
}

void PaimonMultiSettingsPanel::keyBackClicked() {
    animateClose();
}

bool PaimonMultiSettingsPanel::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    if (m_isClosing) return false;

    auto touchPos = touch->getLocation();

    // title bar vacia -> iniciar drag
    if (isTouchInTitleBar(touchPos) && !isTouchInSearchInput(touchPos)) {
        m_isDragging = true;
        m_dragOffset = ccpSub(m_panelContainer->getPosition(), touchPos);
        return true;
    }

    // dentro del panel -> dejar que los hijos manejen (ScrollLayer, CCMenus, TextInput)
    if (isTouchInPanel(touchPos)) {
        return false;
    }

    // fuera del panel -> cerrar
    animateClose();
    return true;
}

void PaimonMultiSettingsPanel::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    if (!m_isDragging) return;
    auto touchPos = touch->getLocation();
    m_panelContainer->setPosition(ccpAdd(touchPos, m_dragOffset));
}

void PaimonMultiSettingsPanel::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    m_isDragging = false;
}

void PaimonMultiSettingsPanel::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    m_isDragging = false;
}
