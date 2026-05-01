#include "PaiConfigLayer.hpp"
#include "../core/UIConstants.hpp"
#include "../features/backgrounds/ui/SameAsPickerPopup.hpp"
#include "../features/backgrounds/ui/VideoSettingsPopup.hpp"
#include "../features/cursor/ui/CursorConfigPopup.hpp"
#include "../features/pet/ui/PetConfigPopup.hpp"
#include "../features/profiles/ui/ProfilePicEditorPopup.hpp"
#include "../features/transitions/ui/TransitionConfigPopup.hpp"
#include "../utils/PaimonNotification.hpp"
#include "../utils/PaimonLoadingOverlay.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include "../utils/InfoButton.hpp"
#include "../utils/FileDialog.hpp"
#include "../utils/ShapeStencil.hpp"
#include "../utils/Localization.hpp"
#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../features/profiles/services/ProfilePicCustomizer.hpp"
#include "../features/profiles/services/ProfilePicRenderer.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/profiles/services/ProfileThumbs.hpp"
#include "../features/profile-music/services/ProfileMusicManager.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../utils/ImageLoadHelper.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../core/QualityConfig.hpp"
#include "../video/VideoPlayer.hpp"
#include "../features/emotes/services/EmoteCache.hpp"
#include "../features/emotes/services/EmoteService.hpp"
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/binding/ButtonSprite.hpp>

// declarada en ProfilePage.cpp
extern void clearProfileImgCache();
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/Slider.hpp>
#include <filesystem>
#include <random>

using namespace geode::prelude;
namespace C = paimon::ui::constants::config;
namespace S = paimon::ui::constants::shared;

// Available shaders for backgrounds
static std::vector<std::pair<std::string, std::string>> BG_SHADERS = {
    {"none",       "pai.config.shader.none"},
    {"grayscale",  "pai.config.shader.grayscale"},
    {"sepia",      "pai.config.shader.sepia"},
    {"vignette",   "pai.config.shader.vignette"},
    {"bloom",      "pai.config.shader.bloom"},
    {"chromatic",  "pai.config.shader.chromatic"},
    {"pixelate",   "pai.config.shader.pixelate"},
    {"posterize",  "pai.config.shader.posterize"},
    {"scanlines",  "pai.config.shader.scanlines"},
};

namespace {
std::string tr(char const* key, char const* fallback = "") {
    auto value = Localization::get().getString(key);
    if (value == key && fallback && fallback[0] != '\0') {
        return fallback;
    }
    return value;
}
}

// ═══════════════════════════════════════════════════════════
// Factory
// ═══════════════════════════════════════════════════════════

PaiConfigLayer* PaiConfigLayer::create() {
    auto ret = new PaiConfigLayer();
    if (ret && ret->init()) { ret->autorelease(); return ret; }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CCScene* PaiConfigLayer::scene() {
    auto scene = CCScene::create();
    scene->addChild(PaiConfigLayer::create());
    return scene;
}

// ═══════════════════════════════════════════════════════════
// Init
// ═══════════════════════════════════════════════════════════

bool PaiConfigLayer::init() {
    if (!CCLayer::init()) return false;
    this->setKeypadEnabled(true);
    this->setTouchEnabled(true);

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    float cx = winSize.width / 2;
    float top = winSize.height;

    // ── Background opaco ──
    // Nota: PaiConfigLayer hereda de CCLayer, no de geode::Popup, por lo que no
    // obtiene theming automatico de Geode 5.6.0. Los popups de perfil/thumbnails que
    // heredan de geode::Popup ya respetan el tema del usuario sin cambios.
    auto bg = CCLayerColor::create(ccc4(25, 25, 45, 255));
    bg->setContentSize(winSize);
    this->addChild(bg, -2);

    // ── Main menu (for all buttons) ──
    m_mainMenu = CCMenu::create();
    m_mainMenu->setID("paimon-config-main-menu"_spr);
    m_mainMenu->setPosition({0, 0});
    this->addChild(m_mainMenu, 10);

    // ── Title ──
    auto title = CCLabelBMFont::create(tr("pai.config.title", "Paimon Settings").c_str(), "goldFont.fnt");
    title->setPosition({cx, top - 20});
    title->setScale(0.65f);
    this->addChild(title);

    // ── Back button ──
    auto backSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    auto backBtn = CCMenuItemSpriteExtra::create(backSpr, this, menu_selector(PaiConfigLayer::onBack));
    backBtn->setPosition({25, top - 20});
    m_mainMenu->addChild(backBtn);

    // ═══════════════════════════════════════════
    // 3 Main Tabs: Backgrounds | Profile | Extras
    // ═══════════════════════════════════════════
    float tabY = top - C::TAB_Y_OFFSET;
    std::vector<std::string> tabNames = {
        tr("pai.config.tab.backgrounds", "Backgrounds"),
        tr("pai.config.tab.profile", "Profile"),
        tr("pai.config.tab.extras", "Extras")
    };
    float tabW = C::TAB_WIDTH;
    float tabStartX = cx - tabW * 1.0f; // 3 tabs centered

    for (int i = 0; i < 3; i++) {
        auto spr = ButtonSprite::create(tabNames[i].c_str(), "bigFont.fnt", "GJ_button_04.png", .8f);
        spr->setScale(0.48f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(PaiConfigLayer::onMainTabSwitch));
        btn->setTag(i);
        btn->setPosition({tabStartX + i * tabW, tabY});
        m_mainMenu->addChild(btn);
        m_mainTabBtns.push_back(btn);
    }

    // ── Separator line ──
    auto sep = CCLayerColor::create({255, 255, 255, 40});
    sep->setContentSize({winSize.width - 30, 1});
    sep->setPosition({15, tabY - C::TAB_SEP_BELOW});
    this->addChild(sep, 5);

    // ═══════════════════════════════════════════
    // Tab content layers
    // ═══════════════════════════════════════════
    m_bgTab = CCLayer::create();
    m_bgTab->setID("bg-tab"_spr);
    this->addChild(m_bgTab, 5);
    m_bgMenu = CCMenu::create();
    m_bgMenu->setID("bg-menu"_spr);
    m_bgMenu->setPosition({0, 0});
    this->addChild(m_bgMenu, 11);

    m_profileTab = CCLayer::create();
    m_profileTab->setID("profile-tab"_spr);
    m_profileTab->setVisible(false);
    this->addChild(m_profileTab, 5);
    m_profileMenu = CCMenu::create();
    m_profileMenu->setID("profile-menu"_spr);
    m_profileMenu->setPosition({0, 0});
    m_profileMenu->setVisible(false);
    this->addChild(m_profileMenu, 11);

    m_extrasTab = CCLayer::create();
    m_extrasTab->setID("extras-tab"_spr);
    m_extrasTab->setVisible(false);
    this->addChild(m_extrasTab, 5);
    m_extrasMenu = CCMenu::create();
    m_extrasMenu->setID("extras-menu"_spr);
    m_extrasMenu->setPosition({0, 0});
    m_extrasMenu->setVisible(false);
    this->addChild(m_extrasMenu, 11);

    buildBackgroundTab();
    buildProfileTab();
    buildExtrasTab();

    // ═══════════════════════════════════════════
    // Apply button (always visible)
    // ═══════════════════════════════════════════
    auto applySpr = ButtonSprite::create(tr("pai.config.apply", "Apply & Restart Menu").c_str(), "goldFont.fnt", "GJ_button_01.png", .8f);
    applySpr->setScale(0.5f);
    auto applyBtn = CCMenuItemSpriteExtra::create(applySpr, this, menu_selector(PaiConfigLayer::onApply));
    applyBtn->setPosition({cx, C::APPLY_BTN_Y});
    m_mainMenu->addChild(applyBtn);

    // Init
    switchMainTab(0);
    updateLayerButtons();
    refreshForCurrentLayer();

    return true;
}

// ═══════════════════════════════════════════════════════════
// Build Background Tab
// ═══════════════════════════════════════════════════════════

void PaiConfigLayer::buildBackgroundTab() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    float cx = winSize.width / 2;
    float contentTop = winSize.height - C::CONTENT_TOP_OFFSET;
    float contentBot = C::CONTENT_BOT;
    float contentH = contentTop - contentBot;

    // ══════════════════════════════════════════
    // LEFT SIDEBAR — Layer buttons vertical
    // ══════════════════════════════════════════
    float sidebarW = C::SIDEBAR_WIDTH;
    float sidebarX = sidebarW / 2 + C::SIDEBAR_PAD;

    // Sidebar dark panel
    auto sidePanel = paimon::SpriteHelper::createDarkPanel(sidebarW, contentH, 70);
    sidePanel->setPosition({sidebarX - sidebarW / 2, contentBot});
    m_bgTab->addChild(sidePanel, 0);

    auto& layers = LayerBackgroundManager::LAYER_OPTIONS;
    int layerCount = (int)layers.size();

    // Sidebar menu with ColumnLayout for even vertical distribution
    auto sidebarMenu = CCMenu::create();
    sidebarMenu->setPosition({sidebarX, contentBot + contentH / 2});
    sidebarMenu->setContentSize({sidebarW, contentH});
    sidebarMenu->setAnchorPoint({0.5f, 0.5f});
    sidebarMenu->setLayout(
        ColumnLayout::create()
            ->setGap(0.f)
            ->setAxisReverse(true)
            ->setAxisAlignment(AxisAlignment::Even)
            ->setCrossAxisLineAlignment(AxisAlignment::Center)
    );

    for (int i = 0; i < layerCount; i++) {
        auto spr = ButtonSprite::create(layers[i].second.c_str(), "bigFont.fnt", "GJ_button_04.png", .65f);
        spr->setScale(S::BTN_SCALE_TINY);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(PaiConfigLayer::onLayerSelect));
        btn->setTag(i);
        sidebarMenu->addChild(btn);
        m_layerBtns.push_back(btn);
    }
    sidebarMenu->updateLayout();
    m_bgSidebarMenu = sidebarMenu;
    this->addChild(sidebarMenu, 11);

    // Vertical separator line
    auto sepLine = CCLayerColor::create({255, 255, 255, 50});
    sepLine->setContentSize({1, contentH});
    sepLine->setPosition({sidebarW + C::SIDEBAR_SEP_X, contentBot});
    m_bgTab->addChild(sepLine, 5);

    // ══════════════════════════════════════════
    // RIGHT SIDE — Controls + Preview
    // ══════════════════════════════════════════
    float rightX = sidebarW + C::SIDEBAR_SEP_X + 8; // left edge of right area
    float rightW = winSize.width - rightX - 8;
    float rightCx = rightX + rightW / 2;

    // ── Controls panel (top-right) ──
    float ctrlPanelH = C::CTRL_PANEL_H;
    float ctrlPanelY = contentTop - ctrlPanelH / 2 - C::CTRL_PANEL_TOP_PAD;

    auto ctrlPanel = paimon::SpriteHelper::createDarkPanel(rightW, ctrlPanelH, 55);
    ctrlPanel->setPosition({rightCx - rightW / 2, ctrlPanelY - ctrlPanelH / 2});
    m_bgTab->addChild(ctrlPanel, 0);

    // Title + info
    float titleY = ctrlPanelY + ctrlPanelH / 2 - 10;
    auto bgTitle = CCLabelBMFont::create(tr("pai.config.background.title", "Background").c_str(), "goldFont.fnt");
    bgTitle->setScale(0.35f);
    bgTitle->setPosition({rightCx - 25, titleY});
    m_bgTab->addChild(bgTitle, 1);

    {
        auto iBtn = PaimonInfo::createInfoBtn(
            tr("pai.config.background.info.title", "Background").c_str(),
            tr("pai.config.background.info.body",
                "<cy>Custom Image</c>: Local PNG/JPG/GIF.\n"
                "<cy>Video</c>: Local MP4 video (looped).\n"
                "<cy>Random</c>: Cached thumbnail.\n"
                "<cy>Same as/Set ID/Default</c>: Other sources.\n"
                "<cy>Dark</c>: Overlay + <cy>Adaptive</c> (Menu)."
            ).c_str(),
            this, 0.49f
        );
        if (iBtn) {
            iBtn->setPosition({rightCx + 12, titleY});
            m_bgMenu->addChild(iBtn);
        }
    }

    // Row 1: action buttons — RowLayout for even horizontal distribution
    float row1 = titleY - C::CTRL_ROW_SPACING;
    auto row1Menu = CCMenu::create();
    row1Menu->setPosition({rightCx, row1});
    row1Menu->setContentSize({rightW - 10, 20});
    row1Menu->setAnchorPoint({0.5f, 0.5f});
    row1Menu->setLayout(
        RowLayout::create()
            ->setGap(0.f)
            ->setAxisAlignment(AxisAlignment::Even)
    );

    auto mkRowBtn = [&](char const* text, SEL_MenuHandler handler, float sc = S::BTN_SCALE_SMALL) {
        auto spr = ButtonSprite::create(text);
        spr->setScale(sc);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, handler);
        row1Menu->addChild(btn);
        return btn;
    };
    mkRowBtn(tr("pai.config.background.custom_image", "Custom Image").c_str(), menu_selector(PaiConfigLayer::onBgCustomImage));
    mkRowBtn(tr("pai.config.background.video", "Video").c_str(), menu_selector(PaiConfigLayer::onBgVideo));
    mkRowBtn("Vid. Settings", menu_selector(PaiConfigLayer::onVideoSettings), 0.30f);
    mkRowBtn(tr("pai.config.background.random", "Random").c_str(), menu_selector(PaiConfigLayer::onBgRandom));
    mkRowBtn(tr("pai.config.background.same_as", "Same as...").c_str(), menu_selector(PaiConfigLayer::onBgSameAs));
    mkRowBtn(tr("pai.config.background.default", "Default").c_str(), menu_selector(PaiConfigLayer::onBgDefault), 0.33f);
    row1Menu->updateLayout();
    m_bgRow1Menu = row1Menu;
    this->addChild(row1Menu, 11);

    // Row 2: ID + Dark + Intensity
    float row2 = row1 - C::CTRL_ROW_SPACING;

    auto inputBg = paimon::SpriteHelper::createDarkPanel(C::INPUT_BG_WIDTH, C::INPUT_BG_HEIGHT, 80);
    inputBg->setPosition({rightX + 42 - 32.5f, row2 - 10.f});
    m_bgTab->addChild(inputBg, 1);

    m_bgIdInput = TextInput::create(58, tr("pai.config.background.level_id", "Level ID").c_str());
    m_bgIdInput->setPosition({rightX + 42, row2});
    m_bgIdInput->setCommonFilter(geode::CommonFilter::Uint);
    m_bgIdInput->setMaxCharCount(10);
    m_bgIdInput->setScale(0.55f);
    m_bgTab->addChild(m_bgIdInput, 2);

    makeBtn(tr("pai.config.background.set", "Set").c_str(), {rightX + 95, row2}, menu_selector(PaiConfigLayer::onBgSetID), m_bgMenu, 0.34f);

    m_darkToggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(PaiConfigLayer::onDarkMode), 0.38f);
    m_darkToggle->setPosition({rightX + 140, row2});
    m_bgMenu->addChild(m_darkToggle);

    auto darkLbl = CCLabelBMFont::create(tr("pai.config.background.dark", "Dark").c_str(), "bigFont.fnt");
    darkLbl->setScale(0.18f);
    darkLbl->setPosition({rightX + 163, row2});
    m_bgTab->addChild(darkLbl, 1);

    m_darkSlider = Slider::create(this, menu_selector(PaiConfigLayer::onDarkIntensity), 0.3f);
    m_darkSlider->setPosition({rightX + 250, row2});
    m_bgTab->addChild(m_darkSlider, 1);

    auto intLbl = CCLabelBMFont::create(tr("pai.config.background.intensity", "Intensity").c_str(), "bigFont.fnt");
    intLbl->setScale(0.16f);
    intLbl->setPosition({rightX + 250, row2 + 10});
    m_bgTab->addChild(intLbl, 1);

    // Adaptive toggle (below controls, only for menu)
    float row3 = row2 - C::CTRL_ROW3_OFFSET;
    m_adaptiveToggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(PaiConfigLayer::onAdaptiveColors), 0.35f);
    m_adaptiveToggle->setPosition({rightX + 15, row3});
    m_bgMenu->addChild(m_adaptiveToggle);

    m_adaptiveLabel = CCLabelBMFont::create(tr("pai.config.background.adaptive_colors", "Adaptive Colors").c_str(), "bigFont.fnt");
    m_adaptiveLabel->setScale(0.2f);
    m_adaptiveLabel->setPosition({rightX + 75, row3});
    m_bgTab->addChild(m_adaptiveLabel, 1);

    // Shader selector (right side of row3)
    auto shaderTitle = CCLabelBMFont::create(tr("pai.config.background.shader", "Shader:").c_str(), "bigFont.fnt");
    shaderTitle->setScale(0.18f);
    shaderTitle->setPosition({rightX + 175, row3});
    m_bgTab->addChild(shaderTitle, 1);

    auto prevArrow = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
    prevArrow->setFlipX(true);
    prevArrow->setScale(0.3f);
    auto prevBtn = CCMenuItemSpriteExtra::create(prevArrow, this, menu_selector(PaiConfigLayer::onShaderPrev));
    prevBtn->setPosition({rightX + 210, row3});
    m_bgMenu->addChild(prevBtn);

    m_shaderLabel = CCLabelBMFont::create(tr("pai.config.shader.none", "None").c_str(), "bigFont.fnt");
    m_shaderLabel->setScale(0.2f);
    m_shaderLabel->setColor({100, 255, 100});
    m_shaderLabel->setPosition({rightX + 270, row3});
    m_bgTab->addChild(m_shaderLabel, 1);

    auto nextArrow = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
    nextArrow->setScale(0.3f);
    auto nextBtn = CCMenuItemSpriteExtra::create(nextArrow, this, menu_selector(PaiConfigLayer::onShaderNext));
    nextBtn->setPosition({rightX + 330, row3});
    m_bgMenu->addChild(nextBtn);

    // ── Preview area (bottom-right) ──
    float previewH = contentH - ctrlPanelH - C::PREVIEW_GAP;
    float previewY = contentBot + previewH / 2 + 2;

    auto previewPanel = paimon::SpriteHelper::createDarkPanel(rightW, previewH, 55);
    previewPanel->setPosition({rightCx - rightW / 2, previewY - previewH / 2});
    m_bgTab->addChild(previewPanel, 0);

    auto prevTitle = CCLabelBMFont::create(tr("pai.config.preview", "Preview").c_str(), "goldFont.fnt");
    prevTitle->setScale(0.28f);
    prevTitle->setPosition({rightCx, previewY + previewH / 2 - 8});
    m_bgTab->addChild(prevTitle, 1);

    // Status label (shows current type)
    m_bgStatusLabel = CCLabelBMFont::create(tr("pai.config.status.default", "Default").c_str(), "bigFont.fnt");
    m_bgStatusLabel->setScale(0.18f);
    m_bgStatusLabel->setColor({180, 180, 180});
    m_bgStatusLabel->setPosition({rightCx, previewY - previewH / 2 + 8});
    m_bgTab->addChild(m_bgStatusLabel, 1);

    // Preview container
    float prevContW = rightW - 16;
    float prevContH = previewH - 24;
    m_bgPreview = CCNode::create();
    m_bgPreview->setPosition({rightCx - prevContW / 2, previewY - prevContH / 2 - 2});
    m_bgPreview->setContentSize({prevContW, prevContH});
    m_bgPreview->setAnchorPoint({0, 0});
    m_bgTab->addChild(m_bgPreview, 2);

    // ── Blocked overlay (covers entire right side) ──
    m_blockedOverlay = CCLayerColor::create({0, 0, 0, 180});
    m_blockedOverlay->setContentSize({rightW + 4, contentH});
    m_blockedOverlay->setPosition({rightX - 2, contentBot});
    m_blockedOverlay->setVisible(false);
    m_bgTab->addChild(m_blockedOverlay, 50);

    m_blockedLabel = CCLabelBMFont::create(
        tr("pai.config.background.blocked_message",
           "Level Info uses its own\nthumbnail background.\n\nChange in Mod Settings\n> Background Style."
        ).c_str(),
        "bigFont.fnt");
    m_blockedLabel->setScale(0.25f);
    m_blockedLabel->setAlignment(kCCTextAlignmentCenter);
    m_blockedLabel->setPosition({rightW / 2 + 2, contentH / 2});
    m_blockedOverlay->addChild(m_blockedLabel);
}

// ═══════════════════════════════════════════════════════════
// Build Profile Tab
// ═══════════════════════════════════════════════════════════

void PaiConfigLayer::buildProfileTab() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    float cx = winSize.width / 2;
    float contentMid = (winSize.height - C::CONTENT_TOP_OFFSET + C::CONTENT_BOT) / 2;

    // ── Dark panel ──
    float panelW = C::PROFILE_PANEL_W;
    float panelH = C::PROFILE_PANEL_H;
    auto panel = paimon::SpriteHelper::createDarkPanel(panelW, panelH, 60);
    panel->setPosition({cx - panelW / 2, contentMid - panelH / 2});
    m_profileTab->addChild(panel, 0);

    // Title
    auto profTitle = CCLabelBMFont::create(tr("pai.config.profile.title", "Profile Picture").c_str(), "goldFont.fnt");
    profTitle->setScale(0.4f);
    profTitle->setPosition({cx - 60, contentMid + panelH / 2 - 12});
    m_profileTab->addChild(profTitle, 1);

    {
        auto iBtn = PaimonInfo::createInfoBtn(
            tr("pai.config.profile.info.title", "Profile").c_str(),
            tr("pai.config.profile.info.body",
               "<cy>Set Image</c>: Pick a local image.\n"
               "<cy>Clear</c>: Remove custom image.\n"
               "<cy>Photo Shape</c>: Edit shape, border, effects.\n"
               "Preview updates in real-time."
            ).c_str(),
            this, 0.49f
        );
        if (iBtn) {
            iBtn->setPosition({cx - 14, contentMid + panelH / 2 - 12});
            m_profileMenu->addChild(iBtn);
        }
    }

    // ── Left column: buttons with ColumnLayout ──
    float leftX = cx - 100;

    auto btnColumn = CCMenu::create();
    btnColumn->setPosition({leftX, contentMid});
    btnColumn->setContentSize({120, panelH - 30});
    btnColumn->setAnchorPoint({0.5f, 0.5f});
    btnColumn->setLayout(
        ColumnLayout::create()
            ->setGap(4.f)
            ->setAxisReverse(true)
            ->setAxisAlignment(AxisAlignment::Center)
    );

    auto imgSpr = ButtonSprite::create(tr("pai.config.profile.set_image", "Set Image").c_str(), "goldFont.fnt", "GJ_button_02.png", .8f);
    imgSpr->setScale(S::BTN_SCALE_MEDIUM);
    auto imgBtn = CCMenuItemSpriteExtra::create(imgSpr, this, menu_selector(PaiConfigLayer::onProfileImage));
    btnColumn->addChild(imgBtn);

    auto clearSpr = ButtonSprite::create(tr("pai.config.profile.clear_image", "Clear Image").c_str(), "goldFont.fnt", "GJ_button_06.png", .8f);
    clearSpr->setScale(S::BTN_SCALE_MEDIUM);
    auto clearBtn = CCMenuItemSpriteExtra::create(clearSpr, this, menu_selector(PaiConfigLayer::onProfileImageClear));
    btnColumn->addChild(clearBtn);

    auto shapeSpr = ButtonSprite::create(tr("pai.config.profile.photo_shape", "Photo Shape").c_str(), "goldFont.fnt", "GJ_button_03.png", .8f);
    shapeSpr->setScale(S::BTN_SCALE_MEDIUM);
    auto shapeBtn = CCMenuItemSpriteExtra::create(shapeSpr, this, menu_selector(PaiConfigLayer::onProfilePhoto));
    btnColumn->addChild(shapeBtn);

    btnColumn->updateLayout();
    m_profileBtnColumn = btnColumn;
    this->addChild(btnColumn, 11);

    // ── Right column: live preview ──
    float previewX = cx + 100;
    float previewY = contentMid;

    auto previewLabel = CCLabelBMFont::create(tr("pai.config.preview", "Preview").c_str(), "goldFont.fnt");
    previewLabel->setScale(0.3f);
    previewLabel->setPosition({previewX, previewY + 48});
    m_profileTab->addChild(previewLabel, 1);

    // Preview container — rebuilt dynamically
    m_profilePreview = CCNode::create();
    m_profilePreview->setPosition({previewX - 40, previewY - 40});
    m_profilePreview->setContentSize({80, 80});
    m_profilePreview->setAnchorPoint({0, 0});
    m_profileTab->addChild(m_profilePreview, 5);

    // Dark circle background for preview
    auto previewBg = paimon::SpriteHelper::createDarkPanel(90, 90, 80);
    previewBg->setPosition({previewX - 45, previewY - 45});
    m_profileTab->addChild(previewBg, -1);

    rebuildProfilePreview();
}

// ═══════════════════════════════════════════════════════════
// Build Extras Tab
// ═══════════════════════════════════════════════════════════

void PaiConfigLayer::buildExtrasTab() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    float cx = winSize.width / 2;
    float contentMid = (winSize.height - C::CONTENT_TOP_OFFSET + C::CONTENT_BOT) / 2;
    float gap = C::EXTRAS_BTN_GAP;
    float petY = contentMid + gap;
    float cursorY = contentMid;
    float transitionsY = contentMid - gap;
    float clearY = contentMid - gap * 2.f;

    // ── Dark panel ──
    float panelW = C::EXTRAS_PANEL_W;
    float panelH = C::EXTRAS_PANEL_H;
    auto panel = paimon::SpriteHelper::createDarkPanel(panelW, panelH, 60);
    panel->setPosition({cx - panelW / 2, contentMid - panelH / 2});
    m_extrasTab->addChild(panel, 0);

    auto extTitle = CCLabelBMFont::create(tr("pai.config.extras.title", "Extras").c_str(), "goldFont.fnt");
    extTitle->setScale(0.4f);
    extTitle->setPosition({cx, contentMid + panelH / 2 - 12});
    m_extrasTab->addChild(extTitle, 1);

    // ── Pet Config button ──
    auto petSpr = ButtonSprite::create(tr("pai.config.extras.pet_config", "Pet Config").c_str(), "goldFont.fnt", "GJ_button_03.png", .8f);
    petSpr->setScale(S::BTN_SCALE_LARGE);
    auto petBtn = CCMenuItemSpriteExtra::create(petSpr, this, menu_selector(PaiConfigLayer::onPetConfig));
    petBtn->setPosition({cx, petY});
    m_extrasMenu->addChild(petBtn);

    // BETA badge
    auto betaLabel = CCLabelBMFont::create(tr("pai.config.extras.beta", "BETA").c_str(), "bigFont.fnt");
    betaLabel->setScale(0.25f);
    betaLabel->setColor({255, 80, 80});
    betaLabel->setPosition({cx + 58, petY + 16.f});
    betaLabel->setRotation(-15.f);
    m_extrasTab->addChild(betaLabel, 10);

    {
        auto iBtn = PaimonInfo::createInfoBtn(
            tr("pai.config.extras.pet_info.title", "Pet").c_str(),
            tr("pai.config.extras.pet_info.body",
               "A cute pet follows your cursor.\nThis feature is in <cr>BETA</c> — expect bugs!"
            ).c_str(),
            this, 0.49f
        );
        if (iBtn) {
            iBtn->setPosition({cx + 48, petY});
            m_extrasMenu->addChild(iBtn);
        }
    }

    // ── Custom Cursor button ──
    auto cursorSpr = ButtonSprite::create(tr("pai.config.extras.custom_cursor", "Custom Cursor").c_str(), "goldFont.fnt", "GJ_button_02.png", .8f);
    cursorSpr->setScale(S::BTN_SCALE_LARGE);
    auto cursorBtn = CCMenuItemSpriteExtra::create(cursorSpr, this, menu_selector(PaiConfigLayer::onCustomCursor));
    cursorBtn->setID("custom-cursor-config-btn"_spr);
    cursorBtn->setPosition({cx, cursorY});
    m_extrasMenu->addChild(cursorBtn);

    {
        auto iBtn = PaimonInfo::createInfoBtn(
            tr("pai.config.extras.custom_cursor_info.title", "Custom Cursor").c_str(),
            tr("pai.config.extras.custom_cursor_info.body",
               "Open the full custom cursor editor.\n"
               "From there you can add images, assign idle/move slots,\n"
               "and tweak scale, opacity, trail, and active layers."
            ).c_str(),
            this, 0.49f
        );
        if (iBtn) {
            iBtn->setPosition({cx + 64, cursorY});
            m_extrasMenu->addChild(iBtn);
        }
    }

    // ── Transitions button ──
    auto transSpr = ButtonSprite::create(tr("pai.config.extras.transitions", "Transitions").c_str(), "goldFont.fnt", "GJ_button_04.png", .8f);
    transSpr->setScale(S::BTN_SCALE_LARGE);
    auto transBtn = CCMenuItemSpriteExtra::create(transSpr, this, menu_selector(PaiConfigLayer::onTransitions));
    transBtn->setID("transitions-config-btn"_spr);
    transBtn->setPosition({cx, transitionsY});
    m_extrasMenu->addChild(transBtn);

    {
        auto iBtn = PaimonInfo::createInfoBtn(
            tr("pai.config.extras.transitions_info.title", "Transitions").c_str(),
            tr("pai.config.extras.transitions_info.body",
               "Configure custom scene transition effects.\n"
               "Choose from 55+ built-in transitions or create your own\n"
               "with a custom command sequence (DSL)."
            ).c_str(),
            this, 0.49f
        );
        if (iBtn) {
            iBtn->setPosition({cx + 55, transitionsY});
            m_extrasMenu->addChild(iBtn);
        }
    }

    // ── Clear All Cache button ──
    auto clearSpr = ButtonSprite::create(tr("pai.config.extras.clear_cache", "Clear All Cache").c_str(), "goldFont.fnt", "GJ_button_06.png", .8f);
    clearSpr->setScale(S::BTN_SCALE_LARGE);
    auto clearBtn = CCMenuItemSpriteExtra::create(clearSpr, this, menu_selector(PaiConfigLayer::onClearAllCache));
    clearBtn->setPosition({cx, clearY});
    m_extrasMenu->addChild(clearBtn);

    {
        auto iBtn = PaimonInfo::createInfoBtn(
            tr("pai.config.extras.clear_cache_info.title", "Clear Cache").c_str(),
            tr("pai.config.extras.clear_cache_info.body",
               "<cr>Deletes ALL cached data:</c>\n"
               "- Downloaded thumbnails (RAM + disk)\n"
               "- Profile thumbnails & images\n"
               "- Profile music cache\n"
               "- GIF cache (RAM + disk)\n"
               "- Profile background settings\n\n"
               "This frees up space and fixes stale data.\n"
               "Everything will re-download as needed."
            ).c_str(),
            this, 0.49f
        );
        if (iBtn) {
            iBtn->setPosition({cx + 68, clearY});
            m_extrasMenu->addChild(iBtn);
        }
    }

    // Coming soon label
    auto comingSoon = CCLabelBMFont::create(tr("pai.config.extras.coming_soon", "More features coming soon...").c_str(), "bigFont.fnt");
    comingSoon->setScale(0.2f);
    comingSoon->setColor({150, 150, 150});
    comingSoon->setPosition({cx, contentMid - panelH / 2 + 12});
    m_extrasTab->addChild(comingSoon, 1);
}

// ═══════════════════════════════════════════════════════════
// Tab switching
// ═══════════════════════════════════════════════════════════

void PaiConfigLayer::onMainTabSwitch(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    switchMainTab(idx);
}

void PaiConfigLayer::switchMainTab(int idx) {
    m_currentMainTab = idx;

    m_bgTab->setVisible(idx == 0);
    m_bgMenu->setVisible(idx == 0);
    if (m_bgSidebarMenu) m_bgSidebarMenu->setVisible(idx == 0);
    if (m_bgRow1Menu) m_bgRow1Menu->setVisible(idx == 0);
    m_profileTab->setVisible(idx == 1);
    m_profileMenu->setVisible(idx == 1);
    if (m_profileBtnColumn) m_profileBtnColumn->setVisible(idx == 1);
    m_extrasTab->setVisible(idx == 2);
    m_extrasMenu->setVisible(idx == 2);

    // Update main tab button colors
    for (int i = 0; i < (int)m_mainTabBtns.size(); i++) {
        auto spr = typeinfo_cast<ButtonSprite*>(m_mainTabBtns[i]->getNormalImage());
        if (!spr) continue;
        spr->setColor(i == idx ? ccColor3B{100, 255, 100} : ccColor3B{255, 255, 255});
    }

    // Refresh profile preview when switching to profile tab
    if (idx == 1) rebuildProfilePreview();
}

// ═══════════════════════════════════════════════════════════
// Profile preview
// ═══════════════════════════════════════════════════════════

void PaiConfigLayer::rebuildProfilePreview() {
    if (!m_profilePreview) return;
    m_profilePreview->removeAllChildren();

    const float thumbSize = C::PROFILE_THUMB_SIZE;
    auto pSize = m_profilePreview->getContentSize();
    float midX = pSize.width / 2;
    float midY = pSize.height / 2;

    std::string type = Mod::get()->getSavedValue<std::string>("profile-bg-type", "none");
    std::string path = Mod::get()->getSavedValue<std::string>("profile-bg-path", "");

    auto picCfg = ProfilePicCustomizer::get().getConfig();
    std::string shapeName = picCfg.stencilSprite;
    if (shapeName.empty()) shapeName = "circle";

    std::error_code ecExists;
    bool hasImage = (type == "custom" && !path.empty() && std::filesystem::exists(path, ecExists));

    // Only Icon Mode: render icon even without custom image
    if (!hasImage && picCfg.onlyIconMode) {
        auto container = paimon::profile_pic::composeProfilePicture(nullptr, thumbSize, picCfg);
        if (container) {
            container->setPosition({midX, midY});
            m_profilePreview->addChild(container);
        }
        return;
    }

    if (!hasImage) {
        // No image — show placeholder
        auto placeholder = CCLayerColor::create({60, 60, 60, 200});
        placeholder->setContentSize({thumbSize, thumbSize});
        placeholder->setPosition({midX - thumbSize / 2, midY - thumbSize / 2});
        m_profilePreview->addChild(placeholder);

        auto noImg = CCLabelBMFont::create(tr("pai.config.profile.no_image", "No\nImage").c_str(), "bigFont.fnt");
        noImg->setScale(0.2f);
        noImg->setColor({180, 180, 180});
        noImg->setAlignment(kCCTextAlignmentCenter);
        noImg->setPosition({midX, midY});
        m_profilePreview->addChild(noImg, 1);
        return;
    }

    // Load image — soportar GIF/APNG animado ademas de imagenes estaticas
    // deteccion por contenido (magic bytes) en vez de extension
    bool isAnimated = false;
    {
        std::ifstream probe(path, std::ios::binary);
        if (probe) {
            char hdr[32]{};
            probe.read(hdr, sizeof(hdr));
            auto fmt = imgp::guessFormat(hdr, static_cast<size_t>(probe.gcount()));
            isAnimated = (fmt == imgp::ImageFormat::Gif);
            // APNG tambien es animado
            if (!isAnimated && fmt == imgp::ImageFormat::Png) {
                auto fileData = ImageLoadHelper::readBinaryFile(path, 10);
                if (!fileData.empty()) isAnimated = imgp::formats::isAPng(fileData.data(), fileData.size());
            }
        }
    }
    CCNode* imageNode = nullptr;

    if (isAnimated) {
        auto gifSprite = AnimatedGIFSprite::create(path);
        if (gifSprite) {
            imageNode = gifSprite;
        }
    }

    if (!imageNode) {
        auto sprite = CCSprite::create(path.c_str());
        if (sprite) {
            imageNode = sprite;
        }
    }

    if (!imageNode) {
        auto errLbl = CCLabelBMFont::create(tr("general.error", "Error").c_str(), "bigFont.fnt");
        errLbl->setScale(0.2f);
        errLbl->setColor({255, 80, 80});
        errLbl->setPosition({midX, midY});
        m_profilePreview->addChild(errLbl);
        return;
    }

    // Delegar en el renderer compartido (aplica scaleX/Y, rotacion, decos, zoom, borde)
    auto container = paimon::profile_pic::composeProfilePicture(imageNode, thumbSize, picCfg);
    if (!container) return;
    container->setPosition({midX, midY});
    m_profilePreview->addChild(container);
}

// ═══════════════════════════════════════════════════════════
// Navigation
// ═══════════════════════════════════════════════════════════

void PaiConfigLayer::keyBackClicked() { onBack(nullptr); }

void PaiConfigLayer::onBack(CCObject*) {
    CCDirector::sharedDirector()->popSceneWithTransition(0.3f, PopTransition::kPopTransitionFade);
}

// ═══════════════════════════════════════════════════════════
// Layer selector
// ═══════════════════════════════════════════════════════════

void PaiConfigLayer::onLayerSelect(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    auto& layers = LayerBackgroundManager::LAYER_OPTIONS;
    if (idx >= 0 && idx < (int)layers.size()) {
        m_selectedKey = layers[idx].first;
        updateLayerButtons();
        refreshForCurrentLayer();
    }
}

void PaiConfigLayer::updateLayerButtons() {
    auto& layers = LayerBackgroundManager::LAYER_OPTIONS;
    for (int i = 0; i < (int)m_layerBtns.size(); i++) {
        auto spr = typeinfo_cast<ButtonSprite*>(m_layerBtns[i]->getNormalImage());
        if (!spr) continue;
        spr->setColor(layers[i].first == m_selectedKey ? ccColor3B{0, 255, 0} : ccColor3B{255, 255, 255});
    }
}

void PaiConfigLayer::refreshForCurrentLayer() {
    auto& mgr = LayerBackgroundManager::get();

    bool isLevelInfo = (m_selectedKey == "levelinfo");
    bool levelInfoBlocked = false;
    if (isLevelInfo) {
        std::string bgStyle = Mod::get()->getSettingValue<std::string>("levelinfo-background-style");
        levelInfoBlocked = (bgStyle != "normal");
    }
    if (m_blockedOverlay) m_blockedOverlay->setVisible(levelInfoBlocked);

    auto bgCfg = mgr.getConfig(m_selectedKey);
    if (m_darkToggle) m_darkToggle->toggle(bgCfg.darkMode);
    if (m_darkSlider) m_darkSlider->setValue(bgCfg.darkIntensity);

    bool isMenu = (m_selectedKey == "menu");
    if (m_adaptiveToggle) {
        m_adaptiveToggle->setVisible(isMenu);
        if (isMenu) {
            bool adaptive = Mod::get()->getSavedValue<bool>("bg-adaptive-colors", false);
            m_adaptiveToggle->toggle(adaptive);
        }
    }
    if (m_adaptiveLabel) m_adaptiveLabel->setVisible(isMenu);

    // Update shader selector
    m_shaderIndex = 0;
    for (int i = 0; i < (int)BG_SHADERS.size(); i++) {
        if (BG_SHADERS[i].first == bgCfg.shader) { m_shaderIndex = i; break; }
    }
    updateShaderLabel();

    // Update status label
    if (m_bgStatusLabel) {
        std::string status = tr("pai.config.status.default", "Default");
        if (bgCfg.type == "custom") status = tr("pai.config.status.custom_image", "Custom Image");
        else if (bgCfg.type == "video") status = tr("pai.config.status.video", "Video");
        else if (bgCfg.type == "random") status = tr("pai.config.status.random", "Random");
        else if (bgCfg.type == "id") status = tr("pai.config.status.level_id", "Level ID: ") + std::to_string(bgCfg.levelId);
        else if (bgCfg.type == "menu") status = tr("pai.config.status.same_as_menu", "Same as Menu");
        if (bgCfg.shader != "none" && !bgCfg.shader.empty()) {
            // Find display name
            for (auto& [k, v] : BG_SHADERS) {
                if (k == bgCfg.shader) { status += " + " + tr(v.c_str(), v.c_str()); break; }
            }
        }
        m_bgStatusLabel->setString(status.c_str());
    }

    // Update preview
    rebuildBgPreview();
}

// ═══════════════════════════════════════════════════════════
// Background preview
// ═══════════════════════════════════════════════════════════

void PaiConfigLayer::rebuildBgPreview() {
    if (!m_bgPreview) return;
    m_bgPreview->removeAllChildren();

    auto pSize = m_bgPreview->getContentSize();
    float pw = pSize.width;
    float ph = pSize.height;
    float midX = pw / 2;
    float midY = ph / 2;

    auto& mgr = LayerBackgroundManager::get();
    auto cfg = mgr.getConfig(m_selectedKey);

    // ─── Helper: show a texture clipped to preview area ───
    auto addTextureToPreview = [&](CCTexture2D* tex) {
        if (!tex || !m_bgPreview) return;
        auto spr = CCSprite::createWithTexture(tex);
        if (!spr) return;

        float scX = pw / spr->getContentWidth();
        float scY = ph / spr->getContentHeight();
        spr->setScale(std::max(scX, scY));
        spr->setPosition({midX, midY});
        spr->setAnchorPoint({0.5f, 0.5f});

        auto stencil = paimon::SpriteHelper::createRectStencil(pw, ph);
        auto clipper = CCClippingNode::create();
        clipper->setStencil(stencil);
        clipper->setAlphaThreshold(0.5f);
        clipper->setContentSize({pw, ph});
        clipper->addChild(spr);
        m_bgPreview->addChild(clipper, 1);
    };

    // ─── Helper: add dark overlay ───
    auto addDarkOverlay = [&]() {
        if (!cfg.darkMode) return;
        auto darkOv = CCLayerColor::create({0, 0, 0, (GLubyte)(cfg.darkIntensity * 200)});
        darkOv->setContentSize({pw, ph});
        m_bgPreview->addChild(darkOv, 5);
    };

    // ─── Helper: show placeholder ───
    auto showPlaceholder = [&](char const* text, ccColor3B color = {150, 150, 150}) {
        auto bg = CCLayerColor::create({40, 40, 60, 200});
        bg->setContentSize({pw, ph});
        m_bgPreview->addChild(bg, 0);
        auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(0.2f);
        lbl->setColor(color);
        lbl->setAlignment(kCCTextAlignmentCenter);
        lbl->setPosition({midX, midY});
        m_bgPreview->addChild(lbl, 1);
        addDarkOverlay();
    };

    // ─── Helper: show a loading spinner ───
    auto showLoading = [&]() {
        auto overlay = PaimonLoadingOverlay::create(tr("leaderboard.loading", "Loading..."), 20.f);
        overlay->showLocal(m_bgPreview, 2);
    };

    // ══════════════════════════════════════
    // Type: default
    // ══════════════════════════════════════
    if (cfg.type == "default") {
        showPlaceholder(tr("pai.config.preview.default_bg", "Default GD\nBackground").c_str());
        return;
    }

    // ══════════════════════════════════════
    // Type: custom image
    // ══════════════════════════════════════
    if (cfg.type == "custom") {
        std::error_code ecCustom;
        if (cfg.customPath.empty() || !std::filesystem::exists(cfg.customPath, ecCustom)) {
            showPlaceholder(tr("pai.config.preview.file_not_found", "File not\nfound").c_str(), {255, 100, 100});
            return;
        }
        auto ext = geode::utils::string::toLower(
            geode::utils::string::pathToString(std::filesystem::path(cfg.customPath).extension()));

        if (ext == ".gif") {
            // Show actual animated GIF in preview
            showLoading();
            std::string gifPath = cfg.customPath;
            std::string selectedKey = m_selectedKey;
            Ref<PaiConfigLayer> self = this;
            AnimatedGIFSprite::pinGIF(gifPath);
            AnimatedGIFSprite::createAsync(gifPath, [self, selectedKey, pw, ph, midX, midY](AnimatedGIFSprite* anim) {
                if (self->m_selectedKey != selectedKey || !self->m_bgPreview) {
                    return;
                }
                self->m_bgPreview->removeAllChildren();
                if (!anim) {
                    auto lbl = CCLabelBMFont::create(tr("pai.config.preview.gif_error", "GIF Error").c_str(), "bigFont.fnt");
                    lbl->setScale(0.2f);
                    lbl->setColor({255, 80, 80});
                    lbl->setPosition({midX, midY});
                    self->m_bgPreview->addChild(lbl, 1);
                    return;
                }
                float scX = pw / anim->getContentWidth();
                float scY = ph / anim->getContentHeight();
                anim->setScale(std::max(scX, scY));
                anim->setPosition({midX, midY});
                anim->setAnchorPoint({0.5f, 0.5f});

                auto stencil = paimon::SpriteHelper::createRectStencil(pw, ph);
                auto clipper = CCClippingNode::create();
                clipper->setStencil(stencil);
                clipper->setAlphaThreshold(0.5f);
                clipper->setContentSize({pw, ph});
                clipper->addChild(anim);
                self->m_bgPreview->addChild(clipper, 1);

                auto cfg2 = LayerBackgroundManager::get().getConfig(selectedKey);
                if (cfg2.darkMode) {
                    auto darkOv = CCLayerColor::create({0, 0, 0, (GLubyte)(cfg2.darkIntensity * 200)});
                    darkOv->setContentSize({pw, ph});
                    self->m_bgPreview->addChild(darkOv, 5);
                }
            });
            return;
        }

        // Static image
        CCTextureCache::sharedTextureCache()->removeTextureForKey(cfg.customPath.c_str());
        auto* tex = CCTextureCache::sharedTextureCache()->addImage(cfg.customPath.c_str(), false);
        if (tex) {
            addTextureToPreview(tex);
            addDarkOverlay();
        } else {
            showPlaceholder(tr("pai.config.preview.load_error", "Load\nerror").c_str(), {255, 100, 100});
        }
        return;
    }

    // ══════════════════════════════════════
    // Type: video
    // ══════════════════════════════════════
    if (cfg.type == "video") {
        std::error_code ecVideo;
        if (cfg.customPath.empty() || !std::filesystem::exists(cfg.customPath, ecVideo)) {
            showPlaceholder(tr("pai.config.preview.file_not_found", "File not\nfound").c_str(), {255, 100, 100});
            return;
        }

        // Lightweight preview: dark background + VIDEO label.
        // Avoids creating a full VideoPlayer which would heavyweight-init
        // MF + FMOD just to produce a black frame.
        auto bg = CCLayerColor::create({20, 20, 35, 255});
        bg->setContentSize({pw, ph});
        m_bgPreview->addChild(bg, 0);

        // Extract filename for display
        auto filename = geode::utils::string::pathToString(std::filesystem::path(cfg.customPath).filename());
        if (filename.size() > 22) filename = filename.substr(0, 19) + "...";
        auto nameLbl = CCLabelBMFont::create(filename.c_str(), "bigFont.fnt");
        nameLbl->setScale(0.14f);
        nameLbl->setColor({180, 180, 180});
        nameLbl->setPosition({midX, midY - 8});
        m_bgPreview->addChild(nameLbl, 1);

        // Video icon overlay
        auto videoLabel = CCLabelBMFont::create("VIDEO", "bigFont.fnt");
        videoLabel->setScale(0.2f);
        videoLabel->setColor({100, 200, 255});
        videoLabel->setPosition({midX, midY + 10});
        m_bgPreview->addChild(videoLabel, 10);

        addDarkOverlay();
        return;
    }

    // ══════════════════════════════════════
    // Type: level ID — async download
    // ══════════════════════════════════════
    if (cfg.type == "id" && cfg.levelId > 0) {
        // Try local first (instant)
        auto* localTex = LocalThumbs::get().loadTexture(cfg.levelId);
        if (localTex) {
            addTextureToPreview(localTex);
            addDarkOverlay();
            return;
        }

        // Not local — show loading and download
        showLoading();
        int levelId = cfg.levelId;
        std::string selectedKey = m_selectedKey;
        Ref<PaiConfigLayer> self = this;

        ThumbnailAPI::get().getThumbnail(levelId, [self, selectedKey, pw, ph, midX, midY](bool success, CCTexture2D* tex) {
            if (self->m_selectedKey != selectedKey || !self->m_bgPreview) {
                return;
            }
            self->m_bgPreview->removeAllChildren();

            if (success && tex) {
                auto spr = CCSprite::createWithTexture(tex);
                if (spr) {
                    float scX = pw / spr->getContentWidth();
                    float scY = ph / spr->getContentHeight();
                    spr->setScale(std::max(scX, scY));
                    spr->setPosition({midX, midY});
                    spr->setAnchorPoint({0.5f, 0.5f});

                    auto stencil = paimon::SpriteHelper::createRectStencil(pw, ph);
                    auto clipper = CCClippingNode::create();
                    clipper->setStencil(stencil);
                    clipper->setAlphaThreshold(0.5f);
                    clipper->setContentSize({pw, ph});
                    clipper->addChild(spr);
                    self->m_bgPreview->addChild(clipper, 1);

                    auto cfg2 = LayerBackgroundManager::get().getConfig(selectedKey);
                    if (cfg2.darkMode) {
                        auto darkOv = CCLayerColor::create({0, 0, 0, (GLubyte)(cfg2.darkIntensity * 200)});
                        darkOv->setContentSize({pw, ph});
                        self->m_bgPreview->addChild(darkOv, 5);
                    }
                }
            } else {
                auto bgRect = CCLayerColor::create({40, 40, 60, 200});
                bgRect->setContentSize({pw, ph});
                self->m_bgPreview->addChild(bgRect, 0);
                auto lbl = CCLabelBMFont::create(tr("pai.config.preview.not_found_server", "Not found\non server").c_str(), "bigFont.fnt");
                lbl->setScale(0.18f);
                lbl->setColor({255, 120, 80});
                lbl->setAlignment(kCCTextAlignmentCenter);
                lbl->setPosition({midX, midY});
                self->m_bgPreview->addChild(lbl, 1);
            }
        });
        return;
    }

    // ══════════════════════════════════════
    // Type: random
    // ══════════════════════════════════════
    if (cfg.type == "random") {
        auto ids = LocalThumbs::get().getAllLevelIDs();
        if (!ids.empty()) {
            // Show a random one each time
            static std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<size_t> dist(0, ids.size() - 1);
            auto* tex = LocalThumbs::get().loadTexture(ids[dist(rng)]);
            if (tex) {
                addTextureToPreview(tex);
                addDarkOverlay();
                return;
            }
        }
        showPlaceholder(tr("pai.config.preview.random_no_cache", "Random\n(no cache)").c_str(), {180, 180, 100});
        return;
    }

    // ══════════════════════════════════════
    // Type: same as another layer — resolve and show
    // ══════════════════════════════════════
    // Resolve the reference chain to find the actual config
    bool isLayerRef = false;
    for (auto& [k, n] : LayerBackgroundManager::LAYER_OPTIONS) {
        if (cfg.type == k) { isLayerRef = true; break; }
    }
    if (isLayerRef || cfg.type == "menu") {
        // Resolve the reference chain (max 5 hops)
        LayerBgConfig resolvedCfg = mgr.getConfig(cfg.type == "menu" ? "menu" : cfg.type);
        int resolveHops = 5;
        while (resolveHops-- > 0) {
            if (resolvedCfg.type == "default") {
                showPlaceholder(("-> " + cfg.type + "\n(Default)").c_str(), {150, 200, 255});
                return;
            }
            bool isRef = false;
            for (auto& [k2, n2] : LayerBackgroundManager::LAYER_OPTIONS) {
                if (resolvedCfg.type == k2) { isRef = true; break; }
            }
            if (isRef) {
                resolvedCfg = mgr.getConfig(resolvedCfg.type);
                continue;
            }
            break; // resolved to a concrete type (custom, random, id)
        }

        // Now resolvedCfg is the actual config — render it
        std::error_code ecResolved;
        if (resolvedCfg.type == "custom" && !resolvedCfg.customPath.empty() && std::filesystem::exists(resolvedCfg.customPath, ecResolved)) {
            auto ext = geode::utils::string::toLower(
                geode::utils::string::pathToString(std::filesystem::path(resolvedCfg.customPath).extension()));
            if (ext == ".gif") {
                showLoading();
                std::string gifPath = resolvedCfg.customPath;
                std::string selectedKey = m_selectedKey;
                Ref<PaiConfigLayer> self = this;
                AnimatedGIFSprite::pinGIF(gifPath);
                AnimatedGIFSprite::createAsync(gifPath, [self, selectedKey, pw, ph, midX, midY, cfg](AnimatedGIFSprite* anim) {
                    if (self->m_selectedKey != selectedKey || !self->m_bgPreview) { return; }
                    self->m_bgPreview->removeAllChildren();
                    if (!anim) {
                        auto lbl = CCLabelBMFont::create(tr("pai.config.preview.gif_error", "GIF Error").c_str(), "bigFont.fnt");
                        lbl->setScale(0.2f); lbl->setColor({255, 80, 80}); lbl->setPosition({midX, midY});
                        self->m_bgPreview->addChild(lbl, 1);
                        return;
                    }
                    float scX2 = pw / anim->getContentWidth();
                    float scY2 = ph / anim->getContentHeight();
                    anim->setScale(std::max(scX2, scY2));
                    anim->setPosition({midX, midY});
                    anim->setAnchorPoint({0.5f, 0.5f});
                    auto stencil2 = paimon::SpriteHelper::createRectStencil(pw, ph);
                    auto clipper2 = CCClippingNode::create();
                    clipper2->setStencil(stencil2);
                    clipper2->setAlphaThreshold(0.5f);
                    clipper2->setContentSize({pw, ph});
                    clipper2->addChild(anim);
                    self->m_bgPreview->addChild(clipper2, 1);
                    if (cfg.darkMode) {
                        auto darkOv = CCLayerColor::create({0, 0, 0, (GLubyte)(cfg.darkIntensity * 200)});
                        darkOv->setContentSize({pw, ph});
                        self->m_bgPreview->addChild(darkOv, 5);
                    }
                });
                return;
            }
            CCTextureCache::sharedTextureCache()->removeTextureForKey(resolvedCfg.customPath.c_str());
            auto* resolvedTex = CCTextureCache::sharedTextureCache()->addImage(resolvedCfg.customPath.c_str(), false);
            if (resolvedTex) {
                addTextureToPreview(resolvedTex);
                addDarkOverlay();
                return;
            }
        } else if (resolvedCfg.type == "id" && resolvedCfg.levelId > 0) {
            auto* localTex = LocalThumbs::get().loadTexture(resolvedCfg.levelId);
            if (localTex) {
                addTextureToPreview(localTex);
                addDarkOverlay();
                return;
            }
        } else if (resolvedCfg.type == "random") {
            auto ids = LocalThumbs::get().getAllLevelIDs();
            if (!ids.empty()) {
                static std::mt19937 refRng(std::random_device{}());
                std::uniform_int_distribution<size_t> dist(0, ids.size() - 1);
                auto* randomTex = LocalThumbs::get().loadTexture(ids[dist(refRng)]);
                if (randomTex) {
                    addTextureToPreview(randomTex);
                    addDarkOverlay();
                    return;
                }
            }
        } else if (resolvedCfg.type == "video" && !resolvedCfg.customPath.empty()) {
            std::error_code ecVideo;
            if (std::filesystem::exists(resolvedCfg.customPath, ecVideo)) {
                auto bg = CCLayerColor::create({20, 20, 35, 255});
                bg->setContentSize({pw, ph});
                m_bgPreview->addChild(bg, 0);

                auto filename = geode::utils::string::pathToString(std::filesystem::path(resolvedCfg.customPath).filename());
                if (filename.size() > 22) filename = filename.substr(0, 19) + "...";
                auto nameLbl = CCLabelBMFont::create(filename.c_str(), "bigFont.fnt");
                nameLbl->setScale(0.12f);
                nameLbl->setColor({180, 180, 180});
                nameLbl->setPosition({midX, midY - 8});
                m_bgPreview->addChild(nameLbl, 1);

                auto videoLabel = CCLabelBMFont::create("VIDEO", "bigFont.fnt");
                videoLabel->setScale(0.15f);
                videoLabel->setColor({100, 200, 255});
                videoLabel->setOpacity(200);
                videoLabel->setPosition({pw - 25, 10});
                m_bgPreview->addChild(videoLabel, 10);
                addDarkOverlay();
                return;
            }
        }
        showPlaceholder(("-> " + cfg.type).c_str(), {150, 200, 255});
        return;
    }

    // Fallback
        showPlaceholder(tr("pai.config.preview.unknown_type", "Unknown\ntype").c_str(), {200, 150, 150});
}

// ═══════════════════════════════════════════════════════════
// Background actions
// ═══════════════════════════════════════════════════════════

void PaiConfigLayer::onBgCustomImage(CCObject*) {
    WeakRef<PaiConfigLayer> self = this;
    std::string key = m_selectedKey;
    pt::pickImage([self, key](geode::Result<std::optional<std::filesystem::path>> result) {
        auto layer = self.lock();
        if (!layer) return;
        auto pathOpt = std::move(result).unwrapOr(std::nullopt);
        if (!pathOpt || pathOpt->empty()) return;

        auto pathStr = geode::utils::string::replace(
            geode::utils::string::pathToString(*pathOpt), "\\", "/");
        auto cfg = LayerBackgroundManager::get().getConfig(key);
        cfg.type = "custom"; cfg.customPath = pathStr;
        LayerBackgroundManager::get().saveConfig(key, cfg);
        PaimonNotify::create(tr("pai.config.notify.custom_image_set", "Custom image set!"), NotificationIcon::Success)->show();
        layer->refreshForCurrentLayer();
    });
}

void PaiConfigLayer::onBgVideo(CCObject*) {
    WeakRef<PaiConfigLayer> self = this;
    std::string key = m_selectedKey;
    pt::pickVideo([self, key](geode::Result<std::optional<std::filesystem::path>> result) {
        auto layer = self.lock();
        if (!layer) return;
        auto pathOpt = std::move(result).unwrapOr(std::nullopt);
        if (!pathOpt || pathOpt->empty()) return;

        auto pathStr = geode::utils::string::replace(
            geode::utils::string::pathToString(*pathOpt), "\\", "/");

        // Check if another layer already has a different video configured
        auto conflictLayer = LayerBackgroundManager::get().hasOtherVideoConfigured(key, pathStr);
        if (!conflictLayer.empty()) {
            FLAlertLayer::create(
                "Video Limit Reached",
                fmt::format("Only one video background is supported at a time.\n\"{}\" already has a different video configured.", conflictLayer).c_str(),
                "OK"
            )->show();
            return;
        }

        auto cfg = LayerBackgroundManager::get().getConfig(key);
        cfg.type = "video"; cfg.customPath = pathStr;
        LayerBackgroundManager::get().saveConfig(key, cfg);
        PaimonNotify::create(tr("pai.config.notify.video_set", "Video background set!"), NotificationIcon::Success)->show();
        layer->refreshForCurrentLayer();
    });
}

void PaiConfigLayer::onVideoSettings(CCObject*) {
    auto popup = VideoSettingsPopup::create();
    if (popup) popup->show();
}

void PaiConfigLayer::onBgRandom(CCObject*) {
    auto cfg = LayerBackgroundManager::get().getConfig(m_selectedKey);
    cfg.type = "random";
    LayerBackgroundManager::get().saveConfig(m_selectedKey, cfg);
    PaimonNotify::create(tr("pai.config.notify.random_set", "Random background set!"), NotificationIcon::Success)->show();
    refreshForCurrentLayer();
}

void PaiConfigLayer::onBgSetID(CCObject*) {
    if (!m_bgIdInput) return;
    std::string idStr = m_bgIdInput->getString();
    if (idStr.empty()) return;
    if (auto res = geode::utils::numFromString<int>(idStr)) {
        int levelId = res.unwrap();
        auto cfg = LayerBackgroundManager::get().getConfig(m_selectedKey);
        cfg.type = "id"; cfg.levelId = levelId;
        LayerBackgroundManager::get().saveConfig(m_selectedKey, cfg);
        PaimonNotify::create(tr("pai.config.notify.level_id_set", "Level ID set!"), NotificationIcon::Success)->show();
        refreshForCurrentLayer(); // triggers preview download + status update
    } else {
        PaimonNotify::create(tr("pai.config.notify.invalid_id", "Invalid ID"), NotificationIcon::Error)->show();
    }
}

void PaiConfigLayer::onBgSameAs(CCObject*) {
    std::string key = m_selectedKey;
    Ref<PaiConfigLayer> self = this;
    auto popup = SameAsPickerPopup::create(key, [self, key](std::string const& picked) {
        auto cfg = LayerBackgroundManager::get().getConfig(key);
        cfg.type = picked;
        LayerBackgroundManager::get().saveConfig(key, cfg);
        PaimonNotify::create((tr("pai.config.notify.same_as_prefix", "Using same bg as ") + picked + "!").c_str(), NotificationIcon::Success)->show();
        if (self->m_selectedKey == key) self->refreshForCurrentLayer();
    });
    if (popup) popup->show();
}

void PaiConfigLayer::onBgDefault(CCObject*) {
    LayerBgConfig cfg; cfg.type = "default";
    LayerBackgroundManager::get().saveConfig(m_selectedKey, cfg);
    PaimonNotify::create(tr("pai.config.notify.reverted_default", "Reverted to default!"), NotificationIcon::Success)->show();
    refreshForCurrentLayer();
}

void PaiConfigLayer::onDarkMode(CCObject* sender) {
    auto toggle = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggle) return;
    auto cfg = LayerBackgroundManager::get().getConfig(m_selectedKey);
    cfg.darkMode = !toggle->isToggled();
    LayerBackgroundManager::get().saveConfig(m_selectedKey, cfg);
    rebuildBgPreview();
}

void PaiConfigLayer::onDarkIntensity(CCObject*) {
    if (!m_darkSlider) return;
    auto cfg = LayerBackgroundManager::get().getConfig(m_selectedKey);
    cfg.darkIntensity = m_darkSlider->getValue();
    LayerBackgroundManager::get().saveConfig(m_selectedKey, cfg);
    rebuildBgPreview();
}

void PaiConfigLayer::onAdaptiveColors(CCObject* sender) {
    auto toggle = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggle) return;
    Mod::get()->setSavedValue("bg-adaptive-colors", !toggle->isToggled());
    (void)Mod::get()->saveData();
}

void PaiConfigLayer::onShaderPrev(CCObject*) {
    m_shaderIndex--;
    if (m_shaderIndex < 0) m_shaderIndex = (int)BG_SHADERS.size() - 1;
    auto cfg = LayerBackgroundManager::get().getConfig(m_selectedKey);
    cfg.shader = BG_SHADERS[m_shaderIndex].first;
    LayerBackgroundManager::get().saveConfig(m_selectedKey, cfg);
    updateShaderLabel();
    refreshForCurrentLayer();
}

void PaiConfigLayer::onShaderNext(CCObject*) {
    m_shaderIndex++;
    if (m_shaderIndex >= (int)BG_SHADERS.size()) m_shaderIndex = 0;
    auto cfg = LayerBackgroundManager::get().getConfig(m_selectedKey);
    cfg.shader = BG_SHADERS[m_shaderIndex].first;
    LayerBackgroundManager::get().saveConfig(m_selectedKey, cfg);
    updateShaderLabel();
    refreshForCurrentLayer();
}

void PaiConfigLayer::updateShaderLabel() {
    if (!m_shaderLabel) return;
    if (m_shaderIndex >= 0 && m_shaderIndex < (int)BG_SHADERS.size()) {
        auto const& shaderLabelKey = BG_SHADERS[m_shaderIndex].second;
        m_shaderLabel->setString(tr(shaderLabelKey.c_str(), shaderLabelKey.c_str()).c_str());
        m_shaderLabel->setColor(m_shaderIndex == 0 ? ccColor3B{180, 180, 180} : ccColor3B{100, 255, 100});
    }
}

// Profile actions

void PaiConfigLayer::onProfileImage(CCObject*) {
    WeakRef<PaiConfigLayer> self = this;
    pt::pickImage([self](geode::Result<std::optional<std::filesystem::path>> result) {
        auto layer = self.lock();
        if (!layer) return;
        auto pathOpt = std::move(result).unwrapOr(std::nullopt);
        if (!pathOpt || pathOpt->empty()) return;

        auto pathStr = geode::utils::string::replace(
            geode::utils::string::pathToString(*pathOpt), "\\", "/");
        Mod::get()->setSavedValue<std::string>("profile-bg-type", "custom");
        Mod::get()->setSavedValue<std::string>("profile-bg-path", pathStr);
        (void)Mod::get()->saveData();
        PaimonNotify::create(tr("pai.config.notify.profile_image_set", "Profile image set!"), NotificationIcon::Success)->show();
        layer->rebuildProfilePreview();
    });
}

void PaiConfigLayer::onProfileImageClear(CCObject*) {
    Mod::get()->setSavedValue<std::string>("profile-bg-type", "none");
    Mod::get()->setSavedValue<std::string>("profile-bg-path", "");
    (void)Mod::get()->saveData();
    PaimonNotify::create(tr("pai.config.notify.profile_image_cleared", "Profile image cleared!"), NotificationIcon::Success)->show();
    rebuildProfilePreview();
}

void PaiConfigLayer::onProfilePhoto(CCObject*) {
    auto popup = ProfilePicEditorPopup::create();
    if (popup) popup->show();
}

// Extras

void PaiConfigLayer::onPetConfig(CCObject*) {
    auto popup = PetConfigPopup::create();
    if (popup) popup->show();
}

void PaiConfigLayer::onCustomCursor(CCObject*) {
    auto popup = CursorConfigPopup::create();
    if (popup) popup->show();
}

void PaiConfigLayer::onTransitions(CCObject*) {
    auto popup = TransitionConfigPopup::create();
    if (popup) popup->show();
}

void PaiConfigLayer::onClearAllCache(CCObject*) {
    WeakRef<PaiConfigLayer> self = this;
    geode::createQuickPopup(
        tr("pai.config.clear_cache.title", "Clear All Cache").c_str(),
        tr("pai.config.clear_cache.message",
           "This will <cr>delete all cached data</c>:\n"
           "thumbnails, profile images, profile music,\n"
           "GIFs, and profile background settings.\n\n"
           "Are you sure?"
        ).c_str(),
        tr("general.cancel", "Cancel").c_str(),
        tr("pai.config.clear_cache.confirm", "Clear").c_str(),
        [self](auto*, bool confirmed) {
            if (!confirmed) return;
            auto layerRef = self.lock();
            auto* layer = static_cast<PaiConfigLayer*>(layerRef.data());
            if (!layer || !layer->getParent()) return;

            // 1) Parar musica de perfil si suena
            ProfileMusicManager::get().stopProfileMusic();
            ProfileMusicManager::get().stopPreview();

            // 2) Cache de thumbnails (RAM)
            ThumbnailLoader::get().clearPendingQueue();
            ThumbnailLoader::get().clearCache();

            // 3) Cache de thumbnails (disco: carpeta "cache/")
            ThumbnailLoader::get().clearDiskCache();

            // 4) Cache de perfiles (RAM: texturas + config + no-profile)
            ProfileThumbs::get().clearAllCache();

            // 5) Cache de profileimg (RAM + disco: carpeta "profileimg_cache/")
            clearProfileImgCache();

            // 6) Cache de musica de perfiles (disco: carpeta "profile_music/")
            ProfileMusicManager::get().clearCache();

            // 7) Cache de GIFs animados (RAM)
            AnimatedGIFSprite::clearCache();

            // 8) Cache de GIFs en disco (quality-aware)
            {
                std::error_code ec;
                auto gifCacheDir = paimon::quality::cacheDir() / "gifs";
                if (std::filesystem::exists(gifCacheDir, ec)) {
                    std::filesystem::remove_all(gifCacheDir, ec);
                }
            }

            // 9) Limpiar settings de imagen de perfil
            Mod::get()->setSavedValue<std::string>("profile-bg-type", "none");
            Mod::get()->setSavedValue<std::string>("profile-bg-path", "");
            (void)Mod::get()->saveData();

            // 10) Cache de emotes (RAM + disco + catalogo)
            paimon::emotes::EmoteCache::get().clearAll();
            paimon::emotes::EmoteService::get().clearCatalog();

            // 11) Rebuild preview si estamos en tab profile
            layer->rebuildProfilePreview();

            log::info("[PaiConfigLayer] All caches cleared by user");
            PaimonNotify::create(tr("pai.config.notify.cache_cleared", "All caches cleared!"), NotificationIcon::Success)->show();
        }
    );
}

void PaiConfigLayer::onApply(CCObject*) {
    TransitionManager::get().replaceScene(MenuLayer::scene(false));
}

// Helper

CCMenuItemSpriteExtra* PaiConfigLayer::makeBtn(char const* text, CCPoint pos,
    SEL_MenuHandler handler, CCNode* parent, float scale) {
    auto spr = ButtonSprite::create(text);
    spr->setScale(scale);
    auto btn = CCMenuItemSpriteExtra::create(spr, this, handler);
    btn->setPosition(pos);
    parent->addChild(btn);
    return btn;
}