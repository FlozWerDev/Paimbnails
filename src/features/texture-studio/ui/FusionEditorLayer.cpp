#include "FusionEditorLayer.hpp"

#include "../data/PlistParser.hpp"
#include "../data/SpritesheetReader.hpp"
#include "../persist/FusionStore.hpp"
#include "../persist/SlotPaths.hpp"
#include "../persist/SlotStore.hpp"
#include "../services/FramePixelCache.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/ThreadTracker.hpp"
#include "ParamSliderRow.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <system_error>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

constexpr float kMargin = 8.f;
constexpr float kHeaderH = 32.f;
constexpr float kFooterH = 26.f;
constexpr float kToolsW = 218.f;
constexpr float kStripH = 52.f;
constexpr float kColGap = 10.f;

char const* blendLabel(FusionBlendMode m) {
    switch (m) {
        case FusionBlendMode::Replace: return "Replace";
        case FusionBlendMode::Overlay: return "Overlay";
        case FusionBlendMode::MultiplyLuma:
        default: return "Luma";
    }
}

char const* fitLabel(ImageFitMode m) {
    switch (m) {
        case ImageFitMode::Fill: return "Fill";
        case ImageFitMode::Stretch: return "Stretch";
        case ImageFitMode::Fit:
        default: return "Fit";
    }
}

void fitInto(CCSprite* spr, float box) {
    if (!spr) return;
    auto sz = spr->getContentSize();
    if (sz.width <= 0 || sz.height <= 0) return;
    float maxSide = box - 14.f;
    spr->setScale(std::min({maxSide / sz.width, maxSide / sz.height, 4.f}));
}

}  // namespace

FusionEditorLayer* FusionEditorLayer::create(std::string slotId, std::string frameName) {
    auto* ret = new FusionEditorLayer();
    if (ret->init(std::move(slotId), std::move(frameName))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

CCScene* FusionEditorLayer::scene(std::string slotId, std::string frameName) {
    auto* scene = CCScene::create();
    if (auto* layer = FusionEditorLayer::create(std::move(slotId), std::move(frameName))) {
        scene->addChild(layer);
    }
    return scene;
}

void FusionEditorLayer::open(std::string slotId, std::string frameName) {
    if (auto* layer = FusionEditorLayer::create(std::move(slotId), std::move(frameName))) {
        geode::pushSceneWithLayer(layer);
    }
}

FusionEditorLayer::~FusionEditorLayer() {
    m_closed->store(true, std::memory_order_release);
    m_loadGen->fetch_add(1, std::memory_order_acq_rel);
    m_renderGen->fetch_add(1, std::memory_order_acq_rel);
    stopAnim();
    m_asset.reset();
    m_mask.reset();
    m_pixels.reset();
}

bool FusionEditorLayer::init(std::string slotId, std::string frameName) {
    if (!CCLayer::init()) return false;
    this->setKeypadEnabled(true);
    this->setKeyboardEnabled(true);
    this->setTouchEnabled(true);
    this->setID("fusion-editor-layer"_spr);
    m_slotId = std::move(slotId);

    auto loaded = SlotStore::get().loadSlot(m_slotId);
    if (!loaded) {
        Notification::create(("Cannot load slot: " + loaded.unwrapErr()).c_str(),
            NotificationIcon::Error, 3.f)->show();
        return true;
    }
    m_project = loaded.unwrap();

    buildBackground();
    buildHeader();
    buildPreviews();
    buildTools();
    buildSpriteStrip();
    buildEntries();

    if (!frameName.empty()) {
        selectFrame(frameName);
    } else if (!m_entries.empty()) {
        selectFrame(m_entries.front().frameName);
    } else {
        setStatus("No button sprites in this pack.");
    }
    return true;
}

void FusionEditorLayer::keyBackClicked() { onBack(nullptr); }

void FusionEditorLayer::keyDown(enumKeyCodes key, double timestamp) {
    if (m_hasSelection) {
        auto* kb = CCDirector::sharedDirector()->getKeyboardDispatcher();
        bool ctrl = kb && kb->getControlKeyPressed();
        bool shift = kb && kb->getShiftKeyPressed();
        if (ctrl && key == KEY_Z) { onUndo(); return; }
        if (m_asset && !m_asset->empty()) {
            int step = shift ? 10 : 1;
            if (key == KEY_Left)  { onNudge(-step, 0); return; }
            if (key == KEY_Right) { onNudge(+step, 0); return; }
            if (key == KEY_Up)    { onNudge(0, -step); return; }
            if (key == KEY_Down)  { onNudge(0, +step); return; }
        }
    }
    CCLayer::keyDown(key, timestamp);
}

void FusionEditorLayer::onBack(CCObject*) {
    stopAnim();
    (void)SlotStore::get().saveSlot(m_project);
    CCDirector::get()->popSceneWithTransition(0.35f, PopTransition::kPopTransitionFade);
}

void FusionEditorLayer::registerWithTouchDispatcher() {
    CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(
        this, 0, true);
}

// ---------------------------------------------------------------------------
// Layout

void FusionEditorLayer::buildBackground() {
    auto win = CCDirector::get()->getWinSize();
    auto* bg = CCLayerColor::create(ccc4(14, 12, 22, 255));
    bg->setContentSize(win);
    this->addChild(bg, -5);
    auto* g = CCLayerGradient::create(ccc4(50, 28, 70, 100), ccc4(8, 6, 14, 160));
    g->setContentSize(win);
    g->setVector({0, -1});
    this->addChild(g, -4);
}

void FusionEditorLayer::buildHeader() {
    auto win = CCDirector::get()->getWinSize();
    auto* menu = CCMenu::create();
    menu->setPosition({0, 0});
    this->addChild(menu, 20);

    if (auto* back = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
        back->setScale(0.7f);
        if (auto* btn = CCMenuItemExt::createSpriteExtra(back,
                [this](CCMenuItemSpriteExtra*) { this->onBack(nullptr); })) {
            btn->setPosition({22.f, win.height - kHeaderH * 0.5f});
            menu->addChild(btn);
        }
    }
    if (auto* title = CCLabelBMFont::create("Fusion Layer", "goldFont.fnt")) {
        title->setScale(0.48f);
        title->setPosition({win.width * 0.5f, win.height - kHeaderH * 0.5f + 2.f});
        this->addChild(title, 10);
        m_titleLbl = title;
    }
    if (auto* saveSpr = ButtonSprite::create("Save", "goldFont.fnt", "GJ_button_05.png", 0.32f)) {
        if (auto* btn = CCMenuItemExt::createSpriteExtra(saveSpr,
                [this](CCMenuItemSpriteExtra*) {
                    auto r = SlotStore::get().saveSlot(m_project);
                    if (!r) {
                        Notification::create(("Save failed: " + r.unwrapErr()).c_str(),
                            NotificationIcon::Error, 2.5f)->show();
                    } else {
                        setStatus("Saved.");
                        Notification::create("Fusion saved.", NotificationIcon::Success, 1.2f)->show();
                    }
                })) {
            btn->setPosition({win.width - 40.f, win.height - kHeaderH * 0.5f});
            menu->addChild(btn);
        }
    }
    if (auto* st = CCLabelBMFont::create("", "chatFont.fnt")) {
        st->setScale(0.38f);
        st->setColor({180, 185, 195});
        st->setAnchorPoint({0.f, 0.5f});
        st->setPosition({48.f, win.height - kHeaderH * 0.5f - 1.f});
        this->addChild(st, 10);
        m_statusLbl = st;
    }
}

void FusionEditorLayer::buildPreviews() {
    auto win = CCDirector::get()->getWinSize();
    const float toolsX = win.width - kMargin - kToolsW;
    const float areaW = toolsX - kMargin - kColGap - kMargin;
    // two big boxes side by side
    const float boxW = (areaW - kColGap) * 0.5f;
    const float boxH = win.height - kHeaderH - kFooterH - kStripH - 28.f;
    const float boxSize = std::min(boxW, boxH);
    const float cy = kFooterH + kStripH + 12.f + boxSize * 0.5f;
    const float leftCx = kMargin + boxSize * 0.5f;
    const float rightCx = leftCx + boxSize + kColGap;

    auto makeBox = [this, boxSize](float cx, float cy, char const* cap) -> CCNode* {
        auto* host = CCNode::create();
        host->setContentSize({boxSize, boxSize});
        host->setAnchorPoint({0.5f, 0.5f});
        host->setPosition({cx, cy});
        this->addChild(host, 5);
        if (auto* frame = CCScale9Sprite::create("GJ_square01.png")) {
            frame->setContentSize({boxSize, boxSize});
            frame->setColor({22, 22, 30});
            host->addChildAtPosition(frame, Anchor::Center);
        }
        if (auto* lbl = CCLabelBMFont::create(cap, "bigFont.fnt")) {
            lbl->setScale(0.32f);
            lbl->setColor({170, 175, 185});
            host->addChildAtPosition(lbl, Anchor::Top, {0.f, 10.f});
        }
        if (auto* loading = CCLabelBMFont::create("...", "bigFont.fnt")) {
            loading->setScale(0.4f);
            loading->setColor({110, 110, 120});
            loading->setID("loading");
            host->addChildAtPosition(loading, Anchor::Center);
        }
        return host;
    };

    m_originalHost = makeBox(leftCx, cy, "ORIGINAL  (tap = fill)");
    m_resultHost   = makeBox(rightCx, cy, "RESULT  (drag = move)");

    if (auto* fl = CCLabelBMFont::create("", "chatFont.fnt")) {
        fl->setScale(0.4f);
        fl->setColor({200, 200, 210});
        fl->setPosition({(leftCx + rightCx) * 0.5f, cy - boxSize * 0.5f - 12.f});
        this->addChild(fl, 5);
        m_frameLbl = fl;
    }
}

void FusionEditorLayer::buildTools() {
    auto win = CCDirector::get()->getWinSize();
    const float panelX = win.width - kMargin - kToolsW;
    const float panelBottom = kFooterH + 6.f;
    const float panelH = win.height - kHeaderH - panelBottom - 4.f;
    const float w = kToolsW;

    auto* panel = CCNode::create();
    panel->setContentSize({w, panelH});
    panel->setAnchorPoint({0.f, 0.f});
    panel->setPosition({panelX, panelBottom});
    this->addChild(panel, 8);

    if (auto* bg = CCScale9Sprite::create("square02b_001.png")) {
        bg->setContentSize({w, panelH});
        bg->setColor({0, 0, 0});
        bg->setOpacity(100);
        panel->addChildAtPosition(bg, Anchor::Center);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0, 0});
    menu->setContentSize({w, panelH});
    panel->addChild(menu);

    float y = panelH - 14.f;
    if (auto* cap = CCLabelBMFont::create("TOOLS", "goldFont.fnt")) {
        cap->setScale(0.36f);
        cap->setPosition({w * 0.5f, y});
        panel->addChild(cap);
    }
    y -= 22.f;

    auto* row1 = CCMenu::create();
    row1->setContentSize({w - 10.f, 20.f});
    row1->setAnchorPoint({0.5f, 0.5f});
    row1->setPosition({w * 0.5f, y});
    row1->setLayout(RowLayout::create()->setGap(3.f)
        ->setAxisAlignment(AxisAlignment::Even)->setAutoScale(false));
    panel->addChild(row1);

    auto addToMenu = [](CCMenu* row, char const* lab, char const* bg,
                        std::function<void()> fn, CCMenuItemSpriteExtra** store = nullptr) {
        if (auto* spr = ButtonSprite::create(
                lab, 62, true, "bigFont.fnt", bg, 20.f, 0.42f)) {
            if (auto* btn = CCMenuItemExt::createSpriteExtra(spr,
                    [fn](CCMenuItemSpriteExtra*) { fn(); })) {
                row->addChild(btn);
                if (store) *store = btn;
            }
        }
    };
    addToMenu(row1, "Pick", "GJ_button_05.png", [this] { onPickTexture(); });
    addToMenu(row1, "Clear", "GJ_button_06.png", [this] { onClearFusion(); });
    addToMenu(row1, "Undo", "GJ_button_06.png", [this] { onUndo(); }, &m_undoBtn);
    row1->updateLayout();
    y -= 24.f;

    auto* row2 = CCMenu::create();
    row2->setContentSize({w - 10.f, 20.f});
    row2->setAnchorPoint({0.5f, 0.5f});
    row2->setPosition({w * 0.5f, y});
    row2->setLayout(RowLayout::create()->setGap(3.f)
        ->setAxisAlignment(AxisAlignment::Even)->setAutoScale(false));
    panel->addChild(row2);
    addToMenu(row2, "Replace", "GJ_button_04.png", [this] { onCycleBlend(); }, &m_blendBtn);
    addToMenu(row2, "Fill", "GJ_button_04.png", [this] { onCycleFit(); }, &m_fitBtn);
    addToMenu(row2, "0,0", "GJ_button_04.png", [this] { onResetPlacement(); });
    row2->updateLayout();
    y -= 22.f;

    if (auto* st = CCLabelBMFont::create("no texture", "bigFont.fnt")) {
        st->setScale(0.2f);
        st->setColor({170, 170, 180});
        st->setPosition({w * 0.5f, y});
        panel->addChild(st);
        m_stateLbl = st;
    }
    y -= 18.f;

    // Paint + nudge
    auto* row3 = CCMenu::create();
    row3->setContentSize({w - 8.f, 22.f});
    row3->setAnchorPoint({0.5f, 0.5f});
    row3->setPosition({w * 0.5f, y});
    panel->addChild(row3);

    auto* paintTog = CCMenuItemExt::createTogglerWithStandardSprites(0.4f,
        [this](CCMenuItemToggler* t) {
            if (!t) return;
            m_paintArmed = !t->isToggled();
            refreshToolsUi();
        });
    if (paintTog) {
        paintTog->toggle(true);
        paintTog->setPosition({12.f, 11.f});
        row3->addChild(paintTog);
        m_paintTog = paintTog;
        if (auto* lbl = CCLabelBMFont::create("Paint", "bigFont.fnt")) {
            lbl->setScale(0.22f);
            lbl->setAnchorPoint({0.f, 0.5f});
            lbl->setPosition({paintTog->getContentSize().width / 2.f + 2.f, 0.f});
            paintTog->addChild(lbl);
        }
    }
    auto nudge = [this, row3](char const* lab, float x, int dx, int dy) {
        if (auto* spr = ButtonSprite::create(
                lab, 26, true, "bigFont.fnt", "GJ_button_04.png", 18.f, 0.46f)) {
            if (auto* btn = CCMenuItemExt::createSpriteExtra(spr,
                    [this, dx, dy](CCMenuItemSpriteExtra*) { onNudge(dx, dy); })) {
                btn->setPosition({x, 11.f});
                row3->addChild(btn);
            }
        }
    };
    nudge("<", 92.f, -1, 0);
    nudge(">", 122.f, 1, 0);
    nudge("^", 152.f, 0, -1);
    nudge("v", 182.f, 0, 1);
    y -= 20.f;

    if (auto* px = CCLabelBMFont::create("px +0, +0", "chatFont.fnt")) {
        px->setScale(0.34f);
        px->setColor({255, 200, 120});
        px->setPosition({w * 0.5f, y});
        panel->addChild(px);
        m_pixelLbl = px;
    }
    y -= 16.f;

    // flips
    auto* flipRow = CCMenu::create();
    flipRow->setContentSize({w - 10.f, 18.f});
    flipRow->setAnchorPoint({0.5f, 0.5f});
    flipRow->setPosition({w * 0.5f, y});
    panel->addChild(flipRow);
    auto mkFlip = [this, flipRow](char const* lab, float x,
                                  CCMenuItemToggler*& out, bool isX) {
        auto* tog = CCMenuItemExt::createTogglerWithStandardSprites(0.36f,
            [this, isX](CCMenuItemToggler* t) {
                if (!t || !m_hasSelection) return;
                auto s = currentSetting();
                bool v = !t->isToggled();
                if (isX) s.fusionTransform.flipX = v;
                else s.fusionTransform.flipY = v;
                storeSetting(s);
                if (s.hasFusion) persistMask();
                refreshPreview();
        });
        if (!tog) return;
        tog->setPosition({x, 9.f});
        flipRow->addChild(tog);
        out = tog;
        if (auto* lbl = CCLabelBMFont::create(lab, "bigFont.fnt")) {
            lbl->setScale(0.22f);
            lbl->setAnchorPoint({0.f, 0.5f});
            lbl->setPosition({tog->getContentSize().width / 2.f + 2.f, 0.f});
            tog->addChild(lbl);
        }
    };
    mkFlip("Flip X", 50.f, m_flipXTog, true);
    mkFlip("Flip Y", 135.f, m_flipYTog, false);
    y -= 18.f;

    if (auto* hint = CCLabelBMFont::create(
            "Original colours. Texture never recolored.", "chatFont.fnt")) {
        hint->setScale(0.28f);
        hint->setColor({150, 155, 165});
        hint->limitLabelWidth(w - 8.f, 0.28f, 0.12f);
        hint->setPosition({w * 0.5f, y});
        panel->addChild(hint);
        m_hintLbl = hint;
    }
    y -= 16.f;

    auto pct = [](float v) { return fmt::format("{:.0f}%", v * 100.f); };
    struct Row {
        char const* lab; float minV, maxV, step;
        std::function<float(SpriteSetting const&)> get;
        std::function<void(SpriteSetting&, float)> set;
        ParamSliderRow::Formatter fmt;
        ParamSliderRow** store;
    };
    const Row rows[] = {
        {"Scale", 0.1f, 4.f, 0.f,
         [](SpriteSetting const& s) { return s.fusionTransform.scale; },
         [](SpriteSetting& s, float v) { s.fusionTransform.scale = v; },
         pct, &m_scaleRow},
        {"Off X", -1.5f, 1.5f, 0.f,
         [](SpriteSetting const& s) { return s.fusionTransform.offsetX; },
         [](SpriteSetting& s, float v) { s.fusionTransform.offsetX = v; },
         [](float v) { return fmt::format("{:+.0f}%", v * 100.f); }, &m_offXRow},
        {"Off Y", -1.5f, 1.5f, 0.f,
         [](SpriteSetting const& s) { return s.fusionTransform.offsetY; },
         [](SpriteSetting& s, float v) { s.fusionTransform.offsetY = v; },
         [](float v) { return fmt::format("{:+.0f}%", v * 100.f); }, &m_offYRow},
        {"Rot", 0.f, 360.f, 1.f,
         [](SpriteSetting const& s) { return s.fusionTransform.rotationDeg; },
         [](SpriteSetting& s, float v) { s.fusionTransform.rotationDeg = v; },
         [](float v) { return fmt::format("{:.0f}", v); }, &m_rotRow},
        {"Opacity", 0.f, 1.f, 0.f,
         [](SpriteSetting const& s) { return s.fusionOpacity; },
         [](SpriteSetting& s, float v) { s.fusionOpacity = v; },
         pct, &m_opacRow},
        // Colour radius: how different a shade can be (light vs dark yellow).
        // Lower if fill eats white ring / BPM letters. Raise if shade gaps remain.
        {"Color R", 20.f, 220.f, 1.f,
         [](SpriteSetting const& s) { return static_cast<float>(s.fusionTolerance); },
         [](SpriteSetting& s, float v) {
             s.fusionTolerance = static_cast<int>(std::lround(v));
         },
         [](float v) { return fmt::format("{:.0f}", v); }, &m_tolRow},
        // Spatial expand: grow fill by N px into SAME-colour neighbours only.
        {"Expand", 0.f, 8.f, 1.f,
         [](SpriteSetting const& s) {
             return static_cast<float>(s.fusionExpandRadius);
         },
         [](SpriteSetting& s, float v) {
             s.fusionExpandRadius = static_cast<int>(std::lround(v));
         },
         [](float v) { return fmt::format("{:.0f}px", v); }, &m_expandRow},
    };
    for (auto const& def : rows) {
        auto set = def.set;
        auto* row = ParamSliderRow::create(def.lab, def.minV, def.maxV, def.step,
            def.get(SpriteSetting{}), w - 12.f,
            [this, set](float v) {
                if (!m_hasSelection) return;
                auto s = currentSetting();
                set(s, v);
                storeSetting(s);
                // Transform/opacity changed → rebuild stamp once, then fast paint.
                invalidateStampCache();
                if (s.hasFusion) persistMask();
                ensureStampCache();
                renderPreviewFast();
                refreshToolsUi();
            }, def.fmt);
        if (row) {
            row->setPosition({6.f, y});
            panel->addChild(row);
            *def.store = row;
        }
        y -= 18.f;
    }
}

void FusionEditorLayer::buildSpriteStrip() {
    auto win = CCDirector::get()->getWinSize();
    auto* host = CCNode::create();
    host->setContentSize({win.width - kToolsW - kMargin * 3.f, kStripH});
    host->setAnchorPoint({0.f, 0.f});
    host->setPosition({kMargin, kFooterH * 0.35f});
    this->addChild(host, 6);
    m_stripHost = host;
    if (auto* bg = CCScale9Sprite::create("square02b_001.png")) {
        bg->setContentSize(host->getContentSize());
        bg->setColor({0, 0, 0});
        bg->setOpacity(70);
        host->addChildAtPosition(bg, Anchor::Center);
    }
}

// ---------------------------------------------------------------------------
// Data

void FusionEditorLayer::buildEntries() {
    m_entries.clear();
    for (int i = 0; i < static_cast<int>(m_project.sheets.size()); ++i) {
        auto const& sheet = m_project.sheets[i];
        auto parsed = PlistParser::parseFile(
            std::filesystem::path(sheet.sourcePlistPath));
        if (!parsed) continue;
        for (auto const& frame : parsed.unwrap().frames) {
            auto kind = UiSpriteCatalog::classify(frame.name, sheet.baseName);
            if (kind != SpriteKind::Button && kind != SpriteKind::MenuUi) continue;
            m_entries.push_back({frame.name, i, kind});
        }
    }
    refreshSpriteStrip();
}

void FusionEditorLayer::refreshSpriteStrip() {
    if (!m_stripHost) return;
    // rebuild clickable chips
    m_stripHost->removeChildByID("strip-menu");
    auto* menu = CCMenu::create();
    menu->setID("strip-menu");
    menu->setPosition({0, 0});
    menu->setContentSize(m_stripHost->getContentSize());
    m_stripHost->addChild(menu);

    float x = 8.f;
    float y = kStripH * 0.5f;
    int shown = 0;
    for (auto const& e : m_entries) {
        if (shown >= 18) break; // keep strip light
        bool sel = m_hasSelection && e.frameName == m_selected.frameName;
        bool hasF = m_project.spriteSettings.count(e.frameName)
            && m_project.spriteSettings[e.frameName].hasFusion;
        std::string shortName = e.frameName;
        if (auto p = shortName.rfind("_001.png"); p != std::string::npos) shortName.resize(p);
        else if (auto p2 = shortName.rfind(".png"); p2 != std::string::npos) shortName.resize(p2);
        if (shortName.size() > 14) shortName = shortName.substr(0, 12) + "..";
        if (hasF) shortName = "F " + shortName;

        auto* spr = ButtonSprite::create(shortName.c_str(), "bigFont.fnt",
            sel ? "GJ_button_01.png" : "GJ_button_04.png", 0.22f);
        if (!spr) continue;
        auto nameCopy = e.frameName;
        if (auto* btn = CCMenuItemExt::createSpriteExtra(spr,
                [this, nameCopy](CCMenuItemSpriteExtra*) {
                    this->selectFrame(nameCopy);
                })) {
            btn->setPosition({x + spr->getContentSize().width * 0.5f * 0.22f / 0.22f, y});
            // Use actual scaled width approx
            auto cs = btn->getContentSize();
            btn->setPosition({x + cs.width * 0.5f, y});
            menu->addChild(btn);
            x += cs.width + 4.f;
            ++shown;
            if (x > m_stripHost->getContentSize().width - 10.f) break;
        }
    }
}

void FusionEditorLayer::selectFrame(std::string const& frameName) {
    unloadFusion();
    m_hasSelection = false;
    for (auto const& e : m_entries) {
        if (e.frameName == frameName) {
            m_selected = e;
            m_hasSelection = true;
            break;
        }
    }
    if (!m_hasSelection) return;
    refreshSpriteStrip();
    if (m_frameLbl) {
        m_frameLbl->setString(frameName.c_str());
        m_frameLbl->limitLabelWidth(280.f, 0.4f, 0.18f);
    }
    loadPixelsForSelection();
    // Load an already-picked texture even when no region is currently painted.
    loadFusionForSelection();
    refreshToolsUi();
    setStatus("Selected — Pick texture, then tap Original to fill.");
}

SpriteSetting FusionEditorLayer::currentSetting() const {
    SpriteSetting s;
    s.color1 = m_project.color1;
    s.color2 = m_project.color2;
    s.colorGlow = m_project.colorGlow;
    s.colorDetail = m_project.colorDetail;
    if (!m_hasSelection) return s;
    auto it = m_project.spriteSettings.find(m_selected.frameName);
    if (it != m_project.spriteSettings.end()) return it->second;
    return s;
}

void FusionEditorLayer::storeSetting(SpriteSetting const& s) {
    if (!m_hasSelection) return;
    // Always keep the entry while editing Fusion. Color R / Expand / scale /
    // offsets must persist even before a texture is loaded (hasFusion false).
    // hasAny() alone erased them and made sliders snap back to defaults.
    m_project.spriteSettings[m_selected.frameName] = s;
    m_project.modifiedAt = nowUnixMs();
}

FusionApplyOptions FusionEditorLayer::makeFusionOptions(SpriteSetting const& s) const {
    FusionApplyOptions o;
    o.blendMode = s.fusionBlend;
    o.opacity = s.fusionOpacity;
    o.transform = s.fusionTransform;
    o.pixelOffsetX = s.fusionPixelX;
    o.pixelOffsetY = s.fusionPixelY;
    if (o.transform.isDefault()) o.transform.fitMode = ImageFitMode::Fill;
    return o;
}

void FusionEditorLayer::setStatus(std::string const& text) {
    if (m_statusLbl) {
        m_statusLbl->setString(text.c_str());
        m_statusLbl->limitLabelWidth(220.f, 0.38f, 0.16f);
    }
}

// ---------------------------------------------------------------------------
// Load / preview

void FusionEditorLayer::loadPixelsForSelection() {
    if (!m_hasSelection) return;
    if (m_selected.sheetIndex < 0
        || m_selected.sheetIndex >= static_cast<int>(m_project.sheets.size())) return;

    int gen = m_loadGen->fetch_add(1, std::memory_order_acq_rel) + 1;
    auto loadGen = m_loadGen;
    auto closed = m_closed;
    auto sheet = m_project.sheets[m_selected.sheetIndex];
    std::string frameName = m_selected.frameName;
    WeakRef<FusionEditorLayer> weakSelf(this);

    paimon::ThreadTracker::get().spawn(
        [weakSelf, loadGen, closed, gen, sheet, frameName]() {
        if (paimon::isRuntimeShuttingDown() || closed->load(std::memory_order_acquire)) return;
        auto dataRes = FramePixelCache::get().frameData(
            std::filesystem::path(sheet.sourcePlistPath),
            std::filesystem::path(sheet.sourcePngPath), frameName);
        if (!dataRes) return;
        auto data = std::make_shared<FramePixelCache::FrameData>(
            std::move(dataRes).unwrap());
        auto logical = std::make_shared<ImageBuffer>(
            SpritesheetReader::composeLogicalFrame(data->pixels, data->info));

        Loader::get()->queueInMainThread(
            [weakSelf, loadGen, closed, gen, data, logical]() {
            if (paimon::isRuntimeShuttingDown()
                || closed->load(std::memory_order_acquire)
                || loadGen->load(std::memory_order_acquire) != gen) return;
            auto self = weakSelf.lock();
            if (!self || !self->getParent()) return;
            self->m_pixels = std::make_shared<ImageBuffer>(data->pixels);
            self->m_frameInfo = data->info;
            if (auto* spr = SpritePreviewRenderer::createSprite(*logical)) {
                self->setOriginalSprite(spr);
            }
            self->refreshPreview();
        });
    });
}

void FusionEditorLayer::setOriginalSprite(CCSprite* spr) {
    if (!m_originalHost || !spr) return;
    if (m_originalSpr) m_originalSpr->removeFromParent();
    if (auto* l = m_originalHost->getChildByID("loading")) l->removeFromParent();
    fitInto(spr, m_originalHost->getContentSize().width);
    m_originalHost->addChildAtPosition(spr, Anchor::Center);
    m_originalSpr = spr;
}

void FusionEditorLayer::setResultSprite(CCSprite* spr) {
    if (!m_resultHost || !spr) return;
    if (m_resultSpr) m_resultSpr->removeFromParent();
    if (auto* l = m_resultHost->getChildByID("loading")) l->removeFromParent();
    fitInto(spr, m_resultHost->getContentSize().width);
    m_resultHost->addChildAtPosition(spr, Anchor::Center);
    m_resultSpr = spr;
}

void FusionEditorLayer::invalidateStampCache() {
    m_cacheValid = false;
    m_cachedStamp = ImageBuffer();
    m_cachedCoverage.clear();
    m_cacheFrame = -1;
    m_cachePixelX = 0;
    m_cachePixelY = 0;
}

void FusionEditorLayer::ensureStampCache() {
    if (!m_pixels || m_pixels->empty()) { invalidateStampCache(); return; }
    if (!m_mask || m_mask->empty() || !m_asset || m_asset->empty()) {
        invalidateStampCache();
        return;
    }
    auto s = currentSetting();
    ImageTransform t = s.fusionTransform;
    if (t.isDefault()) t.fitMode = ImageFitMode::Fill;
    // Pixel offsets are NOT baked into the stamp — applied at composite time.
    int fi = static_cast<int>(m_frameIndex);
    int w = m_pixels->width(), h = m_pixels->height();
    bool same =
        m_cacheValid
        && m_cacheFrame == fi
        && m_cacheW == w && m_cacheH == h
        && m_cachePixelX == s.fusionPixelX
        && m_cachePixelY == s.fusionPixelY
        && m_cacheBlend == s.fusionBlend
        && std::fabs(m_cacheOpacity - s.fusionOpacity) < 1e-4f
        && m_cacheTransform.fitMode == t.fitMode
        && std::fabs(m_cacheTransform.scale - t.scale) < 1e-4f
        && std::fabs(m_cacheTransform.offsetX - t.offsetX) < 1e-4f
        && std::fabs(m_cacheTransform.offsetY - t.offsetY) < 1e-4f
        && std::fabs(m_cacheTransform.rotationDeg - t.rotationDeg) < 1e-3f
        && m_cacheTransform.opacity == t.opacity
        && m_cacheTransform.flipX == t.flipX
        && m_cacheTransform.flipY == t.flipY
        && m_cachedCoverage.size() == static_cast<std::size_t>(w) * h
        && !m_cachedStamp.empty();

    if (same) return;

    m_cachedCoverage = FusionEngine::softCoverage(*m_mask);
    // Stamp is fitted into the painted region (mask bounds), not the whole
    // frame — stops the texture looking cropped to a random slice of the sprite.
    m_cachedStamp = FusionEngine::buildStampCanvas(
        m_asset->frameAt(static_cast<std::size_t>(fi)), w, h, t, m_mask.get(),
        s.fusionPixelX, s.fusionPixelY);
    m_cacheValid = !m_cachedStamp.empty() && !m_cachedCoverage.empty();
    m_cacheFrame = fi;
    m_cacheW = w;
    m_cacheH = h;
    m_cachePixelX = s.fusionPixelX;
    m_cachePixelY = s.fusionPixelY;
    m_cacheTransform = t;
    m_cacheOpacity = s.fusionOpacity;
    m_cacheBlend = s.fusionBlend;
}

void FusionEditorLayer::renderPreviewFast() {
    // Main-thread only: base copy + cached stamp shifted by pixel offset.
    // Avoids worker threads and full bilinear re-sample every mouse move.
    m_fastPreviewPending = false;
    if (!m_pixels || m_pixels->empty()) return;

    auto s = currentSetting();
    ImageBuffer out = *m_pixels;
    if (s.hasFusion && m_mask && !m_mask->empty() && m_asset && !m_asset->empty()) {
        ensureStampCache();
        if (m_cacheValid) {
            auto opts = makeFusionOptions(s);
            // Placement is baked into the stamp so source pixels beyond the
            // mask bounds remain available while the image is moved.
            opts.pixelOffsetX = 0;
            opts.pixelOffsetY = 0;
            FusionEngine::applyCached(out, m_cachedCoverage, m_cachedStamp, opts);
        }
    }
    out = SpritesheetReader::composeLogicalFrame(out, m_frameInfo);
    if (auto* spr = SpritePreviewRenderer::createSprite(out)) {
        setResultSprite(spr);
    }
}

void FusionEditorLayer::refreshPreview() {
    this->unschedule(schedule_selector(FusionEditorLayer::renderPreview));
    if (m_pixels && !m_pixels->empty()) {
        // Full-quality async path (after paint / slider / drag end).
        this->scheduleOnce(schedule_selector(FusionEditorLayer::renderPreview), 0.02f);
    }
}

void FusionEditorLayer::renderPreview(float) {
    if (!m_pixels || m_pixels->empty()) return;

    // Prefer cached path when possible (same visual, less work).
    auto s = currentSetting();
    if (s.hasFusion && m_mask && !m_mask->empty() && m_asset && !m_asset->empty()) {
        ensureStampCache();
        if (m_cacheValid) {
            renderPreviewFast();
            return;
        }
    }

    int gen = m_renderGen->fetch_add(1, std::memory_order_acq_rel) + 1;
    auto renderGen = m_renderGen;
    auto closed = m_closed;
    auto pixels = m_pixels;
    auto mask = m_mask;
    auto asset = m_asset;
    auto frameInfo = m_frameInfo;
    std::size_t fi = m_frameIndex;
    auto opts = makeFusionOptions(s);
    WeakRef<FusionEditorLayer> weakSelf(this);

    paimon::ThreadTracker::get().spawn(
        [weakSelf, renderGen, closed, gen, pixels, mask, asset, frameInfo, fi, opts, s]() {
        if (paimon::isRuntimeShuttingDown() || closed->load(std::memory_order_acquire)) return;

        ImageBuffer out = *pixels;
        if (s.hasFusion && mask && !mask->empty() && asset && !asset->empty()) {
            FusionEngine::apply(out, *mask, asset->frameAt(fi), opts);
        }
        out = SpritesheetReader::composeLogicalFrame(out, frameInfo);
        auto img = std::make_shared<ImageBuffer>(std::move(out));

        Loader::get()->queueInMainThread(
            [weakSelf, renderGen, closed, gen, img]() {
            if (paimon::isRuntimeShuttingDown()
                || closed->load(std::memory_order_acquire)
                || renderGen->load(std::memory_order_acquire) != gen) return;
            auto self = weakSelf.lock();
            if (!self || !self->getParent()) return;
            if (auto* spr = SpritePreviewRenderer::createSprite(*img)) {
                self->setResultSprite(spr);
            }
        });
    });
}

// ---------------------------------------------------------------------------
// Fusion actions

void FusionEditorLayer::onPickTexture() {
    if (!m_hasSelection) {
        Notification::create("Select a sprite in the strip first.",
            NotificationIcon::Info, 1.5f)->show();
        return;
    }
    WeakRef<FusionEditorLayer> weakSelf(this);
    std::string frameName = m_selected.frameName;
    std::string slotId = m_slotId;
    pt::pickImage([weakSelf, frameName, slotId](
            geode::Result<std::optional<std::filesystem::path>> result) {
        auto self = weakSelf.lock();
        if (!self) return;
        auto pathOpt = std::move(result).unwrapOr(std::nullopt);
        if (!pathOpt || pathOpt->empty()) return;
        std::filesystem::path srcPath = *pathOpt;
        paimon::ThreadTracker::get().spawn([weakSelf, frameName, slotId, srcPath]() {
            if (paimon::isRuntimeShuttingDown()) return;
            auto importRes = FusionStore::importTexture(slotId, frameName, srcPath);
            std::shared_ptr<FusionAsset> asset;
            std::string err;
            if (importRes) {
                auto loadRes = FusionAssetLoader::loadFromFile(importRes.unwrap());
                if (loadRes) asset = loadRes.unwrap();
                else err = loadRes.unwrapErr();
            } else err = importRes.unwrapErr();

            Loader::get()->queueInMainThread([weakSelf, frameName, asset, err]() {
                if (paimon::isRuntimeShuttingDown()) return;
                auto self = weakSelf.lock();
                if (!self || !self->getParent()) return;
                if (!asset || asset->empty()) {
                    Notification::create(("Import failed: " + err).c_str(),
                        NotificationIcon::Error, 3.f)->show();
                    return;
                }
                if (!self->m_hasSelection || self->m_selected.frameName != frameName) return;
                self->m_asset = asset;
                self->m_frameIndex = 0;
                self->invalidateStampCache();
                auto s = self->currentSetting();
                s.fusionAnimated = asset->animated;
                if (s.fusionTransform.isDefault())
                    s.fusionTransform.fitMode = ImageFitMode::Fill;
                if (s.fusionTolerance < 40) s.fusionTolerance = 110;
                if (s.fusionExpandRadius < 0) s.fusionExpandRadius = 1;
                if (s.hasFusion) self->persistMask();
                self->storeSetting(s);
                self->refreshToolsUi();
                self->ensureStampCache();
                self->renderPreviewFast();
                if (asset->animated) self->startAnim();
                else self->stopAnim();
                self->setStatus(fmt::format("Texture {}x{} ({}f). Tap Original to fill.",
                    asset->width(), asset->height(), asset->frameCount()));
            });
        });
    });
}

void FusionEditorLayer::onClearFusion() {
    if (!m_hasSelection) return;
    auto s = currentSetting();
    if (!s.hasFusion && !m_asset) return;
    stopAnim();
    s.hasFusion = false;
    s.fusionAnimated = false;
    s.fusionBlend = FusionBlendMode::Replace;
    s.fusionTolerance = 110;
    s.fusionExpandRadius = 1;
    s.fusionOpacity = 1.f;
    s.fusionTransform = ImageTransform{};
    s.fusionTransform.fitMode = ImageFitMode::Fill;
    s.fusionPixelX = 0;
    s.fusionPixelY = 0;
    storeSetting(s);
    m_mask.reset();
    m_asset.reset();
    m_frameIndex = 0;
    clearUndo();
    (void)FusionStore::deleteForSlot(m_slotId, m_selected.frameName);
    refreshToolsUi();
    refreshSpriteStrip();
    refreshPreview();
    setStatus("Fusion cleared.");
}

void FusionEditorLayer::onCycleBlend() {
    if (!m_hasSelection) return;
    auto s = currentSetting();
    s.fusionBlend = static_cast<FusionBlendMode>(
        (static_cast<int>(s.fusionBlend) + 1) % 3);
    storeSetting(s);
    if (s.hasFusion) persistMask();
    invalidateStampCache();
    refreshToolsUi();
    ensureStampCache();
    renderPreviewFast();
}

void FusionEditorLayer::onCycleFit() {
    if (!m_hasSelection) return;
    auto s = currentSetting();
    s.fusionTransform.fitMode = static_cast<ImageFitMode>(
        (static_cast<int>(s.fusionTransform.fitMode) + 1) % 3);
    storeSetting(s);
    if (s.hasFusion) persistMask();
    invalidateStampCache();
    refreshToolsUi();
    ensureStampCache();
    renderPreviewFast();
}

void FusionEditorLayer::onNudge(int dx, int dy) {
    if (!m_hasSelection) return;
    auto s = currentSetting();
    s.fusionPixelX = std::clamp(s.fusionPixelX + dx, -4096, 4096);
    s.fusionPixelY = std::clamp(s.fusionPixelY + dy, -4096, 4096);
    storeSetting(s);
    if (s.hasFusion) persistMask();
    if (m_pixelLbl) {
        m_pixelLbl->setString(fmt::format("px {:+d}, {:+d}",
            s.fusionPixelX, s.fusionPixelY).c_str());
    }
    ensureStampCache();
    renderPreviewFast();
    setStatus(fmt::format("px {:+d}, {:+d}", s.fusionPixelX, s.fusionPixelY));
}

void FusionEditorLayer::onResetPlacement() {
    if (!m_hasSelection) return;
    auto s = currentSetting();
    s.fusionPixelX = 0;
    s.fusionPixelY = 0;
    s.fusionTransform = ImageTransform{};
    s.fusionTransform.fitMode = ImageFitMode::Fill;
    s.fusionOpacity = 1.f;
    storeSetting(s);
    if (s.hasFusion) persistMask();
    refreshToolsUi();
    refreshPreview();
    setStatus("Placement reset.");
}

void FusionEditorLayer::pushUndo() {
    std::shared_ptr<MaskBuffer> snap;
    if (m_mask && !m_mask->empty()) snap = std::make_shared<MaskBuffer>(*m_mask);
    else snap = std::make_shared<MaskBuffer>();
    m_undo.push_back(std::move(snap));
    if (static_cast<int>(m_undo.size()) > kUndoMax)
        m_undo.erase(m_undo.begin());
}

void FusionEditorLayer::clearUndo() { m_undo.clear(); }

void FusionEditorLayer::onUndo() {
    if (!m_hasSelection) return;
    if (m_undo.empty()) {
        Notification::create("Nothing to undo.", NotificationIcon::Info, 1.2f)->show();
        return;
    }
    auto prev = std::move(m_undo.back());
    m_undo.pop_back();
    auto s = currentSetting();
    if (!prev || prev->empty()) {
        m_mask.reset();
        s.hasFusion = false;
        storeSetting(s);
        (void)FusionStore::deleteMaskForSlot(m_slotId, m_selected.frameName);
    } else {
        m_mask = std::move(prev);
        s.hasFusion = true;
        storeSetting(s);
        persistMask();
    }
    invalidateStampCache();
    refreshToolsUi();
    refreshSpriteStrip();
    ensureStampCache();
    renderPreviewFast();
    setStatus(fmt::format("Undo ({} left).", m_undo.size()));
}

void FusionEditorLayer::unloadFusion() {
    stopAnim();
    m_asset.reset();
    m_mask.reset();
    clearUndo();
    invalidateStampCache();
    m_frameIndex = 0;
    m_touchActive = false;
    m_dragging = false;
    m_touchOnResult = false;
    m_fastPreviewPending = false;
}

void FusionEditorLayer::loadFusionForSelection() {
    unloadFusion();
    if (!m_hasSelection) return;
    std::string frameName = m_selected.frameName;
    std::string slotId = m_slotId;
    WeakRef<FusionEditorLayer> weakSelf(this);
    auto closed = m_closed;

    paimon::ThreadTracker::get().spawn([weakSelf, closed, slotId, frameName]() {
        if (paimon::isRuntimeShuttingDown() || closed->load(std::memory_order_acquire)) return;
        std::shared_ptr<MaskBuffer> mask;
        FusionBlendMode blend = FusionBlendMode::Replace;
        int tol = 90;
        float opac = 1.f;
        ImageTransform transform{};
        bool animated = false;
        bool have = false;
        if (auto maskRes = FusionStore::loadForSlot(slotId, frameName)) {
            auto p = std::move(maskRes).unwrap();
            have = true;
            blend = p.blendMode;
            tol = p.colorTolerance;
            opac = p.opacity;
            transform = p.transform;
            animated = p.animated;
            mask = std::make_shared<MaskBuffer>(std::move(p.mask));
        }
        std::shared_ptr<FusionAsset> asset;
        auto tryLoad = [&](std::filesystem::path const& p) {
            auto r = FusionAssetLoader::loadFromFile(p);
            if (r) asset = r.unwrap();
        };
        std::error_code ec;
        auto gif = SlotPaths::fusionTextureFile(slotId, frameName, ".gif");
        auto png = SlotPaths::fusionTextureFile(slotId, frameName, ".png");
        if (std::filesystem::exists(gif, ec)) tryLoad(gif);
        else if (std::filesystem::exists(png, ec)) tryLoad(png);

        Loader::get()->queueInMainThread(
            [weakSelf, closed, frameName, mask, asset, have, blend, tol, opac, transform, animated]() {
            if (paimon::isRuntimeShuttingDown() || closed->load(std::memory_order_acquire)) return;
            auto self = weakSelf.lock();
            if (!self || !self->getParent()) return;
            if (!self->m_hasSelection || self->m_selected.frameName != frameName) return;
            self->m_mask = mask;
            self->m_asset = asset;
            self->m_frameIndex = 0;
            if (have) {
                auto st = self->currentSetting();
                st.fusionBlend = blend;
                st.fusionTolerance = tol;
                st.fusionOpacity = opac;
                st.fusionTransform = transform;
                st.fusionAnimated = animated || (asset && asset->animated);
                st.hasFusion = mask && !mask->empty();
                self->storeSetting(st);
            }
            self->refreshToolsUi();
            self->refreshPreview();
            if (asset && asset->animated) self->startAnim();
        });
    });
}

void FusionEditorLayer::persistMask() {
    if (!m_hasSelection || !m_mask || m_mask->empty()) return;
    FusionPayload p;
    p.mask = *m_mask;
    auto s = currentSetting();
    p.blendMode = s.fusionBlend;
    p.colorTolerance = s.fusionTolerance;
    p.opacity = s.fusionOpacity;
    p.transform = s.fusionTransform;
    p.animated = s.fusionAnimated || (m_asset && m_asset->animated);
    p.textureExt = p.animated ? ".gif" : ".png";
    std::error_code ec;
    if (std::filesystem::exists(
            SlotPaths::fusionTextureFile(m_slotId, m_selected.frameName, ".gif"), ec)) {
        p.textureExt = ".gif";
        p.animated = true;
    }
    auto r = FusionStore::saveForSlot(m_slotId, m_selected.frameName, p);
    if (!r) {
        log::warn("[fusion-layer] persist: {}", r.unwrapErr());
        return;
    }
    s.hasFusion = true;
    s.fusionAnimated = p.animated;
    storeSetting(s);
}

void FusionEditorLayer::refreshToolsUi() {
    auto s = currentSetting();
    if (m_stateLbl) {
        if (!m_asset || m_asset->empty()) {
            m_stateLbl->setString(s.hasFusion ? "mask OK — reload tex" : "no texture");
        } else {
            m_stateLbl->setString(fmt::format("{}x{} {}f {}",
                m_asset->width(), m_asset->height(), m_asset->frameCount(),
                s.hasFusion ? "mask ON" : "tap fill").c_str());
        }
    }
    if (m_blendBtn) {
        if (auto* spr = typeinfo_cast<ButtonSprite*>(m_blendBtn->getNormalImage()))
            spr->setString(blendLabel(s.fusionBlend));
    }
    if (m_fitBtn) {
        if (auto* spr = typeinfo_cast<ButtonSprite*>(m_fitBtn->getNormalImage()))
            spr->setString(fitLabel(s.fusionTransform.fitMode));
    }
    if (m_pixelLbl) {
        m_pixelLbl->setString(fmt::format("px {:+d}, {:+d}  arrows=1  shift=10",
            s.fusionPixelX, s.fusionPixelY).c_str());
    }
    auto sync = [](CCMenuItemToggler* t, bool v) {
        if (t && t->isToggled() != v) t->toggle(v);
    };
    sync(m_flipXTog, s.fusionTransform.flipX);
    sync(m_flipYTog, s.fusionTransform.flipY);
    sync(m_paintTog, m_paintArmed);
    if (m_scaleRow) m_scaleRow->setValue(s.fusionTransform.scale);
    if (m_offXRow) m_offXRow->setValue(s.fusionTransform.offsetX);
    if (m_offYRow) m_offYRow->setValue(s.fusionTransform.offsetY);
    if (m_rotRow) m_rotRow->setValue(s.fusionTransform.rotationDeg);
    if (m_opacRow) m_opacRow->setValue(s.fusionOpacity);
    if (m_tolRow) m_tolRow->setValue(static_cast<float>(s.fusionTolerance));
    if (m_expandRow) m_expandRow->setValue(static_cast<float>(s.fusionExpandRadius));
    if (m_hintLbl) {
        m_hintLbl->setString(m_paintArmed
            ? "Lower Color R if it eats the border/text"
            : "Paint OFF · drag move · arrows 1px");
        m_hintLbl->limitLabelWidth(kToolsW - 8.f, 0.28f, 0.12f);
    }
    if (m_undoBtn) {
        m_undoBtn->setEnabled(!m_undo.empty());
        if (auto* spr = typeinfo_cast<ButtonSprite*>(m_undoBtn->getNormalImage()))
            spr->setOpacity(m_undo.empty() ? 100 : 255);
    }
}

// ---------------------------------------------------------------------------
// Touch / paint / drag

bool FusionEditorLayer::mapTouchToSpriteFloatOn(CCSprite* spr, CCTouch* touch,
                                                float& outX, float& outY,
                                                bool requireInside) {
    if (!spr || !spr->isVisible() || !touch || !m_pixels || m_pixels->empty()) {
        return false;
    }
    auto local = spr->convertToNodeSpace(touch->getLocation());
    auto sz = spr->getContentSize();
    if (sz.width <= 1.f || sz.height <= 1.f) return false;
    if (requireInside) {
        constexpr float kSlop = 3.f;
        if (local.x < -kSlop || local.y < -kSlop
            || local.x > sz.width + kSlop || local.y > sz.height + kSlop) {
            return false;
        }
    }

    // Do not clamp an active Result drag to the preview rectangle. Keeping
    // these coordinates unbounded lets the pointer leave the box naturally.
    float lx = requireInside
        ? std::clamp(local.x, 0.f, sz.width - 0.001f) : local.x;
    float ly = requireInside
        ? std::clamp(local.y, 0.f, sz.height - 0.001f) : local.y;
    auto const& f = m_frameInfo;
    int sw = m_pixels->width(), sh = m_pixels->height();
    int srcW = std::max(f.sourceW, sw), srcH = std::max(f.sourceH, sh);
    if (srcW <= 0 || srcH <= 0) return false;
    float u = lx / sz.width;
    float v = 1.f - (ly / sz.height);
    float logX = u * srcW, logY = v * srcH;
    float dstX = (srcW - sw) * 0.5f + f.offsetX;
    float dstY = (srcH - sh) * 0.5f - f.offsetY;
    float sx = logX - dstX, sy = logY - dstY;
    if (requireInside
        && (sx < -0.5f || sy < -0.5f || sx >= sw + 0.5f || sy >= sh + 0.5f)) {
        return false;
    }
    outX = requireInside
        ? std::clamp(sx, 0.f, static_cast<float>(sw) - 0.001f) : sx;
    outY = requireInside
        ? std::clamp(sy, 0.f, static_cast<float>(sh) - 0.001f) : sy;
    return true;
}

bool FusionEditorLayer::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (!m_hasSelection) return false;
    float fx = 0.f, fy = 0.f;
    bool onOriginal = mapTouchToSpriteFloatOn(m_originalSpr, touch, fx, fy, true);
    bool onResult = !onOriginal
        && mapTouchToSpriteFloatOn(m_resultSpr, touch, fx, fy, true);
    if (!onOriginal && !onResult) return false;
    if (!m_asset || m_asset->empty()) {
        Notification::create("Pick a texture first.", NotificationIcon::Info, 1.5f)->show();
        return false;
    }
    auto s = currentSetting();
    m_touchActive = true;
    m_dragging = false;
    m_touchOnResult = onResult;
    m_touchStartX = fx;
    m_touchStartY = fy;
    m_dragStartPx = s.fusionPixelX;
    m_dragStartPy = s.fusionPixelY;
    return true;
}

void FusionEditorLayer::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (!m_touchActive || !m_touchOnResult) return;
    float fx = 0.f, fy = 0.f;
    if (!mapTouchToSpriteFloatOn(m_resultSpr, touch, fx, fy, false)) return;
    float dx = fx - m_touchStartX, dy = fy - m_touchStartY;
    if (!m_dragging && (dx * dx + dy * dy) >= 6.25f) {
        m_dragging = true;
        ensureStampCache();
    }
    if (!m_dragging) return;
    int newPx = std::clamp(m_dragStartPx + static_cast<int>(std::lround(dx)), -4096, 4096);
    int newPy = std::clamp(m_dragStartPy + static_cast<int>(std::lround(dy)), -4096, 4096);
    auto s = currentSetting();
    if (s.fusionPixelX == newPx && s.fusionPixelY == newPy) return;
    s.fusionPixelX = newPx;
    s.fusionPixelY = newPy;
    // In-memory only during drag — no full UI rebuild / disk IO.
    m_project.spriteSettings[m_selected.frameName] = s;
    if (m_pixelLbl) {
        m_pixelLbl->setString(fmt::format("px {:+d}, {:+d}", newPx, newPy).c_str());
    }
    // Re-sample from the original asset so moving never exposes a clipped
    // edge from the previous placement.
    renderPreviewFast();
}

void FusionEditorLayer::ccTouchEnded(CCTouch* touch, CCEvent*) { endGesture(touch); }
void FusionEditorLayer::ccTouchCancelled(CCTouch* touch, CCEvent*) {
    endGesture(touch, true);
}

void FusionEditorLayer::endGesture(CCTouch* touch, bool cancelled) {
    if (!m_touchActive) return;
    bool wasDrag = m_dragging;
    bool wasResult = m_touchOnResult;
    float sx = m_touchStartX, sy = m_touchStartY;
    m_touchActive = false;
    m_dragging = false;
    m_touchOnResult = false;
    if (cancelled) {
        if (wasResult && wasDrag && m_hasSelection) {
            auto s = currentSetting();
            s.fusionPixelX = m_dragStartPx;
            s.fusionPixelY = m_dragStartPy;
            m_project.spriteSettings[m_selected.frameName] = s;
            renderPreviewFast();
            refreshToolsUi();
        }
        return;
    }
    if (!m_hasSelection) return;
    if (wasResult && wasDrag) {
        auto s = currentSetting();
        storeSetting(s);
        if (s.hasFusion) persistMask();
        m_fastPreviewPending = false;
        refreshToolsUi();
        // One clean composite after drag (cache already warm).
        renderPreviewFast();
        setStatus(fmt::format("Moved px {:+d}, {:+d}", s.fusionPixelX, s.fusionPixelY));
        return;
    }
    if (wasResult) {
        setStatus("Drag Result to move the texture.");
        return;
    }
    if (!m_paintArmed) {
        setStatus("Paint OFF — drag to move the texture.");
        return;
    }
    int px = static_cast<int>(std::floor(sx));
    int py = static_cast<int>(std::floor(sy));
    if (touch) {
        float tx = 0.f, ty = 0.f;
        if (mapTouchToSpriteFloatOn(m_originalSpr, touch, tx, ty, true)) {
            px = static_cast<int>(std::floor(tx));
            py = static_cast<int>(std::floor(ty));
        }
    }
    applyFill(px, py);
}

void FusionEditorLayer::applyFill(int pixelX, int pixelY) {
    if (!m_hasSelection || !m_pixels || m_pixels->empty()) return;
    if (!m_asset || m_asset->empty()) return;
    auto s = currentSetting();
    int colorR = s.fusionTolerance > 0 ? s.fusionTolerance : 110;
    int expand = std::clamp(s.fusionExpandRadius, 0, 12);
    MaskBuffer region = FusionEngine::floodFill(
        *m_pixels, pixelX, pixelY, colorR, /*alphaCutoff=*/12, expand);
    bool any = false;
    for (auto v : region.data) if (v) { any = true; break; }
    if (!any) {
        Notification::create("Nothing to fill there.", NotificationIcon::Info, 1.3f)->show();
        return;
    }
    pushUndo();
    if (!m_mask || m_mask->width != region.width || m_mask->height != region.height) {
        m_mask = std::make_shared<MaskBuffer>(std::move(region));
    } else {
        FusionEngine::orMask(*m_mask, region);
    }
    invalidateStampCache(); // soft coverage depends on mask
    persistMask();
    refreshToolsUi();
    refreshSpriteStrip();
    ensureStampCache();
    renderPreviewFast();
    setStatus("Region filled (Ctrl+Z undo).");
}

void FusionEditorLayer::startAnim() {
    stopAnim();
    if (!m_asset || !m_asset->animated || m_asset->frameCount() < 2) return;
    m_animating = true;
    m_frameAccum = 0.f;
    this->schedule(schedule_selector(FusionEditorLayer::tickAnim));
}

void FusionEditorLayer::stopAnim() {
    if (m_animating) this->unschedule(schedule_selector(FusionEditorLayer::tickAnim));
    m_animating = false;
    m_frameAccum = 0.f;
}

void FusionEditorLayer::tickAnim(float dt) {
    if (!m_asset || !m_asset->animated || m_asset->empty()) { stopAnim(); return; }
    auto s = currentSetting();
    if (!s.hasFusion || !m_mask || m_mask->empty()) return;
    m_frameAccum += dt * 1000.f;
    int delay = m_asset->delayAt(m_frameIndex);
    if (m_frameAccum < static_cast<float>(delay)) return;
    m_frameAccum = 0.f;
    m_frameIndex = (m_frameIndex + 1) % m_asset->frameCount();
    this->unschedule(schedule_selector(FusionEditorLayer::renderPreview));
    this->renderPreview(0.f);
}

}  // namespace paimon::texture_studio
