#include "ButtonEditOverlay.hpp"
#include "../core/UIConstants.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../utils/PaimonDrawNode.hpp"
#include "../managers/ButtonLayoutManager.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/cocos/label_nodes/CCLabelBMFont.h>
#include "../utils/Localization.hpp"
#include <Geode/loader/Log.hpp>
#include <cocos-ext.h>

using namespace cocos2d;
using namespace geode::prelude;
using namespace cocos2d::extension;
namespace E = paimon::ui::constants::editor;

namespace {
GLubyte opacityOf(cocos2d::CCNode* n) {
    if (!n) return 255;
    if (auto* m = typeinfo_cast<cocos2d::CCMenuItem*>(n)) return m->getOpacity();
    if (auto* l = typeinfo_cast<cocos2d::CCLabelBMFont*>(n)) return l->getOpacity();
    return 255;
}
void setOpacityFor(cocos2d::CCNode* n, GLubyte o) {
    if (!n) return;
    if (auto* m = typeinfo_cast<cocos2d::CCMenuItem*>(n)) m->setOpacity(o);
    else if (auto* l = typeinfo_cast<cocos2d::CCLabelBMFont*>(n)) l->setOpacity(o);
}
} // namespace

ButtonEditOverlay* ButtonEditOverlay::create(std::string const& sceneKey, CCMenu* menu,
    std::vector<CCMenu*> const& extraMenus, CCNode* labelScanRoot) {
    auto ret = new ButtonEditOverlay();
    if (ret && ret->init(sceneKey, menu, extraMenus, labelScanRoot)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

std::vector<ButtonEditEntry>* ButtonEditOverlay::activeEntries() {
    return m_facet == ButtonEditFacet::Buttons ? &m_buttonEntries : &m_labelEntries;
}

std::vector<ButtonEditEntry> const* ButtonEditOverlay::activeEntries() const {
    return m_facet == ButtonEditFacet::Buttons ? &m_buttonEntries : &m_labelEntries;
}

void ButtonEditOverlay::collectButtonEntries() {
    m_buttonEntries.clear();

    auto collectFromMenu = [this](CCMenu* menu) {
        if (!menu) return;
        auto children = menu->getChildren();
        if (!children) return;

        int idx = 0;
        for (auto* child : CCArrayExt<CCObject*>(children)) {
            auto item = typeinfo_cast<CCMenuItem*>(child);
            if (!item) {
                ++idx;
                continue;
            }

            std::string layoutId = ButtonLayoutManager::resolveButtonID(item, menu, idx);
            ++idx;
            std::string highlightKey = fmt::format("{}_{}", reinterpret_cast<uintptr_t>(menu), layoutId);

            ButtonEditEntry e;
            e.node = item;
            e.layoutId = layoutId;
            e.highlightKey = highlightKey;
            e.originalPos = item->getPosition();
            e.originalScale = item->getScale();
            e.originalOpacity = item->getOpacity() / 255.0f;
            e.originalZOrder = item->getZOrder();

            m_buttonEntries.push_back(std::move(e));
        }
    };

    collectFromMenu(m_targetMenu);
    for (auto& em : m_extraMenus) {
        collectFromMenu(em);
    }

    log::debug("[ButtonEditOverlay] {} entradas de boton", m_buttonEntries.size());
}

void ButtonEditOverlay::collectLabelEntries() {
    m_labelEntries.clear();
    if (!m_labelScanRoot) {
        return;
    }

    auto labels = ButtonLayoutManager::collectEditableLabels(m_labelScanRoot);
    int idx = 0;
    for (auto* lbl : labels) {
        if (!lbl) continue;
        std::string layoutId = ButtonLayoutManager::resolveLabelID(lbl, idx++);
        std::string highlightKey = fmt::format("lab_{}_{}", reinterpret_cast<uintptr_t>(lbl), layoutId);

        ButtonEditEntry e;
        e.node = lbl;
        e.layoutId = layoutId;
        e.highlightKey = highlightKey;
        e.originalPos = lbl->getPosition();
        e.originalScale = lbl->getScale();
        e.originalOpacity = lbl->getOpacity() / 255.0f;
        e.originalZOrder = lbl->getZOrder();

        m_labelEntries.push_back(std::move(e));
    }

    log::debug("[ButtonEditOverlay] {} entradas de etiqueta", m_labelEntries.size());
}

bool ButtonEditOverlay::init(std::string const& sceneKey, CCMenu* menu,
    std::vector<CCMenu*> const& extraMenus, CCNode* labelScanRoot) {
    if (!CCLayer::init()) return false;

    m_sceneKey = sceneKey;
    m_targetMenu = menu;
    m_labelScanRoot = labelScanRoot;
    m_selectedEntry = nullptr;

    for (auto* em : extraMenus) {
        if (em) m_extraMenus.emplace_back(em);
    }

    m_draggedEntry = nullptr;

    const auto winSize = CCDirector::get()->getWinSize();

    m_darkBG = CCLayerColor::create(ccc4(0, 0, 0, E::OVERLAY_ALPHA));
    m_darkBG->setContentSize(winSize);
    m_darkBG->setZOrder(-1);
    this->addChild(m_darkBG);

    collectButtonEntries();
    collectLabelEntries();

    auto bumpZ = [](std::vector<ButtonEditEntry>& v) {
        for (auto& e : v) {
            if (e.node && e.node->getParent()) {
                e.node->setZOrder(1000);
            }
        }
    };
    bumpZ(m_buttonEntries);
    bumpZ(m_labelEntries);

    createControls();
    showControls(false);
    updateFacetUI();

    if (auto scene = CCDirector::get()->getRunningScene()) {
        disableOtherMenus(scene);
    }

    // m_selectionHighlight es un CCDrawNode dinámico: se redibuja con
    // `drawPolygon` cada vez que cambia la selección, así que tiene que
    // ser CCDrawNode (NineSlice no tiene API de drawing). Usamos
    // createRoundedRect directo, que es la primitiva legacy explícita
    // para este caso.
    cocos2d::ccColor4F selFill{
        100.f / 255.f, 255.f / 255.f, 100.f / 255.f, 150.f / 255.f
    };
    m_selectionHighlight = paimon::SpriteHelper::createRoundedRect(10, 10, 3.f, selFill);
    m_selectionHighlight->setVisible(false);
    m_selectionHighlight->setZOrder(E::Z_SELECTION_HL);
    this->addChild(m_selectionHighlight, E::Z_SELECTION_HL);

    createAllHighlights();
    createSnapGuides();

    this->setTouchEnabled(true);
    this->setTouchMode(kCCTouchesOneByOne);
    this->setTouchPriority(E::TOUCH_PRIORITY);
    this->scheduleUpdate();

    return true;
}

ButtonEditOverlay::~ButtonEditOverlay() {
    for (auto& menuRef : m_disabledMenus) {
        if (menuRef && menuRef->getParent()) menuRef->setEnabled(true);
    }
    m_disabledMenus.clear();

    auto restoreZ = [](std::vector<ButtonEditEntry>& v) {
        for (auto& e : v) {
            if (e.node && e.node->getParent()) {
                e.node->setZOrder(e.originalZOrder);
            }
        }
    };
    restoreZ(m_buttonEntries);
    restoreZ(m_labelEntries);

    if (m_selectionHighlight && m_selectionHighlight->getParent()) {
        m_selectionHighlight->removeFromParent();
    }
    clearAllHighlights();
    m_targetMenu = nullptr;
    m_extraMenus.clear();
    m_labelScanRoot = nullptr;
}

void ButtonEditOverlay::disableOtherMenus(CCNode* root) {
    if (!root) return;
    auto children = root->getChildren();
    if (!children) return;

    for (auto* obj : CCArrayExt<CCObject*>(children)) {
        auto node = typeinfo_cast<CCNode*>(obj);
        if (!node) continue;

        if (auto menu = typeinfo_cast<CCMenu*>(node)) {
            bool isOurs = (menu == m_targetMenu || menu == m_controlsMenu || menu == m_facetMenu);
            for (auto& em : m_extraMenus) {
                if (menu == em) isOurs = true;
            }
            if (!isOurs && menu->isEnabled()) {
                menu->setEnabled(false);
                m_disabledMenus.emplace_back(menu);
            }
        }

        disableOtherMenus(node);
    }
}

void ButtonEditOverlay::setFacet(ButtonEditFacet facet) {
    if (m_facet == facet) return;
    m_facet = facet;
    selectEntry(nullptr);
    clearAllHighlights();
    updateFacetUI();
    createAllHighlights();
}

void ButtonEditOverlay::updateFacetUI() {
    auto& loc = Localization::get();
    std::string facetName = m_facet == ButtonEditFacet::Buttons
        ? loc.getString("edit.facet_buttons")
        : loc.getString("edit.facet_labels");
    if (m_facetBannerLabel) {
        m_facetBannerLabel->setString(
            fmt::format(fmt::runtime(loc.getString("edit.showing_banner")), facetName).c_str());
    }
    if (m_instructionLabel) {
        m_instructionLabel->setString(
            (m_facet == ButtonEditFacet::Buttons ? loc.getString("edit.instruction_buttons")
                                                 : loc.getString("edit.instruction_labels"))
                .c_str());
    }
    if (m_tabButtons) {
        m_tabButtons->setColor(m_facet == ButtonEditFacet::Buttons ? ccWHITE : ccColor3B{160, 160, 160});
    }
    if (m_tabLabels) {
        m_tabLabels->setColor(m_facet == ButtonEditFacet::Labels ? ccWHITE : ccColor3B{160, 160, 160});
    }
}

void ButtonEditOverlay::onFacetTab(CCObject* sender) {
    auto tag = sender ? sender->getTag() : 0;
    setFacet(tag == 1 ? ButtonEditFacet::Labels : ButtonEditFacet::Buttons);
}

void ButtonEditOverlay::createControls() {
    const auto winSize = CCDirector::get()->getWinSize();
    auto& loc = Localization::get();

    m_controlsMenu = CCMenu::create();
    m_controlsMenu->setPosition(CCPointZero);
    m_controlsMenu->setZOrder(E::Z_CONTROLS_MENU);
    this->addChild(m_controlsMenu);

    const float bannerY = winSize.height - 22.f;
    m_facetBannerLabel = CCLabelBMFont::create(
        fmt::format(
            fmt::runtime(loc.getString("edit.showing_banner")),
            loc.getString("edit.facet_buttons"))
            .c_str(),
        "bigFont.fnt");
    m_facetBannerLabel->setScale(0.45f);
    m_facetBannerLabel->setPosition({winSize.width / 2.f, bannerY});
    this->addChild(m_facetBannerLabel);

    m_facetMenu = CCMenu::create();
    m_facetMenu->setPosition({winSize.width / 2.f, winSize.height - 50.f});
    this->addChild(m_facetMenu, E::Z_CONTROLS_MENU + 2);

    auto tabBtnA = ButtonSprite::create(loc.getString("edit.facet_buttons").c_str(), 100, true, "bigFont.fnt", "GJ_button_01.png", 24.f, 0.55f);
    m_tabButtons = CCMenuItemSpriteExtra::create(tabBtnA, this, menu_selector(ButtonEditOverlay::onFacetTab));
    m_tabButtons->setTag(0);
    m_tabButtons->setPosition({-55.f, 0.f});
    m_facetMenu->addChild(m_tabButtons);

    auto tabBtnB = ButtonSprite::create(loc.getString("edit.facet_labels").c_str(), 100, true, "bigFont.fnt", "GJ_button_04.png", 24.f, 0.55f);
    m_tabLabels = CCMenuItemSpriteExtra::create(tabBtnB, this, menu_selector(ButtonEditOverlay::onFacetTab));
    m_tabLabels->setTag(1);
    m_tabLabels->setPosition({55.f, 0.f});
    m_facetMenu->addChild(m_tabLabels);

    m_instructionLabel = CCLabelBMFont::create(loc.getString("edit.instruction_buttons").c_str(), "chatFont.fnt");
    m_instructionLabel->setScale(0.55f);
    m_instructionLabel->setPosition({winSize.width / 2.f, winSize.height - 78.f});
    m_instructionLabel->setColor({220, 220, 220});
    this->addChild(m_instructionLabel);

    const float panelHeight = E::CONTROLS_PANEL_H;
    const float panelY = panelHeight / 2.f + 10.f;
    const float centerX = winSize.width / 2.f;

    auto panelBg = paimon::SpriteHelper::createDarkPanel(winSize.width - 20.f, panelHeight, 200);
    panelBg->setPosition({centerX - (winSize.width - 20.f) / 2.f, panelY - panelHeight / 2.f});
    this->addChild(panelBg, -1);

    const float titleY = panelY + panelHeight / 2.f - 15.f;
    m_panelTitleLabel = CCLabelBMFont::create(loc.getString("edit.panel_title").c_str(), "bigFont.fnt");
    m_panelTitleLabel->setScale(0.55f);
    m_panelTitleLabel->setPosition({centerX, titleY});
    this->addChild(m_panelTitleLabel);

    const float contentStartY = panelY + 10.f;
    const float row1Y = contentStartY;
    const float row2Y = contentStartY - 35.f;

    const float labelX = 30.f;
    const float sliderX = centerX - 60.f;
    const float valueX = sliderX + 130.f;

    auto scaleText = CCLabelBMFont::create(loc.getString("edit.scale").c_str(), "goldFont.fnt");
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

    auto opacityText = CCLabelBMFont::create(loc.getString("edit.opacity").c_str(), "goldFont.fnt");
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

    const float btnX = winSize.width - 70.f;
    const float btnCenterY = panelY - 5.f;

    auto acceptSpr = ButtonSprite::create(loc.getString("edit.accept").c_str(), 80, true, "bigFont.fnt", "GJ_button_01.png", 28.f, 0.6f);
    auto acceptBtn = CCMenuItemSpriteExtra::create(acceptSpr, this, menu_selector(ButtonEditOverlay::onAccept));
    acceptBtn->setPosition({btnX, btnCenterY + 20.f});
    m_controlsMenu->addChild(acceptBtn);

    auto resetSpr = ButtonSprite::create(loc.getString("edit.reset").c_str(), 80, true, "bigFont.fnt", "GJ_button_06.png", 28.f, 0.6f);
    auto resetBtn = CCMenuItemSpriteExtra::create(resetSpr, this, menu_selector(ButtonEditOverlay::onReset));
    resetBtn->setPosition({btnX, btnCenterY - 20.f});
    m_controlsMenu->addChild(resetBtn);
}

void ButtonEditOverlay::showControls(bool show) {
    if (m_scaleSlider) m_scaleSlider->setVisible(show);
    if (m_opacitySlider) m_opacitySlider->setVisible(show);
    if (m_scaleLabel) m_scaleLabel->setVisible(show);
    if (m_opacityLabel) m_opacityLabel->setVisible(show);
}

void ButtonEditOverlay::update(float) {
    if (!m_targetMenu || !m_targetMenu->getParent()) {
        log::warn("[ButtonEditOverlay] Target menu invalido; cerrando editor");
        m_isClosing = true;
        if (m_controlsMenu) m_controlsMenu->setTouchEnabled(false);
        if (m_facetMenu) m_facetMenu->setTouchEnabled(false);
        this->setTouchEnabled(false);
        if (m_selectionHighlight && m_selectionHighlight->getParent()) {
            m_selectionHighlight->removeFromParent();
        }
        clearAllHighlights();
        this->unscheduleUpdate();
        this->removeFromParent();
        return;
    }

    updateAllHighlights();
}

void ButtonEditOverlay::selectEntry(ButtonEditEntry* entry) {
    m_selectedEntry = entry;

    if (!entry) {
        showControls(false);
        if (m_selectionHighlight) m_selectionHighlight->setVisible(false);
        return;
    }

    showControls(true);

    if (!entry->node) return;

    float currentScale = entry->node->getScale();
    float currentOpacity = opacityOf(entry->node) / 255.0f;

    float scaleNorm = (currentScale - E::SCALE_MIN) / (E::SCALE_MAX - E::SCALE_MIN);
    m_scaleSlider->setValue(std::max(0.f, std::min(1.f, scaleNorm)));
    m_opacitySlider->setValue(currentOpacity);

    updateSliderLabels();
    updateSelectionHighlight();
    updateAllHighlights();
}

void ButtonEditOverlay::updateSelectionHighlight() {
    if (!m_selectedEntry || !m_selectionHighlight) return;

    auto* node = m_selectedEntry->node;
    if (!node || !node->getParent()) return;

    auto contentSize = node->getContentSize();
    float scale = node->getScale();
    float w = contentSize.width * scale + 10.f;
    float h = contentSize.height * scale + 10.f;

    ccColor4F fill = {E::SELECTION_R, E::SELECTION_G, E::SELECTION_B, E::SELECTION_A};
    drawRoundedRect(m_selectionHighlight, w, h, fill);

    auto worldCenter = node->getParent()->convertToWorldSpace(node->getPosition());
    auto worldBL = ccp(worldCenter.x - w / 2, worldCenter.y - h / 2);
    m_selectionHighlight->setPosition(this->convertToNodeSpace(worldBL));
    m_selectionHighlight->setVisible(true);
}

void ButtonEditOverlay::updateSliderLabels() {
    if (!m_selectedEntry || !m_selectedEntry->node) return;

    float scale = m_selectedEntry->node->getScale();
    float opacity = opacityOf(m_selectedEntry->node) / 255.0f * 100.0f;

    m_scaleLabel->setString(fmt::format("{:.2f}x", scale).c_str());
    m_opacityLabel->setString(fmt::format("{:.0f}%", opacity).c_str());
}

ButtonEditEntry* ButtonEditOverlay::findEntryAtPoint(CCPoint worldPos) {
    ButtonEditEntry* bestEntry = nullptr;
    int bestLayer = -999;
    int bestZOrder = -999999;
    float bestArea = FLT_MAX;

    for (auto& btn : *activeEntries()) {
        if (!btn.node) continue;

        auto parent = btn.node->getParent();
        if (!parent) continue;

        auto localPos = parent->convertToNodeSpace(worldPos);
        auto bbox = btn.node->boundingBox();

        if (bbox.containsPoint(localPos)) {
            // Determine selection layer (1 for label, 0 for button, -1 for background)
            int layer = 0;
            if (typeinfo_cast<cocos2d::CCLabelBMFont*>(btn.node) || typeinfo_cast<cocos2d::CCLabelTTF*>(btn.node)) {
                layer = 1;
            } else {
                std::string lowerKey = btn.layoutId;
                std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), ::tolower);
                
                if (lowerKey.find("label") != std::string::npos ||
                    lowerKey.find("lbl") != std::string::npos ||
                    lowerKey.find("text") != std::string::npos ||
                    lowerKey.find("txt") != std::string::npos ||
                    lowerKey.find("title") != std::string::npos ||
                    lowerKey.find("desc") != std::string::npos ||
                    lowerKey.find("name") != std::string::npos ||
                    lowerKey.find("tit") != std::string::npos) {
                    layer = 1;
                } else if (lowerKey.find("bg") != std::string::npos ||
                           lowerKey.find("background") != std::string::npos ||
                           lowerKey.find("back") != std::string::npos ||
                           lowerKey.find("card") != std::string::npos ||
                           lowerKey.find("fondo") != std::string::npos ||
                           lowerKey.find("overlay") != std::string::npos ||
                           lowerKey.find("logo") != std::string::npos ||
                           lowerKey.find("banner") != std::string::npos ||
                           lowerKey.find("art") != std::string::npos ||
                           lowerKey.find("decor") != std::string::npos ||
                           lowerKey.find("frame") != std::string::npos ||
                           lowerKey.find("panel") != std::string::npos ||
                           lowerKey.find("shadow") != std::string::npos ||
                           lowerKey.find("shape") != std::string::npos ||
                           lowerKey.find("border") != std::string::npos ||
                           lowerKey.find("rect") != std::string::npos ||
                           lowerKey.find("circle") != std::string::npos ||
                           lowerKey.find("container") != std::string::npos ||
                           lowerKey.find("box") != std::string::npos) {
                    layer = -1;
                } else {
                    auto size = btn.node->getContentSize();
                    if (size.width > 180.f && size.height > 120.f) {
                        layer = -1;
                    }
                }
            }

            int zOrder = btn.originalZOrder;
            float area = bbox.size.width * bbox.size.height;

            bool isBetter = false;
            if (!bestEntry) {
                isBetter = true;
            } else if (layer > bestLayer) {
                isBetter = true;
            } else if (layer == bestLayer) {
                if (zOrder > bestZOrder) {
                    isBetter = true;
                } else if (zOrder == bestZOrder) {
                    if (area < bestArea) {
                        isBetter = true;
                    }
                }
            }

            if (isBetter) {
                bestEntry = &btn;
                bestLayer = layer;
                bestZOrder = zOrder;
                bestArea = area;
            }
        }
    }
    return bestEntry;
}

bool ButtonEditOverlay::isTouchOnSlider(CCTouch* touch) {
    auto checkSlider = [&](Slider* slider) -> bool {
        if (!slider || !slider->isVisible() || !slider->getParent()) return false;
        auto sliderWorldPos = slider->getParent()->convertToWorldSpace(slider->getPosition());
        auto cs = slider->getContentSize();
        float sc = slider->getScale();
        float w = cs.width * sc;
        float h = std::max(cs.height * sc, 30.f);
        CCRect sliderRect(sliderWorldPos.x - w / 2.f, sliderWorldPos.y - h / 2.f, w, h);
        return sliderRect.containsPoint(touch->getLocation());
    };

    if (checkSlider(m_scaleSlider)) return true;
    if (checkSlider(m_opacitySlider)) return true;
    return false;
}

bool ButtonEditOverlay::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    if (isTouchOnSlider(touch)) {
        return false;
    }

    auto touchPos = touch->getLocation();
    auto found = findEntryAtPoint(touchPos);

    if (found) {
        m_draggedEntry = found;
        m_dragStartPos = touchPos;
        m_originalNodePos = found->node->getPosition();

        selectEntry(found);

        return true;
    }

    selectEntry(nullptr);
    return true;
}

void ButtonEditOverlay::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    if (!m_draggedEntry || !m_draggedEntry->node) return;

    const auto touchPos = touch->getLocation();
    const auto delta = ccpSub(touchPos, m_dragStartPos);
    auto newPos = ccpAdd(m_originalNodePos, delta);

    newPos = applySnap(newPos);

    m_draggedEntry->node->setPosition(newPos);
    updateSelectionHighlight();
    updateAllHighlights();
}

void ButtonEditOverlay::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    m_draggedEntry = nullptr;
    hideSnapGuides();
}

void ButtonEditOverlay::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    m_draggedEntry = nullptr;
    hideSnapGuides();
}

void ButtonEditOverlay::onScaleChanged(CCObject*) {
    if (!m_selectedEntry || !m_selectedEntry->node) return;

    const float sliderValue = m_scaleSlider->getValue();
    const float scale = E::SCALE_MIN + sliderValue * (E::SCALE_MAX - E::SCALE_MIN);

    m_selectedEntry->node->setScale(scale);

    if (auto menuItem = typeinfo_cast<CCMenuItemSpriteExtra*>(m_selectedEntry->node)) {
        menuItem->m_baseScale = scale;
    }

    updateSliderLabels();
    updateSelectionHighlight();
}

void ButtonEditOverlay::onOpacityChanged(CCObject*) {
    if (!m_selectedEntry || !m_selectedEntry->node) return;

    float opacity = m_opacitySlider->getValue();
    setOpacityFor(m_selectedEntry->node, static_cast<GLubyte>(opacity * 255));
    updateSliderLabels();
}

namespace {
void saveEntryLayout(std::string const& sceneKey, ButtonEditEntry const& e) {
    if (!e.node || e.layoutId.empty()) return;
    ButtonLayout layout;
    layout.position = e.node->getPosition();
    layout.scale = e.node->getScale();
    layout.opacity = opacityOf(e.node) / 255.0f;
    ButtonLayoutManager::get().setLayout(sceneKey, e.layoutId, layout);
    if (auto menuItem = typeinfo_cast<CCMenuItemSpriteExtra*>(e.node)) {
        menuItem->m_baseScale = layout.scale;
    }
}
} // namespace

void ButtonEditOverlay::onAccept(CCObject*) {
    if (m_isClosing) return;

    if (!m_targetMenu || !m_targetMenu->getParent()) {
        log::warn("[ButtonEditOverlay] Aceptar sin contexto; cerrando sin guardar");
        if (m_selectionHighlight && m_selectionHighlight->getParent()) {
            m_selectionHighlight->removeFromParent();
        }
        m_isClosing = true;
        this->unscheduleUpdate();
        this->removeFromParent();
        return;
    }

    for (auto const& e : m_buttonEntries) {
        saveEntryLayout(m_sceneKey, e);
    }
    for (auto const& e : m_labelEntries) {
        saveEntryLayout(m_sceneKey, e);
    }

    if (m_selectionHighlight && m_selectionHighlight->getParent()) {
        m_selectionHighlight->removeFromParent();
    }
    clearAllHighlights();

    m_isClosing = true;
    this->unscheduleUpdate();
    this->removeFromParent();
}

void ButtonEditOverlay::onReset(CCObject*) {
    if (m_isClosing) return;

    if (!m_targetMenu || !m_targetMenu->getParent()) {
        ButtonLayoutManager::get().resetScene(m_sceneKey);
        if (m_selectionHighlight && m_selectionHighlight->getParent()) {
            m_selectionHighlight->removeFromParent();
        }
        m_isClosing = true;
        this->unscheduleUpdate();
        this->removeFromParent();
        return;
    }

    auto restoreOne = [this](ButtonEditEntry& btn) {
        if (!btn.node || btn.layoutId.empty()) return;
        if (!btn.node->getParent()) return;

        auto def = ButtonLayoutManager::get().getDefaultLayout(m_sceneKey, btn.layoutId);
        float newScale;
        if (def) {
            btn.node->setPosition(def->position);
            btn.node->setScale(def->scale);
            setOpacityFor(btn.node, static_cast<GLubyte>(def->opacity * 255));
            newScale = def->scale;
        } else {
            btn.node->setPosition(btn.originalPos);
            btn.node->setScale(btn.originalScale);
            setOpacityFor(btn.node, static_cast<GLubyte>(btn.originalOpacity * 255));
            newScale = btn.originalScale;
        }

        if (auto menuItem = typeinfo_cast<CCMenuItemSpriteExtra*>(btn.node)) {
            menuItem->m_baseScale = newScale;
        }
    };

    for (auto& e : m_buttonEntries) restoreOne(e);
    for (auto& e : m_labelEntries) restoreOne(e);

    ButtonLayoutManager::get().resetScene(m_sceneKey);

    selectEntry(nullptr);
    updateAllHighlights();
}

void ButtonEditOverlay::createAllHighlights() {
    clearAllHighlights();
    if (!m_targetMenu || !m_targetMenu->getParent()) return;

    m_buttonHighlights.reserve(activeEntries()->size());

    for (auto& btn : *activeEntries()) {
        if (!btn.node || btn.highlightKey.empty()) continue;
        // Mismo razonamiento que m_selectionHighlight: estos highlights
        // se redibujan dinámicamente con drawPolygon → CCDrawNode legacy.
        cocos2d::ccColor4F hlFill{
            80.f / 255.f, 180.f / 255.f, 255.f / 255.f, 120.f / 255.f
        };
        auto spr = paimon::SpriteHelper::createRoundedRect(10, 10, 3.f, hlFill);
        if (!spr) continue;

        spr->setZOrder(E::Z_BUTTON_HL);
        this->addChild(spr, E::Z_BUTTON_HL);
        m_buttonHighlights[btn.highlightKey] = spr;
    }
    updateAllHighlights();
}

void ButtonEditOverlay::updateAllHighlights() {
    if (!m_targetMenu) return;
    for (auto& btn : *activeEntries()) {
        if (!btn.node || btn.highlightKey.empty()) continue;
        auto it = m_buttonHighlights.find(btn.highlightKey);
        if (it == m_buttonHighlights.end()) continue;
        auto hl = it->second;
        if (!hl) continue;

        auto contentSize = btn.node->getContentSize();
        float scale = btn.node->getScale();
        float w = contentSize.width * scale + 10.f;
        float h = contentSize.height * scale + 10.f;

        ccColor4F fill = {E::BUTTON_HL_R, E::BUTTON_HL_G, E::BUTTON_HL_B, E::BUTTON_HL_A};
        drawRoundedRect(hl, w, h, fill);

        if (auto parent = btn.node->getParent()) {
            auto worldCenter = parent->convertToWorldSpace(btn.node->getPosition());
            auto worldBL = ccp(worldCenter.x - w / 2, worldCenter.y - h / 2);
            hl->setPosition(this->convertToNodeSpace(worldBL));
            hl->setVisible(true);
        } else {
            hl->setVisible(false);
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

void ButtonEditOverlay::createSnapGuides() {
    auto winSize = CCDirector::get()->getWinSize();

    m_snapGuideX = PaimonDrawNode::create();
    m_snapGuideX->setZOrder(2000);
    m_snapGuideX->setVisible(false);
    this->addChild(m_snapGuideX);

    m_snapGuideY = PaimonDrawNode::create();
    m_snapGuideY->setZOrder(2000);
    m_snapGuideY->setVisible(false);
    this->addChild(m_snapGuideY);
}

void ButtonEditOverlay::updateSnapGuides(bool showX, bool showY, float snapX, float snapY) {
    auto winSize = CCDirector::get()->getWinSize();

    if (m_snapGuideX) {
        m_snapGuideX->clear();
        if (showX) {
            m_snapGuideX->drawSegment(
                ccp(snapX, 0),
                ccp(snapX, winSize.height),
                1.0f,
                ccc4f(E::SNAP_GUIDE_R, E::SNAP_GUIDE_G, E::SNAP_GUIDE_B, E::SNAP_GUIDE_A));
            m_snapGuideX->setVisible(true);
        } else {
            m_snapGuideX->setVisible(false);
        }
    }

    if (m_snapGuideY) {
        m_snapGuideY->clear();
        if (showY) {
            m_snapGuideY->drawSegment(
                ccp(0, snapY),
                ccp(winSize.width, snapY),
                1.0f,
                ccc4f(E::SNAP_GUIDE_R, E::SNAP_GUIDE_G, E::SNAP_GUIDE_B, E::SNAP_GUIDE_A));
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
    if (!m_draggedEntry || !m_draggedEntry->node) return pos;

    auto* dragParent = m_draggedEntry->node->getParent();
    if (!dragParent) return pos;

    CCPoint posWorld = dragParent->convertToWorldSpace(pos);

    float bestSnapX = posWorld.x;
    float bestSnapY = posWorld.y;
    float minDistX = m_snapThreshold + 1.0f;
    float minDistY = m_snapThreshold + 1.0f;
    bool foundSnapX = false;
    bool foundSnapY = false;

    for (auto& btn : *activeEntries()) {
        if (&btn == m_draggedEntry || !btn.node) continue;

        auto* btnParent = btn.node->getParent();
        if (!btnParent) continue;

        CCPoint otherWorld = btnParent->convertToWorldSpace(btn.node->getPosition());

        float distX = std::abs(posWorld.x - otherWorld.x);
        if (distX < m_snapThreshold && distX < minDistX) {
            minDistX = distX;
            bestSnapX = otherWorld.x;
            foundSnapX = true;
        }

        float distY = std::abs(posWorld.y - otherWorld.y);
        if (distY < m_snapThreshold && distY < minDistY) {
            minDistY = distY;
            bestSnapY = otherWorld.y;
            foundSnapY = true;
        }
    }

    CCPoint resultWorld = posWorld;
    if (foundSnapX) resultWorld.x = bestSnapX;
    if (foundSnapY) resultWorld.y = bestSnapY;
    CCPoint result = dragParent->convertToNodeSpace(resultWorld);

    updateSnapGuides(foundSnapX, foundSnapY, bestSnapX, bestSnapY);

    m_snappedX = foundSnapX;
    m_snappedY = foundSnapY;

    return result;
}
