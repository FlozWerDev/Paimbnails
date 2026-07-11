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
#include "../utils/AsyncImageLoad.hpp"
#include "../utils/FluidReveal.hpp"
#include "../utils/LocalAssetStore.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../core/QualityConfig.hpp"
#include "../video/VideoPlayer.hpp"
#include "../features/emotes/services/EmoteCache.hpp"
#include "../features/emotes/services/EmoteService.hpp"
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/binding/ButtonSprite.hpp>

#include "../features/profiles/services/ProfileImageCache.hpp"
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/Slider.hpp>
#include <filesystem>
#include <random>

using namespace geode::prelude;
namespace C = paimon::ui::constants::config;
namespace S = paimon::ui::constants::shared;

static std::vector<std::pair<std::string, std::string>> BG_SHADERS = {    {"none",             "pai.config.shader.none"},
    {"grayscale",        "pai.config.shader.grayscale"},
    {"sepia",            "pai.config.shader.sepia"},
    {"vignette",         "pai.config.shader.vignette"},
    {"bloom",            "pai.config.shader.bloom"},
    {"chromatic",        "pai.config.shader.chromatic"},
    {"pixelate",         "pai.config.shader.pixelate"},
    {"posterize",        "pai.config.shader.posterize"},
    {"scanlines",        "pai.config.shader.scanlines"},
    {"rain",             "Rain"},
    {"matrix",           "Matrix"},
    {"neon-pulse",       "Neon Pulse"},
    {"wave-distortion",  "Wave Distortion"},
    {"crt",              "CRT"},
    {"glitch",           "Glitch"},
    {"radial-blur",      "Radial Blur"},
    {"shockwave",        "Shockwave"},
    {"vortex",           "Vortex"},
    {"magnetic",         "Magnetic"},
    {"spotlight",        "Spotlight"},
    {"ripple",           "Ripple"},
    {"plasma-cursor",    "Plasma Cursor"},
    {"freeze",           "Freeze"},
    {"pixelate-cursor",  "Pixelate Cursor"},
};

static std::vector<std::pair<std::string, std::string>> PROCEDURAL_BG_SHADERS = {
    {"aurora", "pai.config.shaderbg.aurora"},
    {"nebula", "pai.config.shaderbg.nebula"},
    {"plasma", "pai.config.shaderbg.plasma"},
    {"grid",   "pai.config.shaderbg.grid"},
    {"sunburst", "pai.config.shaderbg.sunburst"},
    {"spiral", "pai.config.shaderbg.spiral"},
    {"warp", "pai.config.shaderbg.warp"},
    {"lava", "pai.config.shaderbg.lava"},
    {"clouds", "pai.config.shaderbg.clouds"},
    {"rings", "pai.config.shaderbg.rings"},
    {"waves", "pai.config.shaderbg.waves"},
    {"hex", "pai.config.shaderbg.hex"},
    {"fireflies", "pai.config.shaderbg.fireflies"},
    {"ripple", "pai.config.shaderbg.ripple"},
    {"starfield", "pai.config.shaderbg.starfield"},
    {"tunnel", "pai.config.shaderbg.tunnel"},
    {"checker", "pai.config.shaderbg.checker"},
    {"digital-rain", "pai.config.shaderbg.digital_rain"},
    {"horizon", "pai.config.shaderbg.horizon"},
    {"fractal", "pai.config.shaderbg.fractal"},
    {"gradient-flow", "pai.config.shaderbg.gradient_flow"},
    {"bubbles", "pai.config.shaderbg.bubbles"},
    {"lightning", "pai.config.shaderbg.lightning"},
    {"moire", "pai.config.shaderbg.moire"},
    {"crystal", "pai.config.shaderbg.crystal"},
    {"embers", "pai.config.shaderbg.embers"},
    {"prism", "pai.config.shaderbg.prism"},
    {"soft-noise", "pai.config.shaderbg.soft_noise"},
    {"pulse", "pai.config.shaderbg.pulse"},
    {"topo", "pai.config.shaderbg.topo"},
    {"bloom-field", "pai.config.shaderbg.bloom_field"},
};

namespace {
std::string tr(char const* key, char const* fallback = "") {
    auto value = Localization::get().getString(key);
    if (value == key && fallback && fallback[0] != '\0') return fallback;
    return value;
}

CCMenu* makeZeroMenu(char const* id = nullptr) {
    auto* menu = CCMenu::create();
    if (id) menu->setID(id);
    menu->setPosition({0.f, 0.f});
    return menu;
}

void addInfoIf(CCMenu* menu, float x, float y, char const* title, char const* body, CCNode* target) {
    auto iBtn = PaimonInfo::createInfoBtn(title, body, target, 0.49f);
    if (!iBtn) return;
    iBtn->setPosition({x, y});
    menu->addChild(iBtn);
}
}

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

bool PaiConfigLayer::init() {
    if (!CCLayer::init()) return false;
    this->setKeypadEnabled(true);
    this->setTouchEnabled(true);

    auto winSize = CCDirector::get()->getWinSize();
    float cx = winSize.width / 2;
    float top = winSize.height;

    auto bg = CCLayerColor::create(ccc4(25, 25, 45, 255));
    bg->setContentSize(winSize);
    this->addChild(bg, -2);

    m_mainMenu = makeZeroMenu("paimon-config-main-menu"_spr);
    this->addChild(m_mainMenu, 10);

    auto title = CCLabelBMFont::create(tr("pai.config.title", "Paimon Settings").c_str(), "goldFont.fnt");
    title->setPosition({cx, top - 20});
    title->setScale(0.65f);
    this->addChild(title);

    auto backSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    auto backBtn = CCMenuItemSpriteExtra::create(backSpr, this, menu_selector(PaiConfigLayer::onBack));
    backBtn->setPosition({25, top - 20});
    m_mainMenu->addChild(backBtn);

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

    auto sep = CCLayerColor::create({255, 255, 255, 40});
    sep->setContentSize({winSize.width - 30, 1});
    sep->setPosition({15, tabY - C::TAB_SEP_BELOW});
    this->addChild(sep, 5);

    auto addTab = [this](CCLayer*& tab, CCMenu*& menu, char const* tabId, char const* menuId, bool visible) {
        tab = CCLayer::create();
        tab->setID(tabId);
        tab->setVisible(visible);
        this->addChild(tab, 5);
        menu = makeZeroMenu(menuId);
        menu->setVisible(visible);
        this->addChild(menu, 11);
    };
    addTab(m_bgTab, m_bgMenu, "bg-tab"_spr, "bg-menu"_spr, true);
    addTab(m_profileTab, m_profileMenu, "profile-tab"_spr, "profile-menu"_spr, false);
    addTab(m_extrasTab, m_extrasMenu, "extras-tab"_spr, "extras-menu"_spr, false);

    buildBackgroundTab();
    buildProfileTab();
    buildExtrasTab();

    auto applySpr = ButtonSprite::create(tr("pai.config.apply", "Apply & Restart Menu").c_str(), "goldFont.fnt", "GJ_button_01.png", .8f);
    applySpr->setScale(0.5f);
    auto applyBtn = CCMenuItemSpriteExtra::create(applySpr, this, menu_selector(PaiConfigLayer::onApply));
    applyBtn->setPosition({cx, C::APPLY_BTN_Y});
    m_mainMenu->addChild(applyBtn);

    switchMainTab(0);
    updateLayerButtons();
    refreshForCurrentLayer();

    paimon::fluid::revealSequential({ title, sep, m_mainMenu },
        {.fadeDuration = 0.18f, .stagger = 0.06f});

    return true;
}

void PaiConfigLayer::buildBackgroundTab() {
    auto winSize = CCDirector::get()->getWinSize();
    float cx = winSize.width / 2;
    float contentTop = winSize.height - C::CONTENT_TOP_OFFSET;
    float contentBot = C::CONTENT_BOT;
    float contentH = contentTop - contentBot;

    float sidebarW = C::SIDEBAR_WIDTH;
    float sidebarX = sidebarW / 2 + C::SIDEBAR_PAD;

    auto sidePanel = paimon::SpriteHelper::createDarkPanel(sidebarW, contentH, 70);
    sidePanel->setPosition({sidebarX - sidebarW / 2, contentBot});
    m_bgTab->addChild(sidePanel, 0);

    auto& layers = LayerBackgroundManager::LAYER_OPTIONS;
    int layerCount = (int)layers.size();

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

    auto sepLine = CCLayerColor::create({255, 255, 255, 50});
    sepLine->setContentSize({1, contentH});
    sepLine->setPosition({sidebarW + C::SIDEBAR_SEP_X, contentBot});
    m_bgTab->addChild(sepLine, 5);

    float rightX = sidebarW + C::SIDEBAR_SEP_X + 8;
    float rightW = winSize.width - rightX - 8;
    float rightCx = rightX + rightW / 2;

    float ctrlPanelH = C::CTRL_PANEL_H;
    float ctrlPanelY = contentTop - ctrlPanelH / 2 - C::CTRL_PANEL_TOP_PAD;

    auto ctrlPanel = paimon::SpriteHelper::createDarkPanel(rightW, ctrlPanelH, 55);
    ctrlPanel->setPosition({rightCx - rightW / 2, ctrlPanelY - ctrlPanelH / 2});
    m_bgTab->addChild(ctrlPanel, 0);

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
    mkRowBtn(tr("pai.config.background.shader_bg", "Shader BG").c_str(), menu_selector(PaiConfigLayer::onBgShader));
    mkRowBtn("Vid. Settings", menu_selector(PaiConfigLayer::onVideoSettings), 0.30f);
    mkRowBtn(tr("pai.config.background.random", "Random").c_str(), menu_selector(PaiConfigLayer::onBgRandom));
    mkRowBtn(tr("pai.config.background.same_as", "Same as...").c_str(), menu_selector(PaiConfigLayer::onBgSameAs));
    mkRowBtn(tr("pai.config.background.default", "Default").c_str(), menu_selector(PaiConfigLayer::onBgDefault), 0.33f);
    row1Menu->updateLayout();
    m_bgRow1Menu = row1Menu;
    this->addChild(row1Menu, 11);

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

    float row3 = row2 - C::CTRL_ROW3_OFFSET;
    m_adaptiveToggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(PaiConfigLayer::onAdaptiveColors), 0.35f);
    m_adaptiveToggle->setPosition({rightX + 15, row3});
    m_bgMenu->addChild(m_adaptiveToggle);

    m_adaptiveLabel = CCLabelBMFont::create(tr("pai.config.background.adaptive_colors", "Adaptive Colors").c_str(), "bigFont.fnt");
    m_adaptiveLabel->setScale(0.2f);
    m_adaptiveLabel->setPosition({rightX + 75, row3});
    m_bgTab->addChild(m_adaptiveLabel, 1);

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

    float row3b = row3 - 18.f;
    auto intTitle2 = CCLabelBMFont::create("Intensity:", "bigFont.fnt");
    intTitle2->setScale(0.15f);
    intTitle2->setPosition({rightX + 185, row3b});
    m_bgTab->addChild(intTitle2, 1);

    m_shaderIntensitySlider = Slider::create(this, menu_selector(PaiConfigLayer::onShaderIntensitySlider), 0.35f);
    m_shaderIntensitySlider->setPosition({rightX + 275, row3b});
    m_bgTab->addChild(m_shaderIntensitySlider, 1);

    m_shaderIntensityLabel = CCLabelBMFont::create("50%", "bigFont.fnt");
    m_shaderIntensityLabel->setScale(0.16f);
    m_shaderIntensityLabel->setColor({255, 200, 100});
    m_shaderIntensityLabel->setPosition({rightX + 335, row3b});
    m_bgTab->addChild(m_shaderIntensityLabel, 1);

    float previewH = contentH - ctrlPanelH - C::PREVIEW_GAP;
    float previewY = contentBot + previewH / 2 + 2;

    auto previewPanel = paimon::SpriteHelper::createDarkPanel(rightW, previewH, 55);
    previewPanel->setPosition({rightCx - rightW / 2, previewY - previewH / 2});
    m_bgTab->addChild(previewPanel, 0);

    auto prevTitle = CCLabelBMFont::create(tr("pai.config.preview", "Preview").c_str(), "goldFont.fnt");
    prevTitle->setScale(0.28f);
    prevTitle->setPosition({rightCx, previewY + previewH / 2 - 8});
    m_bgTab->addChild(prevTitle, 1);

    m_bgStatusLabel = CCLabelBMFont::create(tr("pai.config.status.default", "Default").c_str(), "bigFont.fnt");
    m_bgStatusLabel->setScale(0.18f);
    m_bgStatusLabel->setColor({180, 180, 180});
    m_bgStatusLabel->setPosition({rightCx, previewY - previewH / 2 + 8});
    m_bgTab->addChild(m_bgStatusLabel, 1);

    float prevContW = rightW - 16;
    float prevContH = previewH - 24;
    m_bgPreview = CCNode::create();
    m_bgPreview->setPosition({rightCx - prevContW / 2, previewY - prevContH / 2 - 2});
    m_bgPreview->setContentSize({prevContW, prevContH});
    m_bgPreview->setAnchorPoint({0, 0});
    m_bgTab->addChild(m_bgPreview, 2);

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

void PaiConfigLayer::buildProfileTab() {
    auto winSize = CCDirector::get()->getWinSize();
    float cx = winSize.width / 2;
    float contentMid = (winSize.height - C::CONTENT_TOP_OFFSET + C::CONTENT_BOT) / 2;

    float panelW = C::PROFILE_PANEL_W;
    float panelH = C::PROFILE_PANEL_H;
    auto panel = paimon::SpriteHelper::createDarkPanel(panelW, panelH, 60);
    panel->setPosition({cx - panelW / 2, contentMid - panelH / 2});
    m_profileTab->addChild(panel, 0);

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

    float previewX = cx + 100;
    float previewY = contentMid;

    auto previewLabel = CCLabelBMFont::create(tr("pai.config.preview", "Preview").c_str(), "goldFont.fnt");
    previewLabel->setScale(0.3f);
    previewLabel->setPosition({previewX, previewY + 48});
    m_profileTab->addChild(previewLabel, 1);

    m_profilePreview = CCNode::create();
    m_profilePreview->setPosition({previewX - 40, previewY - 40});
    m_profilePreview->setContentSize({80, 80});
    m_profilePreview->setAnchorPoint({0, 0});
    m_profileTab->addChild(m_profilePreview, 5);

    auto previewBg = paimon::SpriteHelper::createDarkPanel(90, 90, 80);
    previewBg->setPosition({previewX - 45, previewY - 45});
    m_profileTab->addChild(previewBg, -1);

    rebuildProfilePreview();
}

void PaiConfigLayer::buildExtrasTab() {
    auto winSize = CCDirector::get()->getWinSize();
    float cx = winSize.width / 2;
    float contentMid = (winSize.height - C::CONTENT_TOP_OFFSET + C::CONTENT_BOT) / 2;
    float gap = C::EXTRAS_BTN_GAP;
    float petY = contentMid + gap;
    float cursorY = contentMid;
    float transitionsY = contentMid - gap;
    float clearY = contentMid - gap * 2.f;

    float panelW = C::EXTRAS_PANEL_W;
    float panelH = C::EXTRAS_PANEL_H;
    auto panel = paimon::SpriteHelper::createDarkPanel(panelW, panelH, 60);
    panel->setPosition({cx - panelW / 2, contentMid - panelH / 2});
    m_extrasTab->addChild(panel, 0);

    auto extTitle = CCLabelBMFont::create(tr("pai.config.extras.title", "Extras").c_str(), "goldFont.fnt");
    extTitle->setScale(0.4f);
    extTitle->setPosition({cx, contentMid + panelH / 2 - 12});
    m_extrasTab->addChild(extTitle, 1);

    auto petSpr = ButtonSprite::create(tr("pai.config.extras.pet_config", "Pet Config").c_str(), "goldFont.fnt", "GJ_button_03.png", .8f);
    petSpr->setScale(S::BTN_SCALE_LARGE);
    auto petBtn = CCMenuItemSpriteExtra::create(petSpr, this, menu_selector(PaiConfigLayer::onPetConfig));
    petBtn->setPosition({cx, petY});
    m_extrasMenu->addChild(petBtn);

    auto betaLabel = CCLabelBMFont::create(tr("pai.config.extras.beta", "BETA").c_str(), "bigFont.fnt");
    betaLabel->setScale(0.25f);
    betaLabel->setColor({255, 80, 80});
    betaLabel->setPosition({cx + 58, petY + 16.f});
    betaLabel->setRotation(-15.f);
    m_extrasTab->addChild(betaLabel, 10);

    addInfoIf(m_extrasMenu, cx + 48, petY,
        tr("pai.config.extras.pet_info.title", "Pet").c_str(),
        tr("pai.config.extras.pet_info.body",
           "A cute pet follows your cursor.\nThis feature is in <cr>BETA</c> — expect bugs!"
        ).c_str(), this);

    auto cursorSpr = ButtonSprite::create(tr("pai.config.extras.custom_cursor", "Custom Cursor").c_str(), "goldFont.fnt", "GJ_button_02.png", .8f);
    cursorSpr->setScale(S::BTN_SCALE_LARGE);
    auto cursorBtn = CCMenuItemSpriteExtra::create(cursorSpr, this, menu_selector(PaiConfigLayer::onCustomCursor));
    cursorBtn->setID("custom-cursor-config-btn"_spr);
    cursorBtn->setPosition({cx, cursorY});
    m_extrasMenu->addChild(cursorBtn);

    addInfoIf(m_extrasMenu, cx + 64, cursorY,
        tr("pai.config.extras.custom_cursor_info.title", "Custom Cursor").c_str(),
        tr("pai.config.extras.custom_cursor_info.body",
           "Open the full custom cursor editor.\n"
           "From there you can add images, assign idle/move slots,\n"
           "and tweak scale, opacity, trail, and active layers."
        ).c_str(), this);

    auto transSpr = ButtonSprite::create(tr("pai.config.extras.transitions", "Transitions").c_str(), "goldFont.fnt", "GJ_button_04.png", .8f);
    transSpr->setScale(S::BTN_SCALE_LARGE);
    auto transBtn = CCMenuItemSpriteExtra::create(transSpr, this, menu_selector(PaiConfigLayer::onTransitions));
    transBtn->setID("transitions-config-btn"_spr);
    transBtn->setPosition({cx, transitionsY});
    m_extrasMenu->addChild(transBtn);

    addInfoIf(m_extrasMenu, cx + 55, transitionsY,
        tr("pai.config.extras.transitions_info.title", "Transitions").c_str(),
        tr("pai.config.extras.transitions_info.body",
           "Configure custom scene transition effects.\n"
           "Choose from 55+ built-in transitions or create your own\n"
           "with a custom command sequence (DSL)."
        ).c_str(), this);

    auto clearSpr = ButtonSprite::create(tr("pai.config.extras.clear_cache", "Clear All Cache").c_str(), "goldFont.fnt", "GJ_button_06.png", .8f);
    clearSpr->setScale(S::BTN_SCALE_LARGE);
    auto clearBtn = CCMenuItemSpriteExtra::create(clearSpr, this, menu_selector(PaiConfigLayer::onClearAllCache));
    clearBtn->setPosition({cx, clearY});
    m_extrasMenu->addChild(clearBtn);

    addInfoIf(m_extrasMenu, cx + 68, clearY,
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
        ).c_str(), this);

    auto comingSoon = CCLabelBMFont::create(tr("pai.config.extras.coming_soon", "More features coming soon...").c_str(), "bigFont.fnt");
    comingSoon->setScale(0.2f);
    comingSoon->setColor({150, 150, 150});
    comingSoon->setPosition({cx, contentMid - panelH / 2 + 12});
    m_extrasTab->addChild(comingSoon, 1);
}

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

    for (int i = 0; i < (int)m_mainTabBtns.size(); i++) {
        if (auto spr = typeinfo_cast<ButtonSprite*>(m_mainTabBtns[i]->getNormalImage()))
            spr->setColor(i == idx ? ccColor3B{100, 255, 100} : ccColor3B{255, 255, 255});
    }

    if (idx == 1) rebuildProfilePreview();

    std::vector<cocos2d::CCNode*> revealNodes;
    if (idx == 0)      revealNodes = { m_bgTab, m_bgMenu, m_bgSidebarMenu, m_bgRow1Menu };
    else if (idx == 1) revealNodes = { m_profileTab, m_profileMenu, m_profileBtnColumn };
    else               revealNodes = { m_extrasTab, m_extrasMenu };
    paimon::fluid::revealSequential(revealNodes, {.fadeDuration = 0.16f, .stagger = 0.05f});
}

void PaiConfigLayer::rebuildProfilePreview() {
    if (!m_profilePreview) return;
    m_profilePreview->removeAllChildren();
    ++m_profilePreviewGen;

    const float thumbSize = C::PROFILE_THUMB_SIZE;
    auto pSize = m_profilePreview->getContentSize();
    float midX = pSize.width / 2;
    float midY = pSize.height / 2;

    auto picCfg = ProfilePicCustomizer::get().getConfig();
    std::string shapeName = picCfg.stencilSprite;
    if (shapeName.empty()) shapeName = "circle";

    if (picCfg.onlyIconMode) {
        auto container = paimon::profile_pic::composeProfilePicture(nullptr, thumbSize, picCfg);
        if (container) {
            container->setPosition({midX, midY});
            m_profilePreview->addChild(container);
        }
        return;
    }

    auto photo = paimon::profile_pic::resolveProfilePhoto(picCfg);
    using PhotoKind = paimon::profile_pic::ResolvedProfilePhoto::Kind;

    if (photo.kind == PhotoKind::None) {
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

    // composes synchronously; static files decode off the main thread below
    if (photo.kind != PhotoKind::StaticFile) {
        if (auto* imageNode = paimon::profile_pic::createResolvedPhotoNode(photo)) {
            auto container = paimon::profile_pic::composeProfilePicture(imageNode, thumbSize, picCfg);
            if (container) {
                container->setPosition({midX, midY});
                m_profilePreview->addChild(container);
                paimon::fluid::revealNode(container, {.fadeDuration = 0.22f});
            }
            return;
        }
        if (photo.path.empty()) return;
    }

    std::string path = photo.path;

    // static image: decode off the main thread and compose on return so opening
    auto loadingPh = CCLayerColor::create({60, 60, 60, 140});
    loadingPh->setContentSize({thumbSize, thumbSize});
    loadingPh->setPosition({midX - thumbSize / 2, midY - thumbSize / 2});
    m_profilePreview->addChild(loadingPh);

    int gen = m_profilePreviewGen;
    Ref<PaiConfigLayer> self = this;
    auto cfgCopy = picCfg;
    paimon::asyncimg::loadStaticSprite(std::filesystem::path(path), 16,
        [self, gen, cfgCopy, thumbSize](CCSprite* sprite) {
            auto* layer = self.data();
            if (!layer) return;
            if (gen != layer->m_profilePreviewGen) return;
            auto* preview = layer->m_profilePreview;
            if (!preview) return;
            preview->removeAllChildren();

            auto pSize = preview->getContentSize();
            float mx = pSize.width / 2;
            float my = pSize.height / 2;

            if (!sprite) {
                auto errLbl = CCLabelBMFont::create(tr("general.error", "Error").c_str(), "bigFont.fnt");
                errLbl->setScale(0.2f);
                errLbl->setColor({255, 80, 80});
                errLbl->setPosition({mx, my});
                preview->addChild(errLbl);
                return;
            }

            auto container = paimon::profile_pic::composeProfilePicture(sprite, thumbSize, cfgCopy);
            if (!container) return;
            container->setPosition({mx, my});
            preview->addChild(container);
            paimon::fluid::revealNode(container, {.fadeDuration = 0.22f});
        });
}

void PaiConfigLayer::keyBackClicked() { onBack(nullptr); }

void PaiConfigLayer::onBack(CCObject*) {
    CCDirector::get()->popSceneWithTransition(0.3f, PopTransition::kPopTransitionFade);
}

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

    auto& shaderList = bgCfg.type == "shader" ? PROCEDURAL_BG_SHADERS : BG_SHADERS;
    m_shaderIndex = 0;
    for (int i = 0; i < (int)shaderList.size(); i++) {
        if (shaderList[i].first == bgCfg.shader) { m_shaderIndex = i; break; }
    }
    updateShaderLabel();

    float savedIntensity = Mod::get()->getSavedValue<float>("layerbg-shader-intensity", 0.5f);
    m_shaderIntensityIndex = std::clamp(static_cast<int>(savedIntensity * 10.f) - 1, 0, 9);
    if (m_shaderIntensitySlider) m_shaderIntensitySlider->setValue(savedIntensity);
    updateShaderIntensityLabel();

    if (m_bgStatusLabel) {
        std::string status = tr("pai.config.status.default", "Default");
        if (bgCfg.type == "custom") status = tr("pai.config.status.custom_image", "Custom Image");
        else if (bgCfg.type == "video") status = tr("pai.config.status.video", "Video");
        else if (bgCfg.type == "shader") status = tr("pai.config.status.shader_bg", "Shader Background");
        else if (bgCfg.type == "random") status = tr("pai.config.status.random", "Random");
        else if (bgCfg.type == "id") status = tr("pai.config.status.level_id", "Level ID: ") + std::to_string(bgCfg.levelId);
        else if (bgCfg.type == "menu") status = tr("pai.config.status.same_as_menu", "Same as Menu");
        if (bgCfg.shader != "none" && !bgCfg.shader.empty()) {
            for (auto& [k, v] : BG_SHADERS) {
                if (k == bgCfg.shader) { status += " + " + tr(v.c_str(), v.c_str()); break; }
            }
            for (auto& [k, v] : PROCEDURAL_BG_SHADERS) {
                if (k == bgCfg.shader) { status += " + " + tr(v.c_str(), v.c_str()); break; }
            }
        }
        m_bgStatusLabel->setString(status.c_str());
    }

    rebuildBgPreview();
}

static void addBgPreviewDarkOverlay(CCNode* bgPreview, float pw, float ph, LayerBgConfig const& cfg) {
    if (!cfg.darkMode || !bgPreview) return;
    auto darkOv = CCLayerColor::create({0, 0, 0, (GLubyte)(cfg.darkIntensity * 200)});
    darkOv->setContentSize({pw, ph});
    bgPreview->addChild(darkOv, 5);
}

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

    auto addDarkOverlay = [&]() {
        addBgPreviewDarkOverlay(m_bgPreview, pw, ph, cfg);
    };

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

    auto showLoading = [&]() {
        auto overlay = PaimonLoadingOverlay::create(tr("leaderboard.loading", "Loading..."), 20.f);
        overlay->showLocal(m_bgPreview, 2);
    };

    if (cfg.type == "default") {
        showPlaceholder(tr("pai.config.preview.default_bg", "Default GD\nBackground").c_str());
        return;
    }

    if (cfg.type == "shader") {
        auto bg = CCLayerColor::create({18, 18, 28, 255});
        bg->setContentSize({pw, ph});
        m_bgPreview->addChild(bg, 0);

        auto shaderName = cfg.shader;
        std::string label = tr("pai.config.status.shader_bg", "Shader Background");
        for (auto& [k, v] : PROCEDURAL_BG_SHADERS) {
            if (k == shaderName) {
                label += "\n" + tr(v.c_str(), v.c_str());
                break;
            }
        }

        auto lbl = CCLabelBMFont::create(label.c_str(), "bigFont.fnt");
        lbl->setScale(0.18f);
        lbl->setAlignment(kCCTextAlignmentCenter);
        lbl->setColor({120, 220, 255});
        lbl->setPosition({midX, midY});
        m_bgPreview->addChild(lbl, 1);
        addDarkOverlay();
        return;
    }

    if (cfg.type == "custom") {        std::error_code ecCustom;
        if (cfg.customPath.empty() || !std::filesystem::exists(cfg.customPath, ecCustom)) {
            showPlaceholder(tr("pai.config.preview.file_not_found", "File not\nfound").c_str(), {255, 100, 100});
            return;
        }
        auto ext = geode::utils::string::toLower(
            geode::utils::string::pathToString(std::filesystem::path(cfg.customPath).extension()));

        if (ext == ".gif") {
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
                addBgPreviewDarkOverlay(self->m_bgPreview, pw, ph, cfg2);
            });
            return;
        }

        auto loaded = ImageLoadHelper::loadStaticImage(std::filesystem::path(cfg.customPath), 24);        if (loaded.success && loaded.texture) {
            addTextureToPreview(loaded.texture);
            loaded.texture->release();
            addDarkOverlay();
        } else {
            showPlaceholder(tr("pai.config.preview.load_error", "Load\nerror").c_str(), {255, 100, 100});
        }
        return;
    }

    if (cfg.type == "video") {
        std::error_code ecVideo;
        if (cfg.customPath.empty() || !std::filesystem::exists(cfg.customPath, ecVideo)) {
            showPlaceholder(tr("pai.config.preview.file_not_found", "File not\nfound").c_str(), {255, 100, 100});
            return;
        }

        auto bg = CCLayerColor::create({20, 20, 35, 255});
        bg->setContentSize({pw, ph});
        m_bgPreview->addChild(bg, 0);

        auto filename = geode::utils::string::pathToString(std::filesystem::path(cfg.customPath).filename());
        if (filename.size() > 22) filename = filename.substr(0, 19) + "...";
        auto nameLbl = CCLabelBMFont::create(filename.c_str(), "bigFont.fnt");
        nameLbl->setScale(0.14f);
        nameLbl->setColor({180, 180, 180});
        nameLbl->setPosition({midX, midY - 8});
        m_bgPreview->addChild(nameLbl, 1);

        auto videoLabel = CCLabelBMFont::create("VIDEO", "bigFont.fnt");
        videoLabel->setScale(0.2f);
        videoLabel->setColor({100, 200, 255});
        videoLabel->setPosition({midX, midY + 10});
        m_bgPreview->addChild(videoLabel, 10);

        addDarkOverlay();
        return;
    }

    if (cfg.type == "id" && cfg.levelId > 0) {
        auto* localTex = LocalThumbs::get().loadTexture(cfg.levelId);
        if (localTex) {
            addTextureToPreview(localTex);
            addDarkOverlay();
            return;
        }

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
                    addBgPreviewDarkOverlay(self->m_bgPreview, pw, ph, cfg2);
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

    if (cfg.type == "random") {
        auto ids = LocalThumbs::get().getAllLevelIDs();
        if (!ids.empty()) {
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

    bool isLayerRef = false;
    for (auto& [k, n] : LayerBackgroundManager::LAYER_OPTIONS) {
        if (cfg.type == k) { isLayerRef = true; break; }
    }
    if (isLayerRef || cfg.type == "menu") {
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
                    addBgPreviewDarkOverlay(self->m_bgPreview, pw, ph, cfg);
                });
                return;
            }
            auto loaded = ImageLoadHelper::loadStaticImage(std::filesystem::path(resolvedCfg.customPath), 24);
            if (loaded.success && loaded.texture) {
                addTextureToPreview(loaded.texture);
                loaded.texture->release();
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

        showPlaceholder(tr("pai.config.preview.unknown_type", "Unknown\ntype").c_str(), {200, 150, 150});
}

void PaiConfigLayer::onBgCustomImage(CCObject*) {
    WeakRef<PaiConfigLayer> self = this;
    std::string key = m_selectedKey;
    pt::pickImage([self, key](geode::Result<std::optional<std::filesystem::path>> result) {
        auto layer = self.lock();
        if (!layer) return;
        auto pathOpt = std::move(result).unwrapOr(std::nullopt);
        if (!pathOpt || pathOpt->empty()) return;

        auto imported = paimon::assets::importToBucket(*pathOpt, "background_" + key, paimon::assets::Kind::Image);
        if (!imported.success || imported.path.empty()) {
            PaimonNotify::create("Failed to import image", NotificationIcon::Error)->show();
            return;
        }
        auto pathStr = paimon::assets::normalizePathString(imported.path);
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

        auto imported = paimon::assets::importToBucket(*pathOpt, "background_" + key, paimon::assets::Kind::Video);
        if (!imported.success || imported.path.empty()) {
            PaimonNotify::create("Failed to import video", NotificationIcon::Error)->show();
            return;
        }
        auto pathStr = paimon::assets::normalizePathString(imported.path);

        auto cfg = LayerBackgroundManager::get().getConfig(key);
        cfg.type = "video"; cfg.customPath = pathStr;
        LayerBackgroundManager::get().saveConfig(key, cfg);
        PaimonNotify::create(tr("pai.config.notify.video_set", "Video background set!"), NotificationIcon::Success)->show();
        layer->refreshForCurrentLayer();
    });
}

void PaiConfigLayer::onVideoSettings(CCObject*) {
    if (auto popup = VideoSettingsPopup::create()) popup->show();
}

void PaiConfigLayer::onBgRandom(CCObject*) {
    auto cfg = LayerBackgroundManager::get().getConfig(m_selectedKey);
    cfg.type = "random";
    LayerBackgroundManager::get().saveConfig(m_selectedKey, cfg);
    PaimonNotify::create(tr("pai.config.notify.random_set", "Random background set!"), NotificationIcon::Success)->show();
    refreshForCurrentLayer();
}

void PaiConfigLayer::onBgShader(CCObject*) {
    auto cfg = LayerBackgroundManager::get().getConfig(m_selectedKey);
    cfg.type = "shader";
    if (cfg.shader == "none" || cfg.shader == "grayscale" || cfg.shader == "sepia" || cfg.shader == "vignette" ||
        cfg.shader == "bloom" || cfg.shader == "chromatic" || cfg.shader == "pixelate" || cfg.shader == "posterize" || cfg.shader == "scanlines") {
        cfg.shader = PROCEDURAL_BG_SHADERS.front().first;
    }
    LayerBackgroundManager::get().saveConfig(m_selectedKey, cfg);
    PaimonNotify::create(tr("pai.config.notify.shader_bg_set", "Shader background set!"), NotificationIcon::Success)->show();
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
    auto cfg = LayerBackgroundManager::get().getConfig(m_selectedKey);
    auto& shaderList = cfg.type == "shader" ? PROCEDURAL_BG_SHADERS : BG_SHADERS;
    m_shaderIndex = (m_shaderIndex - 1 + (int)shaderList.size()) % (int)shaderList.size();
    cfg.shader = shaderList[m_shaderIndex].first;
    LayerBackgroundManager::get().saveConfig(m_selectedKey, cfg);
    updateShaderLabel();
    refreshForCurrentLayer();
}

void PaiConfigLayer::onShaderNext(CCObject*) {
    auto cfg = LayerBackgroundManager::get().getConfig(m_selectedKey);
    auto& shaderList = cfg.type == "shader" ? PROCEDURAL_BG_SHADERS : BG_SHADERS;
    m_shaderIndex = (m_shaderIndex + 1) % (int)shaderList.size();
    cfg.shader = shaderList[m_shaderIndex].first;
    LayerBackgroundManager::get().saveConfig(m_selectedKey, cfg);
    updateShaderLabel();
    refreshForCurrentLayer();
}

void PaiConfigLayer::updateShaderLabel() {
    if (!m_shaderLabel) return;
    auto cfg = LayerBackgroundManager::get().getConfig(m_selectedKey);
    auto& shaderList = cfg.type == "shader" ? PROCEDURAL_BG_SHADERS : BG_SHADERS;
    if (m_shaderIndex >= 0 && m_shaderIndex < (int)shaderList.size()) {
        auto const& shaderLabelKey = shaderList[m_shaderIndex].second;
        m_shaderLabel->setString(tr(shaderLabelKey.c_str(), shaderLabelKey.c_str()).c_str());
        bool isNone = cfg.type != "shader" && m_shaderIndex == 0;
        m_shaderLabel->setColor(isNone ? ccColor3B{180, 180, 180} : ccColor3B{100, 255, 100});
    }
}

void PaiConfigLayer::onShaderIntensitySlider(CCObject*) {
    if (!m_shaderIntensitySlider) return;
    float intensity = m_shaderIntensitySlider->getValue(); // 0.0 to 1.0
    intensity = std::max(0.1f, intensity);
    Mod::get()->setSavedValue<float>("layerbg-shader-intensity", intensity);
    m_shaderIntensityIndex = std::clamp(static_cast<int>(intensity * 10.f) - 1, 0, 9);
    updateShaderIntensityLabel();
}

void PaiConfigLayer::updateShaderIntensityLabel() {
    if (!m_shaderIntensityLabel) return;
    float intensity = Mod::get()->getSavedValue<float>("layerbg-shader-intensity", 0.5f);
    int percent = static_cast<int>(intensity * 100.f);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", percent);
    m_shaderIntensityLabel->setString(buf);
}

void PaiConfigLayer::onProfileImage(CCObject*) {
    WeakRef<PaiConfigLayer> self = this;
    pt::pickImage([self](geode::Result<std::optional<std::filesystem::path>> result) {
        auto layer = self.lock();
        if (!layer) return;
        auto pathOpt = std::move(result).unwrapOr(std::nullopt);
        if (!pathOpt || pathOpt->empty()) return;

        auto imported = paimon::assets::importToBucket(*pathOpt, "profile_picture", paimon::assets::Kind::Image);
        if (!imported.success || imported.path.empty()) {
            PaimonNotify::create("Failed to import image", NotificationIcon::Error)->show();
            return;
        }
        auto pathStr = paimon::assets::normalizePathString(imported.path);
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
    if (auto popup = ProfilePicEditorPopup::create()) popup->show();
}

void PaiConfigLayer::onPetConfig(CCObject*) {
    if (auto popup = PetConfigPopup::create()) popup->show();
}

void PaiConfigLayer::onCustomCursor(CCObject*) {
    if (auto popup = CursorConfigPopup::create()) popup->show();
}

void PaiConfigLayer::onTransitions(CCObject*) {
    if (auto popup = TransitionConfigPopup::create()) popup->show();
}

void PaiConfigLayer::onClearAllCache(CCObject*) {
    WeakRef<PaiConfigLayer> self = this;
    PopupManager::get().quickPopup(
        tr("pai.config.clear_cache.title", "Clear All Cache"),
        tr("pai.config.clear_cache.message",
           "This will <cr>delete all cached data</c>:\n"
           "thumbnails, profile images, profile music,\n"
           "GIFs, and profile background settings.\n\n"
           "Are you sure?"
        ),
        tr("general.cancel", "Cancel"),
        tr("pai.config.clear_cache.confirm", "Clear"),
        [self](FLAlertLayer*, bool confirmed) {
            if (!confirmed) return;
            auto layerRef = self.lock();
            auto* layer = static_cast<PaiConfigLayer*>(layerRef.data());
            if (!layer || !layer->getParent()) return;

            ProfileMusicManager::get().stopProfileMusic();
            ProfileMusicManager::get().stopPreview();

            ThumbnailLoader::get().clearPendingQueue();
            ThumbnailLoader::get().clearCache();
            ThumbnailLoader::get().clearDiskCache();
            ProfileThumbs::get().clearAllCache();
            clearProfileImgCache();
            ProfileMusicManager::get().clearCache();
            AnimatedGIFSprite::clearCache();

            std::error_code ec;
            auto gifCacheDir = paimon::quality::cacheDir() / "gifs";
            if (std::filesystem::exists(gifCacheDir, ec)) std::filesystem::remove_all(gifCacheDir, ec);

            Mod::get()->setSavedValue<std::string>("profile-bg-type", "none");
            Mod::get()->setSavedValue<std::string>("profile-bg-path", "");
            (void)Mod::get()->saveData();

            paimon::emotes::EmoteCache::get().clearAll();
            paimon::emotes::EmoteService::get().clearCatalog();
            layer->rebuildProfilePreview();
            log::info("[PaiConfigLayer] All caches cleared by user");
            PaimonNotify::create(tr("pai.config.notify.cache_cleared", "All caches cleared!"), NotificationIcon::Success)->show();
        }
    ).showInstant();
}

void PaiConfigLayer::onApply(CCObject*) {
    TransitionManager::get().replaceScene(MenuLayer::scene(false));
}

CCMenuItemSpriteExtra* PaiConfigLayer::makeBtn(char const* text, CCPoint pos,
    SEL_MenuHandler handler, CCNode* parent, float scale) {
    auto spr = ButtonSprite::create(text);
    spr->setScale(scale);
    auto btn = CCMenuItemSpriteExtra::create(spr, this, handler);
    btn->setPosition(pos);
    parent->addChild(btn);
    return btn;
}
