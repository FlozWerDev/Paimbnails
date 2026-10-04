#include "ColorPickerOverlay.hpp"
#include "../services/ColorFormat.hpp"

#include "../../editor-suite/EditorHelpers.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/OverlayManager.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <Geode/binding/GJEffectManager.hpp>
#include <Geode/binding/ColorAction.hpp>
#include <Geode/binding/ColorSelectPopup.hpp>
#include <Geode/utils/general.hpp>

#include <algorithm>
#include <cmath>

using namespace cocos2d;
using namespace geode::prelude;

#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif

namespace paimon::editorcp {

namespace kit = paimon::editor::kit;

namespace {
    constexpr float kHudW         = 464.f;
    constexpr float kHudH         = 96.f;
    constexpr float kHudMargin    = 8.f;
    constexpr float kHeaderH      = 20.f;
    constexpr int   kPickerZOrder = 999500;

    constexpr float kLoupeCell   = 6.f;
    constexpr float kLoupePad    = 3.f;
    constexpr float kLoupeLabelH = 13.f;
    // keeps the loupe clear of the pixels read under the cursor.
    constexpr float kLoupeOffset = 18.f;

// priority: text input, hud menu, picker, then editor.
    constexpr int kMenuPriority = -300;
    constexpr int kPickPriority = -200;

    constexpr char const* kSavedFormat  = "editor-cp-format";
    constexpr char const* kSavedAuto    = "editor-cp-auto-apply";
    constexpr char const* kSavedChannel = "editor-cp-channel-id";
    constexpr char const* kSavedLast    = "editor-cp-last-color";
    constexpr char const* kSavedRecent  = "editor-cp-recent";
    constexpr char const* kSavedLoupe   = "editor-cp-loupe";
    constexpr char const* kSavedDockTop = "editor-cp-dock-top";

    std::string tr(char const* key, char const* fallback) {
        auto value = Localization::get().getString(key);
        return value == key ? std::string(fallback) : value;
    }

    bool sameColor(ccColor3B a, ccColor3B b) {
        return a.r == b.r && a.g == b.g && a.b == b.b;
    }

    std::string encodeRecent(std::vector<ccColor3B> const& colors) {
        std::string out;
        for (auto const& c : colors) {
            if (!out.empty()) out.push_back(',');
            out += formatColor(c, 2);
        }
        return out;
    }

    std::vector<ccColor3B> decodeRecent(std::string const& text, std::size_t limit) {
        std::vector<ccColor3B> out;
        for (auto part : geode::utils::string::split(text, ",")) {
            if (out.size() >= limit) break;
            if (part.size() != 6) continue;
            auto parsed = geode::utils::numFromString<uint32_t>(part, 16);
            if (!parsed) continue;
            uint32_t const v = parsed.unwrap();
            out.push_back({
                static_cast<GLubyte>((v >> 16) & 0xFF),
                static_cast<GLubyte>((v >> 8) & 0xFF),
                static_cast<GLubyte>(v & 0xFF),
            });
        }
        return out;
    }

    char const* channelName(int id) {
        switch (id) {
            case 1000: return "BG";
            case 1001: return "G1";
            case 1002: return "Line";
            case 1003: return "3DL";
            case 1004: return "Obj";
            case 1005: return "P1";
            case 1006: return "P2";
            case 1007: return "LBG";
            case 1009: return "G2";
            case 1010: return "Black";
            case 1011: return "White";
            case 1012: return "Lighter";
            case 1013: return "MG";
            case 1014: return "MG2";
            default:   return nullptr;
        }
    }

    ColorAction* colorActionFor(int channelID) {
        if (channelID <= 0) return nullptr;
        auto* lel = LevelEditorLayer::get();
        if (!lel || !lel->m_levelSettings || !lel->m_levelSettings->m_effectManager) return nullptr;
        return lel->m_levelSettings->m_effectManager->getColorAction(channelID);
    }

    NineSlice* makeSwatch(CCNode* parent, CCRect const& rect, ccColor3B color) {
        auto* frame = paimon::ui::makeInset({rect.size.width + 4.f, rect.size.height + 4.f}, 220);
        frame->setPosition(rect.origin - CCPoint{2.f, 2.f});
        parent->addChild(frame, 1);

        auto* fill = paimon::ui::makeInset(rect.size, 255, color);
        fill->setPosition(rect.origin);
        parent->addChild(fill, 2);
        return fill;
    }

    CCLabelBMFont* caption(CCNode* parent, char const* text, CCPoint pos, ccColor3B color,
        CCPoint anchor = {0.f, 0.5f}, float scale = 0.24f) {
        auto* label = kit::label(text, "bigFont.fnt", scale, color);
        label->setAnchorPoint(anchor);
        label->setPosition(pos);
        parent->addChild(label, 3);
        return label;
    }
}

ColorPickerOverlay* ColorPickerOverlay::s_instance = nullptr;

void ColorPickerOverlay::show() {
    if (s_instance) { s_instance->doClose(); return; }

    auto* ov = ColorPickerOverlay::create();
    if (!ov) return;
    ov->setID("paimbnails/editor-color-picker-overlay"_spr);

    if (auto* host = geode::OverlayManager::get()) {
        host->addChild(ov, kPickerZOrder);
    } else if (auto* scene = CCDirector::get()->getRunningScene()) {
        scene->addChild(ov, 99999);
    }
}

bool ColorPickerOverlay::init() {
    if (!CCLayer::init()) return false;
    s_instance = this;

    auto* mod = Mod::get();
    m_formatIndex = clampFormatIndex(static_cast<int>(mod->getSavedValue<int64_t>(kSavedFormat, 0)));
    m_autoApply = mod->getSavedValue<bool>(kSavedAuto, false);
    m_loupeEnabled = mod->getSavedValue<bool>(kSavedLoupe, true);
    m_dockTop = mod->getSavedValue<bool>(kSavedDockTop, false);
    m_recent = decodeRecent(mod->getSavedValue<std::string>(kSavedRecent, ""), kRecentMax);

    this->setTouchEnabled(true);
    this->setKeypadEnabled(true);

// transparent overlay; the editor remains visible while the framebuffer is sampled live.
    this->buildUI();
    this->scheduleUpdate();
    m_ready = true;
    return true;
}

void ColorPickerOverlay::onExit() {
    CCLayer::onExit();
    if (s_instance == this) s_instance = nullptr;
}

void ColorPickerOverlay::onEnter() {
    CCLayer::onEnter();
// show() runs inside a touch event, so defer priority changes until the
// dispatcher commits the newly registered handler.
    if (m_priorityScheduled) return;
    m_priorityScheduled = true;
    geode::WeakRef<ColorPickerOverlay> weak = this;
    Loader::get()->queueInMainThread([weak]() {
        auto self = weak.lock();
        if (!self || self->m_closing) return;
        if (self->m_controlsMenu) self->m_controlsMenu->setHandlerPriority(kMenuPriority);
    });
}

void ColorPickerOverlay::registerWithTouchDispatcher() {
// run after hud input but before the editor so the overlay is modal.
    CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, kPickPriority, true);
}

void ColorPickerOverlay::keyBackClicked() {
    this->doClose();
}

void ColorPickerOverlay::buildUI() {
    m_hud = CCNode::create();
    m_hud->setContentSize({kHudW, kHudH});
    m_hud->setAnchorPoint({0.5f, 0.f});
    m_hud->ignoreAnchorPointForPosition(false);
    this->addChild(m_hud, 20);

    if (auto* panel = NineSlice::create("GJ_square01.png")) {
        panel->setContentSize({kHudW, kHudH});
        panel->setAnchorPoint({0.f, 0.f});
        m_hud->addChild(panel, 0);
    }

    m_controlsMenu = CCMenu::create();
    m_controlsMenu->setPosition({0.f, 0.f});
    m_controlsMenu->setContentSize({kHudW, kHudH});
    m_hud->addChild(m_controlsMenu, 5);

    this->buildHeader(kHudW, kHudH);

    float const bodyTop = kHudH - kHeaderH - 4.f;
    float const bodyY = 7.f;
    float const bodyH = bodyTop - bodyY;
    this->buildSwatchSection({8.f, bodyY, 202.f, bodyH});
    this->buildChannelSection({216.f, bodyY, 134.f, bodyH});
    this->buildActionSection({356.f, bodyY, kHudW - 364.f, bodyH});

    this->buildLoupe();
    this->placeHud(false);
    this->refreshFormatPills();
    this->refreshRecent();
    this->refreshChannel();
    this->updateReadout();
}

void ColorPickerOverlay::buildHeader(float width, float height) {
    float const y = height - kHeaderH * 0.5f - 3.f;

    auto* icon = kit::glyph("paim_gif_colors.png", "GJ_paintBtn_001.png", 13.f);
    icon->setPosition({17.f, y});
    m_hud->addChild(icon, 3);

    auto* title = kit::label(tr("pai.editorcp.title", "Color Picker").c_str(), "goldFont.fnt", 0.46f);
    title->limitLabelWidth(92.f, 0.46f, 0.25f);
    title->setAnchorPoint({0.f, 0.5f});
    title->setPosition({28.f, y + 1.f});
    m_hud->addChild(title, 3);

    float const iconsLeft = width - 72.f;
    float const hintLeft = 28.f + title->getScaledContentSize().width + 10.f;
    auto* hint = kit::label(
        tr("pai.editorcp.shortcuts",
            "Click/Space: pick   C: copy   Enter: apply   Tab: format   L: loupe   Up/Down: ID").c_str(),
        "chatFont.fnt", 0.5f, kit::tint::muted);
    hint->limitLabelWidth(iconsLeft - hintLeft - 6.f, 0.5f, 0.2f);
    hint->setPosition({(hintLeft + iconsLeft) * 0.5f, y});
    m_hud->addChild(hint, 3);

    m_loupeToggle = kit::toolToggle("paim_ui_zoomIn.png", "GJ_zoomInBtn_001.png",
        EditorBaseColor::Cyan, 15.f, m_loupeEnabled, [this](bool on) { this->setLoupeEnabled(on); });
    if (m_loupeToggle) {
        m_loupeToggle->setPosition({width - 58.f, y});
        m_controlsMenu->addChild(m_loupeToggle);
    }

    m_dockUpBtn = kit::toolButton(nullptr, "edit_upBtn_001.png", EditorBaseColor::Gray, 15.f,
        [this] { this->setDockTop(true); });
    m_dockDownBtn = kit::toolButton(nullptr, "edit_downBtn_001.png", EditorBaseColor::Gray, 15.f,
        [this] { this->setDockTop(false); });
    for (auto* btn : {m_dockUpBtn, m_dockDownBtn}) {
        if (!btn) continue;
        btn->setPosition({width - 38.f, y});
        m_controlsMenu->addChild(btn);
    }

    auto* closeSpr = kit::glyph(nullptr, "GJ_closeBtn_001.png", 17.f);
    auto* closeBtn = CCMenuItemExt::createSpriteExtra(closeSpr, [this](auto*) { this->doClose(); });
    closeBtn->setPosition({width - 16.f, y});
    m_controlsMenu->addChild(closeBtn);

    auto* line = paimon::ui::makeDivider(width - 16.f, kit::tint::gold, 90);
    line->setPosition({width * 0.5f, height - kHeaderH - 3.f});
    m_hud->addChild(line, 2);
}

void ColorPickerOverlay::buildSwatchSection(CCRect const& area) {
    m_hud->addChild(kit::inset(area, 70), 1);

    float const swatch = 34.f;
    float const sy = area.origin.y + 7.f;
    float const liveX = area.origin.x + 8.f;
    float const pickX = liveX + swatch + 8.f;
    float const capY = sy + swatch + 8.f;

    m_liveSwatch = makeSwatch(m_hud, {liveX, sy, swatch, swatch}, m_liveColor);
    caption(m_hud, tr("pai.editorcp.live", "LIVE").c_str(), {liveX + swatch * 0.5f, capY},
        kit::tint::green, {0.5f, 0.5f});

    // a holder lets the pick pop scale around the swatch centre.
    m_pickBox = CCNode::create();
    m_pickBox->setContentSize({swatch, swatch});
    m_pickBox->setAnchorPoint({0.5f, 0.5f});
    m_pickBox->ignoreAnchorPointForPosition(false);
    m_pickBox->setPosition({pickX + swatch * 0.5f, sy + swatch * 0.5f});
    m_hud->addChild(m_pickBox, 2);
    m_pickSwatch = makeSwatch(m_pickBox, {0.f, 0.f, swatch, swatch}, m_selColor);

    m_pickEmpty = kit::label("?", "bigFont.fnt", 0.5f, kit::tint::muted);
    m_pickEmpty->setPosition({swatch * 0.5f, swatch * 0.5f + 1.f});
    m_pickBox->addChild(m_pickEmpty, 4);
    caption(m_hud, tr("pai.editorcp.picked", "PICKED").c_str(), {pickX + swatch * 0.5f, capY},
        kit::tint::gold, {0.5f, 0.5f});

    float const textX = pickX + swatch + 10.f;
    float const textW = area.getMaxX() - 6.f - textX;

    m_valueLabel = kit::label("#FFFFFF", "bigFont.fnt", 0.42f);
    m_valueLabel->setAnchorPoint({0.f, 0.5f});
    m_valueLabel->setPosition({textX, capY - 1.f});
    m_hud->addChild(m_valueLabel, 3);

    m_liveLabel = kit::label("", "chatFont.fnt", 0.5f, kit::tint::muted);
    m_liveLabel->setAnchorPoint({0.f, 0.5f});
    m_liveLabel->setPosition({textX, capY - 15.f});
    m_hud->addChild(m_liveLabel, 3);

    float const gap = 2.f;
    float const pillW = (textW - gap * (kFormatCount - 1)) / kFormatCount;
    float const pillY = sy + 7.f;
    for (int i = 0; i < kFormatCount; ++i) {
        auto pill = kit::pill(nullptr, nullptr, formatName(i), "GJ_button_04.png", {pillW, 15.f},
            [this, i] { this->setFormat(i); }, "bigFont.fnt");
        pill.button->setPosition({textX + pillW * 0.5f + i * (pillW + gap), pillY});
        m_controlsMenu->addChild(pill.button);
        m_formatPills.push_back(pill);
    }
}

void ColorPickerOverlay::buildChannelSection(CCRect const& area) {
    m_hud->addChild(kit::inset(area, 70), 1);

    float const left = area.origin.x + 8.f;
    float const right = area.getMaxX() - 8.f;
    float const top = area.getMaxY();

    caption(m_hud, tr("pai.editorcp.channel", "COLOR ID").c_str(), {left, top - 8.f}, kit::tint::muted);
    m_channelName = caption(m_hud, "", {right, top - 8.f}, kit::tint::gold, {1.f, 0.5f});

    float const rowY = top - 25.f;
    float const swatch = 18.f;
    float const swatchX = right - swatch;
    float const plusX = swatchX - 12.f;
    float const minusX = left + 7.f;
    float const inputX = (minusX + plusX) * 0.5f;
    float const inputW = (plusX - minusX - 20.f) / 0.8f;

    for (int direction : {-1, 1}) {
        auto* sprite = direction < 0
            ? kit::glyph("paim_ui_stepMinus.png", "edit_leftBtn_001.png", 14.f)
            : kit::glyph("paim_ui_stepPlus.png", "GJ_plus2Btn_001.png", 14.f);
        auto* item = CCMenuItemExt::createSpriteExtra(sprite, [this, direction](auto*) {
            this->stepColorID(direction);
        });
        item->setSizeMult(1.3f);
        item->setPosition({direction < 0 ? minusX : plusX, rowY});
        m_controlsMenu->addChild(item);
    }

    m_idInput = geode::TextInput::create(inputW, "0-9999");
    m_idInput->setFilter("0123456789");
    m_idInput->setMaxCharCount(4);
    m_idInput->setScale(0.8f);
    m_idInput->setPosition({inputX, rowY});
    m_hud->addChild(m_idInput, 4);

    auto savedID = Mod::get()->getSavedValue<std::string>(kSavedChannel, "");
    if (!savedID.empty()) m_idInput->setString(savedID);
    m_idInput->setCallback([this](std::string const&) {
        m_hasApplied = false;
        this->refreshChannel();
    });

    m_channelSwatch = makeSwatch(m_hud, {swatchX, rowY - swatch * 0.5f, swatch, swatch}, {0, 0, 0});

    float const recentCapY = area.origin.y + 26.f;
    caption(m_hud, tr("pai.editorcp.recent", "RECENT").c_str(), {left, recentCapY}, kit::tint::muted);

    float const slotGap = 3.f;
    float const slot = (right - left - slotGap * (kRecentMax - 1)) / kRecentMax;
    float const slotY = area.origin.y + 10.f;
    for (std::size_t i = 0; i < kRecentMax; ++i) {
        auto* fill = paimon::ui::makeInset({slot, slot}, 255, {0, 0, 0});
        auto* item = CCMenuItemExt::createSpriteExtra(fill, [this, i](auto*) {
            if (i < m_recent.size()) this->selectColor(m_recent[i]);
        });
        item->setPosition({left + slot * 0.5f + i * (slot + slotGap), slotY});
        m_controlsMenu->addChild(item);
        m_recentSlots.push_back(fill);
        m_recentButtons.push_back(item);
    }
}

void ColorPickerOverlay::buildActionSection(CCRect const& area) {
    m_hud->addChild(kit::inset(area, 70), 1);

    float const cx = area.getMidX();
    float const pillW = area.size.width - 12.f;
    float const top = area.getMaxY();

    auto copy = kit::pill(nullptr, "GJ_copyBtn_001.png", tr("pai.editorcp.copy", "Copy").c_str(),
        "GJ_button_05.png", {pillW, 19.f}, [this] { this->onCopy(); });
    copy.button->setPosition({cx, top - 13.f});
    m_controlsMenu->addChild(copy.button);

    auto apply = kit::pill(nullptr, nullptr, tr("pai.editorcp.apply", "Apply").c_str(),
        "GJ_button_01.png", {pillW, 19.f}, [this] { this->onApply(); });
    apply.button->setPosition({cx, top - 35.f});
    m_controlsMenu->addChild(apply.button);

    m_autoToggle = paimon::ui::makeToggle(m_autoApply, [this](bool on) {
        m_autoApply = on;
        Mod::get()->setSavedValue<bool>(kSavedAuto, m_autoApply);
        if (m_autoApply) {
            m_autoNoIdWarned = false;
            this->tryAutoApply();
        }
    }, 0.42f);
    float const rowY = area.origin.y + 10.f;
    m_autoToggle->setPosition({area.origin.x + 14.f, rowY});
    m_controlsMenu->addChild(m_autoToggle);

    auto* autoLabel = caption(m_hud, tr("pai.editorcp.auto", "Auto-apply").c_str(),
        {area.origin.x + 26.f, rowY}, kit::tint::label);
    autoLabel->limitLabelWidth(area.size.width - 32.f, 0.24f, 0.12f);
}

void ColorPickerOverlay::buildLoupe() {
    float const grid = kLoupeCells * kLoupeCell;
    CCSize const size{grid + kLoupePad * 2.f, grid + kLoupePad * 2.f + kLoupeLabelH};

    m_loupe = CCNode::create();
    m_loupe->setContentSize(size);
    m_loupe->setAnchorPoint({0.f, 0.f});
    m_loupe->setVisible(false);
    this->addChild(m_loupe, 30);

    m_loupe->addChild(paimon::ui::makeInset(size, 235), 0);

    m_loupeGrid = CCDrawNode::create();
    m_loupeGrid->setPosition({kLoupePad, kLoupePad + kLoupeLabelH});
    m_loupe->addChild(m_loupeGrid, 1);

    m_loupeLabel = kit::label("", "bigFont.fnt", 0.26f);
    m_loupeLabel->setPosition({size.width * 0.5f, kLoupePad + kLoupeLabelH * 0.5f});
    m_loupe->addChild(m_loupeLabel, 2);
}

void ColorPickerOverlay::placeHud(bool animate) {
    if (!m_hud) return;
    auto const win = CCDirector::get()->getWinSize();
    CCPoint const target{win.width * 0.5f, m_dockTop ? win.height - kHudMargin - kHudH : kHudMargin};
    m_hud->stopAllActions();
    if (animate && paimon::ui::motionEnabled()) {
        m_hud->runAction(CCEaseSineOut::create(CCMoveTo::create(paimon::ui::motionDuration(0.2f), target)));
    } else {
        m_hud->setPosition(target);
    }
    if (m_dockUpBtn) m_dockUpBtn->setVisible(!m_dockTop);
    if (m_dockDownBtn) m_dockDownBtn->setVisible(m_dockTop);
}

void ColorPickerOverlay::onPreSwapSample() {
    if (s_instance) s_instance->liveSample();
}

void ColorPickerOverlay::liveSample() {
    if (!m_ready || m_closing) return;

    auto* dir    = CCDirector::get();
    auto* glView = dir ? dir->getOpenGLView() : nullptr;
    if (!dir || !glView) return;

    CCSize win = dir->getWinSize();
    CCSize fs  = glView->getFrameSize();
    if (win.width <= 0.f || win.height <= 0.f || fs.width <= 0.f || fs.height <= 0.f) return;

    const float scaleX = fs.width  / win.width;
    const float scaleY = fs.height / win.height;
    const int   fw = static_cast<int>(fs.width);
    const int   fh = static_cast<int>(fs.height);

    CCPoint m = geode::cocos::getMousePos();
    m.x = std::clamp(m.x, 0.f, win.width);
    m.y = std::clamp(m.y, 0.f, win.height);

    const int cxDev = std::clamp(static_cast<int>(std::lround(m.x * scaleX)), 0, fw - 1);
    const int cyDev = std::clamp(static_cast<int>(std::lround(m.y * scaleY)), 0, fh - 1);

    int const want = m_loupeEnabled ? kLoupeCells : 1;
    int const w = std::min(want, fw);
    int const h = std::min(want, fh);
    int const x0 = std::clamp(cxDev - w / 2, 0, fw - w);
    int const y0 = std::clamp(cyDev - h / 2, 0, fh - h);

while (glGetError() != GL_NO_ERROR) {}

    GLint origFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &origFBO);
    if (origFBO != 0) glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x0, y0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, m_loupeBuf.data());
    glPixelStorei(GL_PACK_ALIGNMENT, 4);

    if (origFBO != 0) glBindFramebuffer(GL_FRAMEBUFFER, origFBO);

    if (glGetError() != GL_NO_ERROR) return;

    m_loupeW = w;
    m_loupeH = h;
    m_loupeCX = cxDev - x0;
    m_loupeCY = cyDev - y0;
    m_loupeDirty = true;

    auto const* px = &m_loupeBuf[static_cast<std::size_t>((m_loupeCY * w + m_loupeCX) * 4)];
    m_liveColor = { px[0], px[1], px[2] };
}

void ColorPickerOverlay::updateReadout() {
    ccColor3B const shown = m_hasSelection ? m_selColor : m_liveColor;

    if (m_liveSwatch) m_liveSwatch->setColor(m_liveColor);
    if (m_pickSwatch) {
        m_pickSwatch->setColor(m_hasSelection ? m_selColor : ccColor3B{40, 40, 48});
    }
    if (m_pickEmpty) m_pickEmpty->setVisible(!m_hasSelection);

    auto value = formatColor(shown, m_formatIndex);
    if (m_valueLabel && value != m_shownValue) {
        m_shownValue = value;
        m_valueLabel->setString(value.c_str());
        m_valueLabel->limitLabelWidth(96.f, 0.42f, 0.2f);
    }

    auto live = fmt::format("{} {}", tr("pai.editorcp.live.short", "live"), formatColor(m_liveColor, m_formatIndex));
    if (m_liveLabel && live != m_shownLive) {
        m_shownLive = live;
        m_liveLabel->setString(live.c_str());
        m_liveLabel->limitLabelWidth(96.f, 0.5f, 0.2f);
    }
}

void ColorPickerOverlay::updateLoupe() {
    if (!m_loupe) return;
    auto const mouse = geode::cocos::getMousePos();
    bool const visible = m_loupeEnabled && !this->pointInHud(mouse) && m_loupeW > 1;
    m_loupe->setVisible(visible);
    if (!visible) return;

    auto const win = CCDirector::get()->getWinSize();
    auto const size = m_loupe->getContentSize();
    CCPoint pos = mouse + CCPoint{kLoupeOffset, kLoupeOffset};
    if (pos.x + size.width > win.width - 2.f) pos.x = mouse.x - kLoupeOffset - size.width;
    if (pos.y + size.height > win.height - 2.f) pos.y = mouse.y - kLoupeOffset - size.height;
    m_loupe->setPosition(pos);

    if (!m_loupeDirty || !m_loupeGrid) return;
    m_loupeDirty = false;
    m_loupeGrid->clear();

    // edge clamping can shift the block; keep the cursor pixel in the middle cell.
    int const shiftX = kLoupeCells / 2 - m_loupeCX;
    int const shiftY = kLoupeCells / 2 - m_loupeCY;
    for (int row = 0; row < m_loupeH; ++row) {
        for (int col = 0; col < m_loupeW; ++col) {
            int const cx = col + shiftX;
            int const cy = row + shiftY;
            if (cx < 0 || cy < 0 || cx >= kLoupeCells || cy >= kLoupeCells) continue;
            auto const* px = &m_loupeBuf[static_cast<std::size_t>((row * m_loupeW + col) * 4)];
            ccColor4F const fill{px[0] / 255.f, px[1] / 255.f, px[2] / 255.f, 1.f};
            float const x0 = cx * kLoupeCell;
            float const y0 = cy * kLoupeCell;
            CCPoint quad[4]{{x0, y0}, {x0 + kLoupeCell, y0}, {x0 + kLoupeCell, y0 + kLoupeCell}, {x0, y0 + kLoupeCell}};
            m_loupeGrid->drawPolygon(quad, 4, fill, 0.f, fill);
        }
    }

    float const c0 = (kLoupeCells / 2) * kLoupeCell;
    float const c1 = c0 + kLoupeCell;
    CCPoint center[4]{{c0, c0}, {c1, c0}, {c1, c1}, {c0, c1}};
    float const luma = 0.299f * m_liveColor.r + 0.587f * m_liveColor.g + 0.114f * m_liveColor.b;
    ccColor4F const ring = luma > 140.f ? ccColor4F{0.f, 0.f, 0.f, 1.f} : ccColor4F{1.f, 1.f, 1.f, 1.f};
    m_loupeGrid->drawPolygon(center, 4, {0.f, 0.f, 0.f, 0.f}, 0.8f, ring);

    if (m_loupeLabel) {
        m_loupeLabel->setString(formatColor(m_liveColor, 0).c_str());
        m_loupeLabel->setColor(m_liveColor);
        if (luma < 60.f) m_loupeLabel->setColor(kit::tint::white);
    }
}

void ColorPickerOverlay::refreshFormatPills() {
    for (int i = 0; i < static_cast<int>(m_formatPills.size()); ++i) {
        auto& pill = m_formatPills[static_cast<std::size_t>(i)];
        bool const active = i == m_formatIndex;
        kit::setPillSkin(pill, active ? "GJ_button_01.png" : "GJ_button_04.png");
        if (pill.label) pill.label->setColor(active ? kit::tint::white : kit::tint::off);
    }
}

void ColorPickerOverlay::refreshRecent() {
    for (std::size_t i = 0; i < m_recentSlots.size(); ++i) {
        bool const filled = i < m_recent.size();
        auto* slot = m_recentSlots[i];
        slot->setColor(filled ? m_recent[i] : ccColor3B{0, 0, 0});
        slot->setOpacity(filled ? 255 : 90);
        if (i < m_recentButtons.size()) m_recentButtons[i]->setEnabled(filled);
    }
}

void ColorPickerOverlay::refreshChannel() {
    int const id = this->currentColorID();
    auto* action = colorActionFor(id);
    if (m_channelSwatch) {
        m_channelSwatch->setColor(action ? action->m_color : ccColor3B{0, 0, 0});
        m_channelSwatch->setOpacity(action ? 255 : 90);
    }
    if (m_channelName) {
        char const* name = channelName(id);
        m_channelName->setString(name ? name : (action ? fmt::format("#{}", id).c_str() : "-"));
        m_channelName->setColor(action ? kit::tint::gold : kit::tint::off);
    }
}

void ColorPickerOverlay::pickAt() {
    if (!m_ready) return;
    bool const changed = !m_hasSelection || !sameColor(m_selColor, m_liveColor);
    m_selColor = m_liveColor;
    m_hasSelection = true;
    updateReadout();
    if (changed && m_pickBox) {
        m_pickBox->stopAllActions();
        m_pickBox->setScale(1.12f);
        m_pickBox->runAction(CCEaseBackOut::create(CCScaleTo::create(0.18f, 1.f)));
    }
    if (m_autoApply) this->tryAutoApply();
}

void ColorPickerOverlay::selectColor(ccColor3B color) {
    m_selColor = color;
    m_hasSelection = true;
    updateReadout();
    if (m_pickBox) {
        m_pickBox->stopAllActions();
        m_pickBox->setScale(1.12f);
        m_pickBox->runAction(CCEaseBackOut::create(CCScaleTo::create(0.18f, 1.f)));
    }
    if (m_autoApply) this->tryAutoApply();
}

void ColorPickerOverlay::pushRecent(ccColor3B color) {
    auto it = std::find_if(m_recent.begin(), m_recent.end(),
        [color](ccColor3B const& c) { return sameColor(c, color); });
    if (it != m_recent.end()) m_recent.erase(it);
    m_recent.insert(m_recent.begin(), color);
    if (m_recent.size() > kRecentMax) m_recent.resize(kRecentMax);
    Mod::get()->setSavedValue<std::string>(kSavedRecent, encodeRecent(m_recent));
    this->refreshRecent();
}

std::string ColorPickerOverlay::currentValueString() const {
    ccColor3B c = m_hasSelection ? m_selColor : m_liveColor;
    return formatColor(c, m_formatIndex);
}

bool ColorPickerOverlay::pointInHud(CCPoint p) const {
    return m_hud && m_hud->boundingBox().containsPoint(p);
}

void ColorPickerOverlay::update(float) {
    if (!m_ready || m_closing) return;
    updateReadout();
    updateLoupe();
}

bool ColorPickerOverlay::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (!m_ready || m_closing) return false;
    CCPoint p = touch->getLocation();
    if (pointInHud(p)) return true;
    m_dragging = true;
    this->pickAt();
    return true;
}

void ColorPickerOverlay::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (!m_ready || m_closing || !m_dragging) return;
    this->pickAt();
}

void ColorPickerOverlay::ccTouchEnded(CCTouch*, CCEvent*) {
    // a drag only records its final colour, otherwise history fills with noise.
    if (m_dragging && m_hasSelection && !m_closing) this->pushRecent(m_selColor);
    m_dragging = false;
}

void ColorPickerOverlay::ccTouchCancelled(CCTouch*, CCEvent*) {
    m_dragging = false;
}

bool ColorPickerOverlay::handleKey(KeyboardInputData const& data) {
    auto* self = s_instance;
    if (!self || !self->m_ready || self->m_closing) return false;
    if (data.action == KeyboardInputData::Action::Release) return false;
    if (paimon::editor::focusedTextInput()) return false;

    bool const press = data.action == KeyboardInputData::Action::Press;
    bool const shift = (data.modifiers.value & uint8_t(KeyboardModifier::Shift)) != 0;
    switch (data.key) {
        case KEY_Space:
            if (press && !self->pointInHud(geode::cocos::getMousePos())) {
                self->pickAt();
                self->pushRecent(self->m_selColor);
            }
            return true;
        case KEY_C:
            if (press) self->onCopy();
            return true;
        case KEY_Enter:
        case KEY_NumEnter:
            if (press) self->onApply();
            return true;
        case KEY_Tab:
            self->stepFormat(shift ? -1 : 1);
            return true;
        case KEY_L:
            if (press) {
                self->setLoupeEnabled(!self->m_loupeEnabled);
                if (self->m_loupeToggle) self->m_loupeToggle->toggle(self->m_loupeEnabled);
            }
            return true;
        case KEY_Up:
        case KEY_ArrowUp:
            self->stepColorID(shift ? 10 : 1);
            return true;
        case KEY_Down:
        case KEY_ArrowDown:
            self->stepColorID(shift ? -10 : -1);
            return true;
        default:
            return false;
    }
}

void ColorPickerOverlay::stepFormat(int delta) {
    this->setFormat((m_formatIndex + delta + kFormatCount) % kFormatCount);
}

void ColorPickerOverlay::setFormat(int index) {
    m_formatIndex = clampFormatIndex(index);
    Mod::get()->setSavedValue<int64_t>(kSavedFormat, m_formatIndex);
    this->refreshFormatPills();
    this->updateReadout();
}

int ColorPickerOverlay::currentColorID() const {
    if (!m_idInput) return 0;
    auto txt = m_idInput->getString();
    if (txt.empty()) return 0;
    if (auto r = geode::utils::numFromString<int>(txt); r) return std::clamp(r.unwrap(), 0, 9999);
    return 0;
}

void ColorPickerOverlay::stepColorID(int delta) {
    if (!m_idInput) return;
    int const id = std::clamp(this->currentColorID() + delta, 0, 9999);
    m_idInput->setString(std::to_string(id));
    m_hasApplied = false;
    this->refreshChannel();
    if (m_autoApply) this->tryAutoApply();
}

void ColorPickerOverlay::setLoupeEnabled(bool enabled) {
    m_loupeEnabled = enabled;
    Mod::get()->setSavedValue<bool>(kSavedLoupe, enabled);
    if (!enabled && m_loupe) m_loupe->setVisible(false);
}

void ColorPickerOverlay::setDockTop(bool top) {
    if (m_dockTop == top) return;
    m_dockTop = top;
    Mod::get()->setSavedValue<bool>(kSavedDockTop, top);
    this->placeHud(true);
}

void ColorPickerOverlay::onCopy() {
    if (!m_ready) return;
    std::string s = this->currentValueString();
    geode::utils::clipboard::write(s);
    this->pushRecent(m_hasSelection ? m_selColor : m_liveColor);
    PaimonNotify::show(fmt::format("{} {}", tr("pai.editorcp.copied", "Copied"), s), NotificationIcon::Success);
}

void ColorPickerOverlay::onApply() {
    if (!m_ready) return;

    ccColor3B col = m_hasSelection ? m_selColor : m_liveColor;

    std::string s = this->currentValueString();
    geode::utils::clipboard::write(s);
    this->pushRecent(col);
    Mod::get()->setSavedValue<int64_t>(kSavedFormat, m_formatIndex);
    Mod::get()->setSavedValue<std::string>(kSavedLast, formatColor(col, 2));

    int const channelID = this->currentColorID();
    if (channelID > 0) {
        const int       chId = channelID;
        const ccColor3B c    = col;
        this->doClose();
        Loader::get()->queueInMainThread([chId, c]() {
            if (paimon::isRuntimeShuttingDown()) return;
            if (auto* action = colorActionFor(chId)) {
                if (auto* popup = ColorSelectPopup::create(action)) {
                    popup->show();
                    popup->selectColor(c);
                    PaimonNotify::show(
                        fmt::format(fmt::runtime(tr("pai.editorcp.loaded",
                            "Loaded color into channel {} - confirm to apply")), chId),
                        NotificationIcon::Info);
                    return;
                }
            }
            PaimonNotify::show(tr("pai.editorcp.clipboard", "Color copied to clipboard."),
                NotificationIcon::Success);
        });
        return;
    }

    PaimonNotify::show(fmt::format("{} {}", tr("pai.editorcp.saved", "Saved & copied"), s),
        NotificationIcon::Success);
    this->doClose();
}

void ColorPickerOverlay::tryAutoApply() {
    if (!m_hasSelection) return;

    int const id = this->currentColorID();
    if (id <= 0) {
        if (!m_autoNoIdWarned) {
            m_autoNoIdWarned = true;
            PaimonNotify::show(tr("pai.editorcp.auto.noid",
                "Auto-apply: type a Color ID to push picks live."), NotificationIcon::Info);
        }
        return;
    }

    if (m_hasApplied && sameColor(m_lastApplied, m_selColor)) return;

    this->applyColorToChannel(m_selColor, id);
    m_lastApplied = m_selColor;
    m_hasApplied  = true;
    this->refreshChannel();
}

void ColorPickerOverlay::applyColorToChannel(ccColor3B col, int channelID) {
    auto* lel = LevelEditorLayer::get();
    auto* action = colorActionFor(channelID);
    if (!lel || !action) return;
    auto* em = lel->m_levelSettings->m_effectManager;

    action->m_color = col;
    action->m_fromColor = col;
    action->m_toColor = col;
    em->colorActionChanged(action);
    em->updateColorAction(action);
    lel->updateObjectColors(lel->m_objects);
}

void ColorPickerOverlay::doClose() {
    if (m_closing) return;
    m_closing = true;

    if (m_idInput) {
        Mod::get()->setSavedValue<std::string>(kSavedChannel, m_idInput->getString());
    }

    this->unscheduleAllSelectors();
    this->setTouchEnabled(false);
    if (s_instance == this) s_instance = nullptr;
    this->removeFromParent();
}

}

$execute {
    KeyboardInputEvent().listen(+[](KeyboardInputData& data) {
        return paimon::editorcp::ColorPickerOverlay::handleKey(data);
    }).leak();
}
