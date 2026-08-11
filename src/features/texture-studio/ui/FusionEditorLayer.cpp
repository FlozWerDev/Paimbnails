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
#include <cctype>
#include <cmath>
#include <filesystem>
#include <system_error>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

constexpr float kMargin   = 6.f;
constexpr float kHeaderH  = 30.f;
constexpr float kBrowserW = 128.f;
constexpr float kToolsW   = 204.f;
constexpr float kColGap   = 8.f;
constexpr float kCellW    = 62.f;
constexpr float kCellH    = 50.f;

constexpr int kTagCellBg    = 99;
constexpr int kTagCellThumb = 100;
constexpr int kTagCellLoad  = 102;
constexpr int kTagCellBadge = 103;
constexpr int kTagTexThumb  = 7;

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

std::string toLowerCopy(std::string const& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string shortFrameName(std::string name) {
    if (auto p = name.rfind("_001.png"); p != std::string::npos) name.resize(p);
    else if (auto p2 = name.rfind(".png"); p2 != std::string::npos) name.resize(p2);
    return name;
}

// Show the painted region before a texture is selected or after Clear.
void highlightMask(ImageBuffer& img, MaskBuffer const& mask) {
    if (img.empty() || mask.empty()) return;
    if (img.width() != mask.width || img.height() != mask.height) return;
    auto* d = img.data();
    auto const* m = mask.data.data();
    std::size_t n = img.pixelCount();
    for (std::size_t i = 0; i < n; ++i) {
        if (!m[i]) continue;
        auto* p = d + i * ImageBuffer::kBytesPerPixel;
        if (p[3] == 0) continue;
        p[0] = static_cast<std::uint8_t>((p[0] * 45 + 100 * 55) / 100);
        p[1] = static_cast<std::uint8_t>((p[1] * 45 + 235 * 55) / 100);
        p[2] = static_cast<std::uint8_t>((p[2] * 45 + 140 * 55) / 100);
    }
}

}

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
    m_thumbGen->fetch_add(1, std::memory_order_acq_rel);
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
    buildBrowser();
    buildPreviews();
    buildTools();
    buildEntries();
    applyBrowserFilter();

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
            btn->setPosition({20.f, win.height - kHeaderH * 0.5f});
            menu->addChild(btn);
        }
    }
    if (auto* title = CCLabelBMFont::create("Fusion Layer", "goldFont.fnt")) {
        title->setScale(0.48f);
        title->setPosition({win.width * 0.5f, win.height - kHeaderH * 0.5f + 2.f});
        this->addChild(title, 10);
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
            btn->setPosition({win.width - 38.f, win.height - kHeaderH * 0.5f});
            menu->addChild(btn);
        }
    }
    if (auto* st = CCLabelBMFont::create("", "chatFont.fnt")) {
        st->setScale(0.38f);
        st->setColor({180, 185, 195});
        st->setAnchorPoint({0.f, 0.5f});
        st->setPosition({40.f, win.height - kHeaderH * 0.5f - 1.f});
        this->addChild(st, 10);
        m_statusLbl = st;
    }
}

void FusionEditorLayer::buildBrowser() {
    auto win = CCDirector::get()->getWinSize();
    const float colX = kMargin;
    const float searchY = win.height - kHeaderH - 12.f;
    const float pageY = kMargin + 9.f;
    const float gridTop = searchY - 13.f;
    const float gridBottom = pageY + 12.f;
    const float gridH = std::max(kCellH * 2.f, gridTop - gridBottom);

    if (auto* panelBg = CCScale9Sprite::create("square02b_001.png")) {
        panelBg->setContentSize({kBrowserW, win.height - kHeaderH - kMargin * 2.f + 4.f});
        panelBg->setColor({0, 0, 0});
        panelBg->setOpacity(80);
        panelBg->setAnchorPoint({0.f, 0.f});
        panelBg->setPosition({colX - 2.f, kMargin - 2.f});
        this->addChild(panelBg, 3);
    }

    if (auto* search = TextInput::create(150.f, "Search...")) {
        search->setMaxCharCount(32);
        search->setScale(0.62f);
        search->setID("fusion-search"_spr);
        search->setCallback([this](std::string const& text) {
            m_search = toLowerCopy(text);
            applyBrowserFilter();
        });
        search->setPosition({colX + kBrowserW * 0.5f, searchY});
        this->addChild(search, 6);
    }

    m_browserCols = 2;
    m_browserRows = std::max(2, static_cast<int>(gridH / kCellH));

    auto* host = CCNode::create();
    host->setContentSize({m_browserCols * kCellW, m_browserRows * kCellH});
    host->setAnchorPoint({0.5f, 1.f});
    host->setPosition({colX + kBrowserW * 0.5f, gridTop});
    this->addChild(host, 5);
    m_browserHost = host;

    auto* menu = CCMenu::create();
    menu->setPosition({0, 0});
    this->addChild(menu, 10);
    if (auto* prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
        prevSpr->setScale(0.42f);
        if (auto* btn = CCMenuItemExt::createSpriteExtra(prevSpr,
                [this](CCMenuItemSpriteExtra*) {
                    if (m_page > 0) { --m_page; rebuildBrowser(); }
                })) {
            btn->setPosition({colX + 13.f, pageY});
            menu->addChild(btn);
        }
    }
    if (auto* nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
        nextSpr->setScale(0.42f);
        nextSpr->setFlipX(true);
        if (auto* btn = CCMenuItemExt::createSpriteExtra(nextSpr,
                [this](CCMenuItemSpriteExtra*) {
                    int perPage = m_browserCols * m_browserRows;
                    int pages = std::max(1,
                        (static_cast<int>(m_filtered.size()) + perPage - 1) / perPage);
                    if (m_page + 1 < pages) { ++m_page; rebuildBrowser(); }
                })) {
            btn->setPosition({colX + kBrowserW - 13.f, pageY});
            menu->addChild(btn);
        }
    }
    if (auto* pageLbl = CCLabelBMFont::create("1 / 1", "bigFont.fnt")) {
        pageLbl->setScale(0.28f);
        pageLbl->setPosition({colX + kBrowserW * 0.5f, pageY});
        this->addChild(pageLbl, 5);
        m_pageLbl = pageLbl;
    }
}

void FusionEditorLayer::buildPreviews() {
    auto win = CCDirector::get()->getWinSize();
    const float cLeft = kMargin + kBrowserW + kColGap;
    const float cRight = win.width - kMargin - kToolsW - kColGap;
    const float areaW = std::max(80.f, cRight - cLeft);
    const float top = win.height - kHeaderH - 8.f;
    const float bottom = 22.f;
    const float boxSize = std::min((areaW - kColGap) * 0.5f, top - bottom - 14.f);
    const float cy = bottom + 14.f + (top - bottom - 14.f) * 0.5f;
    const float pairW = boxSize * 2.f + kColGap;
    const float leftCx = cLeft + (areaW - pairW) * 0.5f + boxSize * 0.5f;
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
            lbl->setScale(0.3f);
            lbl->setColor({170, 175, 185});
            lbl->limitLabelWidth(boxSize - 10.f, 0.3f, 0.14f);
            host->addChildAtPosition(lbl, Anchor::Top, {0.f, 9.f});
        }
        if (auto* loading = CCLabelBMFont::create("...", "bigFont.fnt")) {
            loading->setScale(0.4f);
            loading->setColor({110, 110, 120});
            loading->setID("loading");
            host->addChildAtPosition(loading, Anchor::Center);
        }
        return host;
    };

    m_originalHost = makeBox(leftCx, cy, "ORIGINAL (tap = paint)");
    m_resultHost   = makeBox(rightCx, cy, "RESULT (tap = paint, drag = move)");

    if (auto* fl = CCLabelBMFont::create("", "chatFont.fnt")) {
        fl->setScale(0.4f);
        fl->setColor({200, 200, 210});
        fl->setPosition({(leftCx + rightCx) * 0.5f, cy - boxSize * 0.5f - 10.f});
        this->addChild(fl, 5);
        m_frameLbl = fl;
    }
}

void FusionEditorLayer::buildTools() {
    auto win = CCDirector::get()->getWinSize();
    const float panelX = win.width - kMargin - kToolsW;
    const float panelBottom = kMargin;
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

// Keep the menu at panel origin; CCMenu ignores its anchor point when placing
// children, so centering it offsets every control.
    auto* menu = CCMenu::create();
    menu->setPosition({0, 0});
    menu->setContentSize({w, panelH});
    panel->addChild(menu, 2);

    float y = panelH - 12.f;
    if (auto* cap = CCLabelBMFont::create("FUSION TOOLS", "goldFont.fnt")) {
        cap->setScale(0.34f);
        cap->setPosition({w * 0.5f, y});
        panel->addChild(cap);
    }
    y -= 20.f;

    auto addBtn = [menu](char const* lab, char const* bg, float x, float y,
                         std::function<void()> fn,
                         CCMenuItemSpriteExtra** store = nullptr) {
        if (auto* spr = ButtonSprite::create(
                lab, 62, true, "bigFont.fnt", bg, 20.f, 0.42f)) {
            if (auto* btn = CCMenuItemExt::createSpriteExtra(spr,
                    [fn](CCMenuItemSpriteExtra*) { fn(); })) {
                btn->setPosition({x, y});
                menu->addChild(btn);
                if (store) *store = btn;
            }
        }
    };
    addBtn("Pick",  "GJ_button_05.png", w * 0.19f, y, [this] { onPickTexture(); });
    addBtn("Clear", "GJ_button_06.png", w * 0.50f, y, [this] { onClearFusion(); });
    addBtn("Undo",  "GJ_button_06.png", w * 0.81f, y, [this] { onUndo(); }, &m_undoBtn);
    y -= 22.f;
    addBtn("Replace", "GJ_button_04.png", w * 0.19f, y, [this] { onCycleBlend(); }, &m_blendBtn);
    addBtn("Fill",    "GJ_button_04.png", w * 0.50f, y, [this] { onCycleFit(); }, &m_fitBtn);
    addBtn("0,0",     "GJ_button_04.png", w * 0.81f, y, [this] { onResetPlacement(); });
    y -= 21.f;

    {
        auto* thumbHost = CCNode::create();
        thumbHost->setContentSize({26.f, 26.f});
        thumbHost->setAnchorPoint({0.5f, 0.5f});
        thumbHost->setPosition({20.f, y - 3.f});
        panel->addChild(thumbHost);
        if (auto* box = CCScale9Sprite::create("GJ_square01.png")) {
            box->setContentSize({26.f, 26.f});
            box->setColor({28, 28, 36});
            thumbHost->addChildAtPosition(box, Anchor::Center);
        }
        m_texThumbHost = thumbHost;

        if (auto* st = CCLabelBMFont::create("no texture", "bigFont.fnt")) {
            st->setScale(0.22f);
            st->setColor({170, 170, 180});
            st->setAnchorPoint({0.f, 0.5f});
            st->setPosition({38.f, y - 3.f});
            st->limitLabelWidth(w - 44.f, 0.22f, 0.1f);
            panel->addChild(st);
            m_stateLbl = st;
        }
    }
    y -= 24.f;

    auto* paintTog = CCMenuItemExt::createTogglerWithStandardSprites(0.38f,
        [this](CCMenuItemToggler* t) {
            if (!t) return;
            m_paintArmed = !t->isToggled();
            refreshToolsUi();
        });
    if (paintTog) {
        paintTog->toggle(true);
        paintTog->setPosition({14.f, y});
        menu->addChild(paintTog);
        m_paintTog = paintTog;
        if (auto* lbl = CCLabelBMFont::create("Paint", "bigFont.fnt")) {
            lbl->setScale(0.22f);
            lbl->setAnchorPoint({0.f, 0.5f});
            lbl->setPosition({paintTog->getContentSize().width * 0.5f + 3.f, 0.f});
            paintTog->addChild(lbl);
        }
    }
    auto nudge = [this, menu](char const* lab, float x, float y, int dx, int dy) {
        if (auto* spr = ButtonSprite::create(
                lab, 26, true, "bigFont.fnt", "GJ_button_04.png", 18.f, 0.46f)) {
            if (auto* btn = CCMenuItemExt::createSpriteExtra(spr,
                    [this, dx, dy](CCMenuItemSpriteExtra*) { onNudge(dx, dy); })) {
                btn->setPosition({x, y});
                menu->addChild(btn);
            }
        }
    };
    nudge("<", w - 110.f, y, -1, 0);
    nudge(">", w - 82.f,  y,  1, 0);
    nudge("^", w - 54.f,  y,  0, -1);
    nudge("v", w - 26.f,  y,  0,  1);
    y -= 19.f;

    auto mkFlip = [this, menu](char const* lab, float x, float y,
                               CCMenuItemToggler*& out, bool isX) {
        auto* tog = CCMenuItemExt::createTogglerWithStandardSprites(0.34f,
            [this, isX](CCMenuItemToggler* t) {
                if (!t || !m_hasSelection) return;
                auto s = currentSetting();
                bool v = !t->isToggled();
                if (isX) s.fusionTransform.flipX = v;
                else s.fusionTransform.flipY = v;
                storeSetting(s);
                if (s.hasFusion) persistMask();
                invalidateStampCache();
                ensureStampCache();
                renderPreviewFast();
        });
        if (!tog) return;
        tog->setPosition({x, y});
        menu->addChild(tog);
        out = tog;
        if (auto* lbl = CCLabelBMFont::create(lab, "bigFont.fnt")) {
            lbl->setScale(0.2f);
            lbl->setAnchorPoint({0.f, 0.5f});
            lbl->setPosition({tog->getContentSize().width * 0.5f + 3.f, 0.f});
            tog->addChild(lbl);
        }
    };
    mkFlip("Flip X", 14.f, y, m_flipXTog, true);
    mkFlip("Flip Y", 68.f, y, m_flipYTog, false);
    if (auto* px = CCLabelBMFont::create("px +0, +0", "chatFont.fnt")) {
        px->setScale(0.34f);
        px->setColor({255, 200, 120});
        px->setAnchorPoint({1.f, 0.5f});
        px->setPosition({w - 12.f, y});
        panel->addChild(px);
        m_pixelLbl = px;
    }
    y -= 16.f;

    if (auto* hint = CCLabelBMFont::create(
            "Tap Original to paint. Texture never recolored.", "chatFont.fnt")) {
        hint->setScale(0.28f);
        hint->setColor({150, 155, 165});
        hint->limitLabelWidth(w - 10.f, 0.28f, 0.12f);
        hint->setPosition({w * 0.5f, y});
        panel->addChild(hint);
        m_hintLbl = hint;
    }
    y -= 15.f;

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
// Colour radius controls how far the fill spreads across nearby shades.
        {"Color R", 20.f, 220.f, 1.f,
         [](SpriteSetting const& s) { return static_cast<float>(s.fusionTolerance); },
         [](SpriteSetting& s, float v) {
             s.fusionTolerance = static_cast<int>(std::lround(v));
         },
         [](float v) { return fmt::format("{:.0f}", v); }, &m_tolRow},
// Expand only into same-colour neighbors.
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
// Rebuild the stamp once after transform/opacity changes.
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
        y -= 17.f;
    }
}


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
    std::sort(m_entries.begin(), m_entries.end(),
        [](Entry const& a, Entry const& b) {
            if (a.kind != b.kind) return a.kind == SpriteKind::Button;
            return a.frameName < b.frameName;
        });
}

void FusionEditorLayer::applyBrowserFilter() {
    m_filtered.clear();
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        if (!m_search.empty()
            && toLowerCopy(m_entries[i].frameName).find(m_search)
                == std::string::npos) {
            continue;
        }
        m_filtered.push_back(i);
    }
    m_page = 0;
    rebuildBrowser();
}

void FusionEditorLayer::rebuildBrowser() {
    if (!m_browserHost) return;
    int generation = m_thumbGen->fetch_add(1, std::memory_order_acq_rel) + 1;
    m_browserHost->removeAllChildren();
    m_browserMenu = nullptr;

    int perPage = m_browserCols * m_browserRows;
    int total = static_cast<int>(m_filtered.size());
    int pages = std::max(1, (total + perPage - 1) / perPage);
    m_page = std::clamp(m_page, 0, pages - 1);

    if (m_pageLbl) {
        m_pageLbl->setString(
            fmt::format("{} / {}", m_page + 1, pages).c_str());
    }

    if (total == 0) {
        if (auto* empty = CCLabelBMFont::create("No match.", "bigFont.fnt")) {
            empty->setScale(0.32f);
            empty->setColor({170, 170, 180});
            m_browserHost->addChildAtPosition(empty, Anchor::Center);
        }
        return;
    }

    auto* menu = CCMenu::create();
    if (!menu) return;
    menu->setPosition({0, 0});
    menu->setContentSize(m_browserHost->getContentSize());
    m_browserHost->addChild(menu);
    m_browserMenu = menu;

    int start = m_page * perPage;
    int end = std::min(total, start + perPage);
    float gridH = m_browserHost->getContentSize().height;

    for (int slot = 0; slot < end - start; ++slot) {
        auto const& entry = m_entries[m_filtered[start + slot]];
        int col = slot % m_browserCols;
        int row = slot / m_browserCols;
        bool isSelected = m_hasSelection && entry.frameName == m_selected.frameName;

        auto* cell = CCNode::create();
        cell->setContentSize({kCellW - 4.f, kCellH - 4.f});

        if (auto* bg = CCScale9Sprite::create("GJ_square01.png")) {
            bg->setContentSize(cell->getContentSize());
            bg->setTag(kTagCellBg);
            bg->setColor(isSelected
                ? ccColor3B{64, 100, 74}
                : ccColor3B{38, 40, 50});
            cell->addChildAtPosition(bg, Anchor::Center);
        }
        if (auto* loading = CCLabelBMFont::create("...", "bigFont.fnt")) {
            loading->setScale(0.24f);
            loading->setColor({120, 120, 130});
            loading->setTag(kTagCellLoad);
            cell->addChildAtPosition(loading, Anchor::Center, {0.f, 4.f});
        }
        if (auto* nameLbl = CCLabelBMFont::create(
                shortFrameName(entry.frameName).c_str(), "chatFont.fnt")) {
            nameLbl->limitLabelWidth(kCellW - 10.f, 0.36f, 0.1f);
            cell->addChildAtPosition(nameLbl, Anchor::Bottom, {0.f, 6.f});
        }
        auto it = m_project.spriteSettings.find(entry.frameName);
        bool hasF = it != m_project.spriteSettings.end() && it->second.hasFusion;
        if (auto* badge = CCLabelBMFont::create("F", "goldFont.fnt")) {
            badge->setScale(0.34f);
            badge->setTag(kTagCellBadge);
            badge->setVisible(hasF);
            cell->addChildAtPosition(badge, Anchor::TopRight, {-6.f, -7.f});
        }

        std::string nameCopy = entry.frameName;
        auto* item = CCMenuItemExt::createSpriteExtra(cell,
            [this, nameCopy](CCMenuItemSpriteExtra*) {
                this->selectFrame(nameCopy);
            });
        if (!item) continue;
        item->setTag(slot + 1);
        item->setPosition({(col + 0.5f) * kCellW,
                           gridH - (row + 0.5f) * kCellH});
        menu->addChild(item);
    }

    std::vector<Entry> pageEntries;
    pageEntries.reserve(end - start);
    for (int i = start; i < end; ++i) pageEntries.push_back(m_entries[m_filtered[i]]);
    requestThumbnails(std::move(pageEntries), generation);
}

void FusionEditorLayer::requestThumbnails(std::vector<Entry> entries, int generation) {
    if (entries.empty()) return;

    WeakRef<FusionEditorLayer> weakSelf(this);
    auto thumbGen = m_thumbGen;
    auto closed = m_closed;
    auto sheets = m_project.sheets;

    paimon::ThreadTracker::get().spawn(
        [weakSelf, thumbGen, closed, sheets,
         entries = std::move(entries), generation]() {
        for (int slot = 0; slot < static_cast<int>(entries.size()); ++slot) {
            if (paimon::isRuntimeShuttingDown()
                || closed->load(std::memory_order_acquire)
                || thumbGen->load(std::memory_order_acquire) != generation) {
                return;
            }
            auto const& entry = entries[slot];
            if (entry.sheetIndex < 0
                || entry.sheetIndex >= static_cast<int>(sheets.size())) continue;
            auto const& sheet = sheets[entry.sheetIndex];
            auto dataRes = FramePixelCache::get().frameData(
                std::filesystem::path(sheet.sourcePlistPath),
                std::filesystem::path(sheet.sourcePngPath), entry.frameName);
            if (!dataRes) continue;
            auto data = std::move(dataRes).unwrap();
            auto img = std::make_shared<ImageBuffer>(
                SpritesheetReader::composeLogicalFrame(data.pixels, data.info));

            Loader::get()->queueInMainThread(
                [weakSelf, thumbGen, closed, generation, slot, img]() {
                if (paimon::isRuntimeShuttingDown()
                    || closed->load(std::memory_order_acquire)
                    || thumbGen->load(std::memory_order_acquire) != generation) return;
                auto self = weakSelf.lock();
                if (!self || !self->getParent()) return;
                self->applyThumbnail(slot, generation, img);
            });
        }
    });
}

void FusionEditorLayer::applyThumbnail(int slot, int generation,
                                       std::shared_ptr<ImageBuffer> image) {
    if (!image || image->empty() || !m_browserMenu
        || m_thumbGen->load(std::memory_order_acquire) != generation) return;
    auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(
        m_browserMenu->getChildByTag(slot + 1));
    if (!item) return;
    auto* cell = item->getNormalImage();
    if (!cell) return;
    if (auto* old = cell->getChildByTag(kTagCellThumb)) old->removeFromParent();
    if (auto* loading = cell->getChildByTag(kTagCellLoad)) loading->removeFromParent();

    if (auto* spr = SpritePreviewRenderer::createSprite(*image)) {
        auto sz = spr->getContentSize();
        if (sz.width > 0 && sz.height > 0) {
            spr->setScale(std::min({26.f / sz.width, 26.f / sz.height, 2.f}));
        }
        spr->setTag(kTagCellThumb);
        cell->addChildAtPosition(spr, Anchor::Center, {0.f, 4.f});
    }
}

void FusionEditorLayer::highlightBrowserSelection() {
    if (!m_browserMenu) return;
    int perPage = m_browserCols * m_browserRows;
    int start = m_page * perPage;
    auto* children = m_browserMenu->getChildren();
    if (!children) return;
    for (unsigned int i = 0; i < children->count(); ++i) {
        auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(
            static_cast<CCNode*>(children->objectAtIndex(i)));
        if (!item) continue;
        int idx = start + item->getTag() - 1;
        if (idx < 0 || idx >= static_cast<int>(m_filtered.size())) continue;
        auto const& entry = m_entries[m_filtered[idx]];
        auto* cell = item->getNormalImage();
        if (!cell) continue;
        if (auto* bg = typeinfo_cast<CCScale9Sprite*>(cell->getChildByTag(kTagCellBg))) {
            bool sel = m_hasSelection && entry.frameName == m_selected.frameName;
            bg->setColor(sel ? ccColor3B{64, 100, 74} : ccColor3B{38, 40, 50});
        }
    }
}

void FusionEditorLayer::refreshBrowserBadges() {
    if (!m_browserMenu) return;
    int perPage = m_browserCols * m_browserRows;
    int start = m_page * perPage;
    auto* children = m_browserMenu->getChildren();
    if (!children) return;
    for (unsigned int i = 0; i < children->count(); ++i) {
        auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(
            static_cast<CCNode*>(children->objectAtIndex(i)));
        if (!item) continue;
        int idx = start + item->getTag() - 1;
        if (idx < 0 || idx >= static_cast<int>(m_filtered.size())) continue;
        auto const& entry = m_entries[m_filtered[idx]];
        auto* cell = item->getNormalImage();
        if (!cell) continue;
        if (auto* badge = cell->getChildByTag(kTagCellBadge)) {
            auto it = m_project.spriteSettings.find(entry.frameName);
            badge->setVisible(it != m_project.spriteSettings.end()
                && it->second.hasFusion);
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

    int idx = -1;
    for (int i = 0; i < static_cast<int>(m_filtered.size()); ++i) {
        if (m_entries[m_filtered[i]].frameName == frameName) { idx = i; break; }
    }
    int perPage = std::max(1, m_browserCols * m_browserRows);
    if (idx >= 0 && idx / perPage != m_page) {
        m_page = idx / perPage;
        rebuildBrowser();
    } else {
        highlightBrowserSelection();
    }

    if (m_frameLbl) {
        m_frameLbl->setString(frameName.c_str());
        m_frameLbl->limitLabelWidth(240.f, 0.4f, 0.18f);
    }
    loadPixelsForSelection();
// Load a picked texture even before a region is painted.
    loadFusionForSelection();
    refreshToolsUi();
    setStatus("Tap a preview to paint a region, Pick to choose a texture.");
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
// Keep Fusion config while editing so color, expand, scale, and offsets survive
// before a texture is loaded.
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
        m_statusLbl->limitLabelWidth(200.f, 0.38f, 0.16f);
    }
}


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
// Apply pixel offsets at composite time, not when building the stamp.
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
// Fit the stamp to mask bounds rather than the whole frame.
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
// Main-thread composite uses a cached stamp shifted by the pixel offset.
    m_fastPreviewPending = false;
    if (!m_pixels || m_pixels->empty()) return;

    auto s = currentSetting();
    ImageBuffer out = *m_pixels;
    if (m_mask && !m_mask->empty()) {
        if (m_asset && !m_asset->empty()) {
            ensureStampCache();
            if (m_cacheValid) {
                auto opts = makeFusionOptions(s);
// Bake placement into the stamp so moved images retain pixels beyond the mask.
                opts.pixelOffsetX = 0;
                opts.pixelOffsetY = 0;
                FusionEngine::applyCached(out, m_cachedCoverage, m_cachedStamp, opts);
            }
        } else {
            highlightMask(out, *m_mask);
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
        this->scheduleOnce(schedule_selector(FusionEditorLayer::renderPreview), 0.02f);
    }
}

void FusionEditorLayer::renderPreview(float) {
    if (!m_pixels || m_pixels->empty()) return;

// Prefer the cached path when it produces the same visual result.
    auto s = currentSetting();
    if (m_mask && !m_mask->empty() && m_asset && !m_asset->empty()) {
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
        [weakSelf, renderGen, closed, gen, pixels, mask, asset, frameInfo, fi, opts]() {
        if (paimon::isRuntimeShuttingDown() || closed->load(std::memory_order_acquire)) return;

        ImageBuffer out = *pixels;
        if (mask && !mask->empty()) {
            if (asset && !asset->empty()) {
                FusionEngine::apply(out, *mask, asset->frameAt(fi), opts);
            } else {
                highlightMask(out, *mask);
            }
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


void FusionEditorLayer::onPickTexture() {
    if (!m_hasSelection) {
        Notification::create("Select a sprite in the list first.",
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
                self->storeSetting(s);
                if (s.hasFusion) self->persistMask();
                self->refreshToolsUi();
                self->refreshTextureThumb();
                self->ensureStampCache();
                self->renderPreviewFast();
                if (asset->animated) self->startAnim();
                else self->stopAnim();
                bool hasMask = self->m_mask && !self->m_mask->empty();
                self->setStatus(fmt::format("Texture {}x{} ({}f). {}",
                    asset->width(), asset->height(), asset->frameCount(),
                    hasMask ? "Fused into painted region."
                            : "Tap a preview to paint a region."));
            });
        });
    });
}

void FusionEditorLayer::onClearFusion() {
    if (!m_hasSelection) return;
    auto s = currentSetting();
    if (!s.hasFusion && !m_asset && (!m_mask || m_mask->empty())) return;
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
    invalidateStampCache();
    (void)FusionStore::deleteForSlot(m_slotId, m_selected.frameName);
    refreshToolsUi();
    refreshTextureThumb();
    refreshBrowserBadges();
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
    invalidateStampCache();
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
    refreshBrowserBadges();
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
    refreshTextureThumb();
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
            self->refreshTextureThumb();
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

void FusionEditorLayer::refreshTextureThumb() {
    if (!m_texThumbHost) return;
    if (auto* old = m_texThumbHost->getChildByTag(kTagTexThumb)) {
        old->removeFromParent();
    }
    if (!m_asset || m_asset->empty()) return;
    auto const& frame = m_asset->frameAt(0);
    if (frame.empty()) return;
    if (auto* spr = SpritePreviewRenderer::createSprite(frame)) {
        auto sz = spr->getContentSize();
        if (sz.width > 0 && sz.height > 0) {
            spr->setScale(std::min({22.f / sz.width, 22.f / sz.height, 3.f}));
        }
        spr->setTag(kTagTexThumb);
        m_texThumbHost->addChildAtPosition(spr, Anchor::Center);
    }
}

void FusionEditorLayer::refreshToolsUi() {
    auto s = currentSetting();
    bool hasMask = m_mask && !m_mask->empty();
    if (m_stateLbl) {
        if (!m_asset || m_asset->empty()) {
            m_stateLbl->setString(hasMask
                ? "region painted - Pick a texture"
                : "no texture - Pick or paint first");
        } else {
            m_stateLbl->setString(fmt::format("{}x{} {}f {}",
                m_asset->width(), m_asset->height(), m_asset->frameCount(),
                hasMask ? "- fused" : "- tap to paint").c_str());
        }
        m_stateLbl->limitLabelWidth(kToolsW - 44.f, 0.22f, 0.1f);
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
        m_pixelLbl->setString(fmt::format("px {:+d}, {:+d}",
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
            : "Paint OFF - drag Result to move, arrows 1px");
        m_hintLbl->limitLabelWidth(kToolsW - 10.f, 0.28f, 0.12f);
    }
    if (m_undoBtn) {
        m_undoBtn->setEnabled(!m_undo.empty());
        if (auto* spr = typeinfo_cast<ButtonSprite*>(m_undoBtn->getNormalImage()))
            spr->setOpacity(m_undo.empty() ? 100 : 255);
    }
}


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

// Do not clamp an active Result drag; the pointer may leave the preview box.
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
    if (!onOriginal && !onResult) {
        if (m_originalSpr) {
            auto lo = m_originalSpr->convertToNodeSpace(touch->getLocation());
            auto szo = m_originalSpr->getContentSize();
            log::info("[fusion] touch missed both previews: orig-local "
                "({:.1f},{:.1f}) of {:.0f}x{:.0f}", lo.x, lo.y, szo.width, szo.height);
        } else {
            log::info("[fusion] touch ignored: original sprite not loaded yet");
        }
        return false;
    }
    log::info("[fusion] touch began on {} at raw ({:.1f},{:.1f})",
        onResult ? "result" : "original", fx, fy);
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
// Without a texture, keep the gesture as a tap so release still paints.
    if (!m_asset || m_asset->empty()) return;
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
// Keep drag updates in memory; avoid UI rebuilds and disk I/O.
    m_project.spriteSettings[m_selected.frameName] = s;
    if (m_pixelLbl) {
        m_pixelLbl->setString(fmt::format("px {:+d}, {:+d}", newPx, newPy).c_str());
    }
// Re-sample the original asset so movement never exposes a clipped old edge.
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
// Composite once after drag; the cache is already warm.
        renderPreviewFast();
        setStatus(fmt::format("Moved px {:+d}, {:+d}", s.fusionPixelX, s.fusionPixelY));
        return;
    }
// A tap paints either preview; both boxes use the same gesture.
    if (!m_paintArmed) {
        setStatus("Paint OFF - toggle it on to fill regions.");
        return;
    }
    int px = static_cast<int>(std::floor(sx));
    int py = static_cast<int>(std::floor(sy));
    if (touch) {
        float tx = 0.f, ty = 0.f;
        if (mapTouchToSpriteFloatOn(wasResult ? m_resultSpr : m_originalSpr,
                                    touch, tx, ty, true)) {
            px = static_cast<int>(std::floor(tx));
            py = static_cast<int>(std::floor(ty));
        }
    }
    applyFill(px, py);
}

void FusionEditorLayer::applyFill(int pixelX, int pixelY) {
    if (!m_hasSelection || !m_pixels || m_pixels->empty()) return;
    auto s = currentSetting();
    int colorR = s.fusionTolerance > 0 ? s.fusionTolerance : 110;
    int expand = std::clamp(s.fusionExpandRadius, 0, 12);
    MaskBuffer region = FusionEngine::floodFill(
        *m_pixels, pixelX, pixelY, colorR, /*alphaCutoff=*/12, expand);
    bool any = false;
    for (auto v : region.data) if (v) { any = true; break; }
    log::info("[fusion] fill at ({},{}) frame {}x{} tol={} expand={} -> {}",
        pixelX, pixelY, m_pixels->width(), m_pixels->height(),
        colorR, expand, any ? "region" : "empty");
    if (!any) {
        auto seed = m_pixels->at(pixelX, pixelY);
        setStatus(fmt::format("Nothing to fill at {},{} (alpha {}).",
            pixelX, pixelY, seed.a));
        Notification::create("Nothing to fill there.", NotificationIcon::Info, 1.3f)->show();
        return;
    }
    pushUndo();
    if (!m_mask || m_mask->width != region.width || m_mask->height != region.height) {
        m_mask = std::make_shared<MaskBuffer>(std::move(region));
    } else {
        FusionEngine::orMask(*m_mask, region);
    }
    invalidateStampCache();
    persistMask();
    refreshToolsUi();
    refreshBrowserBadges();
    ensureStampCache();
    renderPreviewFast();
    bool hasTex = m_asset && !m_asset->empty();
    setStatus(hasTex
        ? "Region filled (Ctrl+Z undo)."
        : "Region painted - now Pick a texture.");
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
    if (!m_mask || m_mask->empty()) return;
    m_frameAccum += dt * 1000.f;
    int delay = m_asset->delayAt(m_frameIndex);
    if (m_frameAccum < static_cast<float>(delay)) return;
    m_frameAccum = 0.f;
    m_frameIndex = (m_frameIndex + 1) % m_asset->frameCount();
    this->unschedule(schedule_selector(FusionEditorLayer::renderPreview));
    this->renderPreview(0.f);
}

}
