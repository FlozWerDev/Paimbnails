#include "PaiConfigLayer.hpp"

#include "../core/UIConstants.hpp"
#include "../features/backgrounds/ui/LayerPreviewNode.hpp"
#include "../features/backgrounds/ui/SameAsPickerPopup.hpp"
#include "../features/backgrounds/ui/VideoSettingsPopup.hpp"
#include "../features/cursor/ui/CursorConfigPopup.hpp"
#include "../features/emotes/services/EmoteCache.hpp"
#include "../features/emotes/services/EmoteService.hpp"
#include "../features/pet/ui/PetConfigPopup.hpp"
#include "../features/profile-music/services/ProfileMusicManager.hpp"
#include "../features/profiles/services/ProfileImageCache.hpp"
#include "../features/profiles/services/ProfilePicCustomizer.hpp"
#include "../features/profiles/services/ProfilePicRenderer.hpp"
#include "../features/profiles/services/ProfileThumbs.hpp"
#include "../features/profiles/ui/ProfilePicEditorPopup.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include "../features/transitions/ui/TransitionConfigPopup.hpp"
#include "../core/QualityConfig.hpp"
#include "../core/modules/ModuleRegistry.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../utils/AsyncImageLoad.hpp"
#include "../utils/FileDialog.hpp"
#include "../utils/FluidReveal.hpp"
#include "../utils/InfoButton.hpp"
#include "../utils/LocalAssetStore.hpp"
#include "../utils/Localization.hpp"
#include "../utils/PaimonNotification.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../ui/PaiConfigKit.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>

#include <algorithm>
#include <filesystem>

using namespace geode::prelude;
namespace C = paimon::ui::constants::config;
namespace bgp = paimon::bgpreview;

// Post-process filters apply over images, videos, and thumbnails.
static std::vector<std::pair<std::string, std::string>> const BG_FILTERS = {
    {"none",             "pai.config.shader.none"},
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

// GPU-generated backgrounds need no source image.
static std::vector<std::pair<std::string, std::string>> const PROCEDURAL_BGS = {
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

CCNode* gdWindow(CCSize size) {
    CCNode* window = paimon::SpriteHelper::safeCreateNineSliceFromFile("GJ_square01.png");
    if (!window) {
        window = paimon::SpriteHelper::createColorPanel(size.width, size.height, {12, 20, 44}, 230, 7.f);
    }
    if (!window) return nullptr;
    window->setContentSize(size);
    window->setAnchorPoint({0.f, 0.f});
    return window;
}

CCNode* gdPlate(CCSize size, GLubyte opacity = 255) {
    CCNode* plate = paimon::SpriteHelper::safeCreateScale9("GJ_square05.png");
    if (!plate) {
        plate = paimon::SpriteHelper::createColorPanel(size.width, size.height, {40, 58, 96}, opacity, 4.f);
    }
    if (!plate) return nullptr;
    plate->setContentSize(size);
    plate->setAnchorPoint({0.f, 0.f});
    if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(plate)) rgba->setOpacity(opacity);
    return plate;
}

CCLabelBMFont* gdLabel(char const* text, char const* font, float maxWidth, float scale,
                       ccColor3B color = {255, 255, 255}) {
    auto* lbl = CCLabelBMFont::create(text, font);
    if (!lbl) return nullptr;
    lbl->setColor(color);
    lbl->limitLabelWidth(maxWidth, scale, 0.05f);
    return lbl;
}

// Fixed-size game buttons keep grid cells aligned and shrink text to fit.
CCMenuItemSpriteExtra* gdFixedButton(char const* text, char const* sprite,
                                     float width, float height, float textScale,
                                     std::function<void()> onPress) {
    auto* spr = ButtonSprite::create(text, static_cast<int>(width), true, "bigFont.fnt",
                                     sprite, height, textScale);
    if (!spr) {
        spr = ButtonSprite::create(text, "bigFont.fnt", sprite, 0.7f);
        if (!spr) return nullptr;
        float const raw = spr->getContentSize().width;
        if (raw > 1.f) spr->setScale(std::min(0.6f, width / raw));
    }
// ButtonSprite does not shrink overflowing text automatically.
    if (auto* label = spr->m_label) {
        float const maxW = width - 10.f;
        float const raw = label->getContentSize().width;
        if (raw > 1.f && raw * label->getScale() > maxW) {
            label->setScale(maxW / raw);
        }
    }
    return CCMenuItemExt::createSpriteExtra(spr,
        [cb = std::move(onPress)](CCMenuItemSpriteExtra*) { if (cb) cb(); });
}

CCMenuItemSpriteExtra* gdButton(char const* text, char const* sprite, float width,
                                std::function<void()> onPress) {
    return gdFixedButton(text, sprite, width, 28.f, 0.6f, std::move(onPress));
}

// Tint a ButtonSprite and its label without changing the texture.
void tintButton(CCMenuItemSpriteExtra* btn, ccColor3B color) {
    if (!btn) return;
    if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(btn->getNormalImage())) {
        rgba->setColor(color);
    }
}

void setButtonTexture(CCMenuItemSpriteExtra* btn, char const* texture) {
    if (!btn) return;
    if (auto* sprite = typeinfo_cast<ButtonSprite*>(btn->getNormalImage())) {
        sprite->updateBGImage(texture);
        sprite->setColor({255, 255, 255});
    }
}

// Per-screen colors make tabs easy to identify.
ccColor3B screenColor(std::string const& key) {
    if (key == "menu")         return {110, 235, 130};
    if (key == "levelinfo")    return {120, 200, 255};
    if (key == "levelselect")  return {170, 160, 255};
    if (key == "creator")      return {255, 190, 110};
    if (key == "browser")      return {120, 230, 220};
    if (key == "search")       return {255, 150, 190};
    if (key == "leaderboards") return {255, 225, 120};
    if (key == "profile")      return {180, 210, 255};
    if (key == "garage")       return {200, 255, 150};
    return {200, 210, 230};
}

std::string percentText(float value) {
    return std::to_string(static_cast<int>(std::lround(value * 100.f))) + "%";
}

bool isLayerReference(std::string const& type) {
    for (auto const& [key, name] : LayerBackgroundManager::LAYER_OPTIONS) {
        if (type == key) return true;
    }
    return false;
}

}


PaiConfigLayer* PaiConfigLayer::create() {
    auto* ret = new PaiConfigLayer();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CCScene* PaiConfigLayer::scene() {
    auto* scene = CCScene::create();
    if (auto* layer = PaiConfigLayer::create()) scene->addChild(layer);
    return scene;
}

PaiConfigLayer* PaiConfigLayer::openOverlay() {
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene) return nullptr;
    auto* layer = PaiConfigLayer::create();
    if (!layer) return nullptr;
    layer->m_overlayMode = true;
    scene->addChild(layer, 5000);
    return layer;
}

bool PaiConfigLayer::init() {
    if (!CCLayer::init()) return false;

    this->setKeypadEnabled(true);
    this->setTouchEnabled(true);
    this->setMouseEnabled(true);
    this->scheduleUpdate();

    m_selectedKey = Mod::get()->getSavedValue<std::string>("paiconfig-last-screen", "menu");
    bool known = false;
    for (auto const& [key, name] : LayerBackgroundManager::LAYER_OPTIONS) {
        if (key == m_selectedKey) { known = true; break; }
    }
    if (!known) m_selectedKey = "menu";

    buildChrome();
    buildBackgroundsTab();
    buildProfileTab();
    buildExtrasTab();

    switchTab(0);
    refreshAll();

    paimon::fluid::revealSequential({m_chromeMenu, m_tabPages.empty() ? nullptr : m_tabPages[0]},
                                    {.fadeDuration = 0.18f, .stagger = 0.06f});
    return true;
}

void PaiConfigLayer::keyBackClicked() { onBack(); }

void PaiConfigLayer::onBack() {
    if (m_overlayMode) {
        this->removeFromParentAndCleanup(true);
        return;
    }
    CCDirector::get()->popSceneWithTransition(0.3f, PopTransition::kPopTransitionFade);
}

void PaiConfigLayer::update(float dt) {
    paimon::configkit::stepWheelScroll(m_controlsScroll, m_controlsTargetY, m_controlsTargetSet, dt);
    paimon::configkit::stepWheelScroll(m_screenScroll, m_screenTargetY, m_screenTargetSet, dt);
}

void PaiConfigLayer::scrollWheel(float x, float y) {
    if (paimon::configkit::queueWheelScroll(m_controlsScroll, x, y, m_controlsTargetY, m_controlsTargetSet)) return;
    if (paimon::configkit::queueWheelScroll(m_screenScroll, x, y, m_screenTargetY, m_screenTargetSet)) return;
    CCLayer::scrollWheel(x, y);
}


void PaiConfigLayer::buildChrome() {
    auto const win = CCDirector::get()->getWinSize();
    float const cx = win.width / 2.f;

    if (auto* bg = paimon::SpriteHelper::safeCreate("GJ_gradientBG.png")) {
        auto const size = bg->getContentSize();
        bg->setAnchorPoint({0.f, 0.f});
        bg->setPosition({0.f, 0.f});
        bg->setScaleX(win.width / std::max(size.width, 1.f));
        bg->setScaleY(win.height / std::max(size.height, 1.f));
        bg->setColor({0, 44, 102});
        this->addChild(bg, -10);
    } else {
        auto* flat = CCLayerColor::create({0, 30, 72, 255});
        flat->setContentSize(win);
        this->addChild(flat, -10);
    }

    m_chromeMenu = CCMenu::create();
    m_chromeMenu->setPosition({0.f, 0.f});
    m_chromeMenu->setID("paimon-config-chrome"_spr);
    this->addChild(m_chromeMenu, 20);

    auto* title = CCLabelBMFont::create(tr("pai.config.title", "Background Editor").c_str(), "goldFont.fnt");
    if (title) {
        title->limitLabelWidth(win.width - 150.f, 0.85f, 0.35f);
        title->setPosition({cx, win.height - C::HEADER_Y});
        this->addChild(title, 15);
    }

    if (auto* backSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_01_001.png")) {
        backSpr->setScale(0.8f);
        auto* backBtn = CCMenuItemExt::createSpriteExtra(backSpr,
            [this](CCMenuItemSpriteExtra*) { onBack(); });
        if (backBtn) {
            backBtn->setPosition({26.f, win.height - C::HEADER_Y});
            backBtn->setID("back-button"_spr);
            m_chromeMenu->addChild(backBtn);
        }
    }

    if (auto* info = PaimonInfo::createInfoBtn(
            tr("pai.config.background.info.title", "Background").c_str(),
            tr("pai.config.background.info.body",
               "<cy>Custom Image</c>: local PNG/JPG/GIF.\n"
               "<cy>Video</c>: local MP4, looped.\n"
               "<cy>Shader BG</c>: animated GPU background.\n"
               "<cy>Random / Level ID</c>: cached thumbnails.\n"
               "<cy>Same as...</c>: copy another screen.\n"
               "The preview below runs live.").c_str(),
            this, 0.6f)) {
        info->setPosition({win.width - 26.f, win.height - C::HEADER_Y});
        m_chromeMenu->addChild(info);
    }

    std::vector<std::pair<std::string, char const*>> const tabs = {
        {tr("pai.config.tab.backgrounds", "Backgrounds"), "GJ_button_01.png"},
        {tr("pai.config.tab.profile", "Profile"),         "GJ_button_02.png"},
        {tr("pai.config.tab.extras", "Extras"),           "GJ_button_03.png"},
    };
    float const tabW = std::min(C::TAB_WIDTH, (win.width - 30.f) / 3.f);
    float const tabY = win.height - C::TAB_Y;
    for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
        auto* btn = gdFixedButton(tabs[i].first.c_str(), tabs[i].second,
                                  tabW - 10.f, 28.f, 0.62f,
                                  [this, i] { switchTab(i); });
        if (!btn) continue;
        btn->setPosition({cx + (i - 1) * tabW, tabY});
        m_chromeMenu->addChild(btn);
        m_tabButtons.push_back(btn);
    }

    if (auto* apply = gdButton(tr("pai.config.apply", "Apply & Restart Menu").c_str(),
                               "GJ_button_01.png", 190.f, [this] { onApplyAndRestart(); })) {
        apply->setPosition({cx - 62.f, C::FOOTER_Y});
        apply->setID("apply-button"_spr);
        m_chromeMenu->addChild(apply);
    }
    if (auto* reset = gdButton(tr("pai.config.reset_screen", "Reset Screen").c_str(),
                               "GJ_button_06.png", 110.f, [this] { onResetScreen(); })) {
        reset->setPosition({cx + 92.f, C::FOOTER_Y});
        reset->setID("reset-button"_spr);
        m_chromeMenu->addChild(reset);
    }
}

void PaiConfigLayer::switchTab(int index) {
    m_currentTab = index;
    for (int i = 0; i < static_cast<int>(m_tabPages.size()); ++i) {
        if (m_tabPages[i]) m_tabPages[i]->setVisible(i == index);
    }
    for (int i = 0; i < static_cast<int>(m_tabButtons.size()); ++i) {
        bool const active = (i == index);
        tintButton(m_tabButtons[i], active ? ccColor3B{255, 255, 255} : ccColor3B{105, 115, 135});
        if (m_tabButtons[i]) m_tabButtons[i]->setScale(active ? 1.f : 0.92f);
    }
    if (index == 1) rebuildProfilePreview();
    if (index < static_cast<int>(m_tabPages.size()) && m_tabPages[index]) {
        paimon::fluid::revealNode(m_tabPages[index], {.fadeDuration = 0.16f});
    }
}


CCNode* PaiConfigLayer::makeCardWindow(CCRect area, char const* title) {
    auto* card = CCNode::create();
    card->setAnchorPoint({0.f, 0.f});
    card->setPosition(area.origin);
    card->setContentSize(area.size);

    if (auto* window = gdWindow(area.size)) card->addChild(window, 0);

    if (title && title[0] != '\0') {
        if (auto* lbl = gdLabel(title, "goldFont.fnt", area.size.width - 16.f, 0.42f)) {
            lbl->setAnchorPoint({0.f, 0.5f});
            lbl->setPosition({10.f, area.size.height - 11.f});
            card->addChild(lbl, 3);
        }
    }
    return card;
}

// Bobbing arrow that tells the user a card scrolls. It lives in the strip the
// cards leave free below their list, so it never lands on top of a row.
CCNode* PaiConfigLayer::addScrollHint(CCNode* card, CCSize cardSize) {
    auto* hint = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_02_001.png");
    if (!hint) return nullptr;

    hint->setRotation(-90.f);
    hint->setScale(0.3f);
    hint->setOpacity(140);
    hint->setPosition({cardSize.width / 2.f, C::LIST_HINT_Y});
    card->addChild(hint, 6);
    hint->runAction(CCRepeatForever::create(CCSequence::create(
        CCEaseInOut::create(CCMoveBy::create(0.6f, {0.f, -3.f}), 2.f),
        CCEaseInOut::create(CCMoveBy::create(0.6f, {0.f, 3.f}), 2.f),
        nullptr)));
    return hint;
}

void PaiConfigLayer::buildBackgroundsTab() {
    auto const win = CCDirector::get()->getWinSize();

    auto* page = CCNode::create();
    page->setContentSize(win);
    page->setAnchorPoint({0.f, 0.f});
    page->setPosition({0.f, 0.f});
    this->addChild(page, 10);
    m_tabPages.push_back(page);

    float const top = win.height - C::CONTENT_TOP;
    float const bottom = C::CONTENT_BOTTOM;
    float const contentH = top - bottom;

    float const listW = std::clamp(win.width * 0.19f, 88.f, 108.f);
    float const rightX = C::EDGE_PAD + listW + C::GUTTER;
    float const rightW = win.width - rightX - C::EDGE_PAD;

    float const controlsW = std::clamp(rightW * 0.58f, 180.f, 290.f);
    float const previewW = rightW - controlsW - C::GUTTER;

    page->addChild(buildScreenList({{C::EDGE_PAD, bottom}, {listW, contentH}}), 1);
    page->addChild(buildPreviewCard({{rightX, bottom}, {previewW, contentH}}), 1);
    page->addChild(buildControlsCard({{rightX + previewW + C::GUTTER, bottom}, {controlsW, contentH}}), 1);
}

CCNode* PaiConfigLayer::buildScreenList(CCRect area) {
    auto* card = makeCardWindow(area, tr("pai.config.screens", "Screens").c_str());

    auto const& screens = LayerBackgroundManager::LAYER_OPTIONS;
    float const innerW = area.size.width - 14.f;
    float const rowH = 25.f;
    CCSize const scrollSize{innerW, area.size.height - 24.f - C::LIST_BOTTOM};

    std::vector<CCNode*> rows;
    rows.reserve(screens.size());
    m_screenButtons.clear();

    for (int i = 0; i < static_cast<int>(screens.size()); ++i) {
        auto const key = screens[i].first;

        auto* row = CCNode::create();
        row->setAnchorPoint({0.f, 0.f});
        row->setContentSize({innerW, rowH});

        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        row->addChild(menu, 5);

        auto* btn = gdFixedButton(screens[i].second.c_str(), "GJ_button_01.png",
                                  innerW - 2.f, 21.f, 0.44f,
                                  [this, key] { selectScreen(key); });
        if (btn) {
            btn->setPosition({innerW / 2.f, rowH / 2.f});
            menu->addChild(btn);
        }

        rows.push_back(row);
        m_screenButtons.push_back(btn);
    }

    m_screenScroll = paimon::configkit::makeScrollStack(scrollSize, rows, 3.f);
    if (m_screenScroll) {
        m_screenScroll->setPosition({7.f, C::LIST_BOTTOM});
        card->addChild(m_screenScroll, 2);

        bool const overflows = m_screenScroll->m_contentLayer &&
            m_screenScroll->m_contentLayer->getContentSize().height > scrollSize.height + 1.f;
        if (overflows) addScrollHint(card, area.size);
    }
    return card;
}

CCNode* PaiConfigLayer::buildPreviewCard(CCRect area) {
    auto* card = makeCardWindow(area, tr("pai.config.preview", "Live Preview").c_str());

    float const innerW = area.size.width - 14.f;
    float const boxTop = area.size.height - 24.f;
    float const boxBottom = 46.f;

    m_preview = bgp::LayerPreviewNode::create({innerW, boxTop - boxBottom}, m_selectedKey);
    if (m_preview) {
        m_preview->setPosition({area.size.width / 2.f, (boxTop + boxBottom) / 2.f});
        card->addChild(m_preview, 2);

        auto const frame = m_preview->frameSize();
        float const centerY = (boxTop + boxBottom) / 2.f;
        if (auto* outline = paimon::SpriteHelper::createRoundedRectOutline(
                frame.width + 4.f, frame.height + 4.f, 3.f, {0.f, 0.f, 0.f, 0.55f}, 2.f)) {
            outline->setAnchorPoint({0.5f, 0.5f});
            outline->setPosition({area.size.width / 2.f, centerY});
            card->addChild(outline, 3);
        }

        float const chipY = centerY + frame.height / 2.f - 8.f;
        if (auto* chip = paimon::SpriteHelper::createColorPanel(frame.width - 8.f, 14.f,
                                                               {0, 0, 0}, 150, 3.f)) {
            chip->setAnchorPoint({0.5f, 0.5f});
            chip->setPosition({area.size.width / 2.f, chipY});
            card->addChild(chip, 4);
        }
        m_screenChipLabel = gdLabel(bgp::displayNameForLayer(m_selectedKey).c_str(), "bigFont.fnt",
                                    frame.width - 16.f, 0.3f, {255, 255, 255});
        if (m_screenChipLabel) {
            m_screenChipLabel->setPosition({area.size.width / 2.f, chipY});
            card->addChild(m_screenChipLabel, 5);
        }
    }

    m_statusLabel = gdLabel(tr("pai.config.status.default", "Default").c_str(), "bigFont.fnt",
                            innerW, 0.3f, {200, 215, 240});
    if (m_statusLabel) {
        m_statusLabel->setAlignment(kCCTextAlignmentCenter);
        m_statusLabel->setPosition({area.size.width / 2.f, 32.f});
        card->addChild(m_statusLabel, 3);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    card->addChild(menu, 4);

    m_mockToggle = CCMenuItemExt::createTogglerWithStandardSprites(0.42f,
        [this](CCMenuItemToggler*) { onToggleMock(); });
    if (m_mockToggle) {
        m_mockToggle->toggle(Mod::get()->getSavedValue<bool>("paiconfig-preview-mock", true));
        m_mockToggle->setPosition({16.f, 13.f});
        menu->addChild(m_mockToggle);
    }
    if (auto* lbl = gdLabel(tr("pai.config.preview.show_ui", "Game UI").c_str(), "bigFont.fnt",
                            area.size.width - 70.f, 0.28f, {210, 220, 240})) {
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({28.f, 13.f});
        card->addChild(lbl, 3);
    }

// Keep the expand button inside the card: it sits on the bottom-right corner.
    float const zoomX = area.size.width - 18.f;
    if (auto* zoomSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_zoomInBtn_001.png")) {
        float const raw = std::max(zoomSpr->getContentSize().width, 1.f);
        zoomSpr->setScale(std::min(0.6f, 22.f / raw));
        if (auto* zoom = CCMenuItemExt::createSpriteExtra(zoomSpr,
                [this](CCMenuItemSpriteExtra*) { onExpandPreview(); })) {
            zoom->setPosition({zoomX, 14.f});
            menu->addChild(zoom);
        }
    } else if (auto* zoom = gdFixedButton("+", "GJ_button_02.png", 18.f, 18.f, 0.5f,
                                          [this] { onExpandPreview(); })) {
        zoom->setPosition({zoomX, 14.f});
        menu->addChild(zoom);
    }

// Level Info may override this background with its own thumbnail.
    m_blockedOverlay = CCLayerColor::create({0, 0, 0, 190});
    if (m_blockedOverlay) {
        m_blockedOverlay->setContentSize(area.size);
        m_blockedOverlay->setPosition({0.f, 0.f});
        m_blockedOverlay->setVisible(false);
        card->addChild(m_blockedOverlay, 20);

        m_blockedLabel = gdLabel(
            tr("pai.config.background.blocked_message",
               "Level Info uses its own\nthumbnail background.\n\nChange it in Mod Settings\n> Background Style.").c_str(),
            "bigFont.fnt", area.size.width - 20.f, 0.32f, {255, 200, 120});
        if (m_blockedLabel) {
            m_blockedLabel->setAlignment(kCCTextAlignmentCenter);
            m_blockedLabel->setPosition({area.size.width / 2.f, area.size.height / 2.f});
            m_blockedOverlay->addChild(m_blockedLabel);
        }
    }

    return card;
}

CCNode* PaiConfigLayer::buildControlsCard(CCRect area) {
    auto* card = makeCardWindow(area, tr("pai.config.background.title", "Background").c_str());

    float const innerW = area.size.width - 14.f;
    CCSize const scrollSize{innerW, area.size.height - 22.f - C::LIST_BOTTOM};

    m_adaptiveRow = rowAdaptive(innerW);
    m_videoRow = rowVideoSettings(innerW);

    m_moduleWarnRow = rowModuleWarning(innerW);

    std::vector<CCNode*> rows = {
        m_moduleWarnRow,
        rowSources(innerW),
        rowLevelId(innerW),
        rowDarken(innerW),
        rowFilter(innerW),
        m_adaptiveRow,
        m_videoRow,
        rowCopyToAll(innerW),
    };

    m_controlRows.assign(rows.begin(), rows.end());
    m_controlsScroll = paimon::configkit::makeScrollStack(scrollSize, rows, 5.f);
    if (m_controlsScroll) {
        m_controlsScroll->setPosition({7.f, C::LIST_BOTTOM});
        card->addChild(m_controlsScroll, 2);
// relayoutControls() decides whether the hint applies to the active rows.
        m_controlsHint = addScrollHint(card, area.size);
    }
    return card;
}


CCNode* PaiConfigLayer::rowSources(float width) {
    struct SourceDef {
        char const* labelKey;
        char const* fallback;
        char const* sprite;
        std::string type;
        std::function<void()> action;
    };

    std::vector<SourceDef> const defs = {
        {"pai.config.background.src_image",   "Image",    "GJ_button_01.png", "custom", [this] { onPickImage(); }},
        {"pai.config.background.src_video",   "Video",    "GJ_button_02.png", "video",  [this] { onPickVideo(); }},
        {"pai.config.background.src_shader",  "Shader",   "GJ_button_03.png", "shader", [this] { onUseShaderBg(); }},
        {"pai.config.background.src_random",  "Random",   "GJ_button_04.png", "random", [this] { onUseRandom(); }},
        {"pai.config.background.src_dynamic", "Dynamic",  "GJ_button_03.png", "",       [this] { onUseDynamicShader(); }},
        {"pai.config.background.src_sameas",  "Same as",  "GJ_button_02.png", "",       [this] { onUseSameAs(); }},
        {"pai.config.background.src_id",      "Level ID", "GJ_button_04.png", "id",     [this] { onUseLevelId(); }},
        {"pai.config.background.src_default", "Default",  "GJ_button_05.png", "default",[this] { onUseDefault(); }},
    };

    int const cols = width >= 232.f ? 4 : (width >= 155.f ? 3 : 2);
    int const total = static_cast<int>(defs.size());
    int const rowCount = (total + cols - 1) / cols;
    float const cellW = (width - 10.f) / cols;
    float const cellH = 24.f;
    float const titleH = 20.f;
    float const height = titleH + rowCount * cellH + 4.f;

    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, height});
    if (auto* plate = gdPlate({width, height}, 235)) row->addChild(plate, 0);

    if (auto* title = gdLabel(tr("pai.config.background.source", "Source").c_str(), "goldFont.fnt",
                              width - 12.f, 0.34f)) {
        title->setAnchorPoint({0.f, 0.5f});
        title->setPosition({8.f, height - 11.f});
        row->addChild(title, 3);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    row->addChild(menu, 5);

    m_sourceButtons.clear();
    for (int i = 0; i < total; ++i) {
        int const col = i % cols;
        int const line = i / cols;
        auto* btn = gdFixedButton(tr(defs[i].labelKey, defs[i].fallback).c_str(), defs[i].sprite,
                                  cellW - 4.f, 20.f, 0.42f, defs[i].action);
        if (!btn) continue;
        btn->setPosition({5.f + cellW * (col + 0.5f),
                          height - titleH - cellH * (line + 0.5f)});
        menu->addChild(btn);
        m_sourceButtons.emplace_back(defs[i].type, btn);
    }
    return row;
}

CCNode* PaiConfigLayer::rowLevelId(float width) {
    float const height = 32.f;
    float const labelW = std::clamp(width * 0.25f, 48.f, 70.f);
    float const buttonW = std::clamp(width * 0.22f, 42.f, 54.f);
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, height});
    if (auto* plate = gdPlate({width, height}, 235)) row->addChild(plate, 0);

    if (auto* title = gdLabel(tr("pai.config.background.level_id", "Level ID").c_str(), "bigFont.fnt",
                              labelW - 12.f, 0.32f, {200, 215, 240})) {
        title->setAnchorPoint({0.f, 0.5f});
        title->setPosition({8.f, height / 2.f});
        row->addChild(title, 3);
    }

    float const inputX = labelW + 8.f;
    float const inputW = std::max(44.f, width - inputX - buttonW - 18.f);
    if (auto* inputBg = paimon::SpriteHelper::createDarkPanel(inputW, 20.f, 110, 3.f)) {
        inputBg->setAnchorPoint({0.f, 0.f});
        inputBg->setPosition({inputX, height / 2.f - 10.f});
        row->addChild(inputBg, 2);
    }

    m_levelIdInput = TextInput::create(inputW / 0.55f, "0");
    if (m_levelIdInput) {
        m_levelIdInput->setCommonFilter(CommonFilter::Uint);
        m_levelIdInput->setMaxCharCount(10);
        m_levelIdInput->setScale(0.55f);
        m_levelIdInput->setPosition({inputX + inputW / 2.f, height / 2.f});
        row->addChild(m_levelIdInput, 4);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    row->addChild(menu, 5);

    if (auto* set = gdFixedButton(tr("pai.config.background.set", "Set").c_str(), "GJ_button_01.png",
                                  buttonW, 21.f, 0.42f, [this] { onUseLevelId(); })) {
        set->setPosition({width - 7.f - buttonW / 2.f, height / 2.f});
        menu->addChild(set);
    }
    return row;
}

CCNode* PaiConfigLayer::rowDarken(float width) {
    float const height = 44.f;
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, height});
    if (auto* plate = gdPlate({width, height}, 235)) row->addChild(plate, 0);

    if (auto* title = gdLabel(tr("pai.config.background.dark", "Darken").c_str(), "bigFont.fnt",
                              width * 0.5f, 0.32f)) {
        title->setAnchorPoint({0.f, 0.5f});
        title->setPosition({8.f, height - 12.f});
        row->addChild(title, 3);
    }

    m_darkValueLabel = gdLabel("50%", "bigFont.fnt", 40.f, 0.28f, {255, 222, 120});
    if (m_darkValueLabel) {
        m_darkValueLabel->setAnchorPoint({1.f, 0.5f});
        m_darkValueLabel->setPosition({width - 34.f, height - 12.f});
        row->addChild(m_darkValueLabel, 3);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    row->addChild(menu, 5);

    m_darkToggle = CCMenuItemExt::createTogglerWithStandardSprites(0.42f,
        [this](CCMenuItemToggler* t) { if (t) onToggleDark(!t->isToggled()); });
    if (m_darkToggle) {
        m_darkToggle->setPosition({width - 15.f, height - 12.f});
        menu->addChild(m_darkToggle);
    }

    m_darkSlider = Slider::create(this, menu_selector(PaiConfigLayer::onDarkSlider), 0.42f);
    if (m_darkSlider) {
        m_darkSlider->setPosition({width / 2.f, 13.f});
        row->addChild(m_darkSlider, 4);
    }
    return row;
}

CCNode* PaiConfigLayer::rowFilter(float width) {
    float const height = 52.f;
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, height});
    if (auto* plate = gdPlate({width, height}, 235)) row->addChild(plate, 0);

    m_filterTitle = gdLabel(tr("pai.config.filter", "Filter").c_str(), "goldFont.fnt",
                            width * 0.55f, 0.32f);
    if (m_filterTitle) {
        m_filterTitle->setAnchorPoint({0.f, 0.5f});
        m_filterTitle->setPosition({8.f, height - 11.f});
        row->addChild(m_filterTitle, 3);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    row->addChild(menu, 5);

    float const arrowY = height - 30.f;
    if (auto* prevSpr = paimon::SpriteHelper::safeCreateWithFrameName("navArrowBtn_001.png")) {
        prevSpr->setFlipX(true);
        prevSpr->setScale(0.32f);
        if (auto* prev = CCMenuItemExt::createSpriteExtra(prevSpr,
                [this](CCMenuItemSpriteExtra*) { onFilterStep(-1); })) {
            prev->setPosition({14.f, arrowY});
            menu->addChild(prev);
        }
    }
    if (auto* nextSpr = paimon::SpriteHelper::safeCreateWithFrameName("navArrowBtn_001.png")) {
        nextSpr->setScale(0.32f);
        if (auto* next = CCMenuItemExt::createSpriteExtra(nextSpr,
                [this](CCMenuItemSpriteExtra*) { onFilterStep(1); })) {
            next->setPosition({width - 14.f, arrowY});
            menu->addChild(next);
        }
    }

    m_filterLabel = gdLabel(tr("pai.config.shader.none", "None").c_str(), "bigFont.fnt",
                            width - 44.f, 0.32f, {120, 255, 150});
    if (m_filterLabel) {
        m_filterLabel->setPosition({width / 2.f, arrowY});
        row->addChild(m_filterLabel, 3);
    }

    m_filterValueLabel = gdLabel("50%", "bigFont.fnt", 40.f, 0.26f, {255, 222, 120});
    if (m_filterValueLabel) {
        m_filterValueLabel->setAnchorPoint({1.f, 0.5f});
        m_filterValueLabel->setPosition({width - 8.f, height - 11.f});
        row->addChild(m_filterValueLabel, 3);
    }

    m_filterSlider = Slider::create(this, menu_selector(PaiConfigLayer::onFilterSlider), 0.42f);
    if (m_filterSlider) {
        m_filterSlider->setPosition({width / 2.f, 12.f});
        row->addChild(m_filterSlider, 4);
    }
    return row;
}

CCNode* PaiConfigLayer::rowAdaptive(float width) {
    float const height = 30.f;
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, height});
    if (auto* plate = gdPlate({width, height}, 235)) row->addChild(plate, 0);

    if (auto* title = gdLabel(tr("pai.config.background.adaptive_colors", "Adaptive Colors").c_str(),
                              "bigFont.fnt", width - 44.f, 0.3f)) {
        title->setAnchorPoint({0.f, 0.5f});
        title->setPosition({8.f, height / 2.f});
        row->addChild(title, 3);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    row->addChild(menu, 5);

    m_adaptiveToggle = CCMenuItemExt::createTogglerWithStandardSprites(0.42f,
        [this](CCMenuItemToggler* t) { if (t) onToggleAdaptive(!t->isToggled()); });
    if (m_adaptiveToggle) {
        m_adaptiveToggle->setPosition({width - 18.f, height / 2.f});
        menu->addChild(m_adaptiveToggle);
    }
    return row;
}

CCNode* PaiConfigLayer::rowVideoSettings(float width) {
    float const height = 30.f;
    float const buttonW = std::clamp(width * 0.25f, 48.f, 66.f);
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, height});
    if (auto* plate = gdPlate({width, height}, 235)) row->addChild(plate, 0);

    if (auto* title = gdLabel(tr("pai.config.background.video_settings", "Video Settings").c_str(),
                              "bigFont.fnt", width - buttonW - 22.f, 0.3f)) {
        title->setAnchorPoint({0.f, 0.5f});
        title->setPosition({8.f, height / 2.f});
        row->addChild(title, 3);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    row->addChild(menu, 5);

    if (auto* btn = gdFixedButton(tr("general.open", "Open").c_str(), "GJ_button_02.png",
                                  buttonW, 21.f, 0.44f, [this] { onVideoSettings(); })) {
        btn->setPosition({width - 7.f - buttonW / 2.f, height / 2.f});
        menu->addChild(btn);
    }
    return row;
}

CCNode* PaiConfigLayer::rowModuleWarning(float width) {
    float const height = 38.f;
    float const buttonW = std::clamp(width * 0.28f, 54.f, 76.f);
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, height});
    if (auto* plate = paimon::SpriteHelper::createColorPanel(width, height, {110, 30, 30}, 235, 4.f)) {
        plate->setAnchorPoint({0.f, 0.f});
        row->addChild(plate, 0);
    }

    if (auto* title = gdLabel(tr("pai.config.module_off", "Backgrounds module is OFF").c_str(),
                              "bigFont.fnt", width - buttonW - 22.f, 0.28f, {255, 200, 190})) {
        title->setAnchorPoint({0.f, 0.5f});
        title->setPosition({8.f, height / 2.f});
        row->addChild(title, 3);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    row->addChild(menu, 5);

    if (auto* btn = gdFixedButton(tr("pai.config.module_on_btn", "Turn on").c_str(), "GJ_button_01.png",
                                  buttonW, 22.f, 0.44f,
                                  [this] {
                                      paimon::modules::setEnabled("paimbnails.backgrounds.global", true);
                                      PaimonNotify::create(
                                          tr("pai.config.module_on_done", "Backgrounds enabled!"),
                                          NotificationIcon::Success)->show();
                                      refreshAll();
                                  })) {
        btn->setPosition({width - 7.f - buttonW / 2.f, height / 2.f});
        menu->addChild(btn);
    }
    return row;
}

CCNode* PaiConfigLayer::rowCopyToAll(float width) {
    float const height = 34.f;
    float const buttonW = std::clamp(width * 0.24f, 48.f, 64.f);
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, height});
    if (auto* plate = gdPlate({width, height}, 235)) row->addChild(plate, 0);

    if (auto* title = gdLabel(tr("pai.config.copy_all", "Copy to all screens").c_str(),
                              "bigFont.fnt", width - buttonW - 22.f, 0.3f, {210, 220, 240})) {
        title->setAnchorPoint({0.f, 0.5f});
        title->setPosition({8.f, height / 2.f});
        row->addChild(title, 3);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    row->addChild(menu, 5);

    if (auto* btn = gdFixedButton(tr("pai.config.copy_all_btn", "Copy").c_str(), "GJ_button_03.png",
                                  buttonW, 22.f, 0.46f,
                                  [this] { onCopyToAllScreens(); })) {
        btn->setPosition({width - 7.f - buttonW / 2.f, height / 2.f});
        menu->addChild(btn);
    }
    return row;
}


LayerBgConfig PaiConfigLayer::currentConfig() const {
    return LayerBackgroundManager::get().getConfig(m_selectedKey);
}

std::vector<std::pair<std::string, std::string>> const& PaiConfigLayer::activeFilterList() const {
    return currentConfig().type == "shader" ? PROCEDURAL_BGS : BG_FILTERS;
}

void PaiConfigLayer::selectScreen(std::string const& key) {
    if (key == m_selectedKey) return;
    m_selectedKey = key;
    Mod::get()->setSavedValue<std::string>("paiconfig-last-screen", key);
    refreshAll();
}

void PaiConfigLayer::mutateConfig(std::function<void(LayerBgConfig&)> const& fn,
                                  char const* toastKey, char const* toastFallback) {
    auto cfg = LayerBackgroundManager::get().getConfig(m_selectedKey);
    fn(cfg);
    LayerBackgroundManager::get().saveConfig(m_selectedKey, cfg);
    if (toastKey) {
        PaimonNotify::create(tr(toastKey, toastFallback), NotificationIcon::Success)->show();
    }
    refreshAll();
}

void PaiConfigLayer::refreshAll() {
    auto const cfg = currentConfig();

// Level Info may override this background with a thumbnail.
    bool blocked = false;
    if (m_selectedKey == "levelinfo") {
        auto const style = Mod::get()->getSettingValue<std::string>("levelinfo-background-style");
        blocked = (style != "normal");
    }
    if (m_blockedOverlay) m_blockedOverlay->setVisible(blocked);

    if (m_darkToggle) m_darkToggle->toggle(cfg.darkMode);
    if (m_darkSlider) m_darkSlider->setValue(cfg.darkIntensity);
    if (m_darkValueLabel) m_darkValueLabel->setString(percentText(cfg.darkIntensity).c_str());

    if (m_levelIdInput) {
        m_levelIdInput->setString(cfg.levelId > 0 ? std::to_string(cfg.levelId) : "");
    }

    bool const isMenu = (m_selectedKey == "menu");
    if (m_adaptiveToggle) {
        m_adaptiveToggle->toggle(Mod::get()->getSavedValue<bool>("bg-adaptive-colors", false));
    }
    m_showAdaptive = isMenu;
    m_showVideo = (cfg.type == "video");
    m_showModuleWarn = !paimon::modules::isEnabled("paimbnails.backgrounds.global");
    relayoutControls();

    auto const& filters = activeFilterList();
    m_filterIndex = 0;
    for (int i = 0; i < static_cast<int>(filters.size()); ++i) {
        if (filters[i].first == cfg.shader) { m_filterIndex = i; break; }
    }
    updateFilterLabels();

    float const filterIntensity = Mod::get()->getSavedValue<float>("layerbg-shader-intensity", 0.5f);
    if (m_filterSlider) m_filterSlider->setValue(filterIntensity);
    if (m_filterValueLabel) m_filterValueLabel->setString(percentText(filterIntensity).c_str());

    for (auto const& [type, btn] : m_sourceButtons) {
        if (!btn) continue;
        bool const active = !type.empty() && type == cfg.type;
        if (auto* spr = typeinfo_cast<ButtonSprite*>(btn->getNormalImage())) {
            spr->setColor(active ? ccColor3B{255, 255, 255} : ccColor3B{175, 185, 205});
        }
    }

    refreshScreenList();
    refreshPreview();
}

void PaiConfigLayer::refreshScreenList() {
    auto const& screens = LayerBackgroundManager::LAYER_OPTIONS;
    for (int i = 0; i < static_cast<int>(screens.size()) && i < static_cast<int>(m_screenButtons.size()); ++i) {
        bool const selected = screens[i].first == m_selectedKey;
        auto* btn = m_screenButtons[i];
        setButtonTexture(btn, selected ? "GJ_button_02.png" : "GJ_button_01.png");
        if (btn) btn->setScale(1.f);
    }
}

void PaiConfigLayer::refreshPreview() {
    if (m_preview) {
        m_preview->setLayerKey(m_selectedKey);
        m_preview->refresh();
    }
    if (m_screenChipLabel) {
        m_screenChipLabel->setString(bgp::displayNameForLayer(m_selectedKey).c_str());
        m_screenChipLabel->setColor(screenColor(m_selectedKey));
    }

    if (!m_statusLabel) return;

    auto const cfg = currentConfig();
    std::string status = bgp::describeConfig(cfg);
    if (!cfg.shader.empty() && cfg.shader != "none" && cfg.type != "shader") {
        for (auto const& [key, label] : BG_FILTERS) {
            if (key == cfg.shader) { status += " + " + tr(label.c_str(), label.c_str()); break; }
        }
    }
    if (cfg.darkMode) status += " + " + tr("pai.config.background.dark", "Darken");

    if (!paimon::modules::isEnabled("paimbnails.backgrounds.global")) {
        status = tr("pai.config.module_off", "Backgrounds module is OFF");
        m_statusLabel->setColor({255, 140, 120});
    } else if (isLayerReference(cfg.type)) {
        m_statusLabel->setColor({150, 200, 255});
    } else {
        m_statusLabel->setColor({200, 215, 240});
    }

    m_statusLabel->setString(status.c_str());
    m_statusLabel->limitLabelWidth(m_statusLabel->getParent()
                                       ? m_statusLabel->getParent()->getContentSize().width - 14.f
                                       : 160.f,
                                   0.3f, 0.16f);
}

void PaiConfigLayer::updateFilterLabels() {
    if (!m_filterLabel) return;
    auto const cfg = currentConfig();
    bool const procedural = (cfg.type == "shader");
    auto const& filters = activeFilterList();
    if (m_filterIndex < 0 || m_filterIndex >= static_cast<int>(filters.size())) return;

    if (m_filterTitle) {
// Shader BG lists generated backgrounds; other tabs list filters.
        m_filterTitle->setString(procedural ? tr("pai.config.shader_bg_list", "Shader Background").c_str()
                                            : tr("pai.config.filter", "Filter").c_str());
    }

    auto const& key = filters[m_filterIndex].second;
    m_filterLabel->setString(tr(key.c_str(), key.c_str()).c_str());
    m_filterLabel->limitLabelWidth(
        m_filterLabel->getParent() ? m_filterLabel->getParent()->getContentSize().width - 44.f : 120.f,
        0.32f, 0.16f);

    bool const applies = procedural || (cfg.type != "default");
    bool const isNone = (!procedural && m_filterIndex == 0);
    if (!applies)      m_filterLabel->setColor({120, 125, 140});
    else if (isNone)   m_filterLabel->setColor({170, 180, 200});
    else               m_filterLabel->setColor({120, 255, 150});
}

bool PaiConfigLayer::isControlRowEnabled(CCNode* row) const {
    if (row == m_moduleWarnRow.data()) return m_showModuleWarn;
    if (row == m_adaptiveRow.data())   return m_showAdaptive;
    if (row == m_videoRow.data())      return m_showVideo;
    return true;
}

void PaiConfigLayer::relayoutControls() {
    if (!m_controlsScroll || !m_controlsScroll->m_contentLayer) return;

    constexpr float kGap = 4.f;
    auto* content = m_controlsScroll->m_contentLayer;

// The scroll layer hides off-screen children with setVisible(), so a row that
// the config does not want has to leave the content layer entirely.
    std::string signature;
    float total = 0.f;
    std::vector<CCNode*> active;
    int z = 0;
    for (auto const& row : m_controlRows) {
        auto* rowNode = row.data();
        if (!rowNode) continue;
        bool const enabled = isControlRowEnabled(rowNode);
        signature += enabled ? '1' : '0';
        if (!enabled) {
            if (rowNode->getParent()) rowNode->removeFromParentAndCleanup(false);
            continue;
        }
        if (!rowNode->getParent()) content->addChild(rowNode, z);
        rowNode->setZOrder(z);
        rowNode->setVisible(true);
        active.push_back(rowNode);
        total += rowNode->getContentSize().height + kGap;
        ++z;
    }
    if (!active.empty()) total -= kGap;

    float const viewH = m_controlsScroll->getContentSize().height;
    float const contentH = std::max(viewH, total + 4.f);
    content->setContentSize({content->getContentSize().width, contentH});

    float y = contentH - 2.f;
    for (auto* rowNode : active) {
        float const h = rowNode->getContentSize().height;
        y -= h;
        rowNode->setPositionY(y);
        y -= kGap;
    }

    if (m_controlsHint) m_controlsHint->setVisible(contentH > viewH + 1.f);

// Reset scroll only when the active rows change; sliders should not jump.
    if (signature != m_controlsSignature) {
        m_controlsSignature = signature;
        m_controlsScroll->moveToTop();
        return;
    }
// Same rows: keep the scroll where it was, but re-run the scroll layer's
// culling so the rows that just moved get the right visibility.
    float const minY = std::min(0.f, viewH - contentH);
    content->setPositionY(std::clamp(content->getPositionY(), minY, 0.f));
}


void PaiConfigLayer::onPickImage() {
    WeakRef<PaiConfigLayer> self = this;
    std::string const key = m_selectedKey;
    pt::pickImage([self, key](Result<std::optional<std::filesystem::path>> result) {
        auto layer = self.lock();
        if (!layer) return;
        auto pathOpt = std::move(result).unwrapOr(std::nullopt);
        if (!pathOpt || pathOpt->empty()) return;

        auto imported = paimon::assets::importToBucket(*pathOpt, "background_" + key,
                                                       paimon::assets::Kind::Image);
        if (!imported.success || imported.path.empty()) {
            PaimonNotify::create("Failed to import image", NotificationIcon::Error)->show();
            return;
        }
        auto cfg = LayerBackgroundManager::get().getConfig(key);
        cfg.type = "custom";
        cfg.customPath = paimon::assets::normalizePathString(imported.path);
        LayerBackgroundManager::get().saveConfig(key, cfg);
        PaimonNotify::create(tr("pai.config.notify.custom_image_set", "Custom image set!"),
                             NotificationIcon::Success)->show();
        layer->refreshAll();
    });
}

void PaiConfigLayer::onPickVideo() {
    WeakRef<PaiConfigLayer> self = this;
    std::string const key = m_selectedKey;
    pt::pickVideo([self, key](Result<std::optional<std::filesystem::path>> result) {
        auto layer = self.lock();
        if (!layer) return;
        auto pathOpt = std::move(result).unwrapOr(std::nullopt);
        if (!pathOpt || pathOpt->empty()) return;

        auto imported = paimon::assets::importToBucket(*pathOpt, "background_" + key,
                                                       paimon::assets::Kind::Video);
        if (!imported.success || imported.path.empty()) {
            PaimonNotify::create("Failed to import video", NotificationIcon::Error)->show();
            return;
        }
        auto const pathStr = paimon::assets::normalizePathString(imported.path);

// Release the previous player before starting a new video.
        auto const oldCfg = LayerBackgroundManager::get().getConfig(key);
        if (oldCfg.type == "video" && !oldCfg.customPath.empty() && oldCfg.customPath != pathStr) {
            LayerBackgroundManager::get().forceReleaseSharedVideoByPath(oldCfg.customPath);
        }

        auto cfg = oldCfg;
        cfg.type = "video";
        cfg.customPath = pathStr;
        LayerBackgroundManager::get().saveConfig(key, cfg);
        PaimonNotify::create(tr("pai.config.notify.video_set", "Video background set!"),
                             NotificationIcon::Success)->show();
        layer->refreshAll();
    });
}

void PaiConfigLayer::onUseShaderBg() {
    mutateConfig([](LayerBgConfig& cfg) {
        cfg.type = "shader";
        bool known = false;
        for (auto const& [key, label] : PROCEDURAL_BGS) {
            if (key == cfg.shader) { known = true; break; }
        }
        if (!known) cfg.shader = PROCEDURAL_BGS.front().first;
    }, "pai.config.notify.shader_bg_set", "Shader background set!");
}

void PaiConfigLayer::onUseRandom() {
    mutateConfig([](LayerBgConfig& cfg) { cfg.type = "random"; },
                 "pai.config.notify.random_set", "Random background set!");
}

void PaiConfigLayer::onUseDynamicShader() {
// Dynamic mode chooses a procedural background on each press.
    std::vector<std::string> names;
    names.reserve(PROCEDURAL_BGS.size());
    for (auto const& [key, label] : PROCEDURAL_BGS) names.push_back(key);
    if (names.empty()) return;

    auto const picked = geode::utils::random::choice(names);
    mutateConfig([&picked](LayerBgConfig& cfg) {
        cfg.type = "shader";
        cfg.shader = picked;
    }, "pai.config.notify.shader_bg_set", "Shader background set!");
}

void PaiConfigLayer::onUseLevelId() {
    if (!m_levelIdInput) return;
    auto const idStr = m_levelIdInput->getString();
    if (idStr.empty()) {
        PaimonNotify::create(tr("pai.config.notify.invalid_id", "Invalid ID"), NotificationIcon::Error)->show();
        return;
    }
    auto res = geode::utils::numFromString<int>(idStr);
    if (!res || res.unwrap() <= 0) {
        PaimonNotify::create(tr("pai.config.notify.invalid_id", "Invalid ID"), NotificationIcon::Error)->show();
        return;
    }
    int const levelId = res.unwrap();
    mutateConfig([levelId](LayerBgConfig& cfg) {
        cfg.type = "id";
        cfg.levelId = levelId;
    }, "pai.config.notify.level_id_set", "Level ID set!");
}

void PaiConfigLayer::onUseSameAs() {
    std::string const key = m_selectedKey;
    WeakRef<PaiConfigLayer> self = this;
    auto* popup = SameAsPickerPopup::create(key, [self, key](std::string const& picked) {
        auto cfg = LayerBackgroundManager::get().getConfig(key);
        cfg.type = picked;
        LayerBackgroundManager::get().saveConfig(key, cfg);
        PaimonNotify::create(
            (tr("pai.config.notify.same_as_prefix", "Using same bg as ") + bgp::displayNameForLayer(picked) + "!").c_str(),
            NotificationIcon::Success)->show();
        if (auto layer = self.lock()) layer->refreshAll();
    });
    if (popup) popup->show();
}

void PaiConfigLayer::onUseDefault() {
    auto const oldCfg = currentConfig();
    if (oldCfg.type == "video" && !oldCfg.customPath.empty()) {
        LayerBackgroundManager::get().forceReleaseSharedVideoByPath(oldCfg.customPath);
    }
    mutateConfig([](LayerBgConfig& cfg) { cfg = LayerBgConfig{}; },
                 "pai.config.notify.reverted_default", "Reverted to default!");
}

void PaiConfigLayer::onVideoSettings() {
    if (auto* popup = VideoSettingsPopup::create()) popup->show();
}

void PaiConfigLayer::onToggleDark(bool on) {
    mutateConfig([on](LayerBgConfig& cfg) { cfg.darkMode = on; });
}

void PaiConfigLayer::onDarkSlider(CCObject*) {
    if (!m_darkSlider) return;
    float const value = m_darkSlider->getValue();
    auto cfg = currentConfig();
    cfg.darkMode = true;
    cfg.darkIntensity = value;
    LayerBackgroundManager::get().saveConfig(m_selectedKey, cfg);
    if (m_darkToggle) m_darkToggle->toggle(true);
    if (m_darkValueLabel) m_darkValueLabel->setString(percentText(value).c_str());
    refreshPreview();
}

void PaiConfigLayer::onFilterStep(int delta) {
    auto const& filters = activeFilterList();
    if (filters.empty()) return;
    int const count = static_cast<int>(filters.size());
    m_filterIndex = ((m_filterIndex + delta) % count + count) % count;

    auto cfg = currentConfig();
    cfg.shader = filters[m_filterIndex].first;
    LayerBackgroundManager::get().saveConfig(m_selectedKey, cfg);
    updateFilterLabels();
    refreshPreview();
}

void PaiConfigLayer::onFilterSlider(CCObject*) {
    if (!m_filterSlider) return;
    float const value = std::max(0.1f, m_filterSlider->getValue());
    Mod::get()->setSavedValue<float>("layerbg-shader-intensity", value);
    if (m_filterValueLabel) m_filterValueLabel->setString(percentText(value).c_str());
    refreshPreview();
}

void PaiConfigLayer::onToggleAdaptive(bool on) {
    Mod::get()->setSavedValue("bg-adaptive-colors", on);
    (void)Mod::get()->saveData();
}

void PaiConfigLayer::onToggleMock() {
    if (!m_preview) return;
    bool const show = !m_preview->showsMock();
    m_preview->setShowMock(show);
    Mod::get()->setSavedValue<bool>("paiconfig-preview-mock", show);
}

void PaiConfigLayer::onExpandPreview() {
    auto const win = CCDirector::get()->getWinSize();

    auto* overlay = CCLayerColor::create({0, 0, 0, 225});
    overlay->setContentSize(win);
    overlay->setID("paimon-config-preview-overlay"_spr);
    overlay->setTouchEnabled(true);
    this->addChild(overlay, 500);

    auto* big = bgp::LayerPreviewNode::create({win.width - 40.f, win.height - 60.f}, m_selectedKey);
    if (big) {
        big->setShowMock(m_preview ? m_preview->showsMock() : true);
        big->setPosition({win.width / 2.f, win.height / 2.f + 8.f});
        overlay->addChild(big, 1);
    }

    if (auto* caption = gdLabel(
            (bgp::displayNameForLayer(m_selectedKey) + " - " + bgp::describeConfig(currentConfig())).c_str(),
            "bigFont.fnt", win.width - 60.f, 0.42f, {210, 225, 250})) {
        caption->setPosition({win.width / 2.f, 22.f});
        overlay->addChild(caption, 2);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    overlay->addChild(menu, 3);

    auto* closeSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_closeBtn_001.png");
    if (closeSpr) {
        closeSpr->setScale(0.7f);
        if (auto* close = CCMenuItemExt::createSpriteExtra(closeSpr,
                [overlay](CCMenuItemSpriteExtra*) { overlay->removeFromParentAndCleanup(true); })) {
            close->setPosition({26.f, win.height - 24.f});
            menu->addChild(close);
        }
    }

    paimon::fluid::revealNode(overlay, {.fadeDuration = 0.16f});
}

void PaiConfigLayer::onCopyToAllScreens() {
    auto const source = currentConfig();
    std::string const from = m_selectedKey;
    WeakRef<PaiConfigLayer> self = this;

    PopupManager::get().quickPopup(
        tr("pai.config.copy_all", "Copy to all screens"),
        tr("pai.config.copy_all_confirm",
           "Every screen will use the <cy>same background</c> as this one.\n"
           "This overwrites their current settings."),
        tr("general.cancel", "Cancel"),
        tr("pai.config.copy_all_btn", "Copy"),
        [self, source, from](FLAlertLayer*, bool confirmed) {
            if (!confirmed) return;
            for (auto const& [key, name] : LayerBackgroundManager::LAYER_OPTIONS) {
                if (key == from) continue;
                LayerBackgroundManager::get().saveConfig(key, source);
            }
            PaimonNotify::create(tr("pai.config.notify.copied_all", "Copied to every screen!"),
                                 NotificationIcon::Success)->show();
            if (auto layer = self.lock()) layer->refreshAll();
        }
    ).showInstant();
}

void PaiConfigLayer::onApplyAndRestart() {
    TransitionManager::get().replaceScene(MenuLayer::scene(false));
}

void PaiConfigLayer::onResetScreen() {
    onUseDefault();
}


void PaiConfigLayer::buildProfileTab() {
    auto const win = CCDirector::get()->getWinSize();
    float const cx = win.width / 2.f;
    float const top = win.height - C::CONTENT_TOP;
    float const bottom = C::CONTENT_BOTTOM;
    float const midY = (top + bottom) / 2.f;

    auto* page = CCNode::create();
    page->setContentSize(win);
    page->setAnchorPoint({0.f, 0.f});
    page->setVisible(false);
    this->addChild(page, 10);
    m_tabPages.push_back(page);

    CCSize const cardSize{std::min(400.f, win.width - 40.f), std::min(180.f, top - bottom)};
    auto* card = makeCardWindow({{cx - cardSize.width / 2.f, midY - cardSize.height / 2.f}, cardSize},
                                tr("pai.config.profile.title", "Profile Picture").c_str());
    page->addChild(card, 1);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    card->addChild(menu, 5);

    float const btnX = cardSize.width * 0.28f;
    float const startY = cardSize.height - 46.f;
    struct ProfileAction {
        char const* key;
        char const* fallback;
        char const* sprite;
        std::function<void()> action;
    };
    std::vector<ProfileAction> const actions = {
        {"pai.config.profile.set_image", "Set Image", "GJ_button_02.png", [this] { onProfileImage(); }},
        {"pai.config.profile.clear_image", "Clear Image", "GJ_button_06.png", [this] { onProfileClear(); }},
        {"pai.config.profile.photo_shape", "Photo Shape", "GJ_button_03.png", [this] { onProfileShape(); }},
    };
    for (int i = 0; i < static_cast<int>(actions.size()); ++i) {
        if (auto* btn = gdFixedButton(tr(actions[i].key, actions[i].fallback).c_str(), actions[i].sprite,
                                      std::min(130.f, cardSize.width * 0.42f), 26.f, 0.5f,
                                      actions[i].action)) {
            btn->setPosition({btnX, startY - i * 34.f});
            menu->addChild(btn);
        }
    }

    float const previewX = cardSize.width * 0.74f;
    float const previewY = cardSize.height / 2.f - 6.f;
    if (auto* frame = paimon::SpriteHelper::createDarkPanel(94.f, 94.f, 110, 5.f)) {
        frame->setAnchorPoint({0.5f, 0.5f});
        frame->setPosition({previewX, previewY});
        card->addChild(frame, 1);
    }

    m_profilePreview = CCNode::create();
    m_profilePreview->setContentSize({86.f, 86.f});
    m_profilePreview->setAnchorPoint({0.5f, 0.5f});
    m_profilePreview->setPosition({previewX, previewY});
    card->addChild(m_profilePreview, 4);
}

void PaiConfigLayer::rebuildProfilePreview() {
    if (!m_profilePreview) return;
    m_profilePreview->removeAllChildren();
    ++m_profilePreviewGen;

    float const thumbSize = C::PROFILE_THUMB_SIZE;
    auto const size = m_profilePreview->getContentSize();
    float const midX = size.width / 2.f;
    float const midY = size.height / 2.f;

    auto const cfg = ProfilePicCustomizer::get().getConfig();

    if (cfg.onlyIconMode) {
        if (auto* container = paimon::profile_pic::composeProfilePicture(nullptr, thumbSize, cfg)) {
            container->setPosition({midX, midY});
            m_profilePreview->addChild(container);
        }
        return;
    }

    auto const photo = paimon::profile_pic::resolveProfilePhoto(cfg);
    using PhotoKind = paimon::profile_pic::ResolvedProfilePhoto::Kind;

    if (photo.kind == PhotoKind::None) {
        if (auto* lbl = gdLabel(tr("pai.config.profile.no_image", "No\nImage").c_str(), "bigFont.fnt",
                                size.width - 8.f, 0.32f, {170, 180, 200})) {
            lbl->setAlignment(kCCTextAlignmentCenter);
            lbl->setPosition({midX, midY});
            m_profilePreview->addChild(lbl);
        }
        return;
    }

    if (photo.kind != PhotoKind::StaticFile) {
        if (auto* imageNode = paimon::profile_pic::createResolvedPhotoNode(photo)) {
            if (auto* container = paimon::profile_pic::composeProfilePicture(imageNode, thumbSize, cfg)) {
                container->setPosition({midX, midY});
                m_profilePreview->addChild(container);
                paimon::fluid::revealNode(container, {.fadeDuration = 0.22f});
            }
            return;
        }
        if (photo.path.empty()) return;
    }

    int const gen = m_profilePreviewGen;
    Ref<PaiConfigLayer> self = this;
    auto const cfgCopy = cfg;
    paimon::asyncimg::loadStaticSprite(std::filesystem::path(photo.path), 16,
        [self, gen, cfgCopy, thumbSize](CCSprite* sprite) {
            auto* layer = self.data();
            if (!layer || gen != layer->m_profilePreviewGen) return;
            auto* preview = layer->m_profilePreview;
            if (!preview) return;
            preview->removeAllChildren();

            auto const size = preview->getContentSize();
            if (!sprite) {
                if (auto* err = CCLabelBMFont::create(tr("general.error", "Error").c_str(), "bigFont.fnt")) {
                    err->setScale(0.3f);
                    err->setColor({255, 90, 90});
                    err->setPosition({size.width / 2.f, size.height / 2.f});
                    preview->addChild(err);
                }
                return;
            }

            if (auto* container = paimon::profile_pic::composeProfilePicture(sprite, thumbSize, cfgCopy)) {
                container->setPosition({size.width / 2.f, size.height / 2.f});
                preview->addChild(container);
                paimon::fluid::revealNode(container, {.fadeDuration = 0.22f});
            }
        });
}

void PaiConfigLayer::onProfileImage() {
    WeakRef<PaiConfigLayer> self = this;
    pt::pickImage([self](Result<std::optional<std::filesystem::path>> result) {
        auto layer = self.lock();
        if (!layer) return;
        auto pathOpt = std::move(result).unwrapOr(std::nullopt);
        if (!pathOpt || pathOpt->empty()) return;

        auto imported = paimon::assets::importToBucket(*pathOpt, "profile_picture",
                                                       paimon::assets::Kind::Image);
        if (!imported.success || imported.path.empty()) {
            PaimonNotify::create("Failed to import image", NotificationIcon::Error)->show();
            return;
        }
        Mod::get()->setSavedValue<std::string>("profile-bg-type", "custom");
        Mod::get()->setSavedValue<std::string>("profile-bg-path",
                                               paimon::assets::normalizePathString(imported.path));
        (void)Mod::get()->saveData();
        PaimonNotify::create(tr("pai.config.notify.profile_image_set", "Profile image set!"),
                             NotificationIcon::Success)->show();
        layer->rebuildProfilePreview();
    });
}

void PaiConfigLayer::onProfileClear() {
    Mod::get()->setSavedValue<std::string>("profile-bg-type", "none");
    Mod::get()->setSavedValue<std::string>("profile-bg-path", "");
    (void)Mod::get()->saveData();
    PaimonNotify::create(tr("pai.config.notify.profile_image_cleared", "Profile image cleared!"),
                         NotificationIcon::Success)->show();
    rebuildProfilePreview();
}

void PaiConfigLayer::onProfileShape() {
    if (auto* popup = ProfilePicEditorPopup::create()) popup->show();
}


void PaiConfigLayer::buildExtrasTab() {
    auto const win = CCDirector::get()->getWinSize();
    float const cx = win.width / 2.f;
    float const top = win.height - C::CONTENT_TOP;
    float const bottom = C::CONTENT_BOTTOM;
    float const midY = (top + bottom) / 2.f;

    auto* page = CCNode::create();
    page->setContentSize(win);
    page->setAnchorPoint({0.f, 0.f});
    page->setVisible(false);
    this->addChild(page, 10);
    m_tabPages.push_back(page);

    CCSize const cardSize{std::min(320.f, win.width - 40.f), std::min(190.f, top - bottom)};
    auto* card = makeCardWindow({{cx - cardSize.width / 2.f, midY - cardSize.height / 2.f}, cardSize},
                                tr("pai.config.extras.title", "Extras").c_str());
    page->addChild(card, 1);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    card->addChild(menu, 5);

    struct ExtraAction {
        char const* key;
        char const* fallback;
        char const* sprite;
        char const* infoTitleKey;
        char const* infoTitle;
        char const* infoBodyKey;
        char const* infoBody;
        std::function<void()> action;
    };
    std::vector<ExtraAction> const actions = {
        {"pai.config.extras.pet_config", "Pet Config", "GJ_button_03.png",
         "pai.config.extras.pet_info.title", "Pet",
         "pai.config.extras.pet_info.body", "A cute pet follows your cursor.\nThis feature is in <cr>BETA</c>.",
         [] { if (auto* p = PetConfigPopup::create()) p->show(); }},
        {"pai.config.extras.custom_cursor", "Custom Cursor", "GJ_button_02.png",
         "pai.config.extras.custom_cursor_info.title", "Custom Cursor",
         "pai.config.extras.custom_cursor_info.body", "Open the full custom cursor editor.",
         [] { if (auto* p = CursorConfigPopup::create()) p->show(); }},
        {"pai.config.extras.transitions", "Transitions", "GJ_button_04.png",
         "pai.config.extras.transitions_info.title", "Transitions",
         "pai.config.extras.transitions_info.body", "Configure custom scene transition effects.",
         [] { if (auto* p = TransitionConfigPopup::create()) p->show(); }},
        {"pai.config.extras.clear_cache", "Clear All Cache", "GJ_button_06.png",
         "pai.config.extras.clear_cache_info.title", "Clear Cache",
         "pai.config.extras.clear_cache_info.body", "<cr>Deletes ALL cached data.</c>",
         [this] { onClearAllCache(); }},
    };

    float const startY = cardSize.height - 44.f;
    for (int i = 0; i < static_cast<int>(actions.size()); ++i) {
        float const y = startY - i * 34.f;
        if (auto* btn = gdFixedButton(tr(actions[i].key, actions[i].fallback).c_str(), actions[i].sprite,
                                      std::min(170.f, cardSize.width * 0.58f), 26.f, 0.5f,
                                      actions[i].action)) {
            btn->setPosition({cardSize.width / 2.f - 14.f, y});
            menu->addChild(btn);
        }
        if (auto* info = PaimonInfo::createInfoBtn(
                tr(actions[i].infoTitleKey, actions[i].infoTitle).c_str(),
                tr(actions[i].infoBodyKey, actions[i].infoBody).c_str(), this, 0.5f)) {
            info->setPosition({cardSize.width - 20.f, y});
            menu->addChild(info);
        }
    }
}

void PaiConfigLayer::onClearAllCache() {
    WeakRef<PaiConfigLayer> self = this;
    PopupManager::get().quickPopup(
        tr("pai.config.clear_cache.title", "Clear All Cache"),
        tr("pai.config.clear_cache.message",
           "This will <cr>delete all cached data</c>:\n"
           "thumbnails, profile images, profile music,\n"
           "GIFs, and profile background settings.\n\n"
           "Are you sure?"),
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
            auto const gifCacheDir = paimon::quality::cacheDir() / "gifs";
            if (std::filesystem::exists(gifCacheDir, ec)) std::filesystem::remove_all(gifCacheDir, ec);

            Mod::get()->setSavedValue<std::string>("profile-bg-type", "none");
            Mod::get()->setSavedValue<std::string>("profile-bg-path", "");
            (void)Mod::get()->saveData();

            paimon::emotes::EmoteCache::get().clearAll();
            paimon::emotes::EmoteService::get().clearCatalog();
            layer->rebuildProfilePreview();
            log::info("[PaiConfigLayer] All caches cleared by user");
            PaimonNotify::create(tr("pai.config.notify.cache_cleared", "All caches cleared!"),
                                 NotificationIcon::Success)->show();
        }
    ).showInstant();
}
