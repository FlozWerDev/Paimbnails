#include "ProfilePicEditorPopup.hpp"
#include "../../../core/Settings.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/ShapeStencil.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../services/ProfilePicRenderer.hpp"
#include "../services/ProfileImageService.hpp"
#include "../services/ProfileThumbs.hpp"
#include <Geode/ui/ColorPickPopup.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/SliderTouchLogic.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/cocos.hpp>
#include <filesystem>
#include <fstream>
#include <random>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

// Geometria del popup
namespace {
    constexpr float kPopupW = 500.f;
    constexpr float kPopupH = 320.f;

    // Area de preview
    constexpr float kPreviewBoxW = 140.f;
    constexpr float kPreviewBoxH = 190.f;
    constexpr float kPreviewPad  = 10.f;

    // Panel de controles
    constexpr float kPanelX = 160.f;        // x del borde izquierdo del panel
    constexpr float kPanelW = 330.f;        // ancho
    constexpr float kPanelH = 220.f;        // alto

    // Slider
    constexpr float kSliderW = 140.f;

    // Helpers
    CCLabelBMFont* smallLabel(std::string const& str, float scale = 0.4f, char const* font = "goldFont.fnt") {
        auto lbl = CCLabelBMFont::create(str.c_str(), font);
        lbl->setScale(scale);
        return lbl;
    }

    // Crea un slider normalizado [0..1]
    Slider* makeSlider(
        CCNode* parent, float yPos, float normValue,
        SEL_MenuHandler handler, CCNode* target
    ) {
        auto* slider = Slider::create(target, handler, 0.75f);
        slider->setPosition({0, yPos});
        slider->setValue(std::clamp(normValue, 0.f, 1.f));
        slider->setAnchorPoint({0.5f, 0.5f});
        parent->addChild(slider);
        return slider;
    }
}

// create / init

ProfilePicEditorPopup* ProfilePicEditorPopup::create() {
    auto ret = new ProfilePicEditorPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ProfilePicEditorPopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    this->setTitle("Profile Photo Editor");

    // Carga la configuracion actual
    m_editConfig = ProfilePicCustomizer::get().getConfig();

    auto winSize = m_mainLayer->getContentSize();
    float cx = winSize.width * 0.5f;

    // Zona de preview
    float previewCenterX = kPreviewPad + kPreviewBoxW * 0.5f;
    float previewCenterY = winSize.height * 0.5f - 5.f;

    auto previewBg = paimon::SpriteHelper::createRoundedRect(
        kPreviewBoxW, kPreviewBoxH, 8.f,
        ccc4FFromccc4B(ccc4(15, 15, 15, 200)),
        ccc4FFromccc4B(ccc4(255, 255, 255, 60)), 1.2f
    );
    if (previewBg) {
        previewBg->setAnchorPoint({0.5f, 0.5f});
        previewBg->setPosition({previewCenterX, previewCenterY});
        m_mainLayer->addChild(previewBg, -1);
    }

    auto previewLbl = smallLabel("Preview", 0.4f);
    previewLbl->setColor({255, 210, 90});
    previewLbl->setPosition({previewCenterX, previewCenterY + kPreviewBoxH * 0.5f - 12.f});
    m_mainLayer->addChild(previewLbl);

    m_previewContainer = CCNode::create();
    m_previewContainer->setContentSize({kPreviewBoxW - 20.f, kPreviewBoxH - 40.f});
    m_previewContainer->setAnchorPoint({0.5f, 0.5f});
    m_previewContainer->setPosition({previewCenterX, previewCenterY - 8.f});
    m_mainLayer->addChild(m_previewContainer);

    // Panel de controles
    float panelCenterX = kPanelX + kPanelW * 0.5f;
    float panelCenterY = winSize.height * 0.5f + 10.f;

    auto panelBg = paimon::SpriteHelper::createRoundedRect(
        kPanelW, kPanelH, 8.f,
        ccc4FFromccc4B(ccc4(0, 0, 0, 120)),
        ccc4FFromccc4B(ccc4(255, 255, 255, 40)), 1.0f
    );
    if (panelBg) {
        panelBg->setAnchorPoint({0.5f, 0.5f});
        panelBg->setPosition({panelCenterX, panelCenterY});
        m_mainLayer->addChild(panelBg, -1);
    }

    // Tabs del panel
    createTabs();

    // Contenedor del contenido de cada tab
    m_tabContent = CCNode::create();
    m_tabContent->setAnchorPoint({0.5f, 0.5f});
    m_tabContent->setContentSize({kPanelW - 16.f, kPanelH - 40.f});
    m_tabContent->setPosition({panelCenterX, panelCenterY - 14.f});
    m_mainLayer->addChild(m_tabContent);

    // Barra inferior: preset, random, reset, guardar
    float bottomY = 24.f;

    auto bottomMenu = CCMenu::create();
    bottomMenu->setPosition({0, 0});
    bottomMenu->setAnchorPoint({0, 0});
    bottomMenu->setContentSize(winSize);
    bottomMenu->ignoreAnchorPointForPosition(false);
    m_mainLayer->addChild(bottomMenu, 2);

    auto mkToolbarBtn = [&](char const* text, char const* bg, SEL_MenuHandler sel, float scale = 0.55f) {
        auto spr = ButtonSprite::create(text, "bigFont.fnt", bg, 0.7f);
        spr->setScale(scale);
        return CCMenuItemSpriteExtra::create(spr, this, sel);
    };

    auto presetBtn = mkToolbarBtn("Preset", "GJ_button_04.png", menu_selector(ProfilePicEditorPopup::onPreset));
    presetBtn->setPosition({kPreviewPad + kPreviewBoxW * 0.5f - 38.f, bottomY});
    bottomMenu->addChild(presetBtn);

    auto randomBtn = mkToolbarBtn("Random", "GJ_button_02.png", menu_selector(ProfilePicEditorPopup::onRandomize));
    randomBtn->setPosition({kPreviewPad + kPreviewBoxW * 0.5f + 38.f, bottomY});
    bottomMenu->addChild(randomBtn);

    auto resetBtn = mkToolbarBtn("Reset", "GJ_button_06.png", menu_selector(ProfilePicEditorPopup::onResetAll));
    resetBtn->setPosition({panelCenterX - 55.f, bottomY});
    bottomMenu->addChild(resetBtn);

    auto saveBtn = mkToolbarBtn("Save", "GJ_button_01.png", menu_selector(ProfilePicEditorPopup::onSave), 0.7f);
    saveBtn->setPosition({panelCenterX + 70.f, bottomY});
    bottomMenu->addChild(saveBtn);

    (void)cx; // sin uso

    // Tab inicial
    switchTab(0);

    // Preview inicial
    rebuildPreview();

    paimon::markDynamicPopup(this);
    return true;
}

// Tabs

void ProfilePicEditorPopup::createTabs() {
    auto winSize = m_mainLayer->getContentSize();
    float panelCenterX = kPanelX + kPanelW * 0.5f;
    float panelTopY    = winSize.height * 0.5f + kPanelH * 0.5f + 10.f - 14.f;
    // Calcula la posicion superior del panel

    static const std::array<char const*, 4> kTabNames = {
        "Frame", "Shape", "Decorate", "Adjust"
    };

    auto tabMenu = CCMenu::create();
    tabMenu->setPosition({panelCenterX, panelTopY - 4.f});
    tabMenu->setContentSize({kPanelW, 28.f});
    m_mainLayer->addChild(tabMenu, 3);
    tabMenu->setLayout(
        RowLayout::create()->setGap(4.f)->setAutoScale(false)
    );

    m_tabBtns.clear();
    for (int i = 0; i < (int)kTabNames.size(); i++) {
        auto spr = ButtonSprite::create(kTabNames[i], "goldFont.fnt", "GJ_button_03.png", 0.6f);
        spr->setScale(0.55f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ProfilePicEditorPopup::onTabBtn));
        btn->setTag(i);
        tabMenu->addChild(btn);
        m_tabBtns.push_back(btn);
    }
    tabMenu->updateLayout();
}

void ProfilePicEditorPopup::onTabBtn(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    switchTab(idx);
}

void ProfilePicEditorPopup::switchTab(int tab) {
    m_currentTab = tab;

    // Marca el tab activo
    for (int i = 0; i < (int)m_tabBtns.size(); i++) {
        auto* spr = typeinfo_cast<ButtonSprite*>(m_tabBtns[i]->getNormalImage());
        if (!spr) continue;
        spr->setColor(i == tab ? ccColor3B{120, 255, 120} : ccColor3B{255, 255, 255});
    }

    rebuildCurrentTab();
}

void ProfilePicEditorPopup::rebuildCurrentTab() {
    if (!m_tabContent) return;
    m_tabContent->removeAllChildrenWithCleanup(true);

    // Limpia referencias de sliders para evitar punteros invalidos
    m_thicknessSlider = nullptr;
    m_thicknessLabel = nullptr;
    m_frameOpacitySlider = nullptr;
    m_frameOpacityLabel = nullptr;
    m_scaleXSlider = m_scaleYSlider = m_sizeSlider = m_rotationSlider = nullptr;
    m_scaleXLabel = m_scaleYLabel = m_sizeLabel = m_rotationLabel = nullptr;
    m_imgZoomSlider = m_imgRotationSlider = m_imgOffsetXSlider = m_imgOffsetYSlider = nullptr;
    m_imgZoomLabel = m_imgRotationLabel = m_imgOffsetXLabel = m_imgOffsetYLabel = nullptr;
    m_decoScaleSlider = m_decoRotSlider = nullptr;
    m_decoPosXSlider = m_decoPosYSlider = m_decoOpacitySlider = nullptr;
    m_decoScaleLabel = m_decoRotLabel = nullptr;
    m_decoPosXLabel = m_decoPosYLabel = m_decoOpacityLabel = nullptr;

    CCNode* tabNode = nullptr;
    switch (m_currentTab) {
        case 0: tabNode = createFrameTab(); break;
        case 1: tabNode = createShapeTab(); break;
        case 2: tabNode = createDecoTab(); break;
        case 3: tabNode = createAdjustTab(); break;
    }
    if (tabNode) {
        tabNode->setAnchorPoint({0.5f, 0.5f});
        tabNode->setPosition(m_tabContent->getContentSize() * 0.5f);
        m_tabContent->addChild(tabNode);
    }
}

// Tab de borde

CCNode* ProfilePicEditorPopup::createFrameTab() {
    auto root = CCNode::create();
    CCSize area = m_tabContent->getContentSize();
    root->setContentSize(area);

    float cx = area.width * 0.5f;
    float topY = area.height - 18.f;

    // Activa/desactiva el borde
    auto toggleMenu = CCMenu::create();
    toggleMenu->setPosition({cx, topY});
    root->addChild(toggleMenu);

    auto toggle = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(ProfilePicEditorPopup::onFrameToggle), 0.7f
    );
    toggle->toggle(m_editConfig.frameEnabled);
    toggle->setPosition({-90.f, 0.f});
    toggleMenu->addChild(toggle);

    auto toggleLbl = smallLabel("Enable Border", 0.5f, "bigFont.fnt");
    toggleLbl->setAnchorPoint({0.f, 0.5f});
    toggleLbl->setPosition({cx - 78.f, topY});
    root->addChild(toggleLbl);

    // Sliders de grosor y opacidad
    float sliderY1 = topY - 28.f;
    float sliderY2 = topY - 54.f;

    auto addSlider = [&](char const* name, float y, float normValue,
                         SEL_MenuHandler sel, std::string const& valueText,
                         Slider*& outSlider, CCLabelBMFont*& outLabel) {
        auto lbl = smallLabel(name, 0.42f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({18.f, y});
        root->addChild(lbl);

        auto* slider = Slider::create(this, sel, 0.7f);
        slider->setPosition({area.width * 0.5f, y});
        slider->setValue(std::clamp(normValue, 0.f, 1.f));
        slider->setAnchorPoint({0.5f, 0.5f});
        root->addChild(slider);
        outSlider = slider;

        auto vlbl = smallLabel(valueText, 0.42f);
        vlbl->setAnchorPoint({1.f, 0.5f});
        vlbl->setPosition({area.width - 18.f, y});
        root->addChild(vlbl);
        outLabel = vlbl;
    };

    // Grosor: 0-20px normalizado
    float thickNorm = std::clamp(m_editConfig.frame.thickness / 20.f, 0.f, 1.f);
    addSlider("Thickness", sliderY1, thickNorm,
              menu_selector(ProfilePicEditorPopup::onThicknessChanged),
              fmt::format("{:.1f}", m_editConfig.frame.thickness),
              m_thicknessSlider, m_thicknessLabel);

    // Opacidad: 0-255
    float opNorm = std::clamp(m_editConfig.frame.opacity / 255.f, 0.f, 1.f);
    addSlider("Opacity", sliderY2, opNorm,
              menu_selector(ProfilePicEditorPopup::onFrameOpacityChanged),
              std::to_string(static_cast<int>(m_editConfig.frame.opacity)),
              m_frameOpacitySlider, m_frameOpacityLabel);

    // Paleta de colores
    auto colLbl = smallLabel("Color", 0.45f);
    colLbl->setAnchorPoint({0.f, 0.5f});
    colLbl->setPosition({18.f, sliderY2 - 28.f});
    root->addChild(colLbl);

    auto palette = ProfilePicCustomizer::getColorPalette();
    auto colMenu = CCMenu::create();
    colMenu->setAnchorPoint({0.5f, 0.5f});
    colMenu->setPosition({area.width * 0.5f, sliderY2 - 52.f});
    colMenu->setContentSize({area.width - 36.f, 46.f});
    root->addChild(colMenu);
    colMenu->setLayout(
        RowLayout::create()->setGap(3.f)->setGrowCrossAxis(true)
            ->setCrossAxisOverflow(false)->setAutoScale(false)
    );

    for (size_t i = 0; i < palette.size(); i++) {
        auto swatch = paimon::SpriteHelper::createColorPanel(18.f, 18.f, palette[i].second, 255, 2.f);
        if (!swatch) continue;
        bool selected = (palette[i].second.r == m_editConfig.frame.color.r &&
                         palette[i].second.g == m_editConfig.frame.color.g &&
                         palette[i].second.b == m_editConfig.frame.color.b);
        swatch->setContentSize({18.f, 18.f});
        auto btn = CCMenuItemSpriteExtra::create(swatch, this, menu_selector(ProfilePicEditorPopup::onBorderColorSelect));
        btn->setTag(static_cast<int>(i));
        if (selected) {
            auto ring = paimon::SpriteHelper::createRoundedRectOutline(22.f, 22.f, 3.f,
                ccc4FFromccc4B(ccc4(255, 255, 0, 255)), 1.5f);
            if (ring) {
                ring->setPosition({9.f, 9.f});
                ring->setAnchorPoint({0.5f, 0.5f});
                btn->addChild(ring);
            }
        }
        colMenu->addChild(btn);
    }

    // Boton de color personalizado
    auto customSpr = ButtonSprite::create("Custom", "goldFont.fnt", "GJ_button_04.png", 0.6f);
    customSpr->setScale(0.5f);
    auto customBtn = CCMenuItemSpriteExtra::create(customSpr, this,
        menu_selector(ProfilePicEditorPopup::onPickCustomBorderColor));
    colMenu->addChild(customBtn);

    colMenu->updateLayout();

    return root;
}

void ProfilePicEditorPopup::onFrameToggle(CCObject* sender) {
    auto toggler = static_cast<CCMenuItemToggler*>(sender);
    m_editConfig.frameEnabled = !toggler->isToggled(); // Estado antes del toggle
    rebuildPreview();
}

void ProfilePicEditorPopup::onThicknessChanged(CCObject* sender) {
    auto* slider = static_cast<SliderThumb*>(sender);
    float v = slider->getValue();
    m_editConfig.frame.thickness = v * 20.f;
    if (m_thicknessLabel) m_thicknessLabel->setString(fmt::format("{:.1f}", m_editConfig.frame.thickness).c_str());
    rebuildPreview();
}

void ProfilePicEditorPopup::onFrameOpacityChanged(CCObject* sender) {
    auto* slider = static_cast<SliderThumb*>(sender);
    float v = slider->getValue();
    m_editConfig.frame.opacity = v * 255.f;
    if (m_frameOpacityLabel) m_frameOpacityLabel->setString(std::to_string(static_cast<int>(m_editConfig.frame.opacity)).c_str());
    rebuildPreview();
}

void ProfilePicEditorPopup::onBorderColorSelect(CCObject* sender) {
    int idx = static_cast<CCMenuItemSpriteExtra*>(sender)->getTag();
    auto palette = ProfilePicCustomizer::getColorPalette();
    if (idx < 0 || idx >= (int)palette.size()) return;
    m_editConfig.frame.color = palette[idx].second;
    rebuildCurrentTab();
    rebuildPreview();
}

void ProfilePicEditorPopup::onPickCustomBorderColor(CCObject*) {
    auto* popup = geode::ColorPickPopup::create(m_editConfig.frame.color);
    if (!popup) return;
    popup->setCallback([this](ccColor4B const& c) {
        m_editConfig.frame.color = {c.r, c.g, c.b};
        rebuildCurrentTab();
        rebuildPreview();
    });
    popup->show();
}

// Tab de forma

CCNode* ProfilePicEditorPopup::createShapeTab() {
    auto root = CCNode::create();
    CCSize area = m_tabContent->getContentSize();
    root->setContentSize(area);

    float topY = area.height - 18.f;

    auto addSlider = [&](char const* name, float y, float normValue,
                         SEL_MenuHandler sel, std::string const& valueText,
                         Slider*& outSlider, CCLabelBMFont*& outLabel) {
        auto lbl = smallLabel(name, 0.4f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({14.f, y});
        root->addChild(lbl);

        auto* slider = Slider::create(this, sel, 0.65f);
        slider->setPosition({area.width * 0.5f + 6.f, y});
        slider->setValue(std::clamp(normValue, 0.f, 1.f));
        slider->setAnchorPoint({0.5f, 0.5f});
        root->addChild(slider);
        outSlider = slider;

        auto vlbl = smallLabel(valueText, 0.4f);
        vlbl->setAnchorPoint({1.f, 0.5f});
        vlbl->setPosition({area.width - 14.f, y});
        root->addChild(vlbl);
        outLabel = vlbl;
    };

    // Ancho normalizado
    float wNorm = std::clamp((m_editConfig.scaleX - 0.5f) / 1.3f, 0.f, 1.f);
    addSlider("Width", topY, wNorm,
              menu_selector(ProfilePicEditorPopup::onScaleXChanged),
              fmt::format("{:.2f}", m_editConfig.scaleX),
              m_scaleXSlider, m_scaleXLabel);

    // Alto normalizado
    float hNorm = std::clamp((m_editConfig.scaleY - 0.5f) / 1.3f, 0.f, 1.f);
    addSlider("Height", topY - 18.f, hNorm,
              menu_selector(ProfilePicEditorPopup::onScaleYChanged),
              fmt::format("{:.2f}", m_editConfig.scaleY),
              m_scaleYSlider, m_scaleYLabel);

    // Tamano: 60-200
    float sNorm = std::clamp((m_editConfig.size - 60.f) / 140.f, 0.f, 1.f);
    addSlider("Size", topY - 36.f, sNorm,
              menu_selector(ProfilePicEditorPopup::onSizeChanged),
              std::to_string(static_cast<int>(m_editConfig.size)),
              m_sizeSlider, m_sizeLabel);

    // Rotacion: -180..180
    float rNorm = std::clamp((m_editConfig.rotation + 180.f) / 360.f, 0.f, 1.f);
    addSlider("Rotation", topY - 54.f, rNorm,
              menu_selector(ProfilePicEditorPopup::onRotationChanged),
              fmt::format("{:.0f}", m_editConfig.rotation),
              m_rotationSlider, m_rotationLabel);

    // Grid de formas
    auto shapeLbl = smallLabel("Shape", 0.42f);
    shapeLbl->setAnchorPoint({0.f, 0.5f});
    shapeLbl->setPosition({14.f, topY - 78.f});
    root->addChild(shapeLbl);

    auto shapes = ProfilePicCustomizer::getAvailableStencils();
    auto shapeMenu = CCMenu::create();
    shapeMenu->setAnchorPoint({0.5f, 0.5f});
    shapeMenu->setPosition({area.width * 0.5f, topY - 104.f});
    shapeMenu->setContentSize({area.width - 30.f, 58.f});
    root->addChild(shapeMenu);
    shapeMenu->setLayout(
        RowLayout::create()->setGap(4.f)->setGrowCrossAxis(true)
            ->setCrossAxisOverflow(false)->setAutoScale(false)
    );

    for (size_t i = 0; i < shapes.size(); i++) {
        auto const& shapeName = shapes[i].first;
        float cellSize = 26.f;
        bool selected = m_editConfig.stencilSprite == shapeName;

        // Fondo del boton
        auto cellBg = paimon::SpriteHelper::createColorPanel(
            cellSize, cellSize,
            selected ? ccColor3B{60, 160, 60} : ccColor3B{40, 40, 40},
            selected ? 200 : 120, 3.f
        );

        // Icono centrado
        auto stencilIcon = createShapeStencil(shapeName, cellSize - 8.f);
        if (stencilIcon) {
            stencilIcon->setAnchorPoint({0.5f, 0.5f});
            stencilIcon->setPosition({cellSize * 0.5f, cellSize * 0.5f});
            cellBg->addChild(stencilIcon);
        }

        auto btn = CCMenuItemSpriteExtra::create(cellBg, this, menu_selector(ProfilePicEditorPopup::onStencilSelect));
        btn->setTag(static_cast<int>(i));
        shapeMenu->addChild(btn);
    }
    shapeMenu->updateLayout();

    // Boton de resetear forma
    auto resetSpr = ButtonSprite::create("Reset Shape", "goldFont.fnt", "GJ_button_06.png", 0.6f);
    resetSpr->setScale(0.5f);
    auto resetBtn = CCMenuItemSpriteExtra::create(resetSpr, this, menu_selector(ProfilePicEditorPopup::onResetShape));

    auto resetMenu = CCMenu::create();
    resetMenu->setPosition({area.width * 0.5f, 12.f});
    resetMenu->addChild(resetBtn);
    root->addChild(resetMenu);

    return root;
}

void ProfilePicEditorPopup::onScaleXChanged(CCObject* sender) {
    auto* slider = static_cast<SliderThumb*>(sender);
    float v = slider->getValue();
    m_editConfig.scaleX = 0.5f + v * 1.3f;
    if (m_scaleXLabel) m_scaleXLabel->setString(fmt::format("{:.2f}", m_editConfig.scaleX).c_str());
    rebuildPreview();
}

void ProfilePicEditorPopup::onScaleYChanged(CCObject* sender) {
    auto* slider = static_cast<SliderThumb*>(sender);
    float v = slider->getValue();
    m_editConfig.scaleY = 0.5f + v * 1.3f;
    if (m_scaleYLabel) m_scaleYLabel->setString(fmt::format("{:.2f}", m_editConfig.scaleY).c_str());
    rebuildPreview();
}

void ProfilePicEditorPopup::onSizeChanged(CCObject* sender) {
    auto* slider = static_cast<SliderThumb*>(sender);
    float v = slider->getValue();
    m_editConfig.size = 60.f + v * 140.f;
    if (m_sizeLabel) m_sizeLabel->setString(std::to_string(static_cast<int>(m_editConfig.size)).c_str());
    rebuildPreview();
}

void ProfilePicEditorPopup::onRotationChanged(CCObject* sender) {
    auto* slider = static_cast<SliderThumb*>(sender);
    float v = slider->getValue();
    m_editConfig.rotation = -180.f + v * 360.f;
    if (m_rotationLabel) m_rotationLabel->setString(fmt::format("{:.0f}", m_editConfig.rotation).c_str());
    rebuildPreview();
}

void ProfilePicEditorPopup::onStencilSelect(CCObject* sender) {
    int idx = static_cast<CCMenuItemSpriteExtra*>(sender)->getTag();
    auto shapes = ProfilePicCustomizer::getAvailableStencils();
    if (idx < 0 || idx >= (int)shapes.size()) return;
    m_editConfig.stencilSprite = shapes[idx].first;
    rebuildCurrentTab();
    rebuildPreview();
}

void ProfilePicEditorPopup::onResetShape(CCObject*) {
    m_editConfig.scaleX = 1.f;
    m_editConfig.scaleY = 1.f;
    m_editConfig.size = 120.f;
    m_editConfig.rotation = 0.f;
    m_editConfig.stencilSprite = "circle";
    rebuildCurrentTab();
    rebuildPreview();
}

// Tab de decoraciones

namespace {
    constexpr int kDecoGridCols = 6;
    constexpr int kDecoGridRows = 2;
    constexpr int kDecoPerPage = kDecoGridCols * kDecoGridRows;
    constexpr int kMaxDecos = 30;
}

CCNode* ProfilePicEditorPopup::createDecoTab() {
    auto root = CCNode::create();
    CCSize area = m_tabContent->getContentSize();
    root->setContentSize(area);

    auto categories = ProfilePicCustomizer::getDecorationCategories();
    if (categories.empty()) {
        auto lbl = smallLabel("No decorations available", 0.5f);
        lbl->setPosition(area * 0.5f);
        root->addChild(lbl);
        return root;
    }

    m_decoCategoryIdx = std::clamp(m_decoCategoryIdx, 0, (int)categories.size() - 1);

    // Categorias de decoraciones
    float chipsY = area.height - 14.f;
    auto chipsMenu = CCMenu::create();
    chipsMenu->setAnchorPoint({0.5f, 0.5f});
    chipsMenu->setPosition({area.width * 0.5f, chipsY});
    chipsMenu->setContentSize({area.width - 12.f, 22.f});
    chipsMenu->setLayout(RowLayout::create()->setGap(3.f)->setAutoScale(false));
    root->addChild(chipsMenu);

    for (size_t i = 0; i < categories.size(); i++) {
        bool active = ((int)i == m_decoCategoryIdx);
        auto spr = ButtonSprite::create(
            categories[i].displayName.c_str(),
            "goldFont.fnt",
            active ? "GJ_button_01.png" : "GJ_button_04.png", 0.65f
        );
        spr->setScale(0.45f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ProfilePicEditorPopup::onCategorySelect));
        btn->setTag((int)i);
        chipsMenu->addChild(btn);
    }
    chipsMenu->updateLayout();

    // Modo inspector de decoracion seleccionada
    if (m_selectedDecoIdx >= 0 && m_selectedDecoIdx < (int)m_editConfig.decorations.size()) {
        auto const& deco = m_editConfig.decorations[m_selectedDecoIdx];

        // Cabecera: icono + nombre + count
        float headerY = chipsY - 26.f;
        auto headerLbl = smallLabel(
            fmt::format("Editing deco #{} ({}/{})",
                m_selectedDecoIdx + 1, m_selectedDecoIdx + 1,
                (int)m_editConfig.decorations.size()),
            0.45f
        );
        headerLbl->setAnchorPoint({0.5f, 0.5f});
        headerLbl->setPosition({area.width * 0.5f, headerY});
        headerLbl->setColor({255, 220, 120});
        root->addChild(headerLbl);

        // Boton "Back" arriba izq
        auto backSpr = ButtonSprite::create("Back", "goldFont.fnt", "GJ_button_04.png", 0.6f);
        backSpr->setScale(0.45f);
        auto backBtn = CCMenuItemSpriteExtra::create(backSpr, this, menu_selector(ProfilePicEditorPopup::onCategorySelect));
        backBtn->setTag(-1000); // hack: tag especial para deseleccionar
        auto backMenu = CCMenu::create();
        backMenu->setPosition({18.f, headerY});
        backMenu->addChild(backBtn);
        root->addChild(backMenu);

        // Sliders del inspector
        auto addInspector = [&](char const* name, float y, float normValue,
                                SEL_MenuHandler sel, std::string const& valueText,
                                Slider*& outSlider, CCLabelBMFont*& outLabel) {
            auto lbl = smallLabel(name, 0.38f);
            lbl->setAnchorPoint({0.f, 0.5f});
            lbl->setPosition({14.f, y});
            root->addChild(lbl);

            auto* slider = Slider::create(this, sel, 0.6f);
            slider->setPosition({area.width * 0.5f + 6.f, y});
            slider->setValue(std::clamp(normValue, 0.f, 1.f));
            slider->setAnchorPoint({0.5f, 0.5f});
            root->addChild(slider);
            outSlider = slider;

            auto vlbl = smallLabel(valueText, 0.38f);
            vlbl->setAnchorPoint({1.f, 0.5f});
            vlbl->setPosition({area.width - 14.f, y});
            root->addChild(vlbl);
            outLabel = vlbl;
        };

        float y0 = headerY - 22.f;
        // Scale: 0.2-2.0 -> norm = (s-0.2)/1.8
        addInspector("Scale",
            y0, (deco.scale - 0.2f) / 1.8f,
            menu_selector(ProfilePicEditorPopup::onDecoScaleChanged),
            fmt::format("{:.2f}", deco.scale),
            m_decoScaleSlider, m_decoScaleLabel);

        addInspector("Rotation",
            y0 - 18.f, (deco.rotation + 180.f) / 360.f,
            menu_selector(ProfilePicEditorPopup::onDecoRotationChanged),
            fmt::format("{:.0f}", deco.rotation),
            m_decoRotSlider, m_decoRotLabel);

        // posX/Y range: -1.2 .. 1.2
        addInspector("Pos X",
            y0 - 36.f, (deco.posX + 1.2f) / 2.4f,
            menu_selector(ProfilePicEditorPopup::onDecoPosXChanged),
            fmt::format("{:.2f}", deco.posX),
            m_decoPosXSlider, m_decoPosXLabel);

        addInspector("Pos Y",
            y0 - 54.f, (deco.posY + 1.2f) / 2.4f,
            menu_selector(ProfilePicEditorPopup::onDecoPosYChanged),
            fmt::format("{:.2f}", deco.posY),
            m_decoPosYSlider, m_decoPosYLabel);

        addInspector("Opacity",
            y0 - 72.f, deco.opacity / 255.f,
            menu_selector(ProfilePicEditorPopup::onDecoOpacityChanged),
            std::to_string(static_cast<int>(deco.opacity)),
            m_decoOpacitySlider, m_decoOpacityLabel);

        // Botones de accion: voltear, capa, copiar, color, eliminar
        float btnY = y0 - 96.f;
        auto actionsMenu = CCMenu::create();
        actionsMenu->setAnchorPoint({0.5f, 0.5f});
        actionsMenu->setPosition({area.width * 0.5f, btnY});
        actionsMenu->setContentSize({area.width - 20.f, 24.f});
        actionsMenu->setLayout(RowLayout::create()->setGap(3.f)->setAutoScale(false));
        root->addChild(actionsMenu);

        auto mkSmallBtn = [&](char const* text, char const* bg, SEL_MenuHandler sel) {
            auto spr = ButtonSprite::create(text, "goldFont.fnt", bg, 0.6f);
            spr->setScale(0.4f);
            auto btn = CCMenuItemSpriteExtra::create(spr, this, sel);
            actionsMenu->addChild(btn);
            return btn;
        };

        mkSmallBtn(deco.flipX ? "FlipX*" : "FlipX", "GJ_button_03.png",
                   menu_selector(ProfilePicEditorPopup::onDecoFlipX));
        mkSmallBtn(deco.flipY ? "FlipY*" : "FlipY", "GJ_button_03.png",
                   menu_selector(ProfilePicEditorPopup::onDecoFlipY));
        mkSmallBtn("Z+", "GJ_button_05.png",
                   menu_selector(ProfilePicEditorPopup::onDecoZUp));
        mkSmallBtn("Z-", "GJ_button_05.png",
                   menu_selector(ProfilePicEditorPopup::onDecoZDown));
        mkSmallBtn("Copy", "GJ_button_02.png",
                   menu_selector(ProfilePicEditorPopup::onDecoDuplicate));
        mkSmallBtn("Color", "GJ_button_04.png",
                   menu_selector(ProfilePicEditorPopup::onDecoPickColor));
        mkSmallBtn("Del", "GJ_button_06.png",
                   menu_selector(ProfilePicEditorPopup::onDecoDelete));

        actionsMenu->updateLayout();
        return root;
    }

    // Grid de decoraciones disponibles
    auto const& cat = categories[m_decoCategoryIdx];
    int totalPages = std::max(1, ((int)cat.decorations.size() + kDecoPerPage - 1) / kDecoPerPage);
    m_decoPage = std::clamp(m_decoPage, 0, totalPages - 1);

    // Grid 2x6 de decoraciones
    float gridY = chipsY - 24.f - 14.f; // centro vertical del grid
    float cellSize = 26.f;
    float cellGap = 4.f;
    float gridW = kDecoGridCols * cellSize + (kDecoGridCols - 1) * cellGap;
    float startX = (area.width - gridW) * 0.5f + cellSize * 0.5f;

    auto gridMenu = CCMenu::create();
    gridMenu->setPosition({0.f, 0.f});
    gridMenu->setContentSize(area);
    root->addChild(gridMenu);

    int startIdx = m_decoPage * kDecoPerPage;
    int endIdx = std::min((int)cat.decorations.size(), startIdx + kDecoPerPage);

    for (int i = startIdx; i < endIdx; i++) {
        int local = i - startIdx;
        int col = local % kDecoGridCols;
        int row = local / kDecoGridCols;
        float x = startX + col * (cellSize + cellGap);
        float y = gridY + 0.5f * cellSize + ((kDecoGridRows - 1) * 0.5f - row) * (cellSize + cellGap);

        auto cellBg = paimon::SpriteHelper::createColorPanel(
            cellSize, cellSize, {40, 40, 40}, 150, 4.f
        );
        if (!cellBg) continue;

        CCSprite* icon = paimon::SpriteHelper::safeCreateWithFrameName(cat.decorations[i].first.c_str());
        if (!icon) icon = paimon::SpriteHelper::safeCreate(cat.decorations[i].first.c_str());
        if (icon) {
            icon->setAnchorPoint({0.5f, 0.5f});
            icon->setPosition({cellSize * 0.5f, cellSize * 0.5f});
            float iw = std::max(icon->getContentWidth(), 1.f);
            float ih = std::max(icon->getContentHeight(), 1.f);
            float s = std::min((cellSize - 6.f) / iw, (cellSize - 6.f) / ih);
            icon->setScale(s);
            cellBg->addChild(icon);
        }

        auto btn = CCMenuItemSpriteExtra::create(cellBg, this, menu_selector(ProfilePicEditorPopup::onAddDeco));
        btn->setPosition({x, y});
        btn->setTag(i); // indice absoluto en la categoria
        gridMenu->addChild(btn);
    }

    // ── Pagination + counter ──
    float pagY = gridY - cellSize - 10.f;

    auto pageLbl = smallLabel(
        fmt::format("Page {}/{}  •  Placed: {}/{}",
            m_decoPage + 1, totalPages,
            (int)m_editConfig.decorations.size(), kMaxDecos),
        0.38f
    );
    pageLbl->setAnchorPoint({0.5f, 0.5f});
    pageLbl->setPosition({area.width * 0.5f, pagY});
    root->addChild(pageLbl);

    auto pageMenu = CCMenu::create();
    pageMenu->setContentSize(area);
    pageMenu->setPosition({0.f, 0.f});
    root->addChild(pageMenu);

    if (totalPages > 1) {
        auto prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
        if (prevSpr) {
            prevSpr->setScale(0.5f);
            auto prevBtn = CCMenuItemSpriteExtra::create(prevSpr, this, menu_selector(ProfilePicEditorPopup::onDecoPage));
            prevBtn->setTag(-1);
            prevBtn->setPosition({area.width * 0.5f - 60.f, pagY});
            pageMenu->addChild(prevBtn);
        }
        auto nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        if (nextSpr) {
            nextSpr->setScale(0.5f);
            auto nextBtn = CCMenuItemSpriteExtra::create(nextSpr, this, menu_selector(ProfilePicEditorPopup::onDecoPage));
            nextBtn->setTag(1);
            nextBtn->setPosition({area.width * 0.5f + 60.f, pagY});
            pageMenu->addChild(nextBtn);
        }
    }

    // ── Lista de decos colocadas (chips clickables + clear all) ──
    float listY = pagY - 20.f;
    if (!m_editConfig.decorations.empty()) {
        auto placedLbl = smallLabel("Placed:", 0.4f);
        placedLbl->setAnchorPoint({0.f, 0.5f});
        placedLbl->setPosition({10.f, listY});
        root->addChild(placedLbl);

        auto placedMenu = CCMenu::create();
        placedMenu->setAnchorPoint({0.f, 0.5f});
        placedMenu->setPosition({58.f, listY});
        placedMenu->setContentSize({area.width - 130.f, 22.f});
        placedMenu->setLayout(RowLayout::create()->setGap(2.f)->setAxisAlignment(AxisAlignment::Start)->setAutoScale(false));
        root->addChild(placedMenu);

        int shown = 0;
        int maxShown = 10;
        for (int i = 0; i < (int)m_editConfig.decorations.size() && shown < maxShown; i++, shown++) {
            auto const& d = m_editConfig.decorations[i];
            auto chipBg = paimon::SpriteHelper::createColorPanel(18.f, 18.f, {70, 70, 70}, 180, 3.f);
            if (!chipBg) continue;
            CCSprite* chipIcon = paimon::SpriteHelper::safeCreateWithFrameName(d.spriteName.c_str());
            if (!chipIcon) chipIcon = paimon::SpriteHelper::safeCreate(d.spriteName.c_str());
            if (chipIcon) {
                chipIcon->setAnchorPoint({0.5f, 0.5f});
                chipIcon->setPosition({9.f, 9.f});
                float iw = std::max(chipIcon->getContentWidth(), 1.f);
                float ih = std::max(chipIcon->getContentHeight(), 1.f);
                float s = std::min(12.f / iw, 12.f / ih);
                chipIcon->setScale(s);
                chipIcon->setColor(d.color);
                chipBg->addChild(chipIcon);
            }
            auto chipBtn = CCMenuItemSpriteExtra::create(chipBg, this,
                menu_selector(ProfilePicEditorPopup::onSelectPlacedDeco));
            chipBtn->setTag(i);
            placedMenu->addChild(chipBtn);
        }
        placedMenu->updateLayout();

        // Clear all
        auto clearSpr = ButtonSprite::create("Clear", "goldFont.fnt", "GJ_button_06.png", 0.6f);
        clearSpr->setScale(0.4f);
        auto clearBtn = CCMenuItemSpriteExtra::create(clearSpr, this, menu_selector(ProfilePicEditorPopup::onClearAllDecos));
        auto clearMenu = CCMenu::create();
        clearMenu->setPosition({area.width - 32.f, listY});
        clearMenu->addChild(clearBtn);
        root->addChild(clearMenu);
    }

    return root;
}

void ProfilePicEditorPopup::onCategorySelect(CCObject* sender) {
    int tag = static_cast<CCNode*>(sender)->getTag();
    if (tag == -1000) {
        // Back: deseleccionar deco
        m_selectedDecoIdx = -1;
    } else {
        m_decoCategoryIdx = tag;
        m_decoPage = 0;
        m_selectedDecoIdx = -1;
    }
    rebuildCurrentTab();
}

void ProfilePicEditorPopup::onAddDeco(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    auto cats = ProfilePicCustomizer::getDecorationCategories();
    if (m_decoCategoryIdx < 0 || m_decoCategoryIdx >= (int)cats.size()) return;
    auto const& cat = cats[m_decoCategoryIdx];
    if (idx < 0 || idx >= (int)cat.decorations.size()) return;

    if ((int)m_editConfig.decorations.size() >= kMaxDecos) {
        PaimonNotify::create(fmt::format("Max {} decorations reached", kMaxDecos), NotificationIcon::Warning)->show();
        return;
    }

    // Colocar en posicion distribuida radialmente para no apilar todas encima
    int placed = (int)m_editConfig.decorations.size();
    float angle = static_cast<float>(placed) * 0.9f + 0.4f;
    float radius = 0.85f;

    PicDecoration deco;
    deco.spriteName = cat.decorations[idx].first;
    deco.posX = std::cos(angle) * radius;
    deco.posY = std::sin(angle) * radius;
    deco.scale = 0.5f;
    deco.zOrder = placed;
    m_editConfig.decorations.push_back(deco);

    m_selectedDecoIdx = (int)m_editConfig.decorations.size() - 1;
    rebuildCurrentTab();
    rebuildPreview();
}

void ProfilePicEditorPopup::onDecoPage(CCObject* sender) {
    int delta = static_cast<CCNode*>(sender)->getTag();
    m_decoPage += delta;
    rebuildCurrentTab();
}

void ProfilePicEditorPopup::onSelectPlacedDeco(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    if (idx < 0 || idx >= (int)m_editConfig.decorations.size()) return;
    m_selectedDecoIdx = idx;
    rebuildCurrentTab();
}

void ProfilePicEditorPopup::onDecoScaleChanged(CCObject* sender) {
    if (m_selectedDecoIdx < 0 || m_selectedDecoIdx >= (int)m_editConfig.decorations.size()) return;
    auto* slider = static_cast<SliderThumb*>(sender);
    float v = slider->getValue();
    m_editConfig.decorations[m_selectedDecoIdx].scale = 0.2f + v * 1.8f;
    if (m_decoScaleLabel) m_decoScaleLabel->setString(fmt::format("{:.2f}", m_editConfig.decorations[m_selectedDecoIdx].scale).c_str());
    rebuildPreview();
}

void ProfilePicEditorPopup::onDecoRotationChanged(CCObject* sender) {
    if (m_selectedDecoIdx < 0 || m_selectedDecoIdx >= (int)m_editConfig.decorations.size()) return;
    auto* slider = static_cast<SliderThumb*>(sender);
    float v = slider->getValue();
    m_editConfig.decorations[m_selectedDecoIdx].rotation = -180.f + v * 360.f;
    if (m_decoRotLabel) m_decoRotLabel->setString(fmt::format("{:.0f}", m_editConfig.decorations[m_selectedDecoIdx].rotation).c_str());
    rebuildPreview();
}

void ProfilePicEditorPopup::onDecoPosXChanged(CCObject* sender) {
    if (m_selectedDecoIdx < 0 || m_selectedDecoIdx >= (int)m_editConfig.decorations.size()) return;
    auto* slider = static_cast<SliderThumb*>(sender);
    float v = slider->getValue();
    m_editConfig.decorations[m_selectedDecoIdx].posX = -1.2f + v * 2.4f;
    if (m_decoPosXLabel) m_decoPosXLabel->setString(fmt::format("{:.2f}", m_editConfig.decorations[m_selectedDecoIdx].posX).c_str());
    rebuildPreview();
}

void ProfilePicEditorPopup::onDecoPosYChanged(CCObject* sender) {
    if (m_selectedDecoIdx < 0 || m_selectedDecoIdx >= (int)m_editConfig.decorations.size()) return;
    auto* slider = static_cast<SliderThumb*>(sender);
    float v = slider->getValue();
    m_editConfig.decorations[m_selectedDecoIdx].posY = -1.2f + v * 2.4f;
    if (m_decoPosYLabel) m_decoPosYLabel->setString(fmt::format("{:.2f}", m_editConfig.decorations[m_selectedDecoIdx].posY).c_str());
    rebuildPreview();
}

void ProfilePicEditorPopup::onDecoOpacityChanged(CCObject* sender) {
    if (m_selectedDecoIdx < 0 || m_selectedDecoIdx >= (int)m_editConfig.decorations.size()) return;
    auto* slider = static_cast<SliderThumb*>(sender);
    float v = slider->getValue();
    m_editConfig.decorations[m_selectedDecoIdx].opacity = v * 255.f;
    if (m_decoOpacityLabel) m_decoOpacityLabel->setString(std::to_string(static_cast<int>(m_editConfig.decorations[m_selectedDecoIdx].opacity)).c_str());
    rebuildPreview();
}

void ProfilePicEditorPopup::onDecoFlipX(CCObject*) {
    if (m_selectedDecoIdx < 0 || m_selectedDecoIdx >= (int)m_editConfig.decorations.size()) return;
    m_editConfig.decorations[m_selectedDecoIdx].flipX = !m_editConfig.decorations[m_selectedDecoIdx].flipX;
    rebuildCurrentTab();
    rebuildPreview();
}

void ProfilePicEditorPopup::onDecoFlipY(CCObject*) {
    if (m_selectedDecoIdx < 0 || m_selectedDecoIdx >= (int)m_editConfig.decorations.size()) return;
    m_editConfig.decorations[m_selectedDecoIdx].flipY = !m_editConfig.decorations[m_selectedDecoIdx].flipY;
    rebuildCurrentTab();
    rebuildPreview();
}

void ProfilePicEditorPopup::onDecoZUp(CCObject*) {
    if (m_selectedDecoIdx < 0 || m_selectedDecoIdx >= (int)m_editConfig.decorations.size()) return;
    m_editConfig.decorations[m_selectedDecoIdx].zOrder++;
    rebuildPreview();
}

void ProfilePicEditorPopup::onDecoZDown(CCObject*) {
    if (m_selectedDecoIdx < 0 || m_selectedDecoIdx >= (int)m_editConfig.decorations.size()) return;
    m_editConfig.decorations[m_selectedDecoIdx].zOrder--;
    rebuildPreview();
}

void ProfilePicEditorPopup::onDecoDuplicate(CCObject*) {
    if (m_selectedDecoIdx < 0 || m_selectedDecoIdx >= (int)m_editConfig.decorations.size()) return;
    if ((int)m_editConfig.decorations.size() >= kMaxDecos) {
        PaimonNotify::create(fmt::format("Max {} decorations reached", kMaxDecos), NotificationIcon::Warning)->show();
        return;
    }
    auto copy = m_editConfig.decorations[m_selectedDecoIdx];
    copy.posX += 0.15f;
    copy.posY -= 0.1f;
    copy.zOrder = (int)m_editConfig.decorations.size();
    m_editConfig.decorations.push_back(copy);
    m_selectedDecoIdx = (int)m_editConfig.decorations.size() - 1;
    rebuildCurrentTab();
    rebuildPreview();
}

void ProfilePicEditorPopup::onDecoDelete(CCObject*) {
    if (m_selectedDecoIdx < 0 || m_selectedDecoIdx >= (int)m_editConfig.decorations.size()) return;
    m_editConfig.decorations.erase(m_editConfig.decorations.begin() + m_selectedDecoIdx);
    m_selectedDecoIdx = -1;
    rebuildCurrentTab();
    rebuildPreview();
}

void ProfilePicEditorPopup::onDecoColorSelect(CCObject*) { /* no usado */ }

void ProfilePicEditorPopup::onDecoPickColor(CCObject*) {
    if (m_selectedDecoIdx < 0 || m_selectedDecoIdx >= (int)m_editConfig.decorations.size()) return;
    auto current = m_editConfig.decorations[m_selectedDecoIdx].color;
    auto* popup = geode::ColorPickPopup::create(current);
    if (!popup) return;
    int capturedIdx = m_selectedDecoIdx;
    popup->setCallback([this, capturedIdx](ccColor4B const& c) {
        if (capturedIdx < 0 || capturedIdx >= (int)m_editConfig.decorations.size()) return;
        m_editConfig.decorations[capturedIdx].color = {c.r, c.g, c.b};
        rebuildCurrentTab();
        rebuildPreview();
    });
    popup->show();
}

void ProfilePicEditorPopup::onClearAllDecos(CCObject*) {
    m_editConfig.decorations.clear();
    m_selectedDecoIdx = -1;
    rebuildCurrentTab();
    rebuildPreview();
}

// ═══════════════════════════════════════════════════════════
// ADJUST TAB (zoom, rotation de imagen, offsets)
// ═══════════════════════════════════════════════════════════

CCNode* ProfilePicEditorPopup::createAdjustTab() {
    auto root = CCNode::create();
    CCSize area = m_tabContent->getContentSize();
    root->setContentSize(area);

    float topY = area.height - 18.f;

    auto addSlider = [&](char const* name, float y, float normValue,
                         SEL_MenuHandler sel, std::string const& valueText,
                         Slider*& outSlider, CCLabelBMFont*& outLabel) {
        auto lbl = smallLabel(name, 0.4f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({14.f, y});
        root->addChild(lbl);

        auto* slider = Slider::create(this, sel, 0.65f);
        slider->setPosition({area.width * 0.5f + 6.f, y});
        slider->setValue(std::clamp(normValue, 0.f, 1.f));
        slider->setAnchorPoint({0.5f, 0.5f});
        root->addChild(slider);
        outSlider = slider;

        auto vlbl = smallLabel(valueText, 0.4f);
        vlbl->setAnchorPoint({1.f, 0.5f});
        vlbl->setPosition({area.width - 14.f, y});
        root->addChild(vlbl);
        outLabel = vlbl;
    };

    // Image Zoom: 0.5-3.0 -> norm = (v-0.5)/2.5
    float zoomNorm = std::clamp((m_editConfig.imageZoom - 0.5f) / 2.5f, 0.f, 1.f);
    addSlider("Zoom", topY, zoomNorm,
              menu_selector(ProfilePicEditorPopup::onImgZoomChanged),
              fmt::format("{:.2f}x", m_editConfig.imageZoom),
              m_imgZoomSlider, m_imgZoomLabel);

    // Image Rotation: -180..180
    float imgRotNorm = std::clamp((m_editConfig.imageRotation + 180.f) / 360.f, 0.f, 1.f);
    addSlider("Img Rotate", topY - 22.f, imgRotNorm,
              menu_selector(ProfilePicEditorPopup::onImgRotationChanged),
              fmt::format("{:.0f}", m_editConfig.imageRotation),
              m_imgRotationSlider, m_imgRotationLabel);

    // Offset X: -40..40 (pixels logicos)
    float offXNorm = std::clamp((m_editConfig.imageOffsetX + 40.f) / 80.f, 0.f, 1.f);
    addSlider("Offset X", topY - 44.f, offXNorm,
              menu_selector(ProfilePicEditorPopup::onImgOffsetXChanged),
              fmt::format("{:.0f}", m_editConfig.imageOffsetX),
              m_imgOffsetXSlider, m_imgOffsetXLabel);

    // Offset Y
    float offYNorm = std::clamp((m_editConfig.imageOffsetY + 40.f) / 80.f, 0.f, 1.f);
    addSlider("Offset Y", topY - 66.f, offYNorm,
              menu_selector(ProfilePicEditorPopup::onImgOffsetYChanged),
              fmt::format("{:.0f}", m_editConfig.imageOffsetY),
              m_imgOffsetYSlider, m_imgOffsetYLabel);

    // Reset
    auto resetSpr = ButtonSprite::create("Reset Adjust", "goldFont.fnt", "GJ_button_06.png", 0.6f);
    resetSpr->setScale(0.5f);
    auto resetBtn = CCMenuItemSpriteExtra::create(resetSpr, this, menu_selector(ProfilePicEditorPopup::onResetAdjust));
    auto resetMenu = CCMenu::create();
    resetMenu->setPosition({area.width * 0.5f, topY - 110.f});
    resetMenu->addChild(resetBtn);
    root->addChild(resetMenu);

    // Tip
    auto tipLbl = smallLabel("Zoom y offset permiten encuadrar tu foto dentro de la forma.", 0.34f, "chatFont.fnt");
    tipLbl->setColor({180, 180, 200});
    tipLbl->setOpacity(200);
    tipLbl->setAnchorPoint({0.5f, 0.5f});
    tipLbl->setPosition({area.width * 0.5f, 14.f});
    root->addChild(tipLbl);

    return root;
}

void ProfilePicEditorPopup::onImgZoomChanged(CCObject* sender) {
    auto* slider = static_cast<SliderThumb*>(sender);
    m_editConfig.imageZoom = 0.5f + slider->getValue() * 2.5f;
    if (m_imgZoomLabel) m_imgZoomLabel->setString(fmt::format("{:.2f}x", m_editConfig.imageZoom).c_str());
    rebuildPreview();
}

void ProfilePicEditorPopup::onImgRotationChanged(CCObject* sender) {
    auto* slider = static_cast<SliderThumb*>(sender);
    m_editConfig.imageRotation = -180.f + slider->getValue() * 360.f;
    if (m_imgRotationLabel) m_imgRotationLabel->setString(fmt::format("{:.0f}", m_editConfig.imageRotation).c_str());
    rebuildPreview();
}

void ProfilePicEditorPopup::onImgOffsetXChanged(CCObject* sender) {
    auto* slider = static_cast<SliderThumb*>(sender);
    m_editConfig.imageOffsetX = -40.f + slider->getValue() * 80.f;
    if (m_imgOffsetXLabel) m_imgOffsetXLabel->setString(fmt::format("{:.0f}", m_editConfig.imageOffsetX).c_str());
    rebuildPreview();
}

void ProfilePicEditorPopup::onImgOffsetYChanged(CCObject* sender) {
    auto* slider = static_cast<SliderThumb*>(sender);
    m_editConfig.imageOffsetY = -40.f + slider->getValue() * 80.f;
    if (m_imgOffsetYLabel) m_imgOffsetYLabel->setString(fmt::format("{:.0f}", m_editConfig.imageOffsetY).c_str());
    rebuildPreview();
}

void ProfilePicEditorPopup::onResetAdjust(CCObject*) {
    m_editConfig.imageZoom = 1.f;
    m_editConfig.imageRotation = 0.f;
    m_editConfig.imageOffsetX = 0.f;
    m_editConfig.imageOffsetY = 0.f;
    rebuildCurrentTab();
    rebuildPreview();
}

// ═══════════════════════════════════════════════════════════
// TOOLBAR: Preset / Randomize / Reset
// ═══════════════════════════════════════════════════════════

void ProfilePicEditorPopup::onPreset(CCObject*) {
    auto presets = ProfilePicCustomizer::getPresets();
    if (presets.empty()) return;

    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // Overlay fullscreen semitransparente
    auto overlay = CCLayerColor::create({0, 0, 0, 160});
    overlay->setContentSize(winSize);
    overlay->setPosition({0, 0});
    overlay->setID("paimon-preset-overlay"_spr);
    this->addChild(overlay, 9999);

    Ref<CCLayerColor> overlayRef = overlay;
    auto closeOverlay = [overlayRef]() {
        if (overlayRef) overlayRef->removeFromParent();
    };

    // Panel
    float panelW = 240.f;
    float rowH = 30.f;
    float panelH = 40.f + (float)presets.size() * rowH;

    auto panel = paimon::SpriteHelper::createRoundedRect(
        panelW, panelH, 8.f,
        ccc4FFromccc4B(ccc4(20, 20, 30, 240)),
        ccc4FFromccc4B(ccc4(255, 215, 0, 200)), 1.5f
    );
    if (!panel) { closeOverlay(); return; }
    panel->setAnchorPoint({0.5f, 0.5f});
    panel->setPosition({winSize.width * 0.5f, winSize.height * 0.5f});
    overlay->addChild(panel);

    auto title = smallLabel("Pick a Preset", 0.55f, "bigFont.fnt");
    title->setColor({255, 220, 100});
    title->setPosition({panelW * 0.5f, panelH - 14.f});
    panel->addChild(title);

    auto btnMenu = CCMenu::create();
    btnMenu->setContentSize({panelW, panelH - 28.f});
    btnMenu->setAnchorPoint({0.5f, 0.5f});
    btnMenu->setPosition({panelW * 0.5f, (panelH - 28.f) * 0.5f});
    btnMenu->ignoreAnchorPointForPosition(false);
    panel->addChild(btnMenu);

    // Boton X para cerrar
    auto closeSpr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
    if (closeSpr) {
        closeSpr->setScale(0.5f);
        auto closeBtn = CCMenuItemExt::createSpriteExtra(closeSpr, [closeOverlay](CCMenuItemSpriteExtra*) {
            closeOverlay();
        });
        auto closeMenu = CCMenu::create();
        closeMenu->setPosition({panelW - 12.f, panelH - 14.f});
        closeMenu->addChild(closeBtn);
        panel->addChild(closeMenu);
    }

    for (size_t i = 0; i < presets.size(); i++) {
        auto spr = ButtonSprite::create(
            presets[i].displayName.c_str(),
            "goldFont.fnt", "GJ_button_04.png", 0.8f
        );
        spr->setScale(0.7f);

        auto captured = presets[i].config;
        auto* btn = CCMenuItemExt::createSpriteExtra(spr,
            [this, captured, closeOverlay](CCMenuItemSpriteExtra*) {
                // Aplicar campos visuales del preset (no sobreescribe size/offsets de imagen)
                m_editConfig.stencilSprite = captured.stencilSprite;
                m_editConfig.frameEnabled = captured.frameEnabled;
                m_editConfig.frame = captured.frame;
                m_editConfig.decorations = captured.decorations;
                m_editConfig.scaleX = captured.scaleX;
                m_editConfig.scaleY = captured.scaleY;
                m_selectedDecoIdx = -1;
                rebuildCurrentTab();
                rebuildPreview();
                closeOverlay();
                PaimonNotify::create("Preset applied!", NotificationIcon::Success)->show();
            }
        );
        btnMenu->addChild(btn);
    }
    // ColumnLayout apila verticalmente (Axis::Vertical por defecto para ColumnLayout)
    btnMenu->setLayout(
        ColumnLayout::create()->setGap(4.f)->setAutoScale(false)
    );
    btnMenu->updateLayout();
}

void ProfilePicEditorPopup::onRandomize(CCObject*) {
    static std::random_device rd;
    static std::mt19937 rng(rd());

    auto shapes = ProfilePicCustomizer::getAvailableStencils();
    auto palette = ProfilePicCustomizer::getColorPalette();
    auto cats = ProfilePicCustomizer::getDecorationCategories();

    if (!shapes.empty()) {
        std::uniform_int_distribution<size_t> d(0, shapes.size() - 1);
        m_editConfig.stencilSprite = shapes[d(rng)].first;
    }

    std::uniform_real_distribution<float> rf01(0.f, 1.f);

    m_editConfig.scaleX = 0.9f + rf01(rng) * 0.3f;
    m_editConfig.scaleY = 0.9f + rf01(rng) * 0.3f;
    m_editConfig.rotation = (rf01(rng) < 0.3f) ? (rf01(rng) * 60.f - 30.f) : 0.f;
    m_editConfig.imageZoom = 1.0f + rf01(rng) * 0.3f;

    m_editConfig.frameEnabled = rf01(rng) < 0.8f;
    if (m_editConfig.frameEnabled && !palette.empty()) {
        std::uniform_int_distribution<size_t> dc(0, palette.size() - 1);
        m_editConfig.frame.color = palette[dc(rng)].second;
        m_editConfig.frame.thickness = 2.f + rf01(rng) * 5.f;
        m_editConfig.frame.opacity = 255.f;
    }

    // Decoraciones: 0-5 random, distribuidas radialmente
    m_editConfig.decorations.clear();
    int numDecos = static_cast<int>(rf01(rng) * 5.f);
    if (!cats.empty()) {
        std::uniform_int_distribution<size_t> dCat(0, cats.size() - 1);
        for (int i = 0; i < numDecos; i++) {
            auto const& cat = cats[dCat(rng)];
            if (cat.decorations.empty()) continue;
            std::uniform_int_distribution<size_t> dDeco(0, cat.decorations.size() - 1);
            PicDecoration d;
            d.spriteName = cat.decorations[dDeco(rng)].first;
            float ang = rf01(rng) * 6.2832f;
            float r = 0.75f + rf01(rng) * 0.3f;
            d.posX = std::cos(ang) * r;
            d.posY = std::sin(ang) * r;
            d.scale = 0.35f + rf01(rng) * 0.4f;
            d.rotation = rf01(rng) * 360.f - 180.f;
            if (!palette.empty()) {
                std::uniform_int_distribution<size_t> dc2(0, palette.size() - 1);
                d.color = palette[dc2(rng)].second;
            }
            d.zOrder = i;
            m_editConfig.decorations.push_back(d);
        }
    }

    m_selectedDecoIdx = -1;
    rebuildCurrentTab();
    rebuildPreview();
    PaimonNotify::create("Randomized!", NotificationIcon::Success)->show();
}

void ProfilePicEditorPopup::onResetAll(CCObject*) {
    m_editConfig = ProfilePicConfig(); // reset completo
    m_selectedDecoIdx = -1;
    m_decoCategoryIdx = 0;
    m_decoPage = 0;
    rebuildCurrentTab();
    rebuildPreview();
    PaimonNotify::create("Config reset", NotificationIcon::Info)->show();
}

// ═══════════════════════════════════════════════════════════
// PREVIEW (con fix de carga de imagen)
// ═══════════════════════════════════════════════════════════

void ProfilePicEditorPopup::triggerImageDownloadIfNeeded() {
    if (m_triggeredDownload) return;
    auto* acc = GJAccountManager::sharedState();
    if (!acc) return;
    int myID = acc->m_accountID;
    if (myID <= 0) return;

    // Si ya hay texture cacheada, nada que hacer
    if (ProfileThumbs::get().has(myID)) return;

    m_triggeredDownload = true;

    Ref<CCNode> safeSelf = this;
    ProfileImageService::get().downloadProfileImg(myID, [safeSelf, this, myID](bool success, CCTexture2D* tex) {
        if (success && tex) {
            ProfileThumbs::get().cacheProfile(myID, tex, {255,255,255}, {255,255,255}, 0.5f);
        }
        Loader::get()->queueInMainThread([safeSelf, this]() {
            if (!safeSelf || !safeSelf->getParent()) return;
            rebuildPreview();
        });
    }, /*isSelf=*/true);
}

void ProfilePicEditorPopup::rebuildPreview() {
    if (!m_previewContainer) return;
    m_previewContainer->removeAllChildrenWithCleanup(true);
    m_previewTexture = nullptr; // liberar textura anterior

    CCSize box = m_previewContainer->getContentSize();
    float center = std::min(box.width, box.height);
    float targetSize = center * 0.72f;

    // Intentar cargar imagen del usuario
    // Misma logica que MenuLayer::updateProfileButton y PaiConfigLayer::rebuildProfilePreview
    CCNode* imageNode = nullptr;

    std::string bgType = Mod::get()->getSavedValue<std::string>("profile-bg-type", "none");
    std::string bgPath = Mod::get()->getSavedValue<std::string>("profile-bg-path", "");

    std::error_code fsEc;
    if (bgType == "custom" && !bgPath.empty() && std::filesystem::exists(bgPath, fsEc) && !fsEc) {
        // Detectar GIF/APNG por contenido (magic bytes)
        bool isAnimated = false;
        {
            std::ifstream probe(bgPath, std::ios::binary);
            if (probe) {
                char hdr[32]{};
                probe.read(hdr, sizeof(hdr));
                auto fmt = imgp::guessFormat(hdr, static_cast<size_t>(probe.gcount()));
                isAnimated = (fmt == imgp::ImageFormat::Gif);
                if (!isAnimated && fmt == imgp::ImageFormat::Png) {
                    auto fileData = ImageLoadHelper::readBinaryFile(bgPath, 10);
                    if (!fileData.empty()) isAnimated = imgp::formats::isAPng(fileData.data(), fileData.size());
                }
            }
        }

        if (isAnimated) {
            auto* gif = AnimatedGIFSprite::create(bgPath);
            if (gif) imageNode = gif;
        }
        if (!imageNode) {
            auto* sprite = CCSprite::create(bgPath.c_str());
            if (sprite) imageNode = sprite;
        }
    }

    // Fallback: imagen de perfil descargada del servidor (profileimg_cache)
    if (!imageNode) {
        auto* acc = GJAccountManager::sharedState();
        int myID = acc ? acc->m_accountID : 0;
        if (myID > 0) {
            // 1) GIF animado cacheado
            std::string gifKey = ProfileImageService::get().getProfileImgGifKey(myID);
            if (!gifKey.empty() && AnimatedGIFSprite::isCached(gifKey)) {
                imageNode = AnimatedGIFSprite::createFromCache(gifKey);
            }

            // 2) Cache RAM del ProfilePage
            CCTexture2D* tex = nullptr;
            if (!imageNode) {
                extern CCTexture2D* getProfileImgCachedTexture(int accountID);
                m_previewTexture = getProfileImgCachedTexture(myID);
                tex = m_previewTexture;
            }

            // 3) Cache disco profileimg_cache
            if (!imageNode && !tex) {
                auto cachePath = geode::Mod::get()->getSaveDir() / "profileimg_cache" / fmt::format("{}.dat", myID);
                std::error_code ec;
                if (std::filesystem::exists(cachePath, ec) && !ec) {
                    std::ifstream file(cachePath, std::ios::binary | std::ios::ate);
                    if (file) {
                        auto size = file.tellg();
                        if (size > 0) {
                            file.seekg(0, std::ios::beg);
                            std::vector<uint8_t> data(static_cast<size_t>(size));
                            if (file.read(reinterpret_cast<char*>(data.data()), size)) {
                                bool isAnim = imgp::formats::isGif(data.data(), data.size())
                                           || imgp::formats::isAPng(data.data(), data.size());
                                bool isVideo = false;
                                if (data.size() > 12) {
                                    for (size_t i = 0; i + 3 < 12 && i + 3 < data.size(); ++i) {
                                        if (data[i]=='f' && data[i+1]=='t' && data[i+2]=='y' && data[i+3]=='p') {
                                            isVideo = true; break;
                                        }
                                    }
                                }
                                if (!isAnim && !isVideo) {
                                    auto loaded = ImageLoadHelper::loadWithSTBFromMemory(data.data(), data.size());
                                    if (loaded.success && loaded.texture) {
                                        m_previewTexture = loaded.texture;
                                        loaded.texture->autorelease();
                                        tex = m_previewTexture;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (tex) {
                imageNode = CCSprite::createWithTexture(tex);
                // m_previewTexture (Ref) mantiene la textura viva
            }

            // 4) Si no hay nada local ni cache, descargar async
            if (!imageNode) {
                triggerImageDownloadIfNeeded();
            }
        }
    }

    if (imageNode) {
        auto composed = paimon::profile_pic::composeProfilePicture(imageNode, targetSize, m_editConfig);
        if (composed) {
            composed->setPosition({box.width * 0.5f, box.height * 0.5f});
            m_previewContainer->addChild(composed);
            return;
        }
    }

    // Placeholder: render con config pero sin imagen (el renderer dibuja un rect oscuro)
    auto composed = paimon::profile_pic::composeProfilePicture(nullptr, targetSize, m_editConfig);
    if (composed) {
        composed->setPosition({box.width * 0.5f, box.height * 0.5f});
        m_previewContainer->addChild(composed);
    }

    auto lbl = smallLabel("NO IMAGE", 0.45f, "bigFont.fnt");
    lbl->setColor({200, 200, 200});
    lbl->setOpacity(180);
    lbl->setPosition({box.width * 0.5f, 8.f});
    m_previewContainer->addChild(lbl);
}

// ═══════════════════════════════════════════════════════════
// SAVE
// ═══════════════════════════════════════════════════════════

void ProfilePicEditorPopup::onSave(CCObject*) {
    ProfilePicCustomizer::get().setConfig(m_editConfig);
    ProfilePicCustomizer::get().save();
    ProfilePicCustomizer::get().setDirty(true);

    PaimonNotify::create("Profile photo saved!", NotificationIcon::Success)->show();
    this->onClose(nullptr);
}
