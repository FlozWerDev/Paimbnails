#include "ButtonEditOverlay.hpp"
#include "../core/UIConstants.hpp"
#include "../utils/PaimonButtonHighlighter.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../utils/PaimonDrawNode.hpp"
#include "../managers/ButtonLayoutManager.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include "../utils/Localization.hpp"
#include <Geode/loader/Log.hpp>
#include <cocos-ext.h>

using namespace cocos2d;
using namespace geode::prelude;
using namespace cocos2d::extension;
namespace E = paimon::ui::constants::editor;

ButtonEditOverlay* ButtonEditOverlay::create(std::string const& sceneKey, CCMenu* menu,
                                             std::vector<CCMenu*> const& extraMenus) {
    auto ret = new ButtonEditOverlay();
    if (ret && ret->init(sceneKey, menu, extraMenus)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ButtonEditOverlay::init(std::string const& sceneKey, CCMenu* menu,
                             std::vector<CCMenu*> const& extraMenus) {
    if (!CCLayer::init()) return false;

    m_sceneKey = sceneKey;
    m_targetMenu = menu;
    m_selectedButton = nullptr;

    // Retener extra menus
    for (auto* em : extraMenus) {
        if (em) m_extraMenus.emplace_back(em);
    }

    m_draggedButton = nullptr;

    // cachear winsize para evitar multiples llamadas
    const auto winSize = CCDirector::sharedDirector()->getWinSize();
    
    m_darkBG = CCLayerColor::create(ccc4(0, 0, 0, E::OVERLAY_ALPHA));
    m_darkBG->setContentSize(winSize);
    m_darkBG->setZOrder(-1);
    this->addChild(m_darkBG);
    
    collectEditableButtons();
    
    for (auto& btn : m_editableButtons) {
        if (btn.item && btn.item->getParent()) {
            btn.item->setZOrder(1000);
        }
    }
    
    createControls();
    showControls(false);

    // Desactivar todos los CCMenus en la escena que NO sean nuestros menus editables
    if (auto scene = CCDirector::sharedDirector()->getRunningScene()) {
        disableOtherMenus(scene);
    }
    
    m_selectionHighlight = paimon::SpriteHelper::createColorPanel(10, 10, ccColor3B{100, 255, 100}, 150, 3.f);
    m_selectionHighlight->setVisible(false);
    m_selectionHighlight->setZOrder(E::Z_SELECTION_HL);
    
    if (m_targetMenu && m_targetMenu->getParent()) {
        m_targetMenu->getParent()->addChild(m_selectionHighlight, E::Z_SELECTION_HL);
    }

    createAllHighlights();
    createSnapGuides();

    this->setTouchEnabled(true);
    this->setTouchMode(kCCTouchesOneByOne);
    this->setTouchPriority(E::TOUCH_PRIORITY);
    this->scheduleUpdate();
    
    return true;
}

ButtonEditOverlay::~ButtonEditOverlay() {
    // Re-habilitar menus desactivados (Ref mantiene los punteros validos)
    for (auto& menuRef : m_disabledMenus) {
        if (menuRef && menuRef->getParent()) menuRef->setEnabled(true);
    }
    m_disabledMenus.clear();

    if (m_selectionHighlight && m_selectionHighlight->getParent()) {
        m_selectionHighlight->removeFromParent();
    }
    clearAllHighlights();
    m_targetMenu = nullptr;
    m_extraMenus.clear();
}

void ButtonEditOverlay::collectEditableButtons() {
    m_editableButtons.clear();

    // Helper lambda para recoger botones de un menu
    auto collectFromMenu = [this](CCMenu* menu) {
        if (!menu) return;
        auto children = menu->getChildren();
        if (!children) return;

        for (auto* child : CCArrayExt<CCObject*>(children)) {
            auto item = typeinfo_cast<CCMenuItem*>(child);
            if (!item) continue;

            auto buttonID = item->getID();
            if (buttonID.empty()) continue;

            EditableButton editable;
            editable.item = item;
            editable.buttonID = std::string(buttonID);
            editable.originalPos = item->getPosition();
            editable.originalScale = item->getScale();
            editable.originalOpacity = item->getOpacity() / 255.0f;
            editable.highlightKey = fmt::format("{}_{}", reinterpret_cast<uintptr_t>(menu), editable.buttonID);

            m_editableButtons.push_back(std::move(editable));
        }
    };

    // Recoger del menu principal
    collectFromMenu(m_targetMenu);

    // Recoger de menus extra
    for (auto& em : m_extraMenus) {
        collectFromMenu(em);
    }

    log::debug("[ButtonEditOverlay] Collected {} editable buttons", m_editableButtons.size());
}

void ButtonEditOverlay::disableOtherMenus(CCNode* root) {
    if (!root) return;
    auto children = root->getChildren();
    if (!children) return;

    for (auto* obj : CCArrayExt<CCObject*>(children)) {
        auto node = typeinfo_cast<CCNode*>(obj);
        if (!node) continue;

        // Si es un CCMenu, verificar si es uno de los nuestros
        if (auto menu = typeinfo_cast<CCMenu*>(node)) {
            // No desactivar nuestros menus editables ni el menu de controles
            bool isOurs = (menu == m_targetMenu || menu == m_controlsMenu);
            for (auto& em : m_extraMenus) {
                if (menu == em) isOurs = true;
            }
            if (!isOurs && menu->isEnabled()) {
                menu->setEnabled(false);
                m_disabledMenus.push_back(geode::Ref<CCMenu>(menu));
            }
        }

        // Recursivamente buscar en hijos
        disableOtherMenus(node);
    }
}

void ButtonEditOverlay::createControls() {
    const auto winSize = CCDirector::sharedDirector()->getWinSize();

    m_controlsMenu = CCMenu::create();
    m_controlsMenu->setPosition(CCPointZero);  // usar constante de cocos2d
    m_controlsMenu->setZOrder(E::Z_CONTROLS_MENU);
    this->addChild(m_controlsMenu);

    // panel de controles en la parte inferior
    const float panelHeight = E::CONTROLS_PANEL_H;
    const float panelY = panelHeight / 2.f + 10.f;
    const float centerX = winSize.width / 2.f;

    // fondo panel
    auto panelBg = paimon::SpriteHelper::createDarkPanel(winSize.width - 20.f, panelHeight, 200);
    panelBg->setPosition({centerX - (winSize.width - 20.f) / 2.f, panelY - panelHeight / 2.f});
    this->addChild(panelBg, -1);

    // titulo panel
    const float titleY = panelY + panelHeight / 2.f - 15.f;
    auto titleLabel = CCLabelBMFont::create(Localization::get().getString("edit.buttons_title").c_str(), "bigFont.fnt");
    titleLabel->setScale(0.55f);
    titleLabel->setPosition({centerX, titleY});
    this->addChild(titleLabel);

    // sliders + botones
    const float contentStartY = panelY + 10.f;
    const float row1Y = contentStartY;          // fila de scale
    const float row2Y = contentStartY - 35.f;   // fila de opacity

    // posiciones para los elementos de sliders
    const float labelX = 30.f;
    const float sliderX = centerX - 60.f;
    const float valueX = sliderX + 130.f;

    // --- escala ---
    auto scaleText = CCLabelBMFont::create(Localization::get().getString("edit.scale").c_str(), "goldFont.fnt");
    scaleText->setScale(0.5f);
    scaleText->setAnchorPoint({0.f, 0.5f});
    scaleText->setPosition({labelX, row1Y});
    this->addChild(scaleText);
    
    m_scaleSlider = Slider::create(this, menu_selector(ButtonEditOverlay::onScaleChanged));
    m_scaleSlider->setPosition({sliderX, row1Y});
    m_scaleSlider->setScale(0.8f);
    m_scaleSlider->setValue(0.5f);
    this->addChild(m_scaleSlider);

    m_scaleLabel = CCLabelBMFont::create("1.00x", "bigFont.fnt");
    m_scaleLabel->setScale(0.4f);
    m_scaleLabel->setAnchorPoint({0.f, 0.5f});
    m_scaleLabel->setPosition({valueX, row1Y});
    this->addChild(m_scaleLabel);

    // --- opacidad ---
    auto opacityText = CCLabelBMFont::create(Localization::get().getString("edit.opacity").c_str(), "goldFont.fnt");
    opacityText->setScale(0.5f);
    opacityText->setAnchorPoint({0.f, 0.5f});
    opacityText->setPosition({labelX, row2Y});
    this->addChild(opacityText);

    m_opacitySlider = Slider::create(this, menu_selector(ButtonEditOverlay::onOpacityChanged));
    m_opacitySlider->setPosition({sliderX, row2Y});
    m_opacitySlider->setScale(0.8f);
    m_opacitySlider->setValue(1.0f);
    this->addChild(m_opacitySlider);

    m_opacityLabel = CCLabelBMFont::create("100%", "bigFont.fnt");
    m_opacityLabel->setScale(0.4f);
    m_opacityLabel->setAnchorPoint({0.f, 0.5f});
    m_opacityLabel->setPosition({valueX, row2Y});
    this->addChild(m_opacityLabel);

    // botones accion
    const float btnX = winSize.width - 70.f;
    const float btnCenterY = panelY - 5.f;

    auto acceptSpr = ButtonSprite::create(Localization::get().getString("edit.accept").c_str(), 80, true, "bigFont.fnt", "GJ_button_01.png", 28.f, 0.6f);
    auto acceptBtn = CCMenuItemSpriteExtra::create(acceptSpr, this, menu_selector(ButtonEditOverlay::onAccept));
    acceptBtn->setPosition({btnX, btnCenterY + 20.f});
    m_controlsMenu->addChild(acceptBtn);

    auto resetSpr = ButtonSprite::create(Localization::get().getString("edit.reset").c_str(), 80, true, "bigFont.fnt", "GJ_button_06.png", 28.f, 0.6f);
    auto resetBtn = CCMenuItemSpriteExtra::create(resetSpr, this, menu_selector(ButtonEditOverlay::onReset));
    resetBtn->setPosition({btnX, btnCenterY - 20.f});
    m_controlsMenu->addChild(resetBtn);

    // instruccion arriba
    auto instrLabel = CCLabelBMFont::create("Drag buttons to move them", "chatFont.fnt");
    instrLabel->setScale(0.6f);
    instrLabel->setPosition({centerX, winSize.height - 20.f});
    instrLabel->setColor({220, 220, 220});
    this->addChild(instrLabel);
}

void ButtonEditOverlay::showControls(bool show) {
    if (m_scaleSlider) m_scaleSlider->setVisible(show);
    if (m_opacitySlider) m_opacitySlider->setVisible(show);
    if (m_scaleLabel) m_scaleLabel->setVisible(show);
    if (m_opacityLabel) m_opacityLabel->setVisible(show);
}

void ButtonEditOverlay::update(float) {
    // menu invalido, cierro
    if (!m_targetMenu || !m_targetMenu->getParent()) {
        log::warn("[ButtonEditOverlay] Target menu no longer valid; closing editor to avoid crash");
        m_isClosing = true;
        // desactivo controles
        if (m_controlsMenu) m_controlsMenu->setTouchEnabled(false);
        this->setTouchEnabled(false);
        // elimino el resaltado de seleccion si todavia esta adjunto
        if (m_selectionHighlight && m_selectionHighlight->getParent()) {
            m_selectionHighlight->removeFromParent();
        }
        clearAllHighlights();
        // elimino superposicion
        this->unscheduleUpdate();
        this->removeFromParent();
        return;
    }

    // mantengo todos los resaltados por boton sincronizados con sus elementos
    updateAllHighlights();
}

void ButtonEditOverlay::selectButton(EditableButton* btn) {
    m_selectedButton = btn;
    
    if (!btn) {
        showControls(false);
        m_selectionHighlight->setVisible(false);
        return;
    }

    showControls(true);
    
    // establezco valores del deslizador desde el estado actual del boton
    float currentScale = btn->item->getScale();
    float currentOpacity = btn->item->getOpacity() / 255.0f;
    
    // mapeo escala [SCALE_MIN, SCALE_MAX] a deslizador [0, 1]
    float scaleNorm = (currentScale - E::SCALE_MIN) / (E::SCALE_MAX - E::SCALE_MIN);
    m_scaleSlider->setValue(std::max(0.f, std::min(1.f, scaleNorm)));
    
    m_opacitySlider->setValue(currentOpacity);
    
    updateSliderLabels();
    updateSelectionHighlight();
    updateAllHighlights();
}

void ButtonEditOverlay::updateSelectionHighlight() {
    if (!m_selectedButton || !m_selectionHighlight) return;
    
    auto item = m_selectedButton->item;
    if (!item || !item->getParent()) return;

    auto contentSize = item->getContentSize();
    float scale = item->getScale();
    float w = contentSize.width * scale + 10.f;
    float h = contentSize.height * scale + 10.f;

    ccColor4F fill = {E::SELECTION_R, E::SELECTION_G, E::SELECTION_B, E::SELECTION_A};
    drawRoundedRect(m_selectionHighlight, w, h, fill);
    
    auto worldPos = item->getParent()->convertToWorldSpace(item->getPosition());
    m_selectionHighlight->setPosition({worldPos.x - w/2, worldPos.y - h/2});
    m_selectionHighlight->setVisible(true);
}

void ButtonEditOverlay::updateSliderLabels() {
    if (!m_selectedButton) return;
    
    float scale = m_selectedButton->item->getScale();
    float opacity = m_selectedButton->item->getOpacity() / 255.0f * 100.0f;
    
    m_scaleLabel->setString(fmt::format("{:.2f}x", scale).c_str());
    m_opacityLabel->setString(fmt::format("{:.0f}%", opacity).c_str());
}

EditableButton* ButtonEditOverlay::findButtonAtPoint(CCPoint worldPos) {
    for (auto& btn : m_editableButtons) {
        if (!btn.item) continue;
        
        auto parent = btn.item->getParent();
        if (!parent) continue;
        
        auto localPos = parent->convertToNodeSpace(worldPos);
        auto bbox = btn.item->boundingBox();
        
        if (bbox.containsPoint(localPos)) {
            return &btn;
        }
    }
    return nullptr;
}

// Helper: verifica si el toque cae sobre el area de un Slider (thumb + groove).
// Los Sliders de GD manejan sus propios toques via registerWithTouchDispatcher;
// si nosotros tragamos el toque aqui, nunca les llega.
bool ButtonEditOverlay::isTouchOnSlider(CCTouch* touch) {
    auto checkSlider = [&](Slider* slider) -> bool {
        if (!slider || !slider->isVisible() || !slider->getParent()) return false;
        // Usar el bounding box del slider completo (groove + thumb)
        auto sliderWorldPos = slider->getParent()->convertToWorldSpace(slider->getPosition());
        // El Slider de GD tiene un ancho/alto basado en su escala y contenido
        auto cs = slider->getContentSize();
        float sc = slider->getScale();
        float w = cs.width * sc;
        float h = std::max(cs.height * sc, 30.f); // minimo 30px de area tactil vertical
        CCRect sliderRect(sliderWorldPos.x - w / 2.f, sliderWorldPos.y - h / 2.f, w, h);
        return sliderRect.containsPoint(touch->getLocation());
    };

    if (checkSlider(m_scaleSlider)) return true;
    if (checkSlider(m_opacitySlider)) return true;
    return false;
}

bool ButtonEditOverlay::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    // Si el toque cae sobre un slider, no tragarlo — dejar que el Slider lo maneje
    if (isTouchOnSlider(touch)) {
        return false;
    }

    auto touchPos = touch->getLocation();
    auto foundBtn = findButtonAtPoint(touchPos);
    
    if (foundBtn) {
        m_draggedButton = foundBtn;
        m_dragStartPos = touchPos;
        m_originalButtonPos = foundBtn->item->getPosition();
        
        // selecciono
        selectButton(foundBtn);
        
        return true;
    }
    
    // no toco btn, deselecciono
    selectButton(nullptr);
    return true;
}

void ButtonEditOverlay::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    if (!m_draggedButton || !m_draggedButton->item) return;

    const auto touchPos = touch->getLocation();
    const auto delta = ccpSub(touchPos, m_dragStartPos);
    auto newPos = ccpAdd(m_originalButtonPos, delta);
    
    // snap
    newPos = applySnap(newPos);

    m_draggedButton->item->setPosition(newPos);
    updateSelectionHighlight();
    updateAllHighlights();
}

void ButtonEditOverlay::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    m_draggedButton = nullptr;
    hideSnapGuides();
}

void ButtonEditOverlay::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    m_draggedButton = nullptr;
    hideSnapGuides();
}

void ButtonEditOverlay::onScaleChanged(CCObject*) {
    if (!m_selectedButton || !m_selectedButton->item) return;

    const float sliderValue = m_scaleSlider->getValue();
    const float scale = E::SCALE_MIN + sliderValue * (E::SCALE_MAX - E::SCALE_MIN); // map [0,1] a [SCALE_MIN, SCALE_MAX]

    m_selectedButton->item->setScale(scale);

    // importante: actualizar m_basescale para que el hover no resetee la escala
    // cachear el cast si es ccmenuitemspriteextra
    if (auto menuItem = typeinfo_cast<CCMenuItemSpriteExtra*>(m_selectedButton->item)) {
        menuItem->m_baseScale = scale;
    }

    updateSliderLabels();
    updateSelectionHighlight();
}

void ButtonEditOverlay::onOpacityChanged(CCObject*) {
    if (!m_selectedButton || !m_selectedButton->item) return;
    
    float opacity = m_opacitySlider->getValue();
    m_selectedButton->item->setOpacity(static_cast<GLubyte>(opacity * 255));
    updateSliderLabels();
}

void ButtonEditOverlay::onAccept(CCObject*) {
    if (m_isClosing) {
        // ya se esta cerrando; ignorar accion
        return;
    }
    // si el contexto se ha ido (navegacion a otra parte), cierro de forma segura
    if (!m_targetMenu || !m_targetMenu->getParent()) {
        log::warn("[ButtonEditOverlay] Accept pressed after leaving page; closing without saving");
        if (m_selectionHighlight && m_selectionHighlight->getParent()) {
            m_selectionHighlight->removeFromParent();
        }
        m_isClosing = true;
        this->unscheduleUpdate();
        this->removeFromParent();
        return;
    }

    // Helper: guardar layouts de un menu
    auto saveMenuButtons = [this](CCMenu* menu) {
        if (!menu) return;
        auto children = menu->getChildren();
        if (!children) return;
        for (auto obj : CCArrayExt<CCObject*>(children)) {
            auto item = typeinfo_cast<CCMenuItem*>(obj);
            if (!item) continue;
            std::string id = item->getID();
            if (id.empty()) continue;

            ButtonLayout layout;
            layout.position = item->getPosition();
            layout.scale = item->getScale();
            layout.opacity = item->getOpacity() / 255.0f;

            ButtonLayoutManager::get().setLayout(m_sceneKey, id, layout);

            if (auto spriteExtra = typeinfo_cast<CCMenuItemSpriteExtra*>(item)) {
                spriteExtra->m_baseScale = layout.scale;
            }

            log::info("[ButtonEditOverlay] Saved button '{}': pos({}, {}), scale={}, opacity={}",
                id, layout.position.x, layout.position.y, layout.scale, layout.opacity);
        }
    };

    // Guardar de todos los menus
    saveMenuButtons(m_targetMenu);
    for (auto& em : m_extraMenus) {
        saveMenuButtons(em);
    }
    
    // elimino resaltado de seleccion
    if (m_selectionHighlight && m_selectionHighlight->getParent()) {
        m_selectionHighlight->removeFromParent();
    }
    clearAllHighlights();

    // elimino superposicion
    m_isClosing = true;
    this->unscheduleUpdate();
    this->removeFromParent();
}

void ButtonEditOverlay::onReset(CCObject*) {
    if (m_isClosing) {
        return;
    }
    // si el contexto se ha ido, solo limpio disenos guardados y cierro
    if (!m_targetMenu || !m_targetMenu->getParent()) {
        ButtonLayoutManager::get().resetScene(m_sceneKey);
        ButtonLayoutManager::get().save();
        if (m_selectionHighlight && m_selectionHighlight->getParent()) {
            m_selectionHighlight->removeFromParent();
        }
        m_isClosing = true;
        this->unscheduleUpdate();
        this->removeFromParent();
        return;
    }

    // restauro botones por defecto persistentes cuando esten disponibles, sino a originales capturados
    for (auto& btn : m_editableButtons) {
        if (!btn.item || btn.buttonID.empty()) continue;
        if (!btn.item->getParent()) continue;

        auto def = ButtonLayoutManager::get().getDefaultLayout(m_sceneKey, btn.buttonID);
        float newScale;
        if (def) {
            btn.item->setPosition(def->position);
            btn.item->setScale(def->scale);
            btn.item->setOpacity(static_cast<GLubyte>(def->opacity * 255));
            newScale = def->scale;
        } else {
            btn.item->setPosition(btn.originalPos);
            btn.item->setScale(btn.originalScale);
            btn.item->setOpacity(static_cast<GLubyte>(btn.originalOpacity * 255));
            newScale = btn.originalScale;
        }

        // importante: actualizar m_basescale para que el hover no resetee la escala
        if (auto spriteExtra = typeinfo_cast<CCMenuItemSpriteExtra*>(btn.item)) {
            spriteExtra->m_baseScale = newScale;
        }
    }

    // limpio disenos guardados para esta escena y persisto
    ButtonLayoutManager::get().resetScene(m_sceneKey);
    ButtonLayoutManager::get().save();

    // deselecciono
    selectButton(nullptr);
    updateAllHighlights();
}

void ButtonEditOverlay::createAllHighlights() {
    clearAllHighlights();
    if (!m_targetMenu || !m_targetMenu->getParent()) return;

    auto parent = m_targetMenu->getParent();
    // reservar espacio en el mapa para evitar rehashing
    m_buttonHighlights.reserve(m_editableButtons.size());

    for (auto& btn : m_editableButtons) {
        if (!btn.item || btn.highlightKey.empty()) continue;
        auto spr = paimon::SpriteHelper::createColorPanel(10, 10, ccColor3B{80, 180, 255}, 120, 3.f);
        if (!spr) continue;

        spr->setZOrder(E::Z_BUTTON_HL);
        parent->addChild(spr, E::Z_BUTTON_HL);
        m_buttonHighlights[btn.highlightKey] = spr;
    }
    updateAllHighlights();
}

void ButtonEditOverlay::updateAllHighlights() {
    if (!m_targetMenu) return;
    for (auto& btn : m_editableButtons) {
        if (!btn.item || btn.highlightKey.empty()) continue;
        auto it = m_buttonHighlights.find(btn.highlightKey);
        if (it == m_buttonHighlights.end()) continue;
        auto node = it->second;
        if (!node) continue;

        auto contentSize = btn.item->getContentSize();
        float scale = btn.item->getScale();
        float w = contentSize.width * scale + 10.f;
        float h = contentSize.height * scale + 10.f;

        ccColor4F fill = {E::BUTTON_HL_R, E::BUTTON_HL_G, E::BUTTON_HL_B, E::BUTTON_HL_A};
        drawRoundedRect(node, w, h, fill);

        if (auto parent = btn.item->getParent()) {
            auto worldPos = parent->convertToWorldSpace(btn.item->getPosition());
            node->setPosition({worldPos.x - w/2, worldPos.y - h/2});
            node->setVisible(true);
        } else {
            node->setVisible(false);
        }
    }
}

void ButtonEditOverlay::clearAllHighlights() {
    for (auto it = m_buttonHighlights.begin(); it != m_buttonHighlights.end(); ++it) {
        auto node = it->second;
        if (node && node->getParent()) {
            node->removeFromParent();
        }
    }
    m_buttonHighlights.clear();
}

void ButtonEditOverlay::drawRoundedRect(CCDrawNode* node, float w, float h, ccColor4F fill) {
    node->clear();
    ccColor4F none = {0, 0, 0, 0};
    constexpr int seg = E::ARC_SEGMENTS;
    float r = E::CORNER_RADIUS;
    std::vector<CCPoint> pts;
    pts.reserve(4 * (seg + 1));
    auto addArc = [&](float cx, float cy, float sa) {
        for (int i = 0; i <= seg; ++i) {
            float a = sa + (M_PI * 0.5f) * (float(i) / float(seg));
            pts.push_back(ccp(cx + cosf(a) * r, cy + sinf(a) * r));
        }
    };
    addArc(r, r, M_PI);
    addArc(w - r, r, M_PI * 1.5f);
    addArc(w - r, h - r, 0);
    addArc(r, h - r, M_PI * 0.5f);
    node->drawPolygon(pts.data(), static_cast<unsigned int>(pts.size()), fill, 0, none);
    node->setContentSize({w, h});
}

// snap guides
void ButtonEditOverlay::createSnapGuides() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // guia x
    m_snapGuideX = PaimonDrawNode::create();
    m_snapGuideX->setZOrder(2000);
    m_snapGuideX->setVisible(false);
    this->addChild(m_snapGuideX);

    // guia y
    m_snapGuideY = PaimonDrawNode::create();
    m_snapGuideY->setZOrder(2000);
    m_snapGuideY->setVisible(false);
    this->addChild(m_snapGuideY);
}

void ButtonEditOverlay::updateSnapGuides(bool showX, bool showY, float snapX, float snapY) {
    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // linea x
    if (m_snapGuideX) {
        m_snapGuideX->clear();
        if (showX) {
            m_snapGuideX->drawSegment(
                ccp(snapX, 0),
                ccp(snapX, winSize.height),
                1.0f,
                ccc4f(E::SNAP_GUIDE_R, E::SNAP_GUIDE_G, E::SNAP_GUIDE_B, E::SNAP_GUIDE_A)
            );
            m_snapGuideX->setVisible(true);
        } else {
            m_snapGuideX->setVisible(false);
        }
    }

    // linea y
    if (m_snapGuideY) {
        m_snapGuideY->clear();
        if (showY) {
            m_snapGuideY->drawSegment(
                ccp(0, snapY),
                ccp(winSize.width, snapY),
                1.0f,
                ccc4f(E::SNAP_GUIDE_R, E::SNAP_GUIDE_G, E::SNAP_GUIDE_B, E::SNAP_GUIDE_A)
            );
            m_snapGuideY->setVisible(true);
        } else {
            m_snapGuideY->setVisible(false);
        }
    }
}

void ButtonEditOverlay::hideSnapGuides() {
    if (m_snapGuideX) m_snapGuideX->setVisible(false);
    if (m_snapGuideY) m_snapGuideY->setVisible(false);
    m_snappedX = false;
    m_snappedY = false;
}

CCPoint ButtonEditOverlay::applySnap(CCPoint pos) {
    if (!m_draggedButton || !m_draggedButton->item) return pos;

    auto* dragParent = m_draggedButton->item->getParent();
    if (!dragParent) return pos;

    // convertir posicion propuesta a coordenadas mundo para comparacion uniforme
    CCPoint posWorld = dragParent->convertToWorldSpace(pos);

    float bestSnapX = posWorld.x;
    float bestSnapY = posWorld.y;
    float minDistX = m_snapThreshold + 1.0f;
    float minDistY = m_snapThreshold + 1.0f;
    bool foundSnapX = false;
    bool foundSnapY = false;

    // buscar otros para alinear (en coordenadas mundo)
    for (auto& btn : m_editableButtons) {
        if (&btn == m_draggedButton || !btn.item) continue;

        auto* btnParent = btn.item->getParent();
        if (!btnParent) continue;

        CCPoint otherWorld = btnParent->convertToWorldSpace(btn.item->getPosition());

        // alineacion x
        float distX = std::abs(posWorld.x - otherWorld.x);
        if (distX < m_snapThreshold && distX < minDistX) {
            minDistX = distX;
            bestSnapX = otherWorld.x;
            foundSnapX = true;
        }

        // alineacion y
        float distY = std::abs(posWorld.y - otherWorld.y);
        if (distY < m_snapThreshold && distY < minDistY) {
            minDistY = distY;
            bestSnapY = otherWorld.y;
            foundSnapY = true;
        }
    }

    // convertir posicion snap de vuelta a espacio local del padre
    CCPoint resultWorld = posWorld;
    if (foundSnapX) resultWorld.x = bestSnapX;
    if (foundSnapY) resultWorld.y = bestSnapY;
    CCPoint result = dragParent->convertToNodeSpace(resultWorld);

    // guias usan coordenadas mundo directamente
    updateSnapGuides(foundSnapX, foundSnapY, bestSnapX, bestSnapY);

    m_snappedX = foundSnapX;
    m_snappedY = foundSnapY;

    return result;
}
