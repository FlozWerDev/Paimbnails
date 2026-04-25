#include "TransitionConfigPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../layers/PaimonInfoPopup.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include "CustomTransitionEditorPopup.hpp"

using namespace geode::prelude;
using namespace cocos2d;

// ════════════════════════════════════════════════════════════
// Helpers
// ════════════════════════════════════════════════════════════

static void cycleType(TransitionConfig& cfg, int dir) {
    auto const& types = TransitionManager::allTypes();
    int idx = 0;
    for (int i = 0; i < (int)types.size(); i++) {
        if (types[i] == cfg.type) { idx = i; break; }
    }
    idx += dir;
    if (idx < 0) idx = (int)types.size() - 1;
    if (idx >= (int)types.size()) idx = 0;
    cfg.type = types[idx];
}

static CCMenuItemSpriteExtra* createArrowBtn(bool left, CCObject* target, SEL_MenuHandler sel) {
    auto spr = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
    if (left) spr->setFlipX(true);
    spr->setScale(0.35f);
    auto btn = CCMenuItemSpriteExtra::create(spr, target, sel);
    return btn;
}

static CCMenuItemSpriteExtra* createInfoBtn(CCObject* target, SEL_MenuHandler sel) {
    auto spr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
    spr->setScale(0.4f);
    auto btn = CCMenuItemSpriteExtra::create(spr, target, sel);
    return btn;
}

static CCMenuItemSpriteExtra* createSmallButton(const char* text, CCObject* target, SEL_MenuHandler sel) {
    auto spr = ButtonSprite::create(text, "bigFont.fnt", "GJ_button_04.png", .6f);
    spr->setScale(0.55f);
    auto btn = CCMenuItemSpriteExtra::create(spr, target, sel);
    return btn;
}

// ════════════════════════════════════════════════════════════
// Create / Init
// ════════════════════════════════════════════════════════════

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
    if (!Popup::init(380.f, 220.f)) return false;

    this->setTitle("Transition Settings");

    auto& tm = TransitionManager::get();
    if (!tm.isEnabled()) tm.loadConfig();

    m_editingGlobal = tm.getGlobalConfig();
    m_editingLevel = tm.getLevelEntryConfig();

    auto ws = m_mainLayer->getContentSize();
    float cx = ws.width / 2.f;
    float y = ws.height - 42.f;

    // ════════════════════════════════════════════════════
    // ENABLE TOGGLE
    // ════════════════════════════════════════════════════
    auto enableLbl = CCLabelBMFont::create("Enabled", "bigFont.fnt");
    enableLbl->setScale(0.3f);
    enableLbl->setPosition({cx - 35, y});
    m_mainLayer->addChild(enableLbl);

    auto onSpr  = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
    auto offSpr = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
    onSpr->setScale(0.48f);
    offSpr->setScale(0.48f);
    m_enableToggle = CCMenuItemToggler::create(offSpr, onSpr, this, menu_selector(TransitionConfigPopup::onToggleEnabled));
    m_enableToggle->toggle(tm.isEnabled());
    m_enableToggle->setPosition({cx + 10, y});
    m_buttonMenu->addChild(m_enableToggle);

    // ════════════════════════════════════════════════════
    // GLOBAL TRANSITION SECTION
    // ════════════════════════════════════════════════════
    y -= 18;
    auto gTitle = CCLabelBMFont::create("Global Transition", "goldFont.fnt");
    gTitle->setScale(0.38f);
    gTitle->setPosition({cx - 30, y});
    m_mainLayer->addChild(gTitle);

    auto gInfo = createInfoBtn(this, menu_selector(TransitionConfigPopup::onInfoGlobal));
    gInfo->setPosition({cx + 70, y});
    m_buttonMenu->addChild(gInfo);

    // ── Type selector + Duration (same row) ──
    y -= 16;
    auto gLeftArr = createArrowBtn(true, this, menu_selector(TransitionConfigPopup::onGlobalPrevType));
    gLeftArr->setPosition({cx - 115, y});
    m_buttonMenu->addChild(gLeftArr);

    m_globalNameLabel = CCLabelBMFont::create("", "goldFont.fnt");
    m_globalNameLabel->setScale(0.36f);
    m_globalNameLabel->setPosition({cx - 40, y});
    m_mainLayer->addChild(m_globalNameLabel);

    auto gRightArr = createArrowBtn(false, this, menu_selector(TransitionConfigPopup::onGlobalNextType));
    gRightArr->setPosition({cx + 20, y});
    m_buttonMenu->addChild(gRightArr);

    m_globalIndexLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_globalIndexLabel->setScale(0.38f);
    m_globalIndexLabel->setColor({180, 180, 180});
    m_globalIndexLabel->setPosition({cx + 48, y});
    m_mainLayer->addChild(m_globalIndexLabel);

    auto gDurDown = createArrowBtn(true, this, menu_selector(TransitionConfigPopup::onGlobalDurDown));
    gDurDown->setPosition({cx + 90, y});
    m_buttonMenu->addChild(gDurDown);

    m_globalDurLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_globalDurLabel->setScale(0.24f);
    m_globalDurLabel->setPosition({cx + 120, y});
    m_mainLayer->addChild(m_globalDurLabel);

    auto gDurUp = createArrowBtn(false, this, menu_selector(TransitionConfigPopup::onGlobalDurUp));
    gDurUp->setPosition({cx + 148, y});
    m_buttonMenu->addChild(gDurUp);

    // Description (hidden — saves vertical space)
    m_globalDescLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_globalDescLabel->setScale(0.35f);
    m_globalDescLabel->setColor({200, 200, 200});
    m_globalDescLabel->setVisible(false);
    m_mainLayer->addChild(m_globalDescLabel);

    // ── Conditional: Color swatch + Custom DSL button ──
    y -= 14;
    m_globalColorSwatch = CCLayerColor::create({0, 0, 0, 255}, 14, 14);
    m_globalColorSwatch->setPosition({cx - 115, y - 7});
    m_mainLayer->addChild(m_globalColorSwatch);

    m_globalColorBtn = createSmallButton("Color", this, menu_selector(TransitionConfigPopup::onGlobalColor));
    m_globalColorBtn->setPosition({cx - 85, y});
    m_buttonMenu->addChild(m_globalColorBtn);

    m_globalCustomBtn = createSmallButton("Edit Custom...", this, menu_selector(TransitionConfigPopup::onGlobalCustom));
    m_globalCustomBtn->setPosition({cx - 35, y});
    m_buttonMenu->addChild(m_globalCustomBtn);

    // ════════════════════════════════════════════════════
    // LEVEL ENTRY SECTION
    // ════════════════════════════════════════════════════
    y -= 20;
    auto lTitle = CCLabelBMFont::create("Level Entry", "goldFont.fnt");
    lTitle->setScale(0.36f);
    lTitle->setPosition({cx - 55, y});
    m_mainLayer->addChild(lTitle);

    auto sepLbl = CCLabelBMFont::create("Override:", "bigFont.fnt");
    sepLbl->setScale(0.22f);
    sepLbl->setPosition({cx + 10, y});
    m_mainLayer->addChild(sepLbl);

    auto onSpr2  = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
    auto offSpr2 = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
    onSpr2->setScale(0.4f);
    offSpr2->setScale(0.4f);
    m_levelToggle = CCMenuItemToggler::create(offSpr2, onSpr2, this, menu_selector(TransitionConfigPopup::onToggleLevelEntry));
    m_levelToggle->toggle(tm.hasLevelEntryConfig());
    m_levelToggle->setPosition({cx + 52, y});
    m_buttonMenu->addChild(m_levelToggle);

    auto lInfo = createInfoBtn(this, menu_selector(TransitionConfigPopup::onInfoLevel));
    lInfo->setPosition({cx + 75, y});
    m_buttonMenu->addChild(lInfo);

    // ── Type selector + Duration (same row) ──
    y -= 16;
    auto lLeftArr = createArrowBtn(true, this, menu_selector(TransitionConfigPopup::onLevelPrevType));
    lLeftArr->setPosition({cx - 115, y});
    m_buttonMenu->addChild(lLeftArr);

    m_levelNameLabel = CCLabelBMFont::create("", "goldFont.fnt");
    m_levelNameLabel->setScale(0.36f);
    m_levelNameLabel->setPosition({cx - 40, y});
    m_mainLayer->addChild(m_levelNameLabel);

    auto lRightArr = createArrowBtn(false, this, menu_selector(TransitionConfigPopup::onLevelNextType));
    lRightArr->setPosition({cx + 20, y});
    m_buttonMenu->addChild(lRightArr);

    m_levelIndexLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_levelIndexLabel->setScale(0.38f);
    m_levelIndexLabel->setColor({180, 180, 180});
    m_levelIndexLabel->setPosition({cx + 48, y});
    m_mainLayer->addChild(m_levelIndexLabel);

    auto lDurDown = createArrowBtn(true, this, menu_selector(TransitionConfigPopup::onLevelDurDown));
    lDurDown->setPosition({cx + 90, y});
    m_buttonMenu->addChild(lDurDown);

    m_levelDurLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_levelDurLabel->setScale(0.24f);
    m_levelDurLabel->setPosition({cx + 120, y});
    m_mainLayer->addChild(m_levelDurLabel);

    auto lDurUp = createArrowBtn(false, this, menu_selector(TransitionConfigPopup::onLevelDurUp));
    lDurUp->setPosition({cx + 148, y});
    m_buttonMenu->addChild(lDurUp);

    // Description (hidden)
    m_levelDescLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_levelDescLabel->setScale(0.35f);
    m_levelDescLabel->setColor({200, 200, 200});
    m_levelDescLabel->setVisible(false);
    m_mainLayer->addChild(m_levelDescLabel);

    // ── Conditional ──
    y -= 14;
    m_levelColorSwatch = CCLayerColor::create({0, 0, 0, 255}, 14, 14);
    m_levelColorSwatch->setPosition({cx - 115, y - 7});
    m_mainLayer->addChild(m_levelColorSwatch);

    m_levelColorBtn = createSmallButton("Color", this, menu_selector(TransitionConfigPopup::onLevelColor));
    m_levelColorBtn->setPosition({cx - 85, y});
    m_buttonMenu->addChild(m_levelColorBtn);

    m_levelCustomBtn = createSmallButton("Edit Custom...", this, menu_selector(TransitionConfigPopup::onLevelCustom));
    m_levelCustomBtn->setPosition({cx - 35, y});
    m_buttonMenu->addChild(m_levelCustomBtn);

    // ════════════════════════════════════════════════════
    // BOTTOM BUTTONS
    // ════════════════════════════════════════════════════
    float btnY = 26;

    auto saveSpr = ButtonSprite::create("Save", "goldFont.fnt", "GJ_button_01.png", .8f);
    saveSpr->setScale(0.6f);
    auto saveBtn = CCMenuItemSpriteExtra::create(saveSpr, this, menu_selector(TransitionConfigPopup::onSave));
    saveBtn->setPosition({cx - 45, btnY});
    m_buttonMenu->addChild(saveBtn);

    m_statusLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_statusLabel->setScale(0.2f);
    m_statusLabel->setColor({100, 255, 100});
    m_statusLabel->setPosition({cx, 8});
    m_mainLayer->addChild(m_statusLabel);

    updateGlobalDisplay();
    updateLevelDisplay();
    updateConditionalButtons();

    paimon::markDynamicPopup(this);
    return true;
}

// ════════════════════════════════════════════════════════════
// Display updates
// ════════════════════════════════════════════════════════════

int TransitionConfigPopup::getTypeIndex(TransitionType t) const {
    auto const& types = TransitionManager::allTypes();
    for (int i = 0; i < (int)types.size(); i++) {
        if (types[i] == t) return i;
    }
    return 0;
}

void TransitionConfigPopup::updateGlobalDisplay() {
    m_globalNameLabel->setString(TransitionManager::typeDisplayName(m_editingGlobal.type).c_str());
    m_globalDescLabel->setString(TransitionManager::typeDescription(m_editingGlobal.type).c_str());

    char buf[16]; snprintf(buf, sizeof(buf), "%.2fs", m_editingGlobal.duration);
    m_globalDurLabel->setString(buf);

    int idx = getTypeIndex(m_editingGlobal.type);
    char idxBuf[16]; snprintf(idxBuf, sizeof(idxBuf), "%d/%d", idx + 1, (int)TransitionManager::allTypes().size());
    m_globalIndexLabel->setString(idxBuf);

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

    char buf[16]; snprintf(buf, sizeof(buf), "%.2fs", m_editingLevel.duration);
    m_levelDurLabel->setString(buf);

    int idx = getTypeIndex(m_editingLevel.type);
    char idxBuf[16]; snprintf(idxBuf, sizeof(idxBuf), "%d/%d", idx + 1, (int)TransitionManager::allTypes().size());
    m_levelIndexLabel->setString(idxBuf);

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
    // Global: show color button only for FadeColor
    bool gShowColor = (m_editingGlobal.type == TransitionType::FadeColor);
    bool gShowCustom = (m_editingGlobal.type == TransitionType::Custom);
    m_globalColorBtn->setVisible(gShowColor);
    m_globalColorSwatch->setVisible(gShowColor);
    m_globalCustomBtn->setVisible(gShowCustom);

    // Level: show color button only for FadeColor
    bool lShowColor = (m_editingLevel.type == TransitionType::FadeColor);
    bool lShowCustom = (m_editingLevel.type == TransitionType::Custom);
    m_levelColorBtn->setVisible(lShowColor);
    m_levelColorSwatch->setVisible(lShowColor);
    m_levelCustomBtn->setVisible(lShowCustom);
}

// ════════════════════════════════════════════════════════════
// Callbacks — Toggle
// ════════════════════════════════════════════════════════════

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

// ════════════════════════════════════════════════════════════
// Callbacks — Type cycling
// ════════════════════════════════════════════════════════════

void TransitionConfigPopup::onGlobalPrevType(CCObject*) { cycleType(m_editingGlobal, -1); updateGlobalDisplay(); }
void TransitionConfigPopup::onGlobalNextType(CCObject*) { cycleType(m_editingGlobal, 1);  updateGlobalDisplay(); }
void TransitionConfigPopup::onLevelPrevType(CCObject*)  { cycleType(m_editingLevel, -1);  updateLevelDisplay(); }
void TransitionConfigPopup::onLevelNextType(CCObject*)  { cycleType(m_editingLevel, 1);   updateLevelDisplay(); }

// ════════════════════════════════════════════════════════════
// Callbacks — Duration
// ════════════════════════════════════════════════════════════

void TransitionConfigPopup::onGlobalDurDown(CCObject*) { m_editingGlobal.duration = std::max(0.05f, m_editingGlobal.duration - 0.05f); updateGlobalDisplay(); }
void TransitionConfigPopup::onGlobalDurUp(CCObject*)   { m_editingGlobal.duration = std::min(3.0f,  m_editingGlobal.duration + 0.05f); updateGlobalDisplay(); }
void TransitionConfigPopup::onLevelDurDown(CCObject*)  { m_editingLevel.duration  = std::max(0.05f, m_editingLevel.duration  - 0.05f); updateLevelDisplay(); }
void TransitionConfigPopup::onLevelDurUp(CCObject*)    { m_editingLevel.duration  = std::min(3.0f,  m_editingLevel.duration  + 0.05f); updateLevelDisplay(); }

// ════════════════════════════════════════════════════════════
// Callbacks — Color (simple R/G/B cycle for now)
// ════════════════════════════════════════════════════════════

static void cycleColor(TransitionConfig& cfg) {
    // Cycle through some preset colors
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

// ════════════════════════════════════════════════════════════
// Callbacks — Custom DSL
// ════════════════════════════════════════════════════════════

void TransitionConfigPopup::onGlobalCustom(CCObject*) {
    m_editingIsGlobal = true;
    auto popup = CustomTransitionEditorPopup::create(&m_editingGlobal, true);
    if (popup) popup->show();
}

void TransitionConfigPopup::onLevelCustom(CCObject*) {
    m_editingIsGlobal = false;
    auto popup = CustomTransitionEditorPopup::create(&m_editingLevel, false);
    if (popup) popup->show();
}

// ════════════════════════════════════════════════════════════
// Callbacks — Info popups
// ════════════════════════════════════════════════════════════

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
        "Great for having a dramatic entry effect for levels!"
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
        "<cy>Custom:</c> Your own DSL script commands!\n"
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

// ════════════════════════════════════════════════════════════
// Save
// ════════════════════════════════════════════════════════════

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
