#include "FeedbackPopup.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../utils/ImageConverter.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../emotes/ui/EmoteButton.hpp"
#include "../../emotes/ui/EmoteAutocomplete.hpp"

#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/cocos/extensions/GUI/CCControlExtension/CCScale9Sprite.h>
#include <Geode/cocos/misc_nodes/CCRenderTexture.h>
#include <Geode/ui/BasedButtonSprite.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::feedback {

namespace {

// on-screen radius presets, scaled into screenshot pixel space per stroke.
float const kBrushScreenRadius[] = { 2.5f, 4.5f, 8.f };
char const* kBrushLabel[] = { "S", "M", "L" };
constexpr float kEraserScale = 1.8f;

ccColor4F const kPalette[] = {
    { 1.00f, 0.32f, 0.32f, 1.f },
    { 1.00f, 0.84f, 0.28f, 1.f },
    { 0.42f, 1.00f, 0.42f, 1.f },
    { 0.38f, 0.86f, 1.00f, 1.f },
    { 0.76f, 0.52f, 1.00f, 1.f },
    { 0.96f, 0.96f, 0.96f, 1.f },
    { 0.12f, 0.12f, 0.15f, 1.f },
};
constexpr int kPaletteCount = sizeof(kPalette) / sizeof(kPalette[0]);
constexpr size_t kMaxHistory = 40;

constexpr float kPad = 16.f;
constexpr float kWindowMargin = 28.f;
constexpr float kButtonRowY = 27.f;
constexpr float kInputH = 62.f;
constexpr float kToolbarH = 58.f;
constexpr float kTopMargin = 44.f;
constexpr float kChipSize = 26.f;
constexpr float kChipGap = 27.f;

ccColor3B toColor3B(ccColor4F const& c) {
    return {
        static_cast<GLubyte>(c.r * 255.f),
        static_cast<GLubyte>(c.g * 255.f),
        static_cast<GLubyte>(c.b * 255.f),
    };
}

std::string loc(char const* key) {
    return Localization::get().getString(key);
}

float spriteMaxDim(CCSprite* sprite) {
    if (!sprite) return 1.f;
    auto size = sprite->getContentSize();
    return std::max(1.f, std::max(size.width, size.height));
}

} // namespace

FeedbackPopup* FeedbackPopup::s_active = nullptr;

FeedbackPopup* FeedbackPopup::getActive() {
    return s_active;
}

void FeedbackPopup::open() {
    if (s_active) {
        s_active->onClose(nullptr);
        return;
    }

    auto shot = captureScreen();
    if (!shot.valid) {
        PaimonNotify::show(loc("feedback.capture_failed").c_str(), NotificationIcon::Error);
        return;
    }

    auto* ret = new FeedbackPopup();
    if (ret->init(std::move(shot))) {
        ret->autorelease();
        s_active = ret;
        ret->show();
    } else {
        delete ret;
    }
}

FeedbackPopup::~FeedbackPopup() {
    if (s_active == this) s_active = nullptr;
}

bool FeedbackPopup::init(FeedbackShot shot) {
    if (!shot.valid || !shot.texture) return false;

    auto const win = CCDirector::get()->getWinSize();
    float const width = std::clamp(win.width - kWindowMargin * 2.f, 560.f, 800.f);
    float const height = std::clamp(win.height - kWindowMargin * 2.f, 430.f, 620.f);
    if (!PaimonPopup::init(width, height)) return false;

    m_shot = std::move(shot);

    this->setTitle(loc("feedback.title").c_str());
    this->addInfoButton(loc("feedback.info.title"), loc("feedback.info.body"));
    this->addCorners();

    buildViewer();
    buildToolbar();
    buildTextInput();
    buildButtons();

    updateToolVisuals();
    updateColorVisuals();
    updateSizeVisuals();
    updateHistoryVisuals();

    return true;
}

float FeedbackPopup::panelWidth() const {
    return m_mainLayer->getContentSize().width - kPad * 2.f;
}

float FeedbackPopup::inputRowY() const {
    return kButtonRowY + 27.f;
}

float FeedbackPopup::toolbarRowY() const {
    return inputRowY() + kInputH + 6.f;
}

float FeedbackPopup::viewerRowY() const {
    return toolbarRowY() + kToolbarH + 4.f;
}

float FeedbackPopup::viewerRowHeight() const {
    return m_mainLayer->getContentSize().height - viewerRowY() - kTopMargin;
}

float FeedbackPopup::brushScale() const {
    return std::max(0.01f, m_shotScale * m_mainLayer->getScale());
}

void FeedbackPopup::buildViewer() {
    float const pW = panelWidth();
    float const vY = viewerRowY();
    float const vH = viewerRowHeight();
    CCPoint const center{ kPad + pW / 2.f, vY + vH / 2.f };

    auto* viewerBg = paimon::ui::makeInset({ pW, vH }, 150);
    viewerBg->setPosition({ kPad, vY });
    viewerBg->setID("feedback-viewer-bg"_spr);
    m_mainLayer->addChild(viewerBg, -1);

    m_shotSprite = CCSprite::createWithTexture(m_shot.texture.data());
    if (!m_shotSprite) return;
    m_shotSprite->setAnchorPoint({ 0.5f, 0.5f });
    m_shotSprite->setFlipY(false);

    CCSize const inner{ pW - 24.f, vH - 24.f };
    float const shotW = std::max(1.f, static_cast<float>(m_shot.width));
    float const shotH = std::max(1.f, static_cast<float>(m_shot.height));
    float const fit = std::min(inner.width / shotW, inner.height / shotH);
    float const drawW = shotW * fit;
    float const drawH = shotH * fit;

    m_shotScale = fit;
    m_shotOrigin = CCPoint{ center.x - drawW / 2.f, center.y - drawH / 2.f };

    // explicit design-space placement: no scaled container in between.
    auto const texSize = m_shotSprite->getContentSize();
    m_shotSprite->setScaleX(drawW / std::max(1.f, texSize.width));
    m_shotSprite->setScaleY(drawH / std::max(1.f, texSize.height));
    m_shotSprite->setPosition(center);
    m_mainLayer->addChild(m_shotSprite, 0);

    m_drawNode = PaimonDrawNode::create();
    if (!m_drawNode) return;
    m_drawNode->setAnchorPoint({ 0.f, 0.f });
    m_drawNode->setScale(fit);
    m_drawNode->setPosition(m_shotOrigin);
    m_mainLayer->addChild(m_drawNode, 2);

    // gd frame hugging the actual capture, not the letterboxed stage.
    if (auto* frame = extension::CCScale9Sprite::create("GJ_square07.png")) {
        frame->setContentSize({ drawW + 6.f, drawH + 6.f });
        frame->setPosition(center);
        frame->setID("feedback-shot-frame"_spr);
        m_mainLayer->addChild(frame, 1);
    }

    log::info("[Feedback] shot {}x{} texture {:.0f}x{:.0f} fit {:.3f}",
        m_shot.width, m_shot.height, texSize.width, texSize.height, fit);
}

void FeedbackPopup::buildToolbar() {
    auto const content = m_mainLayer->getContentSize();
    float const pW = panelWidth();
    float const row1 = toolbarRowY() + kToolbarH - 18.f;
    float const row2 = toolbarRowY() + 17.f;

    if (auto* panel = paimon::ui::makeInset({ pW, kToolbarH }, 100)) {
        panel->setPosition({ kPad, toolbarRowY() });
        panel->setID("feedback-toolbar-bg"_spr);
        m_mainLayer->addChild(panel, -1);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({ 0.f, 0.f });
    menu->setContentSize(content);
    menu->setID("feedback-tools"_spr);
    m_mainLayer->addChild(menu, 5);

    float const pencilX = kPad + 52.f;
    float const eraserX = pencilX + 104.f;
    float const undoX = eraserX + 72.f;
    float const redoX = undoX + 32.f;
    float const clearX = redoX + 32.f;

    m_pencilBtn = paimon::ui::makeButton(
        loc("feedback.tool_pencil").c_str(),
        [this]() { this->setTool(Tool::Pencil); },
        paimon::ui::Btn::Green, 92.f, 0.66f, "bigFont.fnt"
    );
    m_pencilBtn->setPosition({ pencilX, row1 });
    m_pencilBtn->setID("feedback-pencil-btn"_spr);
    menu->addChild(m_pencilBtn);

    m_eraserBtn = paimon::ui::makeButton(
        loc("feedback.tool_eraser").c_str(),
        [this]() { this->setTool(Tool::Eraser); },
        paimon::ui::Btn::Gray, 92.f, 0.66f, "bigFont.fnt"
    );
    m_eraserBtn->setPosition({ eraserX, row1 });
    m_eraserBtn->setID("feedback-eraser-btn"_spr);
    menu->addChild(m_eraserBtn);

    m_undoBtn = paimon::ui::makeCircleButton(
        "GJ_undoBtn_001.png",
        [this]() { this->undo(); },
        CircleBaseColor::Gray, CircleBaseSize::Small, 0.9f
    );
    m_undoBtn->setPosition({ undoX, row1 });
    m_undoBtn->setID("feedback-undo-btn"_spr);
    menu->addChild(m_undoBtn);

    m_redoBtn = paimon::ui::makeCircleButton(
        "GJ_redoBtn_001.png",
        [this]() { this->redo(); },
        CircleBaseColor::Gray, CircleBaseSize::Small, 0.9f
    );
    m_redoBtn->setPosition({ redoX, row1 });
    m_redoBtn->setID("feedback-redo-btn"_spr);
    menu->addChild(m_redoBtn);

    m_clearBtn = paimon::ui::makeCircleButton(
        "GJ_deleteIcon_001.png",
        [this]() { this->clearStrokes(); },
        CircleBaseColor::Red, CircleBaseSize::Small, 0.9f
    );
    m_clearBtn->setPosition({ clearX, row1 });
    m_clearBtn->setID("feedback-clear-btn"_spr);
    menu->addChild(m_clearBtn);

    if (auto* divider = paimon::ui::makeDivider(22.f, paimon::ui::palette::gold, 80)) {
        divider->setRotation(90.f);
        divider->setPosition({ eraserX + 54.f, row1 });
        m_mainLayer->addChild(divider, 6);
    }

    m_hintLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_hintLabel->setScale(0.34f);
    m_hintLabel->setAnchorPoint({ 1.f, 0.5f });
    m_hintLabel->setPosition({ kPad + pW - 10.f, row1 });
    m_hintLabel->setColor(paimon::ui::palette::muted);
    m_mainLayer->addChild(m_hintLabel, 6);

    // second row: palette + brush size + live brush preview.
    auto* colorLabel = paimon::ui::makeLabel(
        loc("feedback.color").c_str(), 44.f, 0.3f, paimon::ui::palette::muted
    );
    colorLabel->setAnchorPoint({ 0.f, 0.5f });
    colorLabel->setPosition({ kPad + 10.f, row2 });
    m_mainLayer->addChild(colorLabel, 6);

    float const chipStart = kPad + 54.f;
    for (int i = 0; i < kPaletteCount; ++i) {
        auto* holder = CCNode::create();
        holder->setContentSize({ kChipSize, kChipSize });

        auto* ring = CCSprite::createWithSpriteFrameName("GJ_colorBtn_001.png");
        if (!ring) break;
        float const ringScale = kChipSize / spriteMaxDim(ring);
        ring->setScale(ringScale);
        ring->setPosition({ kChipSize / 2.f, kChipSize / 2.f });
        holder->addChild(ring);

        auto* chip = CCSprite::createWithSpriteFrameName("GJ_colorBtn_001.png");
        if (!chip) break;
        chip->setColor(toColor3B(kPalette[i]));
        float const chipScale = (kChipSize - 6.f) / spriteMaxDim(chip);
        chip->setScale(chipScale);
        chip->setPosition({ kChipSize / 2.f, kChipSize / 2.f });
        holder->addChild(chip);

        auto* btn = CCMenuItemExt::createSpriteExtra(
            holder,
            [this, i](CCMenuItemSpriteExtra*) {
                m_colorIndex = i;
                updateColorVisuals();
            }
        );
        btn->setPosition({ chipStart + i * kChipGap, row2 });
        btn->setID("feedback-color-" + std::to_string(i) + "-btn");
        menu->addChild(btn);

        m_colorBtns.push_back(btn);
        m_colorRings.push_back(ring);
        m_colorChips.push_back(chip);
    }

    float const chipEnd = chipStart + (kPaletteCount - 1) * kChipGap + kChipSize;
    if (auto* divider = paimon::ui::makeDivider(22.f, paimon::ui::palette::gold, 80)) {
        divider->setRotation(90.f);
        divider->setPosition({ chipEnd + 9.f, row2 });
        m_mainLayer->addChild(divider, 6);
    }
    float const sizeLabelX = chipEnd + 28.f;
    float const sizeStart = sizeLabelX + 50.f;

    auto* sizeLabel = paimon::ui::makeLabel(
        loc("feedback.brush").c_str(), 52.f, 0.3f, paimon::ui::palette::muted
    );
    sizeLabel->setAnchorPoint({ 0.f, 0.5f });
    sizeLabel->setPosition({ sizeLabelX, row2 });
    m_mainLayer->addChild(sizeLabel, 6);

    for (int i = 0; i < 3; ++i) {
        auto* btn = paimon::ui::makeButton(
            kBrushLabel[i],
            [this, i]() {
                m_sizeIndex = i;
                updateSizeVisuals();
            },
            paimon::ui::Btn::Gray, 34.f, 0.62f, "bigFont.fnt"
        );
        btn->setPosition({ sizeStart + i * 38.f, row2 });
        btn->setID("feedback-size-" + std::to_string(i) + "-btn");
        menu->addChild(btn);
        m_sizeBtns.push_back(btn);
    }

    m_brushPreview = PaimonDrawNode::create();
    if (m_brushPreview) {
        m_brushPreview->setPosition({ sizeStart + 3 * 38.f + 18.f, row2 });
        m_brushPreview->setID("feedback-brush-preview"_spr);
        m_mainLayer->addChild(m_brushPreview, 6);
    }

    if (pW >= 640.f) {
        std::string const dims = std::to_string(m_shot.width) + " x " + std::to_string(m_shot.height);
        auto* resolution = paimon::ui::makeLabel(
            dims.c_str(), 110.f, 0.24f, paimon::ui::palette::dim
        );
        resolution->setAnchorPoint({ 1.f, 0.5f });
        resolution->setPosition({ kPad + pW - 10.f, row2 });
        resolution->setID("feedback-resolution-label"_spr);
        m_mainLayer->addChild(resolution, 6);
    }
}

void FeedbackPopup::buildTextInput() {
    float const pW = panelWidth();
    float const rowY = inputRowY();

    auto* panel = paimon::ui::makePanel(
        { pW, kInputH }, loc("feedback.question").c_str(), 100
    );
    panel->setPosition({ kPad, rowY });
    panel->setID("feedback-composer"_spr);
    m_mainLayer->addChild(panel, 2);

    float const inputW = pW - 64.f;
    float const inputCx = 12.f + inputW / 2.f;

    m_textInput = geode::TextInput::create(
        inputW, loc("feedback.input_placeholder").c_str(), "chatFont.fnt"
    );
    m_textInput->setCommonFilter(geode::CommonFilter::TextEmojis);
    m_textInput->setMaxCharCount(240);
    m_textInput->setPosition({ inputCx, 19.f });
    panel->addChild(m_textInput, 2);

    {
        paimon::emotes::EmoteInputContext ctx;
        ctx.getText = [this]() -> std::string {
            return m_textInput ? std::string(m_textInput->getString()) : std::string();
        };
        ctx.setText = [this](std::string const& text) {
            if (m_textInput) m_textInput->setString(text);
        };
        ctx.charLimit = 240;
        ctx.centerPicker = true;
        auto* emoteBtn = paimon::emotes::EmoteButton::create(std::move(ctx));
        auto* emoteMenu = CCMenu::create();
        emoteMenu->setID("feedback-emote-menu"_spr);
        emoteMenu->setPosition({ 0.f, 0.f });
        emoteBtn->setScale(0.45f);
        emoteBtn->setPosition({ pW - 26.f, 19.f });
        emoteMenu->addChild(emoteBtn);
        panel->addChild(emoteMenu, 5);
    }

    {
        auto* ac = paimon::emotes::EmoteAutocomplete::create(
            m_textInput,
            [this](std::string const& newText) {
                if (m_textInput) m_textInput->setString(newText);
            }
        );
        ac->setPosition({ kPad + 26.f, rowY + 38.f });
        m_mainLayer->addChild(ac, 100);
    }
}

void FeedbackPopup::buildButtons() {
    auto const content = m_mainLayer->getContentSize();
    float const right = content.width - kPad;

    auto* submit = paimon::ui::makeButton(
        loc("feedback.submit").c_str(),
        [this]() { this->onSubmit(nullptr); },
        paimon::ui::Btn::Green, 104.f, 0.72f
    );
    submit->setPosition({ right - 52.f, kButtonRowY });
    submit->setID("feedback-submit-btn"_spr);
    m_buttonMenu->addChild(submit);

    auto* cancel = paimon::ui::makeButton(
        loc("feedback.cancel").c_str(),
        [this]() { this->onCancel(nullptr); },
        paimon::ui::Btn::Red, 104.f, 0.72f
    );
    cancel->setPosition({ right - 168.f, kButtonRowY });
    cancel->setID("feedback-cancel-btn"_spr);
    m_buttonMenu->addChild(cancel);

    paimon::ui::matchButtonLabels({ submit, cancel });

    auto* note = paimon::ui::makeLabel(
        loc("feedback.offline_note").c_str(),
        std::max(60.f, right - 224.f - kPad),
        0.3f, paimon::ui::palette::dim
    );
    note->setAnchorPoint({ 0.f, 0.5f });
    note->setPosition({ kPad, kButtonRowY });
    note->setID("feedback-offline-note"_spr);
    m_mainLayer->addChild(note, 4);
}

void FeedbackPopup::drawStrokes(PaimonDrawNode* node) const {
    if (!node) return;
    node->clear();
    for (auto const& stroke : m_strokes) {
        if (stroke.points.empty()) continue;
        if (stroke.points.size() == 1) {
            node->drawSolidCircle(stroke.points.front(), stroke.radius, stroke.color, 28);
            continue;
        }
        for (size_t i = 1; i < stroke.points.size(); ++i) {
            // capsules give round caps and joins, so fast strokes stay smooth.
            node->drawCapsuleSegment(
                stroke.points[i - 1], stroke.points[i],
                stroke.radius * 2.f, stroke.color, 16
            );
        }
    }
}

void FeedbackPopup::redrawAnnotations() {
    drawStrokes(m_drawNode);
}

void FeedbackPopup::setTool(Tool tool) {
    if (m_tool == tool) return;
    if (m_drawing) {
        m_drawing = false;
        m_activeTouch = nullptr;
        commitTouchHistory();
    }
    m_tool = tool;
    updateToolVisuals();
}

void FeedbackPopup::updateToolVisuals() {
    paimon::ui::setButtonSkin(
        m_pencilBtn, m_tool == Tool::Pencil ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray
    );
    paimon::ui::setButtonSkin(
        m_eraserBtn, m_tool == Tool::Eraser ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray
    );
    updateHint();
    updateBrushPreview();
}

void FeedbackPopup::updateColorVisuals() {
    for (size_t i = 0; i < m_colorBtns.size(); ++i) {
        bool const selected = static_cast<int>(i) == m_colorIndex;
        if (i < m_colorRings.size() && m_colorRings[i]) {
            m_colorRings[i]->setColor(selected
                ? paimon::ui::palette::gold
                : ccColor3B{ 48, 48, 58 });
            m_colorRings[i]->setOpacity(selected ? 255 : 190);
        }
        if (i < m_colorChips.size() && m_colorChips[i]) {
            m_colorChips[i]->setOpacity(selected ? 255 : 215);
        }
        m_colorBtns[i]->setScale(selected ? 1.1f : 0.94f);
    }
    updateBrushPreview();
}

void FeedbackPopup::updateSizeVisuals() {
    for (size_t i = 0; i < m_sizeBtns.size(); ++i) {
        bool const selected = static_cast<int>(i) == m_sizeIndex;
        paimon::ui::setButtonSkin(
            m_sizeBtns[i],
            selected ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray
        );
    }
    updateBrushPreview();
}

void FeedbackPopup::updateHistoryVisuals() {
    auto styleButton = [](CCMenuItemSpriteExtra* button, bool enabled) {
        if (!button) return;
        button->setEnabled(enabled);
        if (auto* image = button->getNormalImage()) {
            if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(image)) {
                rgba->setOpacity(enabled ? 255 : 90);
            }
        }
    };
    styleButton(m_undoBtn, !m_undoStack.empty());
    styleButton(m_redoBtn, !m_redoStack.empty());
    styleButton(m_clearBtn, !m_strokes.empty());
}

void FeedbackPopup::updateHint() {
    if (!m_hintLabel) return;
    std::string const text = loc(
        m_tool == Tool::Pencil ? "feedback.draw_hint" : "feedback.erase_hint"
    );
    m_hintLabel->setString(text.c_str());

    float const left = m_clearBtn
        ? m_clearBtn->getPositionX() + 26.f
        : kPad + 300.f;
    float const right = kPad + panelWidth() - 10.f;
    m_hintLabel->limitLabelWidth(std::max(60.f, right - left), 0.34f, 0.16f);
}

void FeedbackPopup::updateBrushPreview() {
    if (!m_brushPreview) return;
    m_brushPreview->clear();

    float radius = kBrushScreenRadius[m_sizeIndex];
    ccColor4F color = kPalette[m_colorIndex % kPaletteCount];
    if (m_tool == Tool::Eraser) {
        radius *= kEraserScale;
        color = { 1.f, 0.45f, 0.45f, 1.f };
    }
    radius = std::clamp(radius, 2.2f, 9.f);

    m_brushPreview->drawSolidCircle({ 0.f, 0.f }, radius + 1.6f,
        { 1.f, 1.f, 1.f, 0.5f }, 28);
    m_brushPreview->drawSolidCircle({ 0.f, 0.f }, radius + 0.8f,
        { 0.f, 0.f, 0.f, 0.5f }, 28);
    m_brushPreview->drawSolidCircle({ 0.f, 0.f }, radius, color, 28);
}

void FeedbackPopup::beginTouchHistory() {
    m_touchSnapshot = m_strokes;
    m_touchChanged = false;
}

void FeedbackPopup::commitTouchHistory() {
    if (m_touchChanged) {
        pushUndo(std::move(m_touchSnapshot));
    } else {
        m_touchSnapshot.clear();
    }
    m_touchChanged = false;
    updateHistoryVisuals();
}

void FeedbackPopup::pushUndo(std::vector<FeedbackStroke> snapshot) {
    m_undoStack.push_back(std::move(snapshot));
    if (m_undoStack.size() > kMaxHistory) m_undoStack.erase(m_undoStack.begin());
    m_redoStack.clear();
    updateHistoryVisuals();
}

void FeedbackPopup::undo() {
    if (m_undoStack.empty()) return;
    m_redoStack.push_back(m_strokes);
    if (m_redoStack.size() > kMaxHistory) m_redoStack.erase(m_redoStack.begin());
    m_strokes = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    redrawAnnotations();
    updateHistoryVisuals();
}

void FeedbackPopup::redo() {
    if (m_redoStack.empty()) return;
    m_undoStack.push_back(m_strokes);
    if (m_undoStack.size() > kMaxHistory) m_undoStack.erase(m_undoStack.begin());
    m_strokes = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    redrawAnnotations();
    updateHistoryVisuals();
}

void FeedbackPopup::clearStrokes() {
    if (m_strokes.empty()) return;
    pushUndo(m_strokes);
    m_strokes.clear();
    redrawAnnotations();
    updateHistoryVisuals();
}

bool FeedbackPopup::eraseAt(CCPoint const& center, float radius) {
    float const r2 = radius * radius;
    bool changed = false;
    std::vector<FeedbackStroke> next;
    next.reserve(m_strokes.size() + 4);

    for (auto const& stroke : m_strokes) {
        bool hit = false;
        for (auto const& point : stroke.points) {
            float const dx = point.x - center.x;
            float const dy = point.y - center.y;
            if (dx * dx + dy * dy <= r2) {
                hit = true;
                break;
            }
        }
        if (!hit) {
            next.push_back(stroke);
            continue;
        }

        changed = true;
        FeedbackStroke piece = stroke;
        piece.points.clear();
        for (auto const& point : stroke.points) {
            float const dx = point.x - center.x;
            float const dy = point.y - center.y;
            if (dx * dx + dy * dy <= r2) {
                if (!piece.points.empty()) {
                    next.push_back(piece);
                    piece.points.clear();
                }
            } else {
                piece.points.push_back(point);
            }
        }
        if (!piece.points.empty()) next.push_back(std::move(piece));
    }

    if (changed) m_strokes = std::move(next);
    return changed;
}

bool FeedbackPopup::eraseAlong(CCPoint const& from, CCPoint const& to, float radius) {
    float const dx = to.x - from.x;
    float const dy = to.y - from.y;
    float const distance = std::sqrt(dx * dx + dy * dy);
    int steps = static_cast<int>(std::ceil(distance / std::max(1.f, radius * 0.5f)));
    steps = std::clamp(steps, 1, 64);

    bool changed = false;
    for (int i = 1; i <= steps; ++i) {
        float const t = static_cast<float>(i) / static_cast<float>(steps);
        CCPoint const point{ from.x + dx * t, from.y + dy * t };
        if (eraseAt(point, radius)) changed = true;
    }
    return changed;
}

void FeedbackPopup::registerWithTouchDispatcher() {
    // same urgency pattern as capturepreviewpopup: pan/draw above, buttons intact.
    auto* dispatcher = CCDirector::get()->getTouchDispatcher();
    dispatcher->addTargetedDelegate(this, dispatcher->getTargetPrio() - 1, true);
}

CCPoint FeedbackPopup::clampToViewer(CCPoint const& pixel) const {
    return {
        std::clamp(pixel.x, 0.f, static_cast<float>(m_shot.width)),
        std::clamp(pixel.y, 0.f, static_cast<float>(m_shot.height)),
    };
}

bool FeedbackPopup::touchOnViewer(CCTouch* touch, CCPoint& outPixel, bool clampInside) const {
    if (!m_shotSprite || !touch || m_shotScale <= 0.f) return false;
    CCPoint const local = m_mainLayer->convertToNodeSpace(touch->getLocation());
    CCPoint const pixel{
        (local.x - m_shotOrigin.x) / m_shotScale,
        (local.y - m_shotOrigin.y) / m_shotScale,
    };
    bool const inside = pixel.x >= 0.f && pixel.y >= 0.f
        && pixel.x <= static_cast<float>(m_shot.width)
        && pixel.y <= static_cast<float>(m_shot.height);
    if (inside) {
        outPixel = pixel;
        return true;
    }
    if (clampInside) {
        outPixel = clampToViewer(pixel);
        return true;
    }
    return false;
}

bool FeedbackPopup::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (m_drawing || !touch) return true;

    CCPoint pixel;
    if (!touchOnViewer(touch, pixel)) return true;

    m_drawing = true;
    m_activeTouch = touch;
    beginTouchHistory();

    if (m_tool == Tool::Pencil) {
        FeedbackStroke stroke;
        stroke.color = kPalette[m_colorIndex % kPaletteCount];
        stroke.radius = kBrushScreenRadius[m_sizeIndex] / brushScale();
        stroke.points.push_back(pixel);
        m_strokes.push_back(std::move(stroke));
        m_lastStrokePoint = pixel;
        m_touchChanged = true;
        redrawAnnotations();
    } else {
        float const radius = kBrushScreenRadius[m_sizeIndex] * kEraserScale / brushScale();
        m_lastErasePoint = pixel;
        if (eraseAt(pixel, radius)) {
            m_touchChanged = true;
            redrawAnnotations();
        }
    }
    return true;
}

void FeedbackPopup::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (!m_drawing || !touch || touch != m_activeTouch) return;

    CCPoint pixel;
    if (!touchOnViewer(touch, pixel, true)) return;

    if (m_tool == Tool::Pencil) {
        if (m_strokes.empty()) return;
        float const minDist = 2.f / brushScale();
        float const dx = pixel.x - m_lastStrokePoint.x;
        float const dy = pixel.y - m_lastStrokePoint.y;
        if (dx * dx + dy * dy < minDist * minDist) return;

        m_strokes.back().points.push_back(pixel);
        m_lastStrokePoint = pixel;
        redrawAnnotations();
    } else {
        float const radius = kBrushScreenRadius[m_sizeIndex] * kEraserScale / brushScale();
        if (eraseAlong(m_lastErasePoint, pixel, radius)) {
            m_touchChanged = true;
            redrawAnnotations();
        }
        m_lastErasePoint = pixel;
    }
}

void FeedbackPopup::ccTouchEnded(CCTouch* touch, CCEvent*) {
    if (!m_drawing) return;
    if (touch && touch != m_activeTouch) return;

    if (m_tool == Tool::Pencil && !m_strokes.empty() && touch) {
        CCPoint pixel;
        if (touchOnViewer(touch, pixel, true)) {
            float const minDist = 2.f / brushScale();
            float const dx = pixel.x - m_lastStrokePoint.x;
            float const dy = pixel.y - m_lastStrokePoint.y;
            if (dx * dx + dy * dy >= minDist * minDist) {
                m_strokes.back().points.push_back(pixel);
                redrawAnnotations();
            }
        }
    } else if (m_tool == Tool::Eraser && touch) {
        CCPoint pixel;
        if (touchOnViewer(touch, pixel, true)) {
            float const radius = kBrushScreenRadius[m_sizeIndex] * kEraserScale / brushScale();
            if (eraseAlong(m_lastErasePoint, pixel, radius)) {
                m_touchChanged = true;
                redrawAnnotations();
            }
        }
    }

    m_drawing = false;
    m_activeTouch = nullptr;
    commitTouchHistory();
}

void FeedbackPopup::ccTouchCancelled(CCTouch* touch, CCEvent*) {
    if (!m_drawing) return;
    if (touch && touch != m_activeTouch) return;
    m_drawing = false;
    m_activeTouch = nullptr;
    commitTouchHistory();
}

std::vector<uint8_t> FeedbackPopup::renderAnnotatedPng() const {
    if (!m_shot.texture || m_shot.width <= 0 || m_shot.height <= 0) return {};

    auto* container = CCNode::create();
    container->setContentSize({
        static_cast<float>(m_shot.width), static_cast<float>(m_shot.height)
    });

    auto* sprite = CCSprite::createWithTexture(m_shot.texture.data());
    if (!sprite) return {};
    sprite->setAnchorPoint({ 0.f, 0.f });
    sprite->setPosition({ 0.f, 0.f });
    sprite->setFlipY(false);
    container->addChild(sprite, 0);

    auto* exportDraw = PaimonDrawNode::create();
    if (!exportDraw) return {};
    drawStrokes(exportDraw);
    container->addChild(exportDraw, 1);

    auto* rt = CCRenderTexture::create(
        m_shot.width, m_shot.height,
        kCCTexture2DPixelFormat_RGBA8888, 0
    );
    if (!rt) return {};
    rt->beginWithClear(0.f, 0.f, 0.f, 1.f);
    container->visit();
    rt->end();

    CCImage* img = rt->newCCImage(true);
    if (!img) return {};
    std::vector<uint8_t> out;
    bool const ok = ImageConverter::rgbaToPngBuffer(
        img->getData(), m_shot.width, m_shot.height, out);
    img->release();
    if (!ok) return {};
    return out;
}

void FeedbackPopup::onSubmit(CCObject*) {
    if (m_submitting) return;

    std::string message = m_textInput ? std::string(m_textInput->getString()) : std::string();
    if (message.empty()) {
        PaimonNotify::show(loc("feedback.need_text").c_str(), NotificationIcon::Warning);
        return;
    }

    m_submitting = true;
    if (m_textInput) paimon::ui::detachGeodeTextInput(m_textInput);
    PaimonNotify::show(loc("feedback.sending").c_str(), NotificationIcon::Loading);

    auto png = renderAnnotatedPng();
    bool const hasImage = !png.empty();
    if (!hasImage && !m_strokes.empty()) {
        log::warn("[Feedback] annotated export failed; sending text only");
    }

    WeakRef<FeedbackPopup> self = this;
    submit(message, png, hasImage,
        collectCurrentMeta(),
        [self](bool ok, std::string const&) {
            Loader::get()->queueInMainThread([self, ok]() {
                if (paimon::isRuntimeShuttingDown()) return;
                auto popup = self.lock();
                if (!popup) return;
                if (ok) {
                    PaimonNotify::show(
                        Localization::get().getString("feedback.sent").c_str(),
                        NotificationIcon::Success);
                } else {
                    PaimonNotify::show(
                        Localization::get().getString("feedback.saved_local").c_str(),
                        NotificationIcon::Warning);
                }
                popup->onClose(nullptr);
            });
        }
    );
}

void FeedbackPopup::onCancel(CCObject*) {
    this->onClose(nullptr);
}

void FeedbackPopup::onClose(CCObject* sender) {
    if (m_textInput) paimon::ui::detachGeodeTextInput(m_textInput);
    if (s_active == this) s_active = nullptr;
    Popup::onClose(sender);
}

} // namespace paimon::feedback
