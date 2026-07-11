#include "ProfilePicEditorPopup.hpp"
#include "ProfilePicIconsDetailPopup.hpp"
#include "../../../core/Settings.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/ShapeStencil.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include "../services/ProfilePicRenderer.hpp"
#include "../services/ProfileImageService.hpp"
#include "../services/ProfileImageCache.hpp"
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
#include <random>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
    constexpr float kPopupW = 440.f;
    constexpr float kPopupH = 270.f;

    constexpr float kPreviewBoxW = 110.f;
    constexpr float kPreviewBoxH = 150.f;
    constexpr float kPreviewPad  = 10.f;

    constexpr float kPanelX = 130.f;
    constexpr float kPanelW = 290.f;
    constexpr float kPanelH = 185.f;

    CCLabelBMFont* smallLabel(std::string const& str, float scale = 0.4f, char const* font = "goldFont.fnt") {
        auto lbl = CCLabelBMFont::create(str.c_str(), font);
        lbl->setScale(scale);
        return lbl;
    }

    int equippedIconIdForType(GameManager* gm, int type) {
        if (!gm) return 1;
        switch (type) {
            case 1: return std::max(1, static_cast<int>(gm->m_playerShip));
            case 2: return std::max(1, static_cast<int>(gm->m_playerBall));
            case 3: return std::max(1, static_cast<int>(gm->m_playerBird));
            case 4: return std::max(1, static_cast<int>(gm->m_playerDart));
            case 5: return std::max(1, static_cast<int>(gm->m_playerRobot));
            case 6: return std::max(1, static_cast<int>(gm->m_playerSpider));
            case 7: return std::max(1, static_cast<int>(gm->m_playerSwing));
            default: return std::max(1, static_cast<int>(gm->m_playerFrame));
        }
    }
}


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

    m_editConfig = ProfilePicCustomizer::get().getConfig();

    auto winSize = m_mainLayer->getContentSize();

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

    m_previewStatusLabel = smallLabel("", 0.3f, "chatFont.fnt");
    m_previewStatusLabel->setColor({190, 190, 210});
    m_previewStatusLabel->setPosition({previewCenterX, previewCenterY - kPreviewBoxH * 0.5f + 10.f});
    m_mainLayer->addChild(m_previewStatusLabel);

    m_previewContainer = CCNode::create();
    m_previewContainer->setContentSize({kPreviewBoxW - 20.f, kPreviewBoxH - 40.f});
    m_previewContainer->setAnchorPoint({0.5f, 0.5f});
    m_previewContainer->setPosition({previewCenterX, previewCenterY - 4.f});
    m_mainLayer->addChild(m_previewContainer);

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

    createTabs();

    m_tabContent = CCNode::create();
    m_tabContent->setAnchorPoint({0.5f, 0.5f});
    m_tabContent->setContentSize({kPanelW - 16.f, kPanelH - 40.f});
    m_tabContent->setPosition({panelCenterX, panelCenterY - 14.f});
    m_mainLayer->addChild(m_tabContent);

    float bottomY = 19.f;

    auto bottomMenu = CCMenu::create();
    bottomMenu->setPosition({0, 0});
    bottomMenu->setAnchorPoint({0, 0});
    bottomMenu->setContentSize(winSize);
    bottomMenu->ignoreAnchorPointForPosition(false);
    m_mainLayer->addChild(bottomMenu, 2);

    auto saveSpr = ButtonSprite::create("Save", "bigFont.fnt", "GJ_button_01.png", 0.7f);
    saveSpr->setScale(0.7f);
    auto saveBtn = CCMenuItemSpriteExtra::create(saveSpr, this, menu_selector(ProfilePicEditorPopup::onSave));
    saveBtn->setPosition({panelCenterX + 10.f, bottomY});
    bottomMenu->addChild(saveBtn);

    switchTab(0);

    rebuildPreview();

    paimon::markDynamicPopup(this);
    return true;
}


void ProfilePicEditorPopup::createTabs() {
    auto winSize = m_mainLayer->getContentSize();
    float panelCenterX = kPanelX + kPanelW * 0.5f;
    float panelTopY    = winSize.height * 0.5f + kPanelH * 0.5f + 10.f - 14.f;

    static const std::array<char const*, 6> kTabNames = {
        "Photo", "Shape", "Border", "Icon", "Deco", "Style"
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

    m_thicknessSlider = nullptr;
    m_thicknessLabel = nullptr;
    m_frameOpacitySlider = nullptr;
    m_frameOpacityLabel = nullptr;
    m_scaleXSlider = m_scaleYSlider = m_sizeSlider = m_rotationSlider = nullptr;
    m_scaleXLabel = m_scaleYLabel = m_sizeLabel = m_rotationLabel = nullptr;
    m_imgZoomSlider = m_imgRotationSlider = m_imgOffsetXSlider = m_imgOffsetYSlider = nullptr;
    m_imgOpacitySlider = nullptr;
    m_imgZoomLabel = m_imgRotationLabel = m_imgOffsetXLabel = m_imgOffsetYLabel = nullptr;
    m_imgOpacityLabel = nullptr;
    m_decoScaleSlider = m_decoRotSlider = nullptr;
    m_decoPosXSlider = m_decoPosYSlider = m_decoOpacitySlider = nullptr;
    m_decoScaleLabel = m_decoRotLabel = nullptr;
    m_decoPosXLabel = m_decoPosYLabel = m_decoOpacityLabel = nullptr;

    CCNode* tabNode = nullptr;
    switch (m_currentTab) {
        case 0: tabNode = createPhotoTab(); break;
        case 1: tabNode = createShapeTab(); break;
        case 2: tabNode = createBorderTab(); break;
        case 3: tabNode = createIconTab(); break;
        case 4: tabNode = createDecoTab(); break;
        case 5: tabNode = createStyleTab(); break;
    }
    if (tabNode) {
        tabNode->setAnchorPoint({0.5f, 0.5f});
        tabNode->setPosition(m_tabContent->getContentSize() * 0.5f);
        m_tabContent->addChild(tabNode);
    }
}


// Shared slider row with fixed columns so labels never overlap the groove:
// [label 8..80] [slider ~92..210] [value ..area.width-8]
namespace {
    void addSliderRow(
        CCNode* root, CCNode* target, CCSize const& area,
        char const* name, float y, float normValue,
        SEL_MenuHandler sel, std::string const& valueText,
        Slider*& outSlider, CCLabelBMFont*& outLabel
    ) {
        auto lbl = smallLabel(name, 0.38f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({8.f, y});
        root->addChild(lbl);

        auto* slider = Slider::create(target, sel, 0.58f);
        slider->setPosition({area.width * 0.5f + 14.f, y});
        slider->setValue(std::clamp(normValue, 0.f, 1.f));
        slider->setAnchorPoint({0.5f, 0.5f});
        root->addChild(slider);
        outSlider = slider;

        auto vlbl = smallLabel(valueText, 0.38f);
        vlbl->setAnchorPoint({1.f, 0.5f});
        vlbl->setPosition({area.width - 8.f, y});
        root->addChild(vlbl);
        outLabel = vlbl;
    }
}


// ============================ Photo tab ============================

CCNode* ProfilePicEditorPopup::createPhotoTab() {
    auto root = CCNode::create();
    CCSize area = m_tabContent->getContentSize();
    root->setContentSize(area);

    auto photo = paimon::profile_pic::resolveProfilePhoto(m_editConfig);
    bool customActive = m_editConfig.photoSource == "custom";

    float srcY = area.height - 9.f;
    auto srcLbl = smallLabel("Source", 0.42f);
    srcLbl->setAnchorPoint({0.f, 0.5f});
    srcLbl->setPosition({8.f, srcY});
    root->addChild(srcLbl);

    auto srcMenu = CCMenu::create();
    srcMenu->setAnchorPoint({0.5f, 0.5f});
    srcMenu->setPosition({area.width * 0.5f + 30.f, srcY});
    srcMenu->setContentSize({190.f, 22.f});
    srcMenu->setLayout(RowLayout::create()->setGap(4.f)->setAutoScale(false));
    root->addChild(srcMenu);

    auto profileSpr = ButtonSprite::create("My Profile", "goldFont.fnt",
        customActive ? "GJ_button_04.png" : "GJ_button_01.png", 0.65f);
    profileSpr->setScale(0.42f);
    srcMenu->addChild(CCMenuItemSpriteExtra::create(profileSpr, this,
        menu_selector(ProfilePicEditorPopup::onPhotoSourceProfile)));

    auto customSpr = ButtonSprite::create(customActive ? "Change..." : "Custom...", "goldFont.fnt",
        customActive ? "GJ_button_01.png" : "GJ_button_04.png", 0.65f);
    customSpr->setScale(0.42f);
    srcMenu->addChild(CCMenuItemSpriteExtra::create(customSpr, this,
        menu_selector(ProfilePicEditorPopup::onPickCustomPhoto)));
    srcMenu->updateLayout();

    std::string status;
    using Source = paimon::profile_pic::ResolvedProfilePhoto::Source;
    switch (photo.source) {
        case Source::Custom: {
            auto name = std::filesystem::path(m_editConfig.photoPath).filename().string();
            if (name.size() > 28) name = name.substr(0, 25) + "...";
            status = fmt::format("Custom image: {}", name);
            break;
        }
        case Source::OwnProfile:
            status = "Using your profile picture";
            break;
        case Source::LegacyBackground:
            status = "Using profile background (no profile picture found)";
            break;
        default:
            status = photo.ownPhotoMissing
                ? "Downloading your profile picture..."
                : "No image available - pick a custom one";
            break;
    }
    auto statusLbl = smallLabel(status, 0.3f, "chatFont.fnt");
    statusLbl->setColor({180, 180, 200});
    statusLbl->setOpacity(200);
    statusLbl->setAnchorPoint({0.5f, 0.5f});
    statusLbl->setPosition({area.width * 0.5f, srcY - 15.f});
    root->addChild(statusLbl);

    float y0 = srcY - 31.f;
    float step = 15.f;

    float zoomNorm = std::clamp((m_editConfig.imageZoom - 0.5f) / 2.5f, 0.f, 1.f);
    addSliderRow(root, this, area, "Zoom", y0, zoomNorm,
                 menu_selector(ProfilePicEditorPopup::onImgZoomChanged),
                 fmt::format("{:.2f}x", m_editConfig.imageZoom),
                 m_imgZoomSlider, m_imgZoomLabel);

    float imgRotNorm = std::clamp((m_editConfig.imageRotation + 180.f) / 360.f, 0.f, 1.f);
    addSliderRow(root, this, area, "Rotation", y0 - step, imgRotNorm,
                 menu_selector(ProfilePicEditorPopup::onImgRotationChanged),
                 fmt::format("{:.0f}", m_editConfig.imageRotation),
                 m_imgRotationSlider, m_imgRotationLabel);

    float offXNorm = std::clamp((m_editConfig.imageOffsetX + 40.f) / 80.f, 0.f, 1.f);
    addSliderRow(root, this, area, "Offset X", y0 - step * 2.f, offXNorm,
                 menu_selector(ProfilePicEditorPopup::onImgOffsetXChanged),
                 fmt::format("{:.0f}", m_editConfig.imageOffsetX),
                 m_imgOffsetXSlider, m_imgOffsetXLabel);

    float offYNorm = std::clamp((m_editConfig.imageOffsetY + 40.f) / 80.f, 0.f, 1.f);
    addSliderRow(root, this, area, "Offset Y", y0 - step * 3.f, offYNorm,
                 menu_selector(ProfilePicEditorPopup::onImgOffsetYChanged),
                 fmt::format("{:.0f}", m_editConfig.imageOffsetY),
                 m_imgOffsetYSlider, m_imgOffsetYLabel);

    float opNorm = std::clamp(m_editConfig.imageOpacity / 255.f, 0.f, 1.f);
    addSliderRow(root, this, area, "Opacity", y0 - step * 4.f, opNorm,
                 menu_selector(ProfilePicEditorPopup::onImgOpacityChanged),
                 std::to_string(static_cast<int>(m_editConfig.imageOpacity)),
                 m_imgOpacitySlider, m_imgOpacityLabel);

    float rowY = 14.f;
    auto bottomMenu = CCMenu::create();
    bottomMenu->setPosition({0.f, 0.f});
    bottomMenu->setContentSize(area);
    root->addChild(bottomMenu);

    auto flipXToggle = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(ProfilePicEditorPopup::onImgFlipX), 0.5f);
    flipXToggle->toggle(m_editConfig.imageFlipX);
    flipXToggle->setPosition({20.f, rowY});
    bottomMenu->addChild(flipXToggle);

    auto flipXLbl = smallLabel("Flip X", 0.36f);
    flipXLbl->setAnchorPoint({0.f, 0.5f});
    flipXLbl->setPosition({32.f, rowY});
    root->addChild(flipXLbl);

    auto flipYToggle = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(ProfilePicEditorPopup::onImgFlipY), 0.5f);
    flipYToggle->toggle(m_editConfig.imageFlipY);
    flipYToggle->setPosition({88.f, rowY});
    bottomMenu->addChild(flipYToggle);

    auto flipYLbl = smallLabel("Flip Y", 0.36f);
    flipYLbl->setAnchorPoint({0.f, 0.5f});
    flipYLbl->setPosition({100.f, rowY});
    root->addChild(flipYLbl);

    auto resetSpr = ButtonSprite::create("Reset Photo", "goldFont.fnt", "GJ_button_06.png", 0.6f);
    resetSpr->setScale(0.42f);
    auto resetBtn = CCMenuItemSpriteExtra::create(resetSpr, this,
        menu_selector(ProfilePicEditorPopup::onResetAdjust));
    resetBtn->setPosition({area.width - 52.f, rowY});
    bottomMenu->addChild(resetBtn);

    return root;
}

void ProfilePicEditorPopup::onPhotoSourceProfile(CCObject*) {
    m_editConfig.photoSource = "profile";
    rebuildCurrentTab();
    rebuildPreview();
}

void ProfilePicEditorPopup::onPickCustomPhoto(CCObject*) {
    // if a custom image already exists but isn't active, just switch to it
    std::error_code ec;
    if (m_editConfig.photoSource != "custom" &&
        !m_editConfig.photoPath.empty() &&
        std::filesystem::exists(m_editConfig.photoPath, ec) && !ec) {
        m_editConfig.photoSource = "custom";
        rebuildCurrentTab();
        rebuildPreview();
        return;
    }

    WeakRef<ProfilePicEditorPopup> self = this;
    pt::pickImage([self](geode::Result<std::optional<std::filesystem::path>> result) {
        auto popup = self.lock();
        if (!popup) return;
        auto pathOpt = std::move(result).unwrapOr(std::nullopt);
        if (!pathOpt || pathOpt->empty()) return;

        auto imported = paimon::assets::importToBucket(*pathOpt, "profile_photo", paimon::assets::Kind::Image);
        if (!imported.success || imported.path.empty()) {
            PaimonNotify::create("Failed to import image", NotificationIcon::Error)->show();
            return;
        }
        popup->m_editConfig.photoSource = "custom";
        popup->m_editConfig.photoPath = paimon::assets::normalizePathString(imported.path);
        popup->rebuildCurrentTab();
        popup->rebuildPreview();
    });
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

void ProfilePicEditorPopup::onImgOpacityChanged(CCObject* sender) {
    auto* slider = static_cast<SliderThumb*>(sender);
    m_editConfig.imageOpacity = slider->getValue() * 255.f;
    if (m_imgOpacityLabel) m_imgOpacityLabel->setString(std::to_string(static_cast<int>(m_editConfig.imageOpacity)).c_str());
    rebuildPreview();
}

void ProfilePicEditorPopup::onImgFlipX(CCObject* sender) {
    auto toggler = static_cast<CCMenuItemToggler*>(sender);
    m_editConfig.imageFlipX = !toggler->isToggled();
    rebuildPreview();
}

void ProfilePicEditorPopup::onImgFlipY(CCObject* sender) {
    auto toggler = static_cast<CCMenuItemToggler*>(sender);
    m_editConfig.imageFlipY = !toggler->isToggled();
    rebuildPreview();
}

void ProfilePicEditorPopup::onResetAdjust(CCObject*) {
    m_editConfig.imageZoom = 1.f;
    m_editConfig.imageRotation = 0.f;
    m_editConfig.imageOffsetX = 0.f;
    m_editConfig.imageOffsetY = 0.f;
    m_editConfig.imageOpacity = 255.f;
    m_editConfig.imageFlipX = false;
    m_editConfig.imageFlipY = false;
    rebuildCurrentTab();
    rebuildPreview();
}


// ============================ Shape tab ============================

CCNode* ProfilePicEditorPopup::createShapeTab() {
    auto root = CCNode::create();
    CCSize area = m_tabContent->getContentSize();
    root->setContentSize(area);

    float topY = area.height - 10.f;
    float step = 15.f;

    float wNorm = std::clamp((m_editConfig.scaleX - 0.5f) / 1.3f, 0.f, 1.f);
    addSliderRow(root, this, area, "Width", topY, wNorm,
                 menu_selector(ProfilePicEditorPopup::onScaleXChanged),
                 fmt::format("{:.2f}", m_editConfig.scaleX),
                 m_scaleXSlider, m_scaleXLabel);

    float hNorm = std::clamp((m_editConfig.scaleY - 0.5f) / 1.3f, 0.f, 1.f);
    addSliderRow(root, this, area, "Height", topY - step, hNorm,
                 menu_selector(ProfilePicEditorPopup::onScaleYChanged),
                 fmt::format("{:.2f}", m_editConfig.scaleY),
                 m_scaleYSlider, m_scaleYLabel);

    float sNorm = std::clamp((m_editConfig.size - 60.f) / 140.f, 0.f, 1.f);
    addSliderRow(root, this, area, "Size", topY - step * 2.f, sNorm,
                 menu_selector(ProfilePicEditorPopup::onSizeChanged),
                 fmt::format("{:.0f}%", m_editConfig.size / 120.f * 100.f),
                 m_sizeSlider, m_sizeLabel);

    float rNorm = std::clamp((m_editConfig.rotation + 180.f) / 360.f, 0.f, 1.f);
    addSliderRow(root, this, area, "Rotation", topY - step * 3.f, rNorm,
                 menu_selector(ProfilePicEditorPopup::onRotationChanged),
                 fmt::format("{:.0f}", m_editConfig.rotation),
                 m_rotationSlider, m_rotationLabel);

    float shapeRowY = topY - step * 4.f - 4.f;
    auto shapeLbl = smallLabel("Shape", 0.42f);
    shapeLbl->setAnchorPoint({0.f, 0.5f});
    shapeLbl->setPosition({8.f, shapeRowY});
    root->addChild(shapeLbl);

    auto resetSpr = ButtonSprite::create("Reset", "goldFont.fnt", "GJ_button_06.png", 0.6f);
    resetSpr->setScale(0.42f);
    auto resetBtn = CCMenuItemSpriteExtra::create(resetSpr, this, menu_selector(ProfilePicEditorPopup::onResetShape));
    auto resetMenu = CCMenu::create();
    resetMenu->setPosition({area.width - 34.f, shapeRowY});
    resetMenu->addChild(resetBtn);
    root->addChild(resetMenu);

    auto shapes = ProfilePicCustomizer::getAvailableStencils();
    auto shapeMenu = CCMenu::create();
    shapeMenu->setAnchorPoint({0.5f, 0.5f});
    shapeMenu->setPosition({area.width * 0.5f, shapeRowY - 32.f});
    shapeMenu->setContentSize({area.width - 12.f, 46.f});
    root->addChild(shapeMenu);
    shapeMenu->setLayout(
        RowLayout::create()->setGap(3.f)->setGrowCrossAxis(true)
            ->setCrossAxisOverflow(false)->setAutoScale(false)
    );

    for (size_t i = 0; i < shapes.size(); i++) {
        auto const& shapeName = shapes[i].first;
        float cellSize = 20.f;
        bool selected = m_editConfig.stencilSprite == shapeName;

        auto cellBg = paimon::SpriteHelper::createColorPanel(
            cellSize, cellSize,
            selected ? ccColor3B{60, 160, 60} : ccColor3B{40, 40, 40},
            selected ? 200 : 120, 3.f
        );

        auto stencilIcon = createShapeStencil(shapeName, cellSize - 6.f);
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
    if (m_sizeLabel) m_sizeLabel->setString(fmt::format("{:.0f}%", m_editConfig.size / 120.f * 100.f).c_str());
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


// ============================ Border tab ============================

CCNode* ProfilePicEditorPopup::createBorderTab() {
    auto root = CCNode::create();
    CCSize area = m_tabContent->getContentSize();
    root->setContentSize(area);

    float cx = area.width * 0.5f;
    float topY = area.height - 14.f;

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

    if (!m_editConfig.frameEnabled) {
        auto hint = smallLabel("Turn the border on to customize\nthickness, opacity and color.", 0.38f, "chatFont.fnt");
        hint->setAlignment(kCCTextAlignmentCenter);
        hint->setColor({180, 180, 200});
        hint->setOpacity(200);
        hint->setPosition({cx, area.height * 0.45f});
        root->addChild(hint);
        return root;
    }

    float sliderY1 = topY - 26.f;
    float sliderY2 = sliderY1 - 16.f;

    float thickNorm = std::clamp(m_editConfig.frame.thickness / 20.f, 0.f, 1.f);
    addSliderRow(root, this, area, "Thickness", sliderY1, thickNorm,
                 menu_selector(ProfilePicEditorPopup::onThicknessChanged),
                 fmt::format("{:.1f}", m_editConfig.frame.thickness),
                 m_thicknessSlider, m_thicknessLabel);

    float opNorm = std::clamp(m_editConfig.frame.opacity / 255.f, 0.f, 1.f);
    addSliderRow(root, this, area, "Opacity", sliderY2, opNorm,
                 menu_selector(ProfilePicEditorPopup::onFrameOpacityChanged),
                 std::to_string(static_cast<int>(m_editConfig.frame.opacity)),
                 m_frameOpacitySlider, m_frameOpacityLabel);

    auto colLbl = smallLabel("Color", 0.42f);
    colLbl->setAnchorPoint({0.f, 0.5f});
    colLbl->setPosition({8.f, sliderY2 - 20.f});
    root->addChild(colLbl);

    auto palette = ProfilePicCustomizer::getColorPalette();
    auto colMenu = CCMenu::create();
    colMenu->setAnchorPoint({0.5f, 0.5f});
    colMenu->setPosition({cx, sliderY2 - 44.f});
    colMenu->setContentSize({area.width - 24.f, 44.f});
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

    auto customSpr = ButtonSprite::create("Custom", "goldFont.fnt", "GJ_button_04.png", 0.6f);
    customSpr->setScale(0.42f);
    auto customBtn = CCMenuItemSpriteExtra::create(customSpr, this,
        menu_selector(ProfilePicEditorPopup::onPickCustomBorderColor));
    colMenu->addChild(customBtn);

    colMenu->updateLayout();

    return root;
}

void ProfilePicEditorPopup::onFrameToggle(CCObject* sender) {
    auto toggler = static_cast<CCMenuItemToggler*>(sender);
    m_editConfig.frameEnabled = !toggler->isToggled();
    rebuildCurrentTab();
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


// ============================ Deco tab ============================

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

    if (m_selectedDecoIdx >= 0 && m_selectedDecoIdx < (int)m_editConfig.decorations.size()) {
        auto const& deco = m_editConfig.decorations[m_selectedDecoIdx];

        float headerY = chipsY - 24.f;
        auto headerLbl = smallLabel(
            fmt::format("Editing deco {}/{}",
                m_selectedDecoIdx + 1,
                (int)m_editConfig.decorations.size()),
            0.42f
        );
        headerLbl->setAnchorPoint({0.5f, 0.5f});
        headerLbl->setPosition({area.width * 0.5f, headerY});
        headerLbl->setColor({255, 220, 120});
        root->addChild(headerLbl);

        auto backSpr = ButtonSprite::create("Back", "goldFont.fnt", "GJ_button_04.png", 0.6f);
        backSpr->setScale(0.42f);
        auto backBtn = CCMenuItemSpriteExtra::create(backSpr, this, menu_selector(ProfilePicEditorPopup::onCategorySelect));
        backBtn->setTag(-1000);
        auto backMenu = CCMenu::create();
        backMenu->setPosition({20.f, headerY});
        backMenu->addChild(backBtn);
        root->addChild(backMenu);

        float y0 = headerY - 19.f;
        float step = 15.f;

        addSliderRow(root, this, area, "Scale",
            y0, (deco.scale - 0.2f) / 1.8f,
            menu_selector(ProfilePicEditorPopup::onDecoScaleChanged),
            fmt::format("{:.2f}", deco.scale),
            m_decoScaleSlider, m_decoScaleLabel);

        addSliderRow(root, this, area, "Rotation",
            y0 - step, (deco.rotation + 180.f) / 360.f,
            menu_selector(ProfilePicEditorPopup::onDecoRotationChanged),
            fmt::format("{:.0f}", deco.rotation),
            m_decoRotSlider, m_decoRotLabel);

        addSliderRow(root, this, area, "Pos X",
            y0 - step * 2.f, (deco.posX + 1.2f) / 2.4f,
            menu_selector(ProfilePicEditorPopup::onDecoPosXChanged),
            fmt::format("{:.2f}", deco.posX),
            m_decoPosXSlider, m_decoPosXLabel);

        addSliderRow(root, this, area, "Pos Y",
            y0 - step * 3.f, (deco.posY + 1.2f) / 2.4f,
            menu_selector(ProfilePicEditorPopup::onDecoPosYChanged),
            fmt::format("{:.2f}", deco.posY),
            m_decoPosYSlider, m_decoPosYLabel);

        addSliderRow(root, this, area, "Opacity",
            y0 - step * 4.f, deco.opacity / 255.f,
            menu_selector(ProfilePicEditorPopup::onDecoOpacityChanged),
            std::to_string(static_cast<int>(deco.opacity)),
            m_decoOpacitySlider, m_decoOpacityLabel);

        float btnY = 10.f;
        auto actionsMenu = CCMenu::create();
        actionsMenu->setAnchorPoint({0.5f, 0.5f});
        actionsMenu->setPosition({area.width * 0.5f, btnY});
        actionsMenu->setContentSize({area.width - 12.f, 22.f});
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

    auto const& cat = categories[m_decoCategoryIdx];
    int totalPages = std::max(1, ((int)cat.decorations.size() + kDecoPerPage - 1) / kDecoPerPage);
    m_decoPage = std::clamp(m_decoPage, 0, totalPages - 1);

    float gridY = chipsY - 24.f - 29.f;
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
        btn->setTag(i);
        gridMenu->addChild(btn);
    }

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
        auto prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        if (prevSpr) {
            prevSpr->setScale(0.45f);
            auto prevBtn = CCMenuItemSpriteExtra::create(prevSpr, this, menu_selector(ProfilePicEditorPopup::onDecoPage));
            prevBtn->setTag(-1);
            prevBtn->setPosition({area.width * 0.5f - 82.f, pagY});
            pageMenu->addChild(prevBtn);
        }
        auto nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        if (nextSpr) {
            nextSpr->setScale(0.45f);
            nextSpr->setFlipX(true);
            auto nextBtn = CCMenuItemSpriteExtra::create(nextSpr, this, menu_selector(ProfilePicEditorPopup::onDecoPage));
            nextBtn->setTag(1);
            nextBtn->setPosition({area.width * 0.5f + 82.f, pagY});
            pageMenu->addChild(nextBtn);
        }
    }

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

        auto clearSpr = ButtonSprite::create("Clear", "goldFont.fnt", "GJ_button_06.png", 0.6f);
        clearSpr->setScale(0.4f);
        auto clearBtn = CCMenuItemSpriteExtra::create(clearSpr, this, menu_selector(ProfilePicEditorPopup::onClearAllDecos));
        auto clearMenu = CCMenu::create();
        clearMenu->setPosition({area.width - 32.f, listY});
        clearMenu->addChild(clearBtn);
        root->addChild(clearMenu);
    } else {
        auto hint = smallLabel("Tap a decoration above to place it.", 0.32f, "chatFont.fnt");
        hint->setColor({180, 180, 200});
        hint->setOpacity(200);
        hint->setPosition({area.width * 0.5f, listY});
        root->addChild(hint);
    }

    return root;
}

void ProfilePicEditorPopup::onCategorySelect(CCObject* sender) {
    int tag = static_cast<CCNode*>(sender)->getTag();
    if (tag == -1000) {
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


// ============================ Icon tab ============================

CCNode* ProfilePicEditorPopup::createIconTab() {
    auto root = CCNode::create();
    CCSize area = m_tabContent->getContentSize();
    root->setContentSize(area);

    float topY = area.height - 14.f;

    auto toggleMenu = CCMenu::create();
    toggleMenu->setPosition({area.width * 0.5f, topY});
    root->addChild(toggleMenu);

    auto toggle = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(ProfilePicEditorPopup::onOnlyIconToggle), 0.7f
    );
    toggle->toggle(m_editConfig.onlyIconMode);
    toggle->setPosition({-80.f, 0.f});
    toggleMenu->addChild(toggle);

    auto toggleLbl = smallLabel("Only Icon Mode", 0.45f, "bigFont.fnt");
    toggleLbl->setAnchorPoint({0.f, 0.5f});
    toggleLbl->setPosition({area.width * 0.5f - 68.f, topY});
    root->addChild(toggleLbl);

    float y = topY - 24.f;

    auto idLbl = smallLabel("Your Icons", 0.38f);
    idLbl->setAnchorPoint({0.f, 0.5f});
    idLbl->setPosition({14.f, y});
    root->addChild(idLbl);

    auto iconGrid = CCNode::create();
    iconGrid->setAnchorPoint({0.5f, 0.5f});
    iconGrid->setContentSize({area.width - 24.f, 58.f});
    iconGrid->setPosition({area.width * 0.5f, y - 35.f});
    root->addChild(iconGrid);

    auto gm = GameManager::get();
    struct IconEntry { int type; int id; const char* name; };
    IconEntry entries[] = {
        {0, gm->m_playerFrame, "Cube"},
        {1, gm->m_playerShip, "Ship"},
        {2, gm->m_playerBall, "Ball"},
        {3, gm->m_playerBird, "UFO"},
        {4, gm->m_playerDart, "Wave"},
        {5, gm->m_playerRobot, "Robot"},
        {6, gm->m_playerSpider, "Spider"},
        {7, gm->m_playerSwing, "Swing"},
    };

    for (int row = 0; row < 2; row++) {
        auto iconMenu = CCMenu::create();
        iconMenu->setAnchorPoint({0.5f, 0.5f});
        iconMenu->setPosition({iconGrid->getContentSize().width * 0.5f, row == 0 ? 42.f : 14.f});
        iconMenu->setContentSize({iconGrid->getContentSize().width, 26.f});
        iconGrid->addChild(iconMenu);
        iconMenu->setLayout(
            RowLayout::create()->setGap(8.f)->setGrowCrossAxis(true)
                ->setCrossAxisOverflow(false)->setAutoScale(false)
        );

        for (size_t i = row * 4; i < static_cast<size_t>(row * 4 + 4); i++) {
            bool selected = m_editConfig.iconConfig.iconType == (int)i;
            auto cell = CCNode::create();
            cell->setContentSize({34.f, 26.f});
            cell->setAnchorPoint({0.5f, 0.5f});

            auto bg = paimon::SpriteHelper::createColorPanel(
                34.f, 26.f,
                selected ? ccColor3B{60, 160, 60} : ccColor3B{50, 50, 50},
                selected ? 220 : 140, 4.f);
            bg->setAnchorPoint({0.5f, 0.5f});
            bg->ignoreAnchorPointForPosition(false);
            bg->setPosition({17.f, 13.f});
            cell->addChild(bg);

            int cubeFrame = equippedIconIdForType(gm, 0);
            auto player = SimplePlayer::create(cubeFrame);
            if (player) {
                player->updatePlayerFrame(entries[i].id, static_cast<IconType>(entries[i].type));
                player->setColor(gm->colorForIdx(gm->m_playerColor));
                player->setSecondColor(gm->colorForIdx(gm->m_playerColor2));
                if (gm->m_playerGlow) player->setGlowOutline(gm->colorForIdx(gm->m_playerGlowColor));
                else player->disableGlowOutline();
                float maxDim = std::max(player->getContentSize().width, player->getContentSize().height);
                float gdRefSize = 30.f;
                float scale = (maxDim > 10.f && maxDim < 80.f) ? (18.f / maxDim) : (18.f / gdRefSize);
                player->setScale(std::clamp(scale, 0.25f, 0.55f));
                player->setAnchorPoint({0.5f, 0.5f});
                player->ignoreAnchorPointForPosition(false);
                player->setPosition({17.f, 13.f});
                cell->addChild(player, 1);
            }

            auto btn = CCMenuItemSpriteExtra::create(cell, this, menu_selector(ProfilePicEditorPopup::onGameIconSelect));
            btn->setTag(static_cast<int>(i));
            iconMenu->addChild(btn);
        }
        iconMenu->updateLayout();
    }

    y -= 74.f;

    auto colorTip = smallLabel("Colors, Glow, Scale & Anim in Detail", 0.32f, "chatFont.fnt");
    colorTip->setColor({150, 150, 170});
    colorTip->setOpacity(180);
    colorTip->setAnchorPoint({0.5f, 0.5f});
    colorTip->setPosition({area.width * 0.5f, y});
    root->addChild(colorTip);

    auto moreSpr = ButtonSprite::create("Open Icon Detail", "goldFont.fnt", "GJ_button_02.png", 0.6f);
    moreSpr->setScale(0.42f);
    auto moreBtn = CCMenuItemSpriteExtra::create(moreSpr, this, menu_selector(ProfilePicEditorPopup::onOpenIconsDetail));
    auto moreMenu = CCMenu::create();
    moreMenu->setPosition({area.width * 0.5f, y - 22.f});
    moreMenu->addChild(moreBtn);
    root->addChild(moreMenu);

    return root;
}

void ProfilePicEditorPopup::onOnlyIconToggle(CCObject* sender) {
    auto toggler = static_cast<CCMenuItemToggler*>(sender);
    m_editConfig.onlyIconMode = !toggler->isToggled();
    rebuildPreview();
}

void ProfilePicEditorPopup::onGameIconSelect(CCObject* sender) {
    int idx = static_cast<CCMenuItemSpriteExtra*>(sender)->getTag();
    auto gm = GameManager::get();
    int id = equippedIconIdForType(gm, idx);
    m_editConfig.iconConfig.iconType = idx;
    m_editConfig.iconConfig.iconId = id;
    m_editConfig.onlyIconMode = true;
    rebuildCurrentTab();
    rebuildPreview();
}

void ProfilePicEditorPopup::onOpenIconsDetail(CCObject*) {
    auto* pop = ProfilePicIconsDetailPopup::create(&m_editConfig, [this]() {
        rebuildCurrentTab();
        rebuildPreview();
    });
    if (pop) pop->show();
}


// ============================ Style tab ============================

CCNode* ProfilePicEditorPopup::createStyleTab() {
    auto root = CCNode::create();
    CCSize area = m_tabContent->getContentSize();
    root->setContentSize(area);

    float topY = area.height - 14.f;

    auto presetLbl = smallLabel("Presets", 0.42f);
    presetLbl->setAnchorPoint({0.f, 0.5f});
    presetLbl->setPosition({14.f, topY});
    root->addChild(presetLbl);

    auto actionMenu = CCMenu::create();
    actionMenu->setAnchorPoint({0.5f, 0.5f});
    actionMenu->setPosition({area.width * 0.5f, topY - 18.f});
    actionMenu->setContentSize({area.width - 20.f, 22.f});
    root->addChild(actionMenu);
    actionMenu->setLayout(
        RowLayout::create()->setGap(4.f)->setAutoScale(false)
    );

    auto mkActBtn = [&](char const* text, char const* bg, SEL_MenuHandler sel, float sc = 0.45f) {
        auto spr = ButtonSprite::create(text, "goldFont.fnt", bg, 0.7f);
        spr->setScale(sc);
        return CCMenuItemSpriteExtra::create(spr, this, sel);
    };

    actionMenu->addChild(mkActBtn("Preset", "GJ_button_04.png", menu_selector(ProfilePicEditorPopup::onPreset)));
    actionMenu->addChild(mkActBtn("Random", "GJ_button_02.png", menu_selector(ProfilePicEditorPopup::onRandomize)));
    actionMenu->addChild(mkActBtn("Reset", "GJ_button_06.png", menu_selector(ProfilePicEditorPopup::onResetAll)));
    actionMenu->updateLayout();

    float fontY = topY - 50.f;
    auto fontLbl = smallLabel("Font", 0.42f);
    fontLbl->setAnchorPoint({0.f, 0.5f});
    fontLbl->setPosition({14.f, fontY});
    root->addChild(fontLbl);

    auto allFonts = ProfilePicCustomizer::getAvailableFonts();
    std::vector<std::pair<std::string, std::string>> fonts;
    for (auto const& f : allFonts) {
        if (f.first.size() > 4 && f.first.substr(f.first.size() - 4) == ".fnt")
            fonts.push_back(f);
    }

    auto fontMenu = CCMenu::create();
    fontMenu->setAnchorPoint({0.5f, 0.5f});
    fontMenu->setPosition({area.width * 0.5f, fontY - 22.f});
    fontMenu->setContentSize({area.width - 20.f, 60.f});
    root->addChild(fontMenu);
    fontMenu->setLayout(
        RowLayout::create()->setGap(3.f)->setGrowCrossAxis(true)
            ->setCrossAxisOverflow(false)->setAutoScale(false)
    );

    for (size_t i = 0; i < fonts.size(); i++) {
        auto const& font = fonts[i];
        bool selected = m_editConfig.profileFont == font.first;

        auto cellBg = paimon::SpriteHelper::createColorPanel(
            44.f, 22.f,
            selected ? ccColor3B{60, 160, 60} : ccColor3B{40, 40, 40},
            selected ? 200 : 120, 3.f
        );

        auto lbl = CCLabelBMFont::create(font.second.c_str(), font.first.c_str());
        if (lbl) {
            lbl->setScale(0.32f);
            lbl->setAnchorPoint({0.5f, 0.5f});
            lbl->setPosition({22.f, 11.f});
            cellBg->addChild(lbl);
        }

        auto btn = CCMenuItemSpriteExtra::create(cellBg, this, menu_selector(ProfilePicEditorPopup::onFontSelect));
        btn->setTag(static_cast<int>(i));
        fontMenu->addChild(btn);
    }
    fontMenu->updateLayout();

    auto tipLbl = smallLabel("Font applies to your username in the main menu", 0.32f, "chatFont.fnt");
    tipLbl->setColor({180, 180, 200});
    tipLbl->setOpacity(200);
    tipLbl->setAnchorPoint({0.5f, 0.5f});
    tipLbl->setPosition({area.width * 0.5f, 12.f});
    root->addChild(tipLbl);

    return root;
}

void ProfilePicEditorPopup::onFontSelect(CCObject* sender) {
    int idx = static_cast<CCMenuItemSpriteExtra*>(sender)->getTag();
    auto allFonts = ProfilePicCustomizer::getAvailableFonts();
    std::vector<std::pair<std::string, std::string>> fonts;
    for (auto const& f : allFonts) {
        if (f.first.size() > 4 && f.first.substr(f.first.size() - 4) == ".fnt")
            fonts.push_back(f);
    }
    if (idx < 0 || idx >= (int)fonts.size()) return;
    m_editConfig.profileFont = fonts[idx].first;
    rebuildCurrentTab();
    rebuildPreview();
}

void ProfilePicEditorPopup::onPreset(CCObject*) {
    auto presets = ProfilePicCustomizer::getPresets();
    if (presets.empty()) return;

    auto winSize = CCDirector::get()->getWinSize();

    auto overlay = CCLayerColor::create({0, 0, 0, 160});
    overlay->setContentSize(winSize);
    overlay->setPosition({0, 0});
    overlay->setID("paimon-preset-overlay"_spr);
    this->addChild(overlay, 9999);

    Ref<CCLayerColor> overlayRef = overlay;
    auto closeOverlay = [overlayRef]() {
        if (overlayRef) overlayRef->removeFromParent();
    };

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
    // keep the picked photo so a full style reset doesn't lose the image
    auto keepSource = m_editConfig.photoSource;
    auto keepPath = m_editConfig.photoPath;
    m_editConfig = ProfilePicConfig();
    m_editConfig.photoSource = keepSource;
    m_editConfig.photoPath = keepPath;
    m_selectedDecoIdx = -1;
    m_decoCategoryIdx = 0;
    m_decoPage = 0;
    rebuildCurrentTab();
    rebuildPreview();
    PaimonNotify::create("Config reset", NotificationIcon::Info)->show();
}


// ============================ Preview ============================

void ProfilePicEditorPopup::triggerImageDownloadIfNeeded() {
    if (m_triggeredDownload) return;
    auto* acc = GJAccountManager::sharedState();
    if (!acc) return;
    int myID = acc->m_accountID;
    if (myID <= 0) return;

    if (ProfileThumbs::get().has(myID)) return;

    m_triggeredDownload = true;

    Ref<CCNode> safeSelf = this;
    ProfileImageService::get().downloadProfileImg(myID, [safeSelf, this, myID](bool success, CCTexture2D* tex) {
        if (success && tex) {
            ProfileThumbs::get().cacheProfile(myID, tex, {255,255,255}, {255,255,255}, 0.5f);
        }
        Loader::get()->queueInMainThread([safeSelf, this]() {
            if (paimon::isRuntimeShuttingDown()) return;
            if (!safeSelf || !safeSelf->getParent()) return;
            rebuildPreview();
            if (m_currentTab == 0) rebuildCurrentTab();
        });
    }, true);
}

void ProfilePicEditorPopup::rebuildPreview() {
    if (!m_previewContainer) return;
    m_previewContainer->removeAllChildrenWithCleanup(true);
    m_previewTexture = nullptr;

    CCSize box = m_previewContainer->getContentSize();
    float targetSize = std::min(box.width, box.height) * 0.72f;

    using Photo = paimon::profile_pic::ResolvedProfilePhoto;

    CCNode* imageNode = nullptr;
    std::string statusText = "Icon mode";

    if (!m_editConfig.onlyIconMode) {
        auto photo = paimon::profile_pic::resolveProfilePhoto(m_editConfig);
        if (photo.kind == Photo::Kind::Texture) {
            m_previewTexture = photo.texture;
        }
        imageNode = paimon::profile_pic::createResolvedPhotoNode(photo);

        switch (photo.source) {
            case Photo::Source::Custom:           statusText = "Custom image"; break;
            case Photo::Source::OwnProfile:       statusText = "Your profile picture"; break;
            case Photo::Source::LegacyBackground: statusText = "Profile BG (fallback)"; break;
            default:                              statusText = photo.ownPhotoMissing ? "Downloading..." : "No image"; break;
        }

        if (!imageNode && photo.ownPhotoMissing) {
            triggerImageDownloadIfNeeded();
        }
    }

    if (m_previewStatusLabel) m_previewStatusLabel->setString(statusText.c_str());

    auto composed = paimon::profile_pic::composeProfilePicture(imageNode, targetSize, m_editConfig);
    if (composed) {
        // shrink to fit the preview box when size/scale would overflow it
        float sizeMul = std::clamp(m_editConfig.size, 60.f, 200.f) / 120.f;
        float ext = targetSize * sizeMul * std::max(
            std::clamp(m_editConfig.scaleX, 0.2f, 3.f),
            std::clamp(m_editConfig.scaleY, 0.2f, 3.f));
        if (m_editConfig.frameEnabled) ext += m_editConfig.frame.thickness * 2.f * sizeMul;
        float maxFit = std::min(box.width, box.height);
        if (ext > maxFit && ext > 0.f) {
            float fit = maxFit / ext;
            composed->setScaleX(composed->getScaleX() * fit);
            composed->setScaleY(composed->getScaleY() * fit);
        }
        composed->setPosition({box.width * 0.5f, box.height * 0.5f});
        m_previewContainer->addChild(composed);
    }

    if (!imageNode && !m_editConfig.onlyIconMode) {
        auto lbl = smallLabel("NO IMAGE", 0.45f, "bigFont.fnt");
        lbl->setColor({200, 200, 200});
        lbl->setOpacity(180);
        lbl->setPosition({box.width * 0.5f, 8.f});
        m_previewContainer->addChild(lbl);
    }
}


void ProfilePicEditorPopup::onSave(CCObject*) {
    ProfilePicCustomizer::get().setConfig(m_editConfig);
    ProfilePicCustomizer::get().save();
    ProfilePicCustomizer::get().setDirty(true);

    PaimonNotify::create("Profile photo saved!", NotificationIcon::Success)->show();
    this->onClose(nullptr);
}
