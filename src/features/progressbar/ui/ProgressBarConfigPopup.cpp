#include "../../../utils/FilesystemPath.hpp"
#include "ProgressBarConfigPopup.hpp"
#include "../services/ProgressBarManager.hpp"
#include "ProgressBarEditOverlay.hpp"
#include "../../fonts/ui/FontPickerPopup.hpp"
#include "../../fonts/FontTag.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include "../../../ui/PaimonUI.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <Geode/ui/ColorPickPopup.hpp>
#include <Geode/utils/cocos.hpp>
#include <fmt/format.h>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace {

geode::NineSlice* addInset(CCNode* parent, CCSize size, CCPoint pos, int z = -1, GLubyte opacity = 90) {
    auto* n = paimon::ui::makeInset(size, opacity);
    if (n && parent) {
        n->setPosition(pos);
        parent->addChild(n, z);
    }
    return n;
}

CCSprite* makeColorSwatch(ccColor3B color) {
    auto* chip = CCSprite::createWithSpriteFrameName("GJ_colorBtn_001.png");
    chip->setColor(color);
    chip->setScale(18.f / std::max(1.f, chip->getContentSize().height));
    return chip;
}

float readSliderRange(Slider* s, float minV, float maxV) {
    if (!s || !s->getThumb()) return minV;
    return minV + s->getThumb()->getValue() * (maxV - minV);
}

float normFromValue(float val, float minV, float maxV) {
    if (maxV <= minV) return 0.f;
    return std::clamp((val - minV) / (maxV - minV), 0.f, 1.f);
}

char const* styleName(BarStyle s) {
    switch (s) {
        case BarStyle::VanillaGD:   return "Native GD";
        case BarStyle::Sleek:       return "Sleek Pill";
        case BarStyle::Classic:     return "Classic GD";
        case BarStyle::Minimal:     return "Minimal";
        case BarStyle::NeonGlow:    return "Neon Glow";
        case BarStyle::Segmented:   return "Segmented";
        case BarStyle::Glass:       return "Frosted Glass";
        case BarStyle::Striped:     return "Striped";
        case BarStyle::CustomImage: return "Custom Image";
    }
    return "Native GD";
}

char const* colorModeName(BarColorMode m) {
    switch (m) {
        case BarColorMode::Solid:            return "Solid";
        case BarColorMode::Pulse:            return "Pulse";
        case BarColorMode::Rainbow:          return "Rainbow";
        case BarColorMode::Gradient2:        return "2-Color Grad";
        case BarColorMode::Gradient3:        return "3-Color Grad";
        case BarColorMode::ProgressReactive: return "Reactive %";
    }
    return "Solid";
}

char const* labelFmtName(BarLabelFormat f) {
    switch (f) {
        case BarLabelFormat::Percent:     return "Percent";
        case BarLabelFormat::PercentBest: return "% + Best";
        case BarLabelFormat::RunSegment:  return "Start->Cur";
        case BarLabelFormat::Compact:     return "No % Sign";
        case BarLabelFormat::AutoTime:    return "Auto Time";
    }
    return "Percent";
}

char const* placementName(BarLabelPlacement p) {
    switch (p) {
        case BarLabelPlacement::InsideCenter:  return "Inside";
        case BarLabelPlacement::RightOfBar:    return "Right";
        case BarLabelPlacement::LeftOfBar:     return "Left";
        case BarLabelPlacement::AboveBar:      return "Above";
        case BarLabelPlacement::BelowBar:      return "Below";
        case BarLabelPlacement::FollowTip:     return "Follow Tip";
        case BarLabelPlacement::CustomFreePos: return "Free Pos";
    }
    return "Right";
}

void setBtnLabel(CCMenuItemSpriteExtra* btn, char const* text) {
    if (!btn) return;
    if (auto* spr = typeinfo_cast<ButtonSprite*>(btn->getNormalImage())) {
        spr->setString(text);
    }
}

std::string shortPath(std::string const& p) {
    if (p.empty()) return "(none)";
    std::filesystem::path fp(p);
    auto name = paimon::paths::toUtf8(fp.filename());
    if (name.size() > 16) name = name.substr(0, 14) + "..";
    return name;
}

template<class Cb>
void openColorPicker(ccColor3B current, Cb&& cb) {
    auto* popup = geode::ColorPickPopup::create(
        ccc4(current.r, current.g, current.b, 255));
    if (!popup) return;
    popup->setCallback([cb = std::forward<Cb>(cb)](ccColor4B const& col) {
        cb(ccColor3B{col.r, col.g, col.b});
    });
    popup->show();
}

} // namespace

ProgressBarConfigPopup* ProgressBarConfigPopup::create() {
    auto* ret = new ProgressBarConfigPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ProgressBarConfigPopup::init() {
    if (!PaimonPopup::init(490.f, 320.f)) return false;
    this->setTitle("Progress Bar Engine");
    this->addInfoButton("Custom Progress Bar",
        "Complete progress bar engine with 9 visual styles, 6 color modes,\n"
        "live checkpoints & PB markers, StartPos run tracking, and Free Edit Mode.");
    this->setMouseEnabled(true);

    m_previewTel.bestPercent = 68.f;
    m_previewTel.startPosPercent = 0.f;
    m_previewTel.lastDeathPercent = 82.f;
    m_previewTel.checkpoints = {35.f, 54.f};

    auto content = m_mainLayer->getContentSize();
    for (int i = 0; i < 5; ++i) {
        auto* tab = CCNode::create();
        tab->setContentSize(content);
        tab->setVisible(i == 0);
        m_mainLayer->addChild(tab, 5);
        m_tabContainers[i] = tab;
    }

    buildPreviewStage();
    createTabButtons();
    buildPresetsTab();
    buildStyleTab();
    buildColorsTab();
    buildLabelTab();
    buildLayoutTab();
    syncAllControlsFromConfig();
    refreshPreviewTextures();

    this->scheduleUpdate();
    paimon::markDynamicPopup(this);
    return true;
}

void ProgressBarConfigPopup::onExit() {
    this->unscheduleUpdate();
    applyAndSave();
    Popup::onExit();
}

void ProgressBarConfigPopup::applyAndSave() {
    ProgressBarManager::get().sanitizeConfig();
    ProgressBarManager::get().saveConfig();
    refreshPreviewTextures();
}

void ProgressBarConfigPopup::refreshPreviewTextures() {
    auto const& cfg = ProgressBarManager::get().config();
    auto loadOne = [&](std::string const& path, std::string& loaded, Ref<CCTexture2D>& slot) {
        if (path == loaded) return;
        loaded = path;
        slot = nullptr;
        if (path.empty()) return;
        std::filesystem::path fsPath = paimon::assets::pathFromUtf8(path);
        std::error_code ec;
        if (!std::filesystem::exists(fsPath, ec)) return;
        auto img = ImageLoadHelper::loadStaticImage(fsPath, 8);
        if (img.success && img.texture) {
            slot = img.texture;
            img.texture->release();
        }
    };
    loadOne(cfg.fillTexturePath, m_loadedFillTexPath, m_previewFillTex);
    loadOne(cfg.bgTexturePath,   m_loadedBgTexPath,   m_previewBgTex);
}

void ProgressBarConfigPopup::buildPreviewStage() {
    auto content = m_mainLayer->getContentSize();
    float const stageW = content.width - 34.f;
    float const stageH = 56.f;

    auto* bg = addInset(m_mainLayer, {stageW, stageH}, {17.f, content.height - 88.f}, 1, 210);
    (void)bg;

    m_previewBar = CustomProgressBarNode::create();
    if (m_previewBar) {
        m_previewBar->setPosition({content.width * 0.5f - 10.f, content.height - 60.f});
        m_mainLayer->addChild(m_previewBar, 6);
    }

    m_previewInfoLbl = CCLabelBMFont::create("LIVE PREVIEW", "goldFont.fnt");
    m_previewInfoLbl->setScale(0.32f);
    m_previewInfoLbl->setAnchorPoint({0.f, 0.5f});
    m_previewInfoLbl->setPosition({26.f, content.height - 39.f});
    m_previewInfoLbl->setOpacity(190);
    m_mainLayer->addChild(m_previewInfoLbl, 7);

    auto* menu = CCMenu::create();
    menu->setPosition({0, 0});
    m_mainLayer->addChild(menu, 8);

    auto* playSpr = ButtonSprite::create("Pause", 46, true, "bigFont.fnt", "GJ_button_04.png", 16.f, 0.36f);
    m_previewPlayBtn = CCMenuItemSpriteExtra::create(
        playSpr, this, menu_selector(ProgressBarConfigPopup::onTogglePreviewPlay));
    m_previewPlayBtn->setPosition({content.width - 44.f, content.height - 60.f});
    menu->addChild(m_previewPlayBtn);
}

void ProgressBarConfigPopup::onTogglePreviewPlay(CCObject*) {
    m_previewPlaying = !m_previewPlaying;
    setBtnLabel(m_previewPlayBtn, m_previewPlaying ? "Pause" : "Play");
}

void ProgressBarConfigPopup::update(float dt) {
    m_previewTime += dt;
    if (m_previewPlaying) {
        m_previewProgress += dt * 22.f;
        if (m_previewProgress > 105.f) {
            m_previewProgress = 0.f;
            m_previewTel.newBestReached = false;
            m_previewTel.newBestFlashTimer = 0.f;
        }
    }
    float pct = std::clamp(m_previewProgress, 0.f, 100.f);
    auto const& cfg = ProgressBarManager::get().config();

    m_previewTel.rawPercent = pct;
    m_previewTel.smoothedPercent = pct;
    m_previewTel.elapsedSeconds = static_cast<double>(pct * 0.6f);
    if (pct > m_previewTel.bestPercent) {
        if (!m_previewTel.newBestReached) {
            m_previewTel.newBestReached = true;
            m_previewTel.newBestFlashTimer = 1.f;
        }
    }
    if (m_previewTel.newBestFlashTimer > 0.f) {
        m_previewTel.newBestFlashTimer = std::max(0.f, m_previewTel.newBestFlashTimer - dt * 1.8f);
    }

    if (m_previewBar) {
        // Preview keeps a clean fit inside the 56px banner while reflecting real style/colors/height.
        ProgressBarConfig previewCfg = cfg;
        previewCfg.useCustomLabelPosition = false;
        if (previewCfg.labelPlacement == BarLabelPlacement::CustomFreePos) {
            previewCfg.labelPlacement = BarLabelPlacement::RightOfBar;
        }
        previewCfg.barWidth = std::clamp(cfg.barWidth * cfg.scaleLength, 90.f, 290.f);
        previewCfg.barHeight = std::clamp(cfg.barHeight * cfg.scaleThickness, 5.f, 22.f);
        previewCfg.percentageOffsetX = std::clamp(cfg.percentageOffsetX, -40.f, 40.f);
        previewCfg.percentageOffsetY = std::clamp(cfg.percentageOffsetY, -14.f, 14.f);
        m_previewBar->setScale(1.f);
        m_previewBar->setRotation(0.f);
        m_previewBar->renderState(
            previewCfg, m_previewTel, m_previewTime,
            cfg.useFillTexture ? m_previewFillTex.data() : nullptr,
            cfg.useBgTexture ? m_previewBgTex.data() : nullptr
        );
    }

    if (m_previewInfoLbl) {
        m_previewInfoLbl->setString(cfg.enabled
            ? fmt::format("LIVE PREVIEW  ({})", styleName(cfg.style)).c_str()
            : "LIVE PREVIEW  (DISABLED IN PLAY)");
    }
}

void ProgressBarConfigPopup::createTabButtons() {
    auto content = m_mainLayer->getContentSize();
    float const topY = content.height - 104.f;
    float const cx = content.width * 0.5f;

    auto* menu = CCMenu::create();
    menu->setPosition({0, 0});
    m_mainLayer->addChild(menu, 10);

    struct Def { char const* label; int tag; };
    Def defs[] = {
        {"Presets", 0},
        {"Style",   1},
        {"Colors",  2},
        {"Label",   3},
        {"Layout",  4},
    };
    std::vector<CCMenuItem*> tabItems;
    for (auto const& d : defs) {
        auto* spr = ButtonSprite::create(d.label, 72, true, "bigFont.fnt", "GJ_button_04.png", 18.f, 0.38f);
        auto* btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(ProgressBarConfigPopup::onTabSwitch));
        btn->setTag(d.tag);
        menu->addChild(btn);
        m_tabs.push_back(btn);
        tabItems.push_back(btn);
    }
    paimon::ui::layoutButtonRow(tabItems, 18.f, content.width - 18.f, topY, 6.f, paimon::ui::RowAlign::Center);
    if (!m_tabs.empty()) onTabSwitch(m_tabs.front());
}

void ProgressBarConfigPopup::onTabSwitch(CCObject* sender) {
    auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    m_currentTab = btn->getTag();
    for (int i = 0; i < 5; ++i) {
        if (m_tabContainers[i]) m_tabContainers[i]->setVisible(i == m_currentTab);
    }
    for (auto* tab : m_tabs) {
        paimon::ui::setButtonSkin(tab,
            tab->getTag() == m_currentTab ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    }
}

void ProgressBarConfigPopup::buildPresetsTab() {
    auto* tab = m_tabContainers[0];
    auto content = m_mainLayer->getContentSize();
    float const cx = content.width * 0.5f;
    auto& cfg = ProgressBarManager::get().config();

    addInset(tab, {content.width - 34.f, 184.f}, {17.f, 14.f}, -1);

    auto* menu = CCMenu::create();
    menu->setPosition({0, 0});
    tab->addChild(menu, 2);

    // Top toggle strip inside tab
    auto addTopToggle = [&](char const* title, float x, bool val, CCMenuItemToggler*& out, SEL_MenuHandler cb) {
        out = CCMenuItemToggler::createWithStandardSprites(this, cb, 0.62f);
        out->setPosition({x, 182.f});
        out->toggle(val);
        menu->addChild(out);

        auto* lbl = CCLabelBMFont::create(title, "bigFont.fnt");
        lbl->setScale(0.38f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({x + 15.f, 182.f});
        tab->addChild(lbl);
    };
    addTopToggle("Enable Custom Bar", 36.f, cfg.enabled, m_enableToggle,
        menu_selector(ProgressBarConfigPopup::onEnableToggled));
    addTopToggle("Hide In Practice", 252.f, cfg.hideInPractice, m_hidePracticeToggle,
        menu_selector(ProgressBarConfigPopup::onHidePracticeToggled));

    auto* hdr = CCLabelBMFont::create("One-Click Presets", "goldFont.fnt");
    hdr->setScale(0.46f);
    hdr->setPosition({cx, 158.f});
    tab->addChild(hdr);

    // 4x2 preset grid
    m_presetBtns.clear();
    int const total = ProgressBarManager::presetCount();
    std::vector<CCMenuItem*> presetRows[2];
    for (int i = 0; i < total; ++i) {
        int row = i / 4;
        auto* spr = ButtonSprite::create(
            ProgressBarManager::presetName(i), 88, true, "bigFont.fnt",
            "GJ_button_04.png", 22.f, 0.34f);
        auto* btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(ProgressBarConfigPopup::onSelectPreset));
        btn->setTag(i);
        menu->addChild(btn);
        m_presetBtns.push_back(btn);
        if (row < 2) presetRows[row].push_back(btn);
    }
    for (int r = 0; r < 2; ++r) {
        paimon::ui::layoutButtonRow(presetRows[r], 24.f, content.width - 24.f, 126.f - r * 34.f, 6.f, paimon::ui::RowAlign::Center);
    }

    // Bottom action row
    std::vector<CCMenuItem*> actionItems;
    auto addAction = [&](char const* text, char const* bg, SEL_MenuHandler cb) {
        auto* spr = ButtonSprite::create(text, 108, true, "goldFont.fnt", bg, 22.f, 0.38f);
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, cb);
        menu->addChild(btn);
        actionItems.push_back(btn);
    };
    addAction("Free Edit Mode", "GJ_button_02.png", menu_selector(ProgressBarConfigPopup::onEnterFreeEditMode));
    addAction("Center Top",     "GJ_button_01.png", menu_selector(ProgressBarConfigPopup::onCenterPosition));
    addAction("Reset Defaults", "GJ_button_06.png", menu_selector(ProgressBarConfigPopup::onResetDefaults));
    paimon::ui::layoutButtonRow(actionItems, 24.f, content.width - 24.f, 38.f, 8.f, paimon::ui::RowAlign::Center);
}

void ProgressBarConfigPopup::buildStyleTab() {
    auto* tab = m_tabContainers[1];
    auto content = m_mainLayer->getContentSize();
    auto& cfg = ProgressBarManager::get().config();

    addInset(tab, {content.width - 34.f, 184.f}, {17.f, 14.f}, -1);

    auto* menu = CCMenu::create();
    menu->setPosition({0, 0});
    tab->addChild(menu, 2);

    // Left column: Style selector + 5 sliders
    auto* styleLbl = CCLabelBMFont::create("Style", "bigFont.fnt");
    styleLbl->setScale(0.38f);
    styleLbl->setAnchorPoint({0.f, 0.5f});
    styleLbl->setPosition({30.f, 180.f});
    tab->addChild(styleLbl);

    auto* stSpr = ButtonSprite::create(styleName(cfg.style), 130, true, "goldFont.fnt",
        "GJ_button_01.png", 20.f, 0.40f);
    m_styleBtn = CCMenuItemSpriteExtra::create(stSpr, this,
        menu_selector(ProgressBarConfigPopup::onCycleStyle));
    m_styleBtn->setPosition({155.f, 180.f});
    menu->addChild(m_styleBtn);

    auto addLeftSlider = [&](char const* name, float y, Slider*& sl, CCLabelBMFont*& vl,
                             float val, float lo, float hi, SEL_MenuHandler cb, char const* fmtS) {
        auto* lbl = CCLabelBMFont::create(name, "bigFont.fnt");
        lbl->setScale(0.35f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({30.f, y});
        tab->addChild(lbl);

        sl = Slider::create(this, cb, 0.58f);
        sl->setPosition({146.f, y});
        sl->setValue(normFromValue(val, lo, hi));
        tab->addChild(sl);

        vl = CCLabelBMFont::create(fmt::format(fmt::runtime(fmtS), val).c_str(), "bigFont.fnt");
        vl->setScale(0.34f);
        vl->setAnchorPoint({0.f, 0.5f});
        vl->setPosition({222.f, y});
        tab->addChild(vl);
    };

    addLeftSlider("Width",     151.f, m_widthSlider,   m_widthLabel,   cfg.barWidth,     60.f, 420.f,
        menu_selector(ProgressBarConfigPopup::onWidthChanged), "{:.0f}");
    addLeftSlider("Height",    124.f, m_heightSlider,  m_heightLabel,  cfg.barHeight,     4.f,  32.f,
        menu_selector(ProgressBarConfigPopup::onHeightChanged), "{:.0f}");
    addLeftSlider("Border",     97.f, m_borderWSlider, m_borderWLabel, cfg.borderWidth,   0.f,   4.f,
        menu_selector(ProgressBarConfigPopup::onBorderWChanged), "{:.1f}");
    addLeftSlider("Roundness",  70.f, m_radiusSlider,  m_radiusLabel,  cfg.cornerRadius,  0.f,  16.f,
        menu_selector(ProgressBarConfigPopup::onRadiusChanged), "{:.1f}");
    addLeftSlider("Smooth",     43.f, m_smoothSlider,  m_smoothLabel,  cfg.smoothing,     0.f,   1.f,
        menu_selector(ProgressBarConfigPopup::onSmoothChanged), "{:.2f}");

    // Right column: 6 crisp engine toggles
    auto addRightToggle = [&](char const* name, float y, bool val, CCMenuItemToggler*& out, SEL_MenuHandler cb) {
        out = CCMenuItemToggler::createWithStandardSprites(this, cb, 0.55f);
        out->setPosition({292.f, y});
        out->toggle(val);
        menu->addChild(out);

        auto* lbl = CCLabelBMFont::create(name, "bigFont.fnt");
        lbl->setScale(0.35f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({306.f, y});
        tab->addChild(lbl);
    };
    addRightToggle("Leading Tip Glow",     180.f, cfg.tipGlow,         m_tipGlowToggle,      menu_selector(ProgressBarConfigPopup::onTipGlowToggled));
    addRightToggle("Best (PB) Marker",     152.f, cfg.showPbMarker,    m_pbMarkerToggle,     menu_selector(ProgressBarConfigPopup::onPbMarkerToggled));
    addRightToggle("Checkpoint Ticks",     124.f, cfg.showCheckpoints, m_cpMarkerToggle,     menu_selector(ProgressBarConfigPopup::onCpMarkerToggled));
    addRightToggle("25/50/75 Milestones",   96.f, cfg.showMilestones,  m_milestoneToggle,    menu_selector(ProgressBarConfigPopup::onMilestoneToggled));
    addRightToggle("Last Death Tick",       68.f, cfg.showDeathMarker, m_deathMarkerToggle,  menu_selector(ProgressBarConfigPopup::onDeathMarkerToggled));
    addRightToggle("New Best Gold Flash",   40.f, cfg.newBestFlash,    m_newBestFlashToggle, menu_selector(ProgressBarConfigPopup::onNewBestFlashToggled));
}

void ProgressBarConfigPopup::buildColorsTab() {
    auto* tab = m_tabContainers[2];
    auto content = m_mainLayer->getContentSize();
    auto& cfg = ProgressBarManager::get().config();

    addInset(tab, {content.width - 34.f, 184.f}, {17.f, 14.f}, -1);

    auto* menu = CCMenu::create();
    menu->setPosition({0, 0});
    tab->addChild(menu, 2);

    // Row 1: Fill Color Mode + Swatches 1, 2, 3
    {
        auto* lbl = CCLabelBMFont::create("Fill Mode", "bigFont.fnt");
        lbl->setScale(0.36f); lbl->setAnchorPoint({0.f, 0.5f}); lbl->setPosition({30.f, 178.f});
        tab->addChild(lbl);

        auto* mSpr = ButtonSprite::create(colorModeName(cfg.fillColorMode), 102, true, "bigFont.fnt", "GJ_button_04.png", 18.f, 0.36f);
        m_fillModeBtn = CCMenuItemSpriteExtra::create(mSpr, this, menu_selector(ProgressBarConfigPopup::onCycleFillMode));
        m_fillModeBtn->setPosition({146.f, 178.f});
        menu->addChild(m_fillModeBtn);

        auto addPickChip = [&](float x, ccColor3B col, CCSprite*& sw, char const* tagTxt, SEL_MenuHandler cb) {
            sw = makeColorSwatch(col);
            auto* btn = CCMenuItemSpriteExtra::create(sw, this, cb);
            btn->setPosition({x, 178.f});
            menu->addChild(btn);
            auto* t = CCLabelBMFont::create(tagTxt, "bigFont.fnt");
            t->setScale(0.26f);
            t->setPosition({x, 163.f});
            t->setOpacity(170);
            tab->addChild(t);
        };
        addPickChip(226.f, cfg.fillColor,  m_fillColor1Preview, "C1", menu_selector(ProgressBarConfigPopup::onPickFillColor1));
        addPickChip(260.f, cfg.fillColor2, m_fillColor2Preview, "C2", menu_selector(ProgressBarConfigPopup::onPickFillColor2));
        addPickChip(294.f, cfg.fillColor3, m_fillColor3Preview, "C3", menu_selector(ProgressBarConfigPopup::onPickFillColor3));

        // Shadow toggle on right
        m_shadowToggle = CCMenuItemToggler::createWithStandardSprites(this,
            menu_selector(ProgressBarConfigPopup::onShadowToggled), 0.52f);
        m_shadowToggle->setPosition({352.f, 178.f});
        m_shadowToggle->toggle(cfg.shadowEnabled);
        menu->addChild(m_shadowToggle);
        auto* sLbl = CCLabelBMFont::create("Shadow", "bigFont.fnt");
        sLbl->setScale(0.35f); sLbl->setAnchorPoint({0.f, 0.5f}); sLbl->setPosition({366.f, 178.f});
        tab->addChild(sLbl);
    }

    // Row 2: Track Mode + Track Color + Border Color
    {
        auto* lbl = CCLabelBMFont::create("Track Mode", "bigFont.fnt");
        lbl->setScale(0.36f); lbl->setAnchorPoint({0.f, 0.5f}); lbl->setPosition({30.f, 140.f});
        tab->addChild(lbl);

        auto* mSpr = ButtonSprite::create(colorModeName(cfg.bgColorMode), 102, true, "bigFont.fnt", "GJ_button_04.png", 18.f, 0.36f);
        m_bgModeBtn = CCMenuItemSpriteExtra::create(mSpr, this, menu_selector(ProgressBarConfigPopup::onCycleBgMode));
        m_bgModeBtn->setPosition({146.f, 140.f});
        menu->addChild(m_bgModeBtn);

        m_bgColorPreview = makeColorSwatch(cfg.bgColor);
        auto* bgBtn = CCMenuItemSpriteExtra::create(m_bgColorPreview, this, menu_selector(ProgressBarConfigPopup::onPickBgColor));
        bgBtn->setPosition({226.f, 140.f});
        menu->addChild(bgBtn);
        auto* bgT = CCLabelBMFont::create("Track", "bigFont.fnt");
        bgT->setScale(0.25f); bgT->setPosition({226.f, 125.f}); bgT->setOpacity(170);
        tab->addChild(bgT);

        m_borderColorPreview = makeColorSwatch(cfg.borderColor);
        auto* bdBtn = CCMenuItemSpriteExtra::create(m_borderColorPreview, this, menu_selector(ProgressBarConfigPopup::onPickBorderColor));
        bdBtn->setPosition({276.f, 140.f});
        menu->addChild(bdBtn);
        auto* bdT = CCLabelBMFont::create("Border", "bigFont.fnt");
        bdT->setScale(0.25f); bdT->setPosition({276.f, 125.f}); bdT->setOpacity(170);
        tab->addChild(bdT);
    }

    // Row 3: Opacity & Anim Speed sliders side by side
    {
        auto addMiniSlider = [&](char const* name, float xBase, Slider*& sl, CCLabelBMFont*& vl,
                                 float val, float lo, float hi, SEL_MenuHandler cb, char const* fmtS) {
            auto* lbl = CCLabelBMFont::create(name, "bigFont.fnt");
            lbl->setScale(0.34f); lbl->setAnchorPoint({0.f, 0.5f}); lbl->setPosition({xBase, 102.f});
            tab->addChild(lbl);
            sl = Slider::create(this, cb, 0.52f);
            sl->setPosition({xBase + 98.f, 102.f});
            sl->setValue(normFromValue(val, lo, hi));
            tab->addChild(sl);
            vl = CCLabelBMFont::create(fmt::format(fmt::runtime(fmtS), val).c_str(), "bigFont.fnt");
            vl->setScale(0.34f); vl->setAnchorPoint({0.f, 0.5f}); vl->setPosition({xBase + 162.f, 102.f});
            tab->addChild(vl);
        };
        addMiniSlider("Opacity", 30.f, m_opacitySlider, m_opacityLabel,
            static_cast<float>(cfg.opacity), 20.f, 255.f, menu_selector(ProgressBarConfigPopup::onOpacityChanged), "{:.0f}");
        addMiniSlider("Anim Spd", 252.f, m_animSpeedSlider, m_animSpeedLabel,
            cfg.colorAnimSpeed, 0.1f, 5.f, menu_selector(ProgressBarConfigPopup::onAnimSpeedChanged), "{:.1f}x");
    }

    // Row 4 & 5: Custom PNG / GIF Fill & Track Textures
    auto addTexRow = [&](char const* title, float y, CCMenuItemToggler*& tog, bool val,
                         std::string const& path, CCLabelBMFont*& pLbl,
                         SEL_MenuHandler togCb, SEL_MenuHandler pickCb, SEL_MenuHandler clrCb) {
        auto* lbl = CCLabelBMFont::create(title, "bigFont.fnt");
        lbl->setScale(0.35f); lbl->setAnchorPoint({0.f, 0.5f}); lbl->setPosition({30.f, y});
        tab->addChild(lbl);

        tog = CCMenuItemToggler::createWithStandardSprites(this, togCb, 0.50f);
        tog->setPosition({126.f, y});
        tog->toggle(val);
        menu->addChild(tog);

        auto* pickSpr = ButtonSprite::create("Image/GIF", 70, true, "goldFont.fnt", "GJ_button_04.png", 18.f, 0.34f);
        auto* pickBtn = CCMenuItemSpriteExtra::create(pickSpr, this, pickCb);
        menu->addChild(pickBtn);

        auto* clrSpr = ButtonSprite::create("X", 24, true, "bigFont.fnt", "GJ_button_06.png", 18.f, 0.34f);
        auto* clrBtn = CCMenuItemSpriteExtra::create(clrSpr, this, clrCb);
        menu->addChild(clrBtn);

        paimon::ui::layoutButtonRow({pickBtn, clrBtn}, 148.f, 266.f, y, 6.f, paimon::ui::RowAlign::Left);

        pLbl = CCLabelBMFont::create(shortPath(path).c_str(), "chatFont.fnt");
        pLbl->setScale(0.45f); pLbl->setAnchorPoint({0.f, 0.5f});
        pLbl->setPosition({clrBtn->getPositionX() + clrBtn->getScaledContentSize().width * 0.5f + 8.f, y});
        pLbl->setOpacity(185);
        tab->addChild(pLbl);
    };

    addTexRow("Fill Img", 64.f, m_useFillTexToggle, cfg.useFillTexture, cfg.fillTexturePath, m_fillTexPathLabel,
        menu_selector(ProgressBarConfigPopup::onUseFillTextureToggled),
        menu_selector(ProgressBarConfigPopup::onPickFillTexture),
        menu_selector(ProgressBarConfigPopup::onClearFillTexture));
    addTexRow("Track Img", 34.f, m_useBgTexToggle, cfg.useBgTexture, cfg.bgTexturePath, m_bgTexPathLabel,
        menu_selector(ProgressBarConfigPopup::onUseBgTextureToggled),
        menu_selector(ProgressBarConfigPopup::onPickBgTexture),
        menu_selector(ProgressBarConfigPopup::onClearBgTexture));
}

void ProgressBarConfigPopup::buildLabelTab() {
    auto* tab = m_tabContainers[3];
    auto content = m_mainLayer->getContentSize();
    auto& cfg = ProgressBarManager::get().config();

    addInset(tab, {content.width - 34.f, 184.f}, {17.f, 14.f}, -1);

    auto* menu = CCMenu::create();
    menu->setPosition({0, 0});
    tab->addChild(menu, 2);

    // Row 1: Show Label + Format + Decimals
    m_showPctToggle = CCMenuItemToggler::createWithStandardSprites(this,
        menu_selector(ProgressBarConfigPopup::onShowPctToggled), 0.54f);
    m_showPctToggle->setPosition({38.f, 178.f});
    m_showPctToggle->toggle(cfg.showPercentage);
    menu->addChild(m_showPctToggle);
    auto* shLbl = CCLabelBMFont::create("Show Label", "bigFont.fnt");
    shLbl->setScale(0.36f); shLbl->setAnchorPoint({0.f, 0.5f}); shLbl->setPosition({52.f, 178.f});
    tab->addChild(shLbl);

    auto* fmtSpr = ButtonSprite::create(labelFmtName(cfg.labelFormat), 82, true, "bigFont.fnt", "GJ_button_04.png", 18.f, 0.34f);
    m_labelFmtBtn = CCMenuItemSpriteExtra::create(fmtSpr, this, menu_selector(ProgressBarConfigPopup::onCycleLabelFmt));
    menu->addChild(m_labelFmtBtn);

    auto precText = fmt::format("{} Dec", cfg.decimalPrecision);
    auto* precSpr = ButtonSprite::create(precText.c_str(), 58, true, "goldFont.fnt", "GJ_button_04.png", 18.f, 0.34f);
    m_precisionBtn = CCMenuItemSpriteExtra::create(precSpr, this, menu_selector(ProgressBarConfigPopup::onCyclePrecision));
    menu->addChild(m_precisionBtn);

    auto* plcSpr = ButtonSprite::create(placementName(cfg.labelPlacement), 74, true, "bigFont.fnt", "GJ_button_01.png", 18.f, 0.34f);
    m_placementBtn = CCMenuItemSpriteExtra::create(plcSpr, this, menu_selector(ProgressBarConfigPopup::onCyclePlacement));
    menu->addChild(m_placementBtn);

    paimon::ui::layoutButtonRow({m_labelFmtBtn, m_precisionBtn, m_placementBtn}, 156.f, content.width - 24.f, 178.f, 6.f, paimon::ui::RowAlign::Right);

    // Row 2: Font + Color Mode + Swatches
    auto* fLbl = CCLabelBMFont::create("Font", "bigFont.fnt");
    fLbl->setScale(0.36f); fLbl->setAnchorPoint({0.f, 0.5f}); fLbl->setPosition({30.f, 140.f});
    tab->addChild(fLbl);

    auto* fontSpr = ButtonSprite::create(
        cfg.percentageFont.empty() ? "Default" : cfg.percentageFont.c_str(),
        96, true, "goldFont.fnt", "GJ_button_04.png", 18.f, 0.34f);
    m_fontBtn = CCMenuItemSpriteExtra::create(fontSpr, this, menu_selector(ProgressBarConfigPopup::onPickFont));
    m_fontBtn->setPosition({126.f, 140.f});
    menu->addChild(m_fontBtn);

    m_usePctColorToggle = CCMenuItemToggler::createWithStandardSprites(this,
        menu_selector(ProgressBarConfigPopup::onUsePctColorToggled), 0.52f);
    m_usePctColorToggle->setPosition({206.f, 140.f});
    m_usePctColorToggle->toggle(cfg.useCustomPercentageColor);
    menu->addChild(m_usePctColorToggle);

    auto* pcSpr = ButtonSprite::create(colorModeName(cfg.pctColorMode), 80, true, "bigFont.fnt", "GJ_button_04.png", 18.f, 0.34f);
    m_pctModeBtn = CCMenuItemSpriteExtra::create(pcSpr, this, menu_selector(ProgressBarConfigPopup::onCyclePctMode));
    menu->addChild(m_pctModeBtn);

    m_pctColorPreview = makeColorSwatch(cfg.percentageColor);
    auto* c1Btn = CCMenuItemSpriteExtra::create(m_pctColorPreview, this, menu_selector(ProgressBarConfigPopup::onPickPctColor));
    menu->addChild(c1Btn);

    m_pctColor2Preview = makeColorSwatch(cfg.pctColor2);
    auto* c2Btn = CCMenuItemSpriteExtra::create(m_pctColor2Preview, this, menu_selector(ProgressBarConfigPopup::onPickPctColor2));
    menu->addChild(c2Btn);

    paimon::ui::layoutButtonRow({m_pctModeBtn, c1Btn, c2Btn}, 230.f, content.width - 24.f, 140.f, 8.f, paimon::ui::RowAlign::Left);

    // Sliders: Scale, Offset X, Offset Y
    auto addLblSlider = [&](char const* name, float y, Slider*& sl, CCLabelBMFont*& vl,
                            float val, float lo, float hi, SEL_MenuHandler cb, char const* fmtS) {
        auto* lbl = CCLabelBMFont::create(name, "bigFont.fnt");
        lbl->setScale(0.36f); lbl->setAnchorPoint({0.f, 0.5f}); lbl->setPosition({30.f, y});
        tab->addChild(lbl);
        sl = Slider::create(this, cb, 0.72f);
        sl->setPosition({220.f, y});
        sl->setValue(normFromValue(val, lo, hi));
        tab->addChild(sl);
        vl = CCLabelBMFont::create(fmt::format(fmt::runtime(fmtS), val).c_str(), "bigFont.fnt");
        vl->setScale(0.36f); vl->setAnchorPoint({0.f, 0.5f}); vl->setPosition({335.f, y});
        tab->addChild(vl);
    };
    addLblSlider("Label Scale", 100.f, m_pctScaleSlider, m_pctScaleLabel, cfg.percentageScale, 0.3f, 2.5f,
        menu_selector(ProgressBarConfigPopup::onPctScaleChanged), "{:.2f}");
    addLblSlider("Offset X",     68.f, m_pctOffXSlider,  m_pctOffXLabel,  cfg.percentageOffsetX, -200.f, 200.f,
        menu_selector(ProgressBarConfigPopup::onPctOffXChanged), "{:.0f}");
    addLblSlider("Offset Y",     36.f, m_pctOffYSlider,  m_pctOffYLabel,  cfg.percentageOffsetY, -200.f, 200.f,
        menu_selector(ProgressBarConfigPopup::onPctOffYChanged), "{:.0f}");
}

void ProgressBarConfigPopup::buildLayoutTab() {
    auto* tab = m_tabContainers[4];
    auto content = m_mainLayer->getContentSize();
    auto winSize = CCDirector::get()->getWinSize();
    auto& cfg = ProgressBarManager::get().config();
    if (cfg.posX <= 0.f && cfg.posY <= 0.f) {
        cfg.posX = winSize.width * 0.5f;
        cfg.posY = winSize.height - 20.f;
    }

    addInset(tab, {content.width - 34.f, 184.f}, {17.f, 14.f}, -1);

    auto* menu = CCMenu::create();
    menu->setPosition({0, 0});
    tab->addChild(menu, 2);

    auto addTopTog = [&](char const* title, float x, bool val, CCMenuItemToggler*& out, SEL_MenuHandler cb) {
        out = CCMenuItemToggler::createWithStandardSprites(this, cb, 0.52f);
        out->setPosition({x, 180.f});
        out->toggle(val);
        menu->addChild(out);
        auto* lbl = CCLabelBMFont::create(title, "bigFont.fnt");
        lbl->setScale(0.34f); lbl->setAnchorPoint({0.f, 0.5f}); lbl->setPosition({x + 14.f, 180.f});
        tab->addChild(lbl);
    };
    addTopTog("Vertical 90",  34.f,  cfg.vertical,          m_verticalToggle,     menu_selector(ProgressBarConfigPopup::onVerticalToggled));
    addTopTog("Custom Pos",   168.f, cfg.useCustomPosition, m_useCustomPosToggle, menu_selector(ProgressBarConfigPopup::onUseCustomPosToggled));
    addTopTog("Pause Drag",   306.f, cfg.freeDragMode,      m_freeDragToggle,     menu_selector(ProgressBarConfigPopup::onFreeDragToggled));

    auto addLeftSl = [&](char const* name, float y, Slider*& sl, CCLabelBMFont*& vl,
                         float val, float lo, float hi, SEL_MenuHandler cb, char const* fmtS) {
        auto* lbl = CCLabelBMFont::create(name, "bigFont.fnt");
        lbl->setScale(0.34f); lbl->setAnchorPoint({0.f, 0.5f}); lbl->setPosition({30.f, y});
        tab->addChild(lbl);
        sl = Slider::create(this, cb, 0.55f);
        sl->setPosition({146.f, y});
        sl->setValue(normFromValue(val, lo, hi));
        tab->addChild(sl);
        vl = CCLabelBMFont::create(fmt::format(fmt::runtime(fmtS), val).c_str(), "bigFont.fnt");
        vl->setScale(0.34f); vl->setAnchorPoint({0.f, 0.5f}); vl->setPosition({220.f, y});
        tab->addChild(vl);
    };
    addLeftSl("Pos X",      148.f, m_posXSlider,       m_posXLabel,       cfg.posX,           0.f, winSize.width,  menu_selector(ProgressBarConfigPopup::onPosXChanged), "{:.0f}");
    addLeftSl("Pos Y",      120.f, m_posYSlider,       m_posYLabel,       cfg.posY,           0.f, winSize.height, menu_selector(ProgressBarConfigPopup::onPosYChanged), "{:.0f}");
    addLeftSl("Scale X",     92.f, m_scaleLenSlider,   m_scaleLenLabel,   cfg.scaleLength,    0.2f, 3.0f,          menu_selector(ProgressBarConfigPopup::onScaleLenChanged), "{:.2f}");
    addLeftSl("Scale Y",     64.f, m_scaleThickSlider, m_scaleThickLabel, cfg.scaleThickness, 0.2f, 3.0f,          menu_selector(ProgressBarConfigPopup::onScaleThickChanged), "{:.2f}");
    addLeftSl("Rotation",    36.f, m_rotationSlider,   m_rotationLabel,   cfg.userRotation, -180.f, 180.f,        menu_selector(ProgressBarConfigPopup::onRotationChanged), "{:.0f}");

    // Right column: Decorations + Free Edit
    auto* decHdr = CCLabelBMFont::create("Decorations", "goldFont.fnt");
    decHdr->setScale(0.42f); decHdr->setPosition({368.f, 148.f});
    tab->addChild(decHdr);

    m_decoCountLbl = CCLabelBMFont::create(fmt::format("Active: {}", cfg.decorations.size()).c_str(), "chatFont.fnt");
    m_decoCountLbl->setScale(0.48f); m_decoCountLbl->setPosition({368.f, 128.f});
    tab->addChild(m_decoCountLbl);

    auto* addDecSpr = ButtonSprite::create("+ Add Image", 88, true, "goldFont.fnt", "GJ_button_01.png", 20.f, 0.36f);
    auto* addDecBtn = CCMenuItemSpriteExtra::create(addDecSpr, this, menu_selector(ProgressBarConfigPopup::onAddDecoration));
    menu->addChild(addDecBtn);

    auto* clrDecSpr = ButtonSprite::create("Clr", 34, true, "bigFont.fnt", "GJ_button_06.png", 20.f, 0.36f);
    auto* clrDecBtn = CCMenuItemSpriteExtra::create(clrDecSpr, this, menu_selector(ProgressBarConfigPopup::onClearDecorations));
    menu->addChild(clrDecBtn);

    paimon::ui::layoutButtonRow({addDecBtn, clrDecBtn}, 290.f, content.width - 24.f, 98.f, 6.f, paimon::ui::RowAlign::Center);

    auto* editSpr = ButtonSprite::create("Edit in Level", 136, true, "goldFont.fnt", "GJ_button_02.png", 24.f, 0.42f);
    auto* editBtn = CCMenuItemSpriteExtra::create(editSpr, this, menu_selector(ProgressBarConfigPopup::onEnterFreeEditMode));
    editBtn->setPosition({368.f, 52.f});
    menu->addChild(editBtn);
}

void ProgressBarConfigPopup::syncAllControlsFromConfig() {
    auto& cfg = ProgressBarManager::get().config();
    auto winSize = CCDirector::get()->getWinSize();

    if (m_enableToggle) m_enableToggle->toggle(cfg.enabled);
    if (m_hidePracticeToggle) m_hidePracticeToggle->toggle(cfg.hideInPractice);

    for (auto* btn : m_presetBtns) {
        paimon::ui::setButtonSkin(btn,
            btn->getTag() == cfg.presetId ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    }

    setBtnLabel(m_styleBtn, styleName(cfg.style));
    if (m_widthSlider) m_widthSlider->setValue(normFromValue(cfg.barWidth, 60.f, 420.f));
    if (m_widthLabel)  m_widthLabel->setString(fmt::format("{:.0f}", cfg.barWidth).c_str());
    if (m_heightSlider) m_heightSlider->setValue(normFromValue(cfg.barHeight, 4.f, 32.f));
    if (m_heightLabel)  m_heightLabel->setString(fmt::format("{:.0f}", cfg.barHeight).c_str());
    if (m_borderWSlider) m_borderWSlider->setValue(normFromValue(cfg.borderWidth, 0.f, 4.f));
    if (m_borderWLabel)  m_borderWLabel->setString(fmt::format("{:.1f}", cfg.borderWidth).c_str());
    if (m_radiusSlider) m_radiusSlider->setValue(normFromValue(cfg.cornerRadius, 0.f, 16.f));
    if (m_radiusLabel)  m_radiusLabel->setString(fmt::format("{:.1f}", cfg.cornerRadius).c_str());
    if (m_smoothSlider) m_smoothSlider->setValue(normFromValue(cfg.smoothing, 0.f, 1.f));
    if (m_smoothLabel)  m_smoothLabel->setString(fmt::format("{:.2f}", cfg.smoothing).c_str());

    if (m_tipGlowToggle) m_tipGlowToggle->toggle(cfg.tipGlow);
    if (m_pbMarkerToggle) m_pbMarkerToggle->toggle(cfg.showPbMarker);
    if (m_cpMarkerToggle) m_cpMarkerToggle->toggle(cfg.showCheckpoints);
    if (m_milestoneToggle) m_milestoneToggle->toggle(cfg.showMilestones);
    if (m_deathMarkerToggle) m_deathMarkerToggle->toggle(cfg.showDeathMarker);
    if (m_newBestFlashToggle) m_newBestFlashToggle->toggle(cfg.newBestFlash);

    setBtnLabel(m_fillModeBtn, colorModeName(cfg.fillColorMode));
    setBtnLabel(m_bgModeBtn,   colorModeName(cfg.bgColorMode));
    setBtnLabel(m_pctModeBtn,  colorModeName(cfg.pctColorMode));
    if (m_fillColor1Preview) m_fillColor1Preview->setColor(cfg.fillColor);
    if (m_fillColor2Preview) m_fillColor2Preview->setColor(cfg.fillColor2);
    if (m_fillColor3Preview) m_fillColor3Preview->setColor(cfg.fillColor3);
    if (m_bgColorPreview)    m_bgColorPreview->setColor(cfg.bgColor);
    if (m_borderColorPreview) m_borderColorPreview->setColor(cfg.borderColor);
    if (m_shadowToggle) m_shadowToggle->toggle(cfg.shadowEnabled);
    if (m_opacitySlider) m_opacitySlider->setValue(normFromValue(static_cast<float>(cfg.opacity), 20.f, 255.f));
    if (m_opacityLabel)  m_opacityLabel->setString(fmt::format("{}", cfg.opacity).c_str());
    if (m_animSpeedSlider) m_animSpeedSlider->setValue(normFromValue(cfg.colorAnimSpeed, 0.1f, 5.f));
    if (m_animSpeedLabel)  m_animSpeedLabel->setString(fmt::format("{:.1f}x", cfg.colorAnimSpeed).c_str());
    if (m_useFillTexToggle) m_useFillTexToggle->toggle(cfg.useFillTexture);
    if (m_useBgTexToggle)   m_useBgTexToggle->toggle(cfg.useBgTexture);
    if (m_fillTexPathLabel) m_fillTexPathLabel->setString(shortPath(cfg.fillTexturePath).c_str());
    if (m_bgTexPathLabel)   m_bgTexPathLabel->setString(shortPath(cfg.bgTexturePath).c_str());

    if (m_showPctToggle) m_showPctToggle->toggle(cfg.showPercentage);
    setBtnLabel(m_labelFmtBtn, labelFmtName(cfg.labelFormat));
    setBtnLabel(m_precisionBtn, fmt::format("{} Dec", cfg.decimalPrecision).c_str());
    setBtnLabel(m_placementBtn, placementName(cfg.labelPlacement));
    setBtnLabel(m_fontBtn, cfg.percentageFont.empty() ? "Default" : cfg.percentageFont.c_str());
    if (m_usePctColorToggle) m_usePctColorToggle->toggle(cfg.useCustomPercentageColor);
    if (m_pctColorPreview)   m_pctColorPreview->setColor(cfg.percentageColor);
    if (m_pctColor2Preview)  m_pctColor2Preview->setColor(cfg.pctColor2);
    if (m_pctScaleSlider) m_pctScaleSlider->setValue(normFromValue(cfg.percentageScale, 0.3f, 2.5f));
    if (m_pctScaleLabel)  m_pctScaleLabel->setString(fmt::format("{:.2f}", cfg.percentageScale).c_str());
    if (m_pctOffXSlider)  m_pctOffXSlider->setValue(normFromValue(cfg.percentageOffsetX, -200.f, 200.f));
    if (m_pctOffXLabel)   m_pctOffXLabel->setString(fmt::format("{:.0f}", cfg.percentageOffsetX).c_str());
    if (m_pctOffYSlider)  m_pctOffYSlider->setValue(normFromValue(cfg.percentageOffsetY, -200.f, 200.f));
    if (m_pctOffYLabel)   m_pctOffYLabel->setString(fmt::format("{:.0f}", cfg.percentageOffsetY).c_str());

    if (m_verticalToggle) m_verticalToggle->toggle(cfg.vertical);
    if (m_useCustomPosToggle) m_useCustomPosToggle->toggle(cfg.useCustomPosition);
    if (m_freeDragToggle) m_freeDragToggle->toggle(cfg.freeDragMode);
    if (m_posXSlider) m_posXSlider->setValue(normFromValue(cfg.posX, 0.f, winSize.width));
    if (m_posXLabel)  m_posXLabel->setString(fmt::format("{:.0f}", cfg.posX).c_str());
    if (m_posYSlider) m_posYSlider->setValue(normFromValue(cfg.posY, 0.f, winSize.height));
    if (m_posYLabel)  m_posYLabel->setString(fmt::format("{:.0f}", cfg.posY).c_str());
    if (m_scaleLenSlider) m_scaleLenSlider->setValue(normFromValue(cfg.scaleLength, 0.2f, 3.0f));
    if (m_scaleLenLabel)  m_scaleLenLabel->setString(fmt::format("{:.2f}", cfg.scaleLength).c_str());
    if (m_scaleThickSlider) m_scaleThickSlider->setValue(normFromValue(cfg.scaleThickness, 0.2f, 3.0f));
    if (m_scaleThickLabel)  m_scaleThickLabel->setString(fmt::format("{:.2f}", cfg.scaleThickness).c_str());
    if (m_rotationSlider) m_rotationSlider->setValue(normFromValue(cfg.userRotation, -180.f, 180.f));
    if (m_rotationLabel)  m_rotationLabel->setString(fmt::format("{:.0f}", cfg.userRotation).c_str());
    if (m_decoCountLbl) m_decoCountLbl->setString(fmt::format("Active: {}", cfg.decorations.size()).c_str());
}

// --- Tab 0: Presets & Core ---
void ProgressBarConfigPopup::onEnableToggled(CCObject*) {
    ProgressBarManager::get().config().enabled = !m_enableToggle->isToggled();
    applyAndSave();
}
void ProgressBarConfigPopup::onHidePracticeToggled(CCObject*) {
    ProgressBarManager::get().config().hideInPractice = !m_hidePracticeToggle->isToggled();
    applyAndSave();
}
void ProgressBarConfigPopup::onSelectPreset(CCObject* sender) {
    auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    int idx = btn->getTag();
    ProgressBarManager::get().applyPreset(idx);
    syncAllControlsFromConfig();
    refreshPreviewTextures();
    PaimonNotify::show(fmt::format("Preset: {}", ProgressBarManager::presetName(idx)), NotificationIcon::Success);
}
void ProgressBarConfigPopup::onResetDefaults(CCObject*) {
    ProgressBarManager::get().resetToDefaults();
    syncAllControlsFromConfig();
    refreshPreviewTextures();
    PaimonNotify::show("Progress bar reset to defaults", NotificationIcon::Success);
}
void ProgressBarConfigPopup::onEnterFreeEditMode(CCObject*) {
    auto& cfg = ProgressBarManager::get().config();
    if (!cfg.enabled) cfg.enabled = true;
    applyAndSave();
    ProgressBarEditOverlay::enterEditMode();
}
void ProgressBarConfigPopup::onCenterPosition(CCObject*) {
    auto winSize = CCDirector::get()->getWinSize();
    auto& cfg = ProgressBarManager::get().config();
    cfg.posX = winSize.width * 0.5f;
    cfg.posY = winSize.height - 20.f;
    cfg.useCustomPosition = true;
    cfg.vertical = false;
    cfg.userRotation = 0.f;
    syncAllControlsFromConfig();
    applyAndSave();
}

// --- Tab 1: Style & Engine ---
void ProgressBarConfigPopup::onCycleStyle(CCObject*) {
    auto& cfg = ProgressBarManager::get().config();
    cfg.style = static_cast<BarStyle>((static_cast<int>(cfg.style) + 1) % 9);
    cfg.presetId = -1;
    syncAllControlsFromConfig();
    applyAndSave();
}
void ProgressBarConfigPopup::onWidthChanged(CCObject*) {
    if (!m_widthSlider) return;
    auto& c = ProgressBarManager::get().config();
    c.barWidth = readSliderRange(m_widthSlider, 60.f, 420.f);
    if (m_widthLabel) m_widthLabel->setString(fmt::format("{:.0f}", c.barWidth).c_str());
}
void ProgressBarConfigPopup::onHeightChanged(CCObject*) {
    if (!m_heightSlider) return;
    auto& c = ProgressBarManager::get().config();
    c.barHeight = readSliderRange(m_heightSlider, 4.f, 32.f);
    if (m_heightLabel) m_heightLabel->setString(fmt::format("{:.0f}", c.barHeight).c_str());
}
void ProgressBarConfigPopup::onBorderWChanged(CCObject*) {
    if (!m_borderWSlider) return;
    auto& c = ProgressBarManager::get().config();
    c.borderWidth = readSliderRange(m_borderWSlider, 0.f, 4.f);
    if (m_borderWLabel) m_borderWLabel->setString(fmt::format("{:.1f}", c.borderWidth).c_str());
}
void ProgressBarConfigPopup::onRadiusChanged(CCObject*) {
    if (!m_radiusSlider) return;
    auto& c = ProgressBarManager::get().config();
    c.cornerRadius = readSliderRange(m_radiusSlider, 0.f, 16.f);
    if (m_radiusLabel) m_radiusLabel->setString(fmt::format("{:.1f}", c.cornerRadius).c_str());
}
void ProgressBarConfigPopup::onSmoothChanged(CCObject*) {
    if (!m_smoothSlider) return;
    auto& c = ProgressBarManager::get().config();
    c.smoothing = readSliderRange(m_smoothSlider, 0.f, 1.f);
    if (m_smoothLabel) m_smoothLabel->setString(fmt::format("{:.2f}", c.smoothing).c_str());
}
void ProgressBarConfigPopup::onTipGlowToggled(CCObject*) {
    ProgressBarManager::get().config().tipGlow = !m_tipGlowToggle->isToggled();
    applyAndSave();
}
void ProgressBarConfigPopup::onPbMarkerToggled(CCObject*) {
    ProgressBarManager::get().config().showPbMarker = !m_pbMarkerToggle->isToggled();
    applyAndSave();
}
void ProgressBarConfigPopup::onCpMarkerToggled(CCObject*) {
    ProgressBarManager::get().config().showCheckpoints = !m_cpMarkerToggle->isToggled();
    applyAndSave();
}
void ProgressBarConfigPopup::onMilestoneToggled(CCObject*) {
    ProgressBarManager::get().config().showMilestones = !m_milestoneToggle->isToggled();
    applyAndSave();
}
void ProgressBarConfigPopup::onDeathMarkerToggled(CCObject*) {
    ProgressBarManager::get().config().showDeathMarker = !m_deathMarkerToggle->isToggled();
    applyAndSave();
}
void ProgressBarConfigPopup::onNewBestFlashToggled(CCObject*) {
    ProgressBarManager::get().config().newBestFlash = !m_newBestFlashToggle->isToggled();
    applyAndSave();
}

// --- Tab 2: Colors & Media ---
void ProgressBarConfigPopup::onCycleFillMode(CCObject*) {
    auto& c = ProgressBarManager::get().config();
    c.fillColorMode = static_cast<BarColorMode>((static_cast<int>(c.fillColorMode) + 1) % 6);
    c.useCustomFillColor = true;
    c.presetId = -1;
    syncAllControlsFromConfig();
    applyAndSave();
}
void ProgressBarConfigPopup::onCycleBgMode(CCObject*) {
    auto& c = ProgressBarManager::get().config();
    c.bgColorMode = static_cast<BarColorMode>((static_cast<int>(c.bgColorMode) + 1) % 6);
    syncAllControlsFromConfig();
    applyAndSave();
}
void ProgressBarConfigPopup::onCyclePctMode(CCObject*) {
    auto& c = ProgressBarManager::get().config();
    c.pctColorMode = static_cast<BarColorMode>((static_cast<int>(c.pctColorMode) + 1) % 6);
    syncAllControlsFromConfig();
    applyAndSave();
}
void ProgressBarConfigPopup::onPickFillColor1(CCObject*) {
    auto& cfg = ProgressBarManager::get().config();
    WeakRef<ProgressBarConfigPopup> self = this;
    openColorPicker(cfg.fillColor, [self](ccColor3B col) {
        auto& c = ProgressBarManager::get().config();
        c.fillColor = col;
        c.useCustomFillColor = true;
        c.presetId = -1;
        if (auto p = self.lock()) { p->syncAllControlsFromConfig(); p->applyAndSave(); }
        else ProgressBarManager::get().saveConfig();
    });
}
void ProgressBarConfigPopup::onPickFillColor2(CCObject*) {
    auto& cfg = ProgressBarManager::get().config();
    WeakRef<ProgressBarConfigPopup> self = this;
    openColorPicker(cfg.fillColor2, [self](ccColor3B col) {
        auto& c = ProgressBarManager::get().config();
        c.fillColor2 = col;
        c.presetId = -1;
        if (auto p = self.lock()) { p->syncAllControlsFromConfig(); p->applyAndSave(); }
        else ProgressBarManager::get().saveConfig();
    });
}
void ProgressBarConfigPopup::onPickFillColor3(CCObject*) {
    auto& cfg = ProgressBarManager::get().config();
    WeakRef<ProgressBarConfigPopup> self = this;
    openColorPicker(cfg.fillColor3, [self](ccColor3B col) {
        auto& c = ProgressBarManager::get().config();
        c.fillColor3 = col;
        c.presetId = -1;
        if (auto p = self.lock()) { p->syncAllControlsFromConfig(); p->applyAndSave(); }
        else ProgressBarManager::get().saveConfig();
    });
}
void ProgressBarConfigPopup::onPickBgColor(CCObject*) {
    auto& cfg = ProgressBarManager::get().config();
    WeakRef<ProgressBarConfigPopup> self = this;
    openColorPicker(cfg.bgColor, [self](ccColor3B col) {
        auto& c = ProgressBarManager::get().config();
        c.bgColor = col;
        c.useCustomBgColor = true;
        if (auto p = self.lock()) { p->syncAllControlsFromConfig(); p->applyAndSave(); }
        else ProgressBarManager::get().saveConfig();
    });
}
void ProgressBarConfigPopup::onPickBorderColor(CCObject*) {
    auto& cfg = ProgressBarManager::get().config();
    WeakRef<ProgressBarConfigPopup> self = this;
    openColorPicker(cfg.borderColor, [self](ccColor3B col) {
        ProgressBarManager::get().config().borderColor = col;
        if (auto p = self.lock()) { p->syncAllControlsFromConfig(); p->applyAndSave(); }
        else ProgressBarManager::get().saveConfig();
    });
}
void ProgressBarConfigPopup::onOpacityChanged(CCObject*) {
    if (!m_opacitySlider) return;
    auto& c = ProgressBarManager::get().config();
    c.opacity = static_cast<int>(readSliderRange(m_opacitySlider, 20.f, 255.f));
    if (m_opacityLabel) m_opacityLabel->setString(fmt::format("{}", c.opacity).c_str());
}
void ProgressBarConfigPopup::onAnimSpeedChanged(CCObject*) {
    if (!m_animSpeedSlider) return;
    auto& c = ProgressBarManager::get().config();
    c.colorAnimSpeed = readSliderRange(m_animSpeedSlider, 0.1f, 5.f);
    if (m_animSpeedLabel) m_animSpeedLabel->setString(fmt::format("{:.1f}x", c.colorAnimSpeed).c_str());
}
void ProgressBarConfigPopup::onShadowToggled(CCObject*) {
    ProgressBarManager::get().config().shadowEnabled = !m_shadowToggle->isToggled();
    applyAndSave();
}
void ProgressBarConfigPopup::onUseFillTextureToggled(CCObject*) {
    ProgressBarManager::get().config().useFillTexture = !m_useFillTexToggle->isToggled();
    applyAndSave();
}
void ProgressBarConfigPopup::onUseBgTextureToggled(CCObject*) {
    ProgressBarManager::get().config().useBgTexture = !m_useBgTexToggle->isToggled();
    applyAndSave();
}
void ProgressBarConfigPopup::onPickFillTexture(CCObject*) {
    WeakRef<ProgressBarConfigPopup> self = this;
    pt::pickImage([self](geode::Result<std::optional<std::filesystem::path>> res) {
        auto opt = std::move(res).unwrapOr(std::nullopt);
        if (!opt || opt->empty()) return;
        auto imported = paimon::assets::importToBucket(*opt, "progressbar_fill", paimon::assets::Kind::Image);
        if (!imported.success || imported.path.empty()) {
            PaimonNotify::show("Failed to import fill image", NotificationIcon::Error);
            return;
        }
        auto& c = ProgressBarManager::get().config();
        c.fillTexturePath = paimon::assets::normalizePathString(imported.path);
        c.useFillTexture = true;
        if (auto p = self.lock()) { p->syncAllControlsFromConfig(); p->applyAndSave(); }
        else ProgressBarManager::get().saveConfig();
        PaimonNotify::show("Fill image set", NotificationIcon::Success);
    });
}
void ProgressBarConfigPopup::onPickBgTexture(CCObject*) {
    WeakRef<ProgressBarConfigPopup> self = this;
    pt::pickImage([self](geode::Result<std::optional<std::filesystem::path>> res) {
        auto opt = std::move(res).unwrapOr(std::nullopt);
        if (!opt || opt->empty()) return;
        auto imported = paimon::assets::importToBucket(*opt, "progressbar_bg", paimon::assets::Kind::Image);
        if (!imported.success || imported.path.empty()) {
            PaimonNotify::show("Failed to import track image", NotificationIcon::Error);
            return;
        }
        auto& c = ProgressBarManager::get().config();
        c.bgTexturePath = paimon::assets::normalizePathString(imported.path);
        c.useBgTexture = true;
        if (auto p = self.lock()) { p->syncAllControlsFromConfig(); p->applyAndSave(); }
        else ProgressBarManager::get().saveConfig();
        PaimonNotify::show("Track image set", NotificationIcon::Success);
    });
}
void ProgressBarConfigPopup::onClearFillTexture(CCObject*) {
    auto& c = ProgressBarManager::get().config();
    c.fillTexturePath.clear();
    c.useFillTexture = false;
    syncAllControlsFromConfig();
    applyAndSave();
}
void ProgressBarConfigPopup::onClearBgTexture(CCObject*) {
    auto& c = ProgressBarManager::get().config();
    c.bgTexturePath.clear();
    c.useBgTexture = false;
    syncAllControlsFromConfig();
    applyAndSave();
}

// --- Tab 3: Label & Telemetry ---
void ProgressBarConfigPopup::onShowPctToggled(CCObject*) {
    ProgressBarManager::get().config().showPercentage = !m_showPctToggle->isToggled();
    applyAndSave();
}
void ProgressBarConfigPopup::onCycleLabelFmt(CCObject*) {
    auto& c = ProgressBarManager::get().config();
    c.labelFormat = static_cast<BarLabelFormat>((static_cast<int>(c.labelFormat) + 1) % 5);
    syncAllControlsFromConfig();
    applyAndSave();
}
void ProgressBarConfigPopup::onCyclePrecision(CCObject*) {
    auto& c = ProgressBarManager::get().config();
    c.decimalPrecision = (c.decimalPrecision + 1) % 3;
    syncAllControlsFromConfig();
    applyAndSave();
}
void ProgressBarConfigPopup::onCyclePlacement(CCObject*) {
    auto& c = ProgressBarManager::get().config();
    c.labelPlacement = static_cast<BarLabelPlacement>((static_cast<int>(c.labelPlacement) + 1) % 7);
    c.useCustomLabelPosition = (c.labelPlacement == BarLabelPlacement::CustomFreePos);
    syncAllControlsFromConfig();
    applyAndSave();
}
void ProgressBarConfigPopup::onPickFont(CCObject*) {
    WeakRef<ProgressBarConfigPopup> self = this;
    auto* picker = paimon::fonts::FontPickerPopup::create([self](std::string const& fontTag) {
        auto res = paimon::fonts::parseFontTag(fontTag);
        std::string fntFile = res.hasTag ? res.fontFile : std::string("bigFont.fnt");
        ProgressBarManager::get().config().percentageFont = fntFile;
        ProgressBarManager::get().saveConfig();
        if (auto p = self.lock()) p->syncAllControlsFromConfig();
        PaimonNotify::show("Font: " + fntFile, NotificationIcon::Success);
    });
    if (picker) picker->show();
}
void ProgressBarConfigPopup::onUsePctColorToggled(CCObject*) {
    ProgressBarManager::get().config().useCustomPercentageColor = !m_usePctColorToggle->isToggled();
    applyAndSave();
}
void ProgressBarConfigPopup::onPickPctColor(CCObject*) {
    auto& cfg = ProgressBarManager::get().config();
    WeakRef<ProgressBarConfigPopup> self = this;
    openColorPicker(cfg.percentageColor, [self](ccColor3B col) {
        auto& c = ProgressBarManager::get().config();
        c.percentageColor = col;
        c.useCustomPercentageColor = true;
        if (auto p = self.lock()) { p->syncAllControlsFromConfig(); p->applyAndSave(); }
        else ProgressBarManager::get().saveConfig();
    });
}
void ProgressBarConfigPopup::onPickPctColor2(CCObject*) {
    auto& cfg = ProgressBarManager::get().config();
    WeakRef<ProgressBarConfigPopup> self = this;
    openColorPicker(cfg.pctColor2, [self](ccColor3B col) {
        auto& c = ProgressBarManager::get().config();
        c.pctColor2 = col;
        c.useCustomPercentageColor = true;
        if (auto p = self.lock()) { p->syncAllControlsFromConfig(); p->applyAndSave(); }
        else ProgressBarManager::get().saveConfig();
    });
}
void ProgressBarConfigPopup::onPctScaleChanged(CCObject*) {
    if (!m_pctScaleSlider) return;
    auto& c = ProgressBarManager::get().config();
    c.percentageScale = readSliderRange(m_pctScaleSlider, 0.3f, 2.5f);
    if (m_pctScaleLabel) m_pctScaleLabel->setString(fmt::format("{:.2f}", c.percentageScale).c_str());
}
void ProgressBarConfigPopup::onPctOffXChanged(CCObject*) {
    if (!m_pctOffXSlider) return;
    auto& c = ProgressBarManager::get().config();
    c.percentageOffsetX = readSliderRange(m_pctOffXSlider, -200.f, 200.f);
    if (m_pctOffXLabel) m_pctOffXLabel->setString(fmt::format("{:.0f}", c.percentageOffsetX).c_str());
}
void ProgressBarConfigPopup::onPctOffYChanged(CCObject*) {
    if (!m_pctOffYSlider) return;
    auto& c = ProgressBarManager::get().config();
    c.percentageOffsetY = readSliderRange(m_pctOffYSlider, -200.f, 200.f);
    if (m_pctOffYLabel) m_pctOffYLabel->setString(fmt::format("{:.0f}", c.percentageOffsetY).c_str());
}

// --- Tab 4: Layout & Decorations ---
void ProgressBarConfigPopup::onVerticalToggled(CCObject*) {
    ProgressBarManager::get().config().vertical = !m_verticalToggle->isToggled();
    applyAndSave();
}
void ProgressBarConfigPopup::onUseCustomPosToggled(CCObject*) {
    ProgressBarManager::get().config().useCustomPosition = !m_useCustomPosToggle->isToggled();
    applyAndSave();
}
void ProgressBarConfigPopup::onFreeDragToggled(CCObject*) {
    auto& c = ProgressBarManager::get().config();
    c.freeDragMode = !m_freeDragToggle->isToggled();
    if (c.freeDragMode) {
        c.enabled = true;
        c.useCustomPosition = true;
        syncAllControlsFromConfig();
        PaimonNotify::show("Drag bar directly in Pause menu", NotificationIcon::Info);
    }
    applyAndSave();
}
void ProgressBarConfigPopup::onPosXChanged(CCObject*) {
    if (!m_posXSlider) return;
    auto winSize = CCDirector::get()->getWinSize();
    auto& c = ProgressBarManager::get().config();
    c.posX = readSliderRange(m_posXSlider, 0.f, winSize.width);
    c.useCustomPosition = true;
    if (m_useCustomPosToggle) m_useCustomPosToggle->toggle(true);
    if (m_posXLabel) m_posXLabel->setString(fmt::format("{:.0f}", c.posX).c_str());
}
void ProgressBarConfigPopup::onPosYChanged(CCObject*) {
    if (!m_posYSlider) return;
    auto winSize = CCDirector::get()->getWinSize();
    auto& c = ProgressBarManager::get().config();
    c.posY = readSliderRange(m_posYSlider, 0.f, winSize.height);
    c.useCustomPosition = true;
    if (m_useCustomPosToggle) m_useCustomPosToggle->toggle(true);
    if (m_posYLabel) m_posYLabel->setString(fmt::format("{:.0f}", c.posY).c_str());
}
void ProgressBarConfigPopup::onScaleLenChanged(CCObject*) {
    if (!m_scaleLenSlider) return;
    auto& c = ProgressBarManager::get().config();
    c.scaleLength = readSliderRange(m_scaleLenSlider, 0.2f, 3.0f);
    if (m_scaleLenLabel) m_scaleLenLabel->setString(fmt::format("{:.2f}", c.scaleLength).c_str());
}
void ProgressBarConfigPopup::onScaleThickChanged(CCObject*) {
    if (!m_scaleThickSlider) return;
    auto& c = ProgressBarManager::get().config();
    c.scaleThickness = readSliderRange(m_scaleThickSlider, 0.2f, 3.0f);
    if (m_scaleThickLabel) m_scaleThickLabel->setString(fmt::format("{:.2f}", c.scaleThickness).c_str());
}
void ProgressBarConfigPopup::onRotationChanged(CCObject*) {
    if (!m_rotationSlider) return;
    auto& c = ProgressBarManager::get().config();
    c.userRotation = readSliderRange(m_rotationSlider, -180.f, 180.f);
    if (m_rotationLabel) m_rotationLabel->setString(fmt::format("{:.0f}", c.userRotation).c_str());
}
void ProgressBarConfigPopup::onAddDecoration(CCObject*) {
    WeakRef<ProgressBarConfigPopup> self = this;
    pt::pickImage([self](geode::Result<std::optional<std::filesystem::path>> res) {
        auto opt = std::move(res).unwrapOr(std::nullopt);
        if (!opt || opt->empty()) return;
        auto imported = paimon::assets::importToBucket(*opt, "progressbar_decorations", paimon::assets::Kind::Image);
        if (!imported.success || imported.path.empty()) {
            PaimonNotify::show("Failed to import decoration", NotificationIcon::Error);
            return;
        }
        auto winSize = CCDirector::get()->getWinSize();
        BarDecoration d;
        d.path = paimon::assets::normalizePathString(imported.path);
        d.posX = winSize.width * 0.5f;
        d.posY = winSize.height * 0.5f;
        int idx = ProgressBarManager::get().addDecoration(d);
        if (idx >= 0) {
            if (auto p = self.lock()) p->syncAllControlsFromConfig();
            PaimonNotify::show(fmt::format("Decoration #{} added (use Edit in Level to place)", idx + 1), NotificationIcon::Success);
        }
    });
}
void ProgressBarConfigPopup::onClearDecorations(CCObject*) {
    while (ProgressBarManager::get().decorationCount() > 0) {
        ProgressBarManager::get().removeDecoration(0);
    }
    syncAllControlsFromConfig();
    PaimonNotify::show("Cleared all decorations", NotificationIcon::Info);
}
