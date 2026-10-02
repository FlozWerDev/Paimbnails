#include "StingerConfigPopup.hpp"
#include "TransitionConfigPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../layers/PaimonInfoPopup.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include "CustomTransitionEditorPopup.hpp"
#include "LevelEntryConfigPopup.hpp"
#include "../../../ui/PaimonUI.hpp"

using namespace geode::prelude;
using namespace cocos2d;

namespace ui = paimon::ui;


static int typeIndex(TransitionType type) {
    auto const& types = TransitionManager::allTypes();
    for (int i = 0; i < (int)types.size(); i++) {
        if (types[i] == type) return i;
    }
    return 0;
}

static void cycleType(TransitionConfig& cfg, int dir) {
    auto const& types = TransitionManager::allTypes();
    int count = (int)types.size();
    if (count == 0) return;
    int idx = (typeIndex(cfg.type) + dir) % count;
    if (idx < 0) idx += count;
    cfg.type = types[idx];
}

static CCMenuItemSpriteExtra* createArrowBtn(bool left, CCObject* target, SEL_MenuHandler sel) {
    auto spr = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
    if (!left) spr->setFlipX(true);
    spr->setScale(0.5f);
    auto btn = CCMenuItemSpriteExtra::create(spr, target, sel);
    return btn;
}

static CCMenuItemSpriteExtra* createInfoBtn(CCObject* target, SEL_MenuHandler sel) {
    auto spr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
    spr->setScale(0.6f);
    auto btn = CCMenuItemSpriteExtra::create(spr, target, sel);
    return btn;
}

static CCMenuItemSpriteExtra* createSmallButton(const char* text, CCObject* target, SEL_MenuHandler sel) {
    auto spr = ui::makeButtonSprite(text, ui::Btn::Cyan, 0.f, 0.55f, "bigFont.fnt");
    auto btn = CCMenuItemSpriteExtra::create(spr, target, sel);
    return btn;
}


TransitionConfigPopup* TransitionConfigPopup::create() {
    auto ret = new TransitionConfigPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool TransitionConfigPopup::init() {
    if (!PaimonPopup::init(400.f, 262.f)) return false;

    this->setTitle("Transition Settings");
    this->addCorners();
    this->addInfoButton("Transitions",
        "Set the animation played when the game changes screens.\n\n"
        "<cy>Global</c> applies everywhere. <cy>Level Entry</c> can override it when you enter a level.\n"
        "Use the <cg>arrows</c> to browse styles and adjust the duration; <cg>Save</c> to apply.");

    auto& tm = TransitionManager::get();
    if (!tm.isEnabled()) tm.loadConfig();

    m_editingGlobal = tm.getGlobalConfig();
    m_editingLevel = tm.getLevelEntryConfig();

    auto ws = m_mainLayer->getContentSize();

    float enableH = 24.f;
    auto enableRow = ui::makeInset({ws.width - 24.f, enableH}, 90);
    enableRow->setPosition({12.f, ws.height - 34.f - enableH});
    m_mainLayer->addChild(enableRow);

    auto enableLbl = ui::makeLabel("Transitions enabled", 160.f, 0.45f);
    enableLbl->setAnchorPoint({0.f, 0.5f});
    enableLbl->setPosition({10.f, enableH / 2.f});
    enableRow->addChild(enableLbl);

    m_enableToggle = ui::makeSwitch(this, menu_selector(TransitionConfigPopup::onToggleEnabled), tm.isEnabled(), 0.7f);
    m_enableToggle->setPosition({ws.width - 24.f - 18.f, enableRow->getPositionY() + enableH / 2.f});
    m_buttonMenu->addChild(m_enableToggle);

    float panelW = (ws.width - 30.f) / 2.f;
    float panelH = 120.f;
    float panelY = 44.f;

    auto buildSection = [&](float px, char const* heading, bool isGlobal,
        CCLabelBMFont*& nameLbl, CCLabelBMFont*& idxLbl, CCLabelBMFont*& durLbl,
        CCLabelBMFont*& descLbl, CCLayerColor*& swatch, CCMenuItemSpriteExtra*& colorBtn,
        CCMenuItemSpriteExtra*& customBtn) {
        auto panel = ui::makePanel({panelW, panelH}, heading, 95);
        panel->setPosition({px, panelY});
        m_mainLayer->addChild(panel);

        float inner = panelH - ui::kPanelHeader;
        float midX = panelW / 2.f;

        float ty = inner - 16.f;
        auto leftArr = createArrowBtn(true, this, isGlobal
            ? menu_selector(TransitionConfigPopup::onGlobalPrevType)
            : menu_selector(TransitionConfigPopup::onLevelPrevType));
        leftArr->setPosition({px + 14.f, panelY + ty});
        m_buttonMenu->addChild(leftArr);

        auto rightArr = createArrowBtn(false, this, isGlobal
            ? menu_selector(TransitionConfigPopup::onGlobalNextType)
            : menu_selector(TransitionConfigPopup::onLevelNextType));
        rightArr->setPosition({px + panelW - 14.f, panelY + ty});
        m_buttonMenu->addChild(rightArr);

        nameLbl = CCLabelBMFont::create("", "goldFont.fnt");
        nameLbl->setScale(0.4f);
        nameLbl->setPosition({midX, ty});
        panel->addChild(nameLbl);

        ty -= 15.f;
        idxLbl = CCLabelBMFont::create("", "chatFont.fnt");
        idxLbl->setScale(0.42f);
        idxLbl->setColor(ui::palette::muted);
        idxLbl->setPosition({midX, ty});
        panel->addChild(idxLbl);

        ty -= 18.f;
        auto durLblTag = ui::makeLabel("Duration", 70.f, 0.34f, ui::palette::muted);
        durLblTag->setAnchorPoint({0.f, 0.5f});
        durLblTag->setPosition({10.f, ty});
        panel->addChild(durLblTag);

        auto durDown = createArrowBtn(true, this, isGlobal
            ? menu_selector(TransitionConfigPopup::onGlobalDurDown)
            : menu_selector(TransitionConfigPopup::onLevelDurDown));
        durDown->setScale(0.4f);
        durDown->setPosition({px + midX + 2.f, panelY + ty});
        m_buttonMenu->addChild(durDown);

        durLbl = CCLabelBMFont::create("", "bigFont.fnt");
        durLbl->setScale(0.32f);
        durLbl->setPosition({midX + 30.f, ty});
        panel->addChild(durLbl);

        auto durUp = createArrowBtn(false, this, isGlobal
            ? menu_selector(TransitionConfigPopup::onGlobalDurUp)
            : menu_selector(TransitionConfigPopup::onLevelDurUp));
        durUp->setScale(0.4f);
        durUp->setPosition({px + panelW - 12.f, panelY + ty});
        m_buttonMenu->addChild(durUp);

        descLbl = CCLabelBMFont::create("", "chatFont.fnt");
        descLbl->setScale(0.35f);
        descLbl->setColor(ui::palette::muted);
        descLbl->setVisible(false);
        m_mainLayer->addChild(descLbl);

        ty -= 20.f;
        swatch = CCLayerColor::create({0, 0, 0, 255}, 14, 14);
        swatch->setPosition({px + 12.f, panelY + ty - 7.f});
        m_mainLayer->addChild(swatch);

        colorBtn = createSmallButton("Color", this, isGlobal
            ? menu_selector(TransitionConfigPopup::onGlobalColor)
            : menu_selector(TransitionConfigPopup::onLevelColor));
        colorBtn->setPosition({px + 48.f, panelY + ty});
        m_buttonMenu->addChild(colorBtn);

        customBtn = createSmallButton("Custom...", this, isGlobal
            ? menu_selector(TransitionConfigPopup::onGlobalCustom)
            : menu_selector(TransitionConfigPopup::onLevelCustom));
        customBtn->setPosition({px + panelW - 44.f, panelY + ty});
        m_buttonMenu->addChild(customBtn);
        return panel;
    };

    auto globalPanel = buildSection(12.f, "Global", true,
        m_globalNameLabel, m_globalIndexLabel, m_globalDurLabel, m_globalDescLabel,
        m_globalColorSwatch, m_globalColorBtn, m_globalCustomBtn);

    auto gInfo = createInfoBtn(this, menu_selector(TransitionConfigPopup::onInfoGlobal));
    gInfo->setScale(0.5f);
    gInfo->setPosition({12.f + panelW - 12.f, panelY + panelH - 11.f});
    m_buttonMenu->addChild(gInfo);

    float lpx = 18.f + panelW;
    auto levelPanel = buildSection(lpx, "Level Entry", false,
        m_levelNameLabel, m_levelIndexLabel, m_levelDurLabel, m_levelDescLabel,
        m_levelColorSwatch, m_levelColorBtn, m_levelCustomBtn);

    m_levelToggle = ui::makeSwitch(this, menu_selector(TransitionConfigPopup::onToggleLevelEntry), tm.hasLevelEntryConfig(), 0.5f);
    m_levelToggle->setPosition({lpx + 14.f, panelY + panelH - 11.f});
    m_buttonMenu->addChild(m_levelToggle);

    auto lInfo = createInfoBtn(this, menu_selector(TransitionConfigPopup::onInfoLevel));
    lInfo->setScale(0.5f);
    lInfo->setPosition({lpx + panelW - 12.f, panelY + panelH - 11.f});
    m_buttonMenu->addChild(lInfo);

    auto levelEffectsBtn = createSmallButton("Smooth+...", this, menu_selector(TransitionConfigPopup::onLevelEffects));
    levelEffectsBtn->setPosition({lpx + panelW / 2.f, panelY - 2.f});
    m_buttonMenu->addChild(levelEffectsBtn);

    m_statusLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_statusLabel->setScale(0.3f);
    m_statusLabel->setColor(ui::palette::success);
    m_statusLabel->setAnchorPoint({0.f, 0.5f});
    m_statusLabel->setPosition({14.f, 18.f});
    m_mainLayer->addChild(m_statusLabel);

    auto saveBtn = ui::makeButton("Save", [this]() { onSave(nullptr); }, ui::Btn::Green, 0.f, 0.7f);
    saveBtn->setPosition({ws.width - 48.f, 18.f});
    m_buttonMenu->addChild(saveBtn);

    updateGlobalDisplay();
    updateLevelDisplay();
    updateConditionalButtons();

    paimon::markDynamicPopup(this);
    return true;
}


int TransitionConfigPopup::getTypeIndex(TransitionType t) const {
    return typeIndex(t);
}

void TransitionConfigPopup::updateGlobalDisplay() {
    m_globalNameLabel->setString(TransitionManager::typeDisplayName(m_editingGlobal.type).c_str());
    m_globalDescLabel->setString(TransitionManager::typeDescription(m_editingGlobal.type).c_str());

    m_globalDurLabel->setString(fmt::format("{:.2f}s", m_editingGlobal.duration).c_str());

    int idx = getTypeIndex(m_editingGlobal.type);
    m_globalIndexLabel->setString(
        fmt::format("{}/{}", idx + 1, (int)TransitionManager::allTypes().size()).c_str());

    if (m_globalColorSwatch) {
        m_globalColorSwatch->setColor({
            static_cast<GLubyte>(m_editingGlobal.colorR),
            static_cast<GLubyte>(m_editingGlobal.colorG),
            static_cast<GLubyte>(m_editingGlobal.colorB)
        });
    }
    updateConditionalButtons();
}

void TransitionConfigPopup::updateLevelDisplay() {
    m_levelNameLabel->setString(TransitionManager::typeDisplayName(m_editingLevel.type).c_str());
    m_levelDescLabel->setString(TransitionManager::typeDescription(m_editingLevel.type).c_str());

    m_levelDurLabel->setString(fmt::format("{:.2f}s", m_editingLevel.duration).c_str());

    int idx = getTypeIndex(m_editingLevel.type);
    m_levelIndexLabel->setString(
        fmt::format("{}/{}", idx + 1, (int)TransitionManager::allTypes().size()).c_str());

    if (m_levelColorSwatch) {
        m_levelColorSwatch->setColor({
            static_cast<GLubyte>(m_editingLevel.colorR),
            static_cast<GLubyte>(m_editingLevel.colorG),
            static_cast<GLubyte>(m_editingLevel.colorB)
        });
    }
    updateConditionalButtons();
}

void TransitionConfigPopup::updateConditionalButtons() {
    bool gShowColor = (m_editingGlobal.type == TransitionType::FadeColor);
    bool gShowCustom = (m_editingGlobal.type == TransitionType::Custom || m_editingGlobal.type == TransitionType::Stinger);
    m_globalColorBtn->setVisible(gShowColor);
    m_globalColorSwatch->setVisible(gShowColor);
    m_globalCustomBtn->setVisible(gShowCustom);

    bool lShowColor = (m_editingLevel.type == TransitionType::FadeColor);
    bool lShowCustom = (m_editingLevel.type == TransitionType::Custom || m_editingLevel.type == TransitionType::Stinger);
    m_levelColorBtn->setVisible(lShowColor);
    m_levelColorSwatch->setVisible(lShowColor);
    m_levelCustomBtn->setVisible(lShowCustom);
}


void TransitionConfigPopup::onToggleEnabled(CCObject*) {
    TransitionManager::get().setEnabled(!m_enableToggle->isToggled());
}

void TransitionConfigPopup::onToggleLevelEntry(CCObject*) {
    bool willHave = !m_levelToggle->isToggled();
    if (willHave) {
        m_editingLevel = m_editingGlobal;
        updateLevelDisplay();
    }
}


void TransitionConfigPopup::onGlobalPrevType(CCObject*) { cycleType(m_editingGlobal, -1); updateGlobalDisplay(); }
void TransitionConfigPopup::onGlobalNextType(CCObject*) { cycleType(m_editingGlobal, 1);  updateGlobalDisplay(); }
void TransitionConfigPopup::onLevelPrevType(CCObject*)  { cycleType(m_editingLevel, -1);  updateLevelDisplay(); }
void TransitionConfigPopup::onLevelNextType(CCObject*)  { cycleType(m_editingLevel, 1);   updateLevelDisplay(); }


void TransitionConfigPopup::onGlobalDurDown(CCObject*) { m_editingGlobal.duration = std::max(0.05f, m_editingGlobal.duration - 0.05f); updateGlobalDisplay(); }
void TransitionConfigPopup::onGlobalDurUp(CCObject*)   { m_editingGlobal.duration = std::min(30.0f, m_editingGlobal.duration + 0.05f); updateGlobalDisplay(); }
void TransitionConfigPopup::onLevelDurDown(CCObject*)  { m_editingLevel.duration  = std::max(0.05f, m_editingLevel.duration  - 0.05f); updateLevelDisplay(); }
void TransitionConfigPopup::onLevelDurUp(CCObject*)    { m_editingLevel.duration  = std::min(30.0f, m_editingLevel.duration  + 0.05f); updateLevelDisplay(); }


static void cycleColor(TransitionConfig& cfg) {
    struct ColorPreset { int r, g, b; };
    static const std::vector<ColorPreset> presets = {
        {0,0,0}, {255,255,255}, {255,0,0}, {0,255,0}, {0,0,255},
        {255,255,0}, {255,0,255}, {0,255,255}, {128,0,0}, {0,128,0},
        {0,0,128}, {255,128,0}, {128,0,255}, {255,128,128}, {50,50,50}
    };
    int cur = -1;
    for (int i = 0; i < (int)presets.size(); i++) {
        if (presets[i].r == cfg.colorR && presets[i].g == cfg.colorG && presets[i].b == cfg.colorB) {
            cur = i; break;
        }
    }
    cur = (cur + 1) % (int)presets.size();
    cfg.colorR = presets[cur].r;
    cfg.colorG = presets[cur].g;
    cfg.colorB = presets[cur].b;
}

void TransitionConfigPopup::onGlobalColor(CCObject*) {
    cycleColor(m_editingGlobal);
    updateGlobalDisplay();
}

void TransitionConfigPopup::onLevelColor(CCObject*) {
    cycleColor(m_editingLevel);
    updateLevelDisplay();
}


void TransitionConfigPopup::onGlobalCustom(CCObject*) {
    if (m_editingGlobal.type == TransitionType::Stinger) {
        WeakRef<TransitionConfigPopup> self = this;
        if (auto* popup = StingerConfigPopup::create(m_editingGlobal, [self](auto config) {
            if (auto p = self.lock()) { p->m_editingGlobal = std::move(config); p->updateGlobalDisplay(); }
        })) popup->show();
        return;
    }
    m_editingIsGlobal = true;
    WeakRef<TransitionConfigPopup> self = this;
    auto popup = CustomTransitionEditorPopup::create(m_editingGlobal, true, [self](auto config) {
        if (auto p = self.lock()) { p->m_editingGlobal = std::move(config); p->updateGlobalDisplay(); }
    });
    if (popup) popup->show();
}

void TransitionConfigPopup::onLevelCustom(CCObject*) {
    if (m_editingLevel.type == TransitionType::Stinger) {
        WeakRef<TransitionConfigPopup> self = this;
        if (auto* popup = StingerConfigPopup::create(m_editingLevel, [self](auto config) {
            if (auto p = self.lock()) { p->m_editingLevel = std::move(config); p->updateLevelDisplay(); }
        })) popup->show();
        return;
    }
    m_editingIsGlobal = false;
    WeakRef<TransitionConfigPopup> self = this;
    auto popup = CustomTransitionEditorPopup::create(m_editingLevel, false, [self](auto config) {
        if (auto p = self.lock()) { p->m_editingLevel = std::move(config); p->updateLevelDisplay(); }
    });
    if (popup) popup->show();
}

void TransitionConfigPopup::onLevelEffects(CCObject*) {
    if (auto* popup = paimon::transitions::LevelEntryConfigPopup::create()) popup->show();
}


void TransitionConfigPopup::onInfoGlobal(CCObject*) {
    PaimonInfoPopup::create(
        "Global Transition",
        "This transition applies to <cy>ALL</c> screen changes in the game: menus, level browser, settings, etc.\n\n"
        "Use the <cl>arrows</c> to browse 55+ different transition styles.\n"
        "Adjust <cl>duration</c> to control speed (0.05s to 3.0s)."
    )->show();
}

void TransitionConfigPopup::onInfoLevel(CCObject*) {
    PaimonInfoPopup::create(
        "Level Entry Transition",
        "Enable <cy>Override</c> to use a <cg>different transition</c> when entering a level (PlayLayer).\n\n"
        "If disabled, the <cl>Global</c> transition is used everywhere.\n"
        "When <cl>Smooth+</c> is enabled it replaces that preset with its own live scene choreography. "
        "Disable it to use the transition type selected here."
    )->show();
}

void TransitionConfigPopup::onInfoType(CCObject*) {
    PaimonInfoPopup::create(
        "Transition Types",
        "<cy>Fades:</c> Smooth opacity blends (black, white, color, bounce).\n"
        "<cy>Slides:</c> New screen slides in from any direction.\n"
        "<cy>Move In:</c> New screen moves over the old one.\n"
        "<cy>Flips/Zooms:</c> 3D card-flip or zoom effects.\n"
        "<cy>Tiles:</c> Mosaic-style reveals from different directions.\n"
        "<cy>Progress:</c> Radial/circular/bar wipe effects.\n"
        "<cy>Pages:</c> Book page curl effect.\n"
        "<cy>Creative:</c> Spin, Glitch, Wave, Flash + more!\n"
        "<cy>Random:</c> A different effect every time!\n"
        "<cy>Custom:</c> Commands, media and parallel groups.\n"
        "<cy>Stinger:</c> Image/GIF/video overlay with a scene cut.\n"
        "<cy>None:</c> Instant, no animation."
    )->show();
}

void TransitionConfigPopup::onInfoDuration(CCObject*) {
    PaimonInfoPopup::create(
        "Duration",
        "Controls how long the transition animation takes.\n\n"
        "<cl>0.2s</c> = Very fast\n"
        "<cl>0.5s</c> = Default\n"
        "<cl>1.0s</c> = Slow, dramatic\n"
        "<cl>2.0s+</c> = Very cinematic"
    )->show();
}

void TransitionConfigPopup::onInfoCustom(CCObject*) {
    onGlobalCustom(nullptr);
}


void TransitionConfigPopup::onSave(CCObject*) {
    auto& tm = TransitionManager::get();
    tm.setGlobalConfig(m_editingGlobal);

    bool useSeparateLevel = !m_levelToggle->isToggled();
    if (useSeparateLevel) {
        tm.setLevelEntryConfig(m_editingLevel);
    } else {
        tm.clearLevelEntryConfig();
    }

    tm.saveConfig();
    m_statusLabel->setString("Saved!");
    PaimonNotify::create("Transitions saved!", NotificationIcon::Success)->show();
}
