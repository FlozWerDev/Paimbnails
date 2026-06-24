#include "ColorPickerOverlay.hpp"
#include "../services/ColorFormat.hpp"

#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/OverlayManager.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <Geode/binding/GJEffectManager.hpp>
#include <Geode/binding/ColorAction.hpp>
#include <Geode/binding/ColorSelectPopup.hpp>
#include <Geode/utils/general.hpp>
#include <Geode/cocos/extensions/GUI/CCControlExtension/CCScale9Sprite.h>

#include <algorithm>
#include <cmath>

using namespace cocos2d;
using namespace geode::prelude;

#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif

namespace paimon::editorcp {

namespace {
    // All sizes are in Geometry Dash *points* (winSize is roughly 569x320).
    constexpr float kHudW         = 420.f;
    constexpr float kHudH         = 66.f;
    constexpr int   kPickerZOrder = 999500;

    // Touch priorities (lower value = handled earlier).
    //   text input (default -500) -> HUD menu (-300) -> picker (-200) -> editor
    constexpr int kMenuPriority = -300;
    constexpr int kPickPriority = -200;
}

ColorPickerOverlay* ColorPickerOverlay::s_instance = nullptr;

void ColorPickerOverlay::show() {
    if (s_instance) { s_instance->doClose(); return; }

    auto* ov = ColorPickerOverlay::create();
    if (!ov) return;
    ov->setID("paimbnails/editor-color-picker-overlay");

    if (auto* host = geode::OverlayManager::get()) {
        host->addChild(ov, kPickerZOrder);
    } else if (auto* scene = CCDirector::get()->getRunningScene()) {
        scene->addChild(ov, 99999);
    }
}

void ColorPickerOverlay::hideOverlay() {
    if (s_instance) s_instance->doClose();
}

bool ColorPickerOverlay::init() {
    if (!CCLayer::init()) return false;
    s_instance = this;

    m_formatIndex = clampFormatIndex(
        static_cast<int>(Mod::get()->getSavedValue<int64_t>("editor-cp-format", 0)));
    m_autoApply = Mod::get()->getSavedValue<bool>("editor-cp-auto-apply", false);

    this->setTouchEnabled(true);
    this->setKeypadEnabled(true);

    // No snapshot: the layer is transparent, so the editor keeps rendering
    // underneath and the picked colors stay live. The framebuffer is sampled
    // each frame in onPreSwapSample().
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
    // show() runs inside a touch event (the button click), so the touch
    // dispatcher is LOCKED: the menu's freshly-registered handler is still in
    // the pending-add queue and not findable yet. Calling setHandlerPriority
    // now dereferences a null CCTouchHandler. Defer it to the next main-thread
    // tick, by which point the dispatcher has committed the pending handlers.
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
    // Picking runs AFTER the HUD menu/input but BEFORE the editor, so the
    // overlay is modal (clicks outside the HUD pick a color, never reach the
    // editor underneath).
    CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, kPickPriority, true);
}

void ColorPickerOverlay::keyBackClicked() {
    this->doClose();
}

void ColorPickerOverlay::buildUI() {
    auto win = CCDirector::get()->getWinSize();

    m_hud = CCNode::create();
    m_hud->setContentSize({kHudW, kHudH});
    m_hud->setAnchorPoint({0.5f, 0.f});
    m_hud->ignoreAnchorPointForPosition(false);
    m_hud->setPosition({win.width / 2.f, 8.f});
    this->addChild(m_hud, 20);

    // GD's vanilla brown panel (no tint, full opacity — matches in-game UI)
    {
        auto* panel = cocos2d::extension::CCScale9Sprite::create("GJ_square01.png");
        if (panel) {
            panel->setContentSize({kHudW, kHudH});
            panel->setAnchorPoint({0.f, 0.f});
            m_hud->addChild(panel, 0);
        }
    }

    auto* hint = CCLabelBMFont::create(
        "Live - click to pick the color under the cursor  -  Esc to cancel",
        "bigFont.fnt");
    hint->setScale(0.34f);
    hint->setAnchorPoint({0.5f, 0.f});
    hint->setPosition({win.width / 2.f, 8.f + kHudH + 6.f});
    hint->setOpacity(210);
    this->addChild(hint, 21);

    // Decorative GD square button as the swatch frame (a real game asset, not
    // a draw-node frame). It has no function; it just holds the color preview.
    // The color preview itself is a recolorable fill clipped to rounded corners
    // and sits inside the button. Everything lives in a centered container so
    // the pick "pop" animation scales the whole box from its center.
    const float boxSize  = 34.f;  // decorative square game button
    const float fillSize = 29.f;  // rounded color preview fills the button's inside
    const float sx = 12.f, sy = 18.f;
    const float fillOff = (boxSize - fillSize) / 2.f;

    auto* swatchBox = CCNode::create();
    swatchBox->setContentSize({boxSize, boxSize});
    swatchBox->setAnchorPoint({0.5f, 0.5f});
    swatchBox->ignoreAnchorPointForPosition(false);
    swatchBox->setPosition({sx + boxSize / 2.f, sy + boxSize / 2.f});
    m_hud->addChild(swatchBox, 1);
    m_swatchBox = swatchBox;

    {
        cocos2d::extension::CCScale9Sprite* frame =
            paimon::SpriteHelper::safeCreateScale9("GJ_button_04.png");
        if (!frame) frame = paimon::SpriteHelper::safeCreateScale9("square02_001.png");
        if (frame) {
            frame->setContentSize({boxSize, boxSize});
            frame->setAnchorPoint({0.f, 0.f});
            frame->setPosition({0.f, 0.f});
            swatchBox->addChild(frame, 0);
        }
    }

    {
        auto* clip = CCClippingNode::create();
        clip->setContentSize({fillSize, fillSize});
        clip->setAnchorPoint({0.f, 0.f});
        clip->setPosition({fillOff, fillOff});
        if (auto* stencil = paimon::SpriteHelper::createRoundedRectStencil(fillSize, fillSize, 5.f))
            clip->setStencil(stencil);
        clip->setAlphaThreshold(0.5f);

        m_selSwatch = CCLayerColor::create({255, 255, 255, 255}, fillSize, fillSize);
        clip->addChild(m_selSwatch);
        swatchBox->addChild(clip, 1);
    }

    m_swatchCaption = CCLabelBMFont::create("LIVE", "bigFont.fnt");
    m_swatchCaption->setScale(0.26f);
    m_swatchCaption->setAnchorPoint({0.5f, 0.5f});
    m_swatchCaption->setPosition({sx + boxSize / 2.f, sy + boxSize + 6.f});
    m_hud->addChild(m_swatchCaption, 2);

    m_valueLabel = CCLabelBMFont::create("#FFFFFF", "bigFont.fnt");
    m_valueLabel->setScale(0.38f);
    m_valueLabel->setAnchorPoint({0.f, 0.5f});
    m_valueLabel->setPosition({56.f, 46.f});
    m_hud->addChild(m_valueLabel, 2);

    m_formatLabel = CCLabelBMFont::create(formatName(m_formatIndex), "bigFont.fnt");
    m_formatLabel->setScale(0.34f);
    m_formatLabel->setAnchorPoint({0.5f, 0.5f});
    m_formatLabel->setPosition({95.f, 18.f});
    m_hud->addChild(m_formatLabel, 2);

    auto* idCaption = CCLabelBMFont::create("COLOR ID", "bigFont.fnt");
    idCaption->setScale(0.22f);
    idCaption->setAnchorPoint({0.5f, 0.5f});
    idCaption->setPosition({230.f, 58.f});
    m_hud->addChild(idCaption, 2);

    m_idInput = geode::TextInput::create(56.f, "0-999");
    m_idInput->setFilter("0123456789");
    m_idInput->setMaxCharCount(4);
    m_idInput->setScale(0.85f);
    m_idInput->setPosition({230.f, 38.f});
    m_hud->addChild(m_idInput, 4);

    auto savedID = Mod::get()->getSavedValue<std::string>("editor-cp-channel-id", "");
    if (!savedID.empty()) m_idInput->setString(savedID);

    m_controlsMenu = CCMenu::create();
    m_controlsMenu->setPosition({0.f, 0.f});
    m_controlsMenu->setContentSize({kHudW, kHudH});
    m_hud->addChild(m_controlsMenu, 5);

    auto makeArrow = [&](const char* text, SEL_MenuHandler sel, float x, float y) {
        auto* spr = ButtonSprite::create(text, "bigFont.fnt", "GJ_button_04.png", 0.7f);
        spr->setScale(0.42f);
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, sel);
        btn->setPosition({x, y});
        m_controlsMenu->addChild(btn, 1);
    };
    makeArrow("<", menu_selector(ColorPickerOverlay::onPrevFormat), 62.f, 18.f);
    makeArrow(">", menu_selector(ColorPickerOverlay::onNextFormat), 128.f, 18.f);
    makeArrow("<", menu_selector(ColorPickerOverlay::onPrevColorID), 198.f, 38.f);
    makeArrow(">", menu_selector(ColorPickerOverlay::onNextColorID), 262.f, 38.f);

    auto makeButton = [&](const char* text, const char* font, const char* bg,
                          SEL_MenuHandler sel, float x, float y, float scale) {
        auto* spr = ButtonSprite::create(text, font, bg, 0.8f);
        spr->setScale(scale);
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, sel);
        btn->setPosition({x, y});
        m_controlsMenu->addChild(btn, 1);
    };
    makeButton("Copy", "bigFont.fnt", "GJ_button_05.png",
               menu_selector(ColorPickerOverlay::onCopy), 312.f, 46.f, 0.5f);
    makeButton("Save", "goldFont.fnt", "GJ_button_01.png",
               menu_selector(ColorPickerOverlay::onSave), 362.f, 46.f, 0.5f);

    {
        auto* toggle = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(ColorPickerOverlay::onToggleAuto), 0.5f);
        toggle->toggle(m_autoApply);
        toggle->setPosition({362.f, 18.f});
        m_controlsMenu->addChild(toggle, 1);

        auto* autoLabel = CCLabelBMFont::create("Auto", "bigFont.fnt");
        autoLabel->setScale(0.24f);
        autoLabel->setAnchorPoint({0.5f, 0.5f});
        autoLabel->setPosition({395.f, 18.f});
        m_hud->addChild(autoLabel, 5);
    }

    {
        auto* spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_closeBtn_001.png");
        CCMenuItemSpriteExtra* btn = nullptr;
        if (spr) {
            spr->setScale(0.4f);
            btn = CCMenuItemSpriteExtra::create(
                spr, this, menu_selector(ColorPickerOverlay::onCancel));
        } else {
            auto* fb = ButtonSprite::create("X", "bigFont.fnt", "GJ_button_06.png", 0.7f);
            fb->setScale(0.5f);
            btn = CCMenuItemSpriteExtra::create(
                fb, this, menu_selector(ColorPickerOverlay::onCancel));
        }
        if (btn) {
            btn->setPosition({312.f, 18.f});
            m_controlsMenu->addChild(btn, 1);
        }
    }

    // Touch priority for the HUD menu is set in onEnter(): the menu only owns a
    // registered touch handler once it (and the overlay) are in the scene.

    this->updateReadout();
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

    // glReadPixels shares the cocos origin (bottom-left), so no Y flip on input.
    const int cxDev = std::clamp(static_cast<int>(std::lround(m.x * scaleX)), 0, fw - 1);
    const int cyDev = std::clamp(static_cast<int>(std::lround(m.y * scaleY)), 0, fh - 1);

    m_pixelBuf.resize(4);

    while (glGetError() != GL_NO_ERROR) {} // drain stale errors

    GLint origFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &origFBO);
    if (origFBO != 0) glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(cxDev, cyDev, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, m_pixelBuf.data());
    glPixelStorei(GL_PACK_ALIGNMENT, 4);

    if (origFBO != 0) glBindFramebuffer(GL_FRAMEBUFFER, origFBO);

    if (glGetError() != GL_NO_ERROR) return;

    m_liveColor = { m_pixelBuf[0], m_pixelBuf[1], m_pixelBuf[2] };
}

void ColorPickerOverlay::updateReadout() {
    // Keep tracking the LIVE color under the cursor even after a pick, so the
    // user can keep previewing the colors they hover over. The locked selection
    // is only shown while the cursor is over the HUD (i.e. on the way to press
    // Copy/Save), so those actions still act on exactly what is displayed.
    const CCPoint mouse   = geode::cocos::getMousePos();
    const bool    overHud = m_hasSelection && this->pointInHud(mouse);
    const ccColor3B c     = overHud ? m_selColor : m_liveColor;

    if (m_selSwatch) m_selSwatch->setColor(c);
    if (m_valueLabel) m_valueLabel->setString(formatColor(c, m_formatIndex).c_str());
    if (m_formatLabel) m_formatLabel->setString(formatName(m_formatIndex));
    if (m_swatchCaption) {
        m_swatchCaption->setString(overHud ? "PICKED" : "LIVE");
        // Green for live, gold for picked — visual cue for the state.
        m_swatchCaption->setColor(overHud ? ccColor3B{255, 220, 65} : ccColor3B{100, 230, 100});
    }
}

void ColorPickerOverlay::pickAt(CCPoint /*p*/) {
    // Live mode: the color under the cursor is sampled every frame from the
    // framebuffer (liveSample). Committing a pick just locks in that color.
    if (!m_ready) return;
    m_selColor = m_liveColor;
    m_hasSelection = true;
    updateReadout();
    if (auto* pop = m_swatchBox ? m_swatchBox : static_cast<CCNode*>(m_selSwatch)) {
        pop->stopAllActions();
        pop->setScale(1.12f);
        pop->runAction(CCEaseBackOut::create(CCScaleTo::create(0.18f, 1.f)));
    }
    // Auto mode: every pick (click or drag) is applied to the channel live, so
    // the editor updates without pressing Save.
    if (m_autoApply) this->tryAutoApply();
}

std::string ColorPickerOverlay::currentValueString() const {
    ccColor3B c = m_hasSelection ? m_selColor : m_liveColor;
    return formatColor(c, m_formatIndex);
}

bool ColorPickerOverlay::pointInHud(CCPoint p) const {
    if (!m_hud) return false;
    auto pos = m_hud->getPosition(); // anchor (0.5, 0)
    CCRect rect{ pos.x - kHudW / 2.f, pos.y, kHudW, kHudH };
    return rect.containsPoint(p);
}

void ColorPickerOverlay::update(float) {
    if (!m_ready || m_closing) return;
    // The live color is sampled in liveSample() (called from the pre-swap
    // hook). Here we just refresh the HUD readout every frame so the swatch
    // and value label track the cursor in real time.
    updateReadout();
}

bool ColorPickerOverlay::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (!m_ready || m_closing) return false;
    CCPoint p = touch->getLocation();
    if (pointInHud(p)) return true; // consume; HUD menu/input already handled it
    m_dragging = true;
    this->pickAt(p);
    return true;
}

void ColorPickerOverlay::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (!m_ready || m_closing || !m_dragging) return;
    this->pickAt(touch->getLocation());
}

void ColorPickerOverlay::ccTouchEnded(CCTouch*, CCEvent*) {
    // Auto-apply happens live inside pickAt(), so nothing to commit on release.
    m_dragging = false;
}

void ColorPickerOverlay::ccTouchCancelled(CCTouch*, CCEvent*) {
    m_dragging = false;
}

void ColorPickerOverlay::onPrevFormat(CCObject*) {
    m_formatIndex = clampFormatIndex((m_formatIndex + kFormatCount - 1) % kFormatCount);
    Mod::get()->setSavedValue<int64_t>("editor-cp-format", m_formatIndex);
    this->updateReadout();
}

void ColorPickerOverlay::onNextFormat(CCObject*) {
    m_formatIndex = clampFormatIndex((m_formatIndex + 1) % kFormatCount);
    Mod::get()->setSavedValue<int64_t>("editor-cp-format", m_formatIndex);
    this->updateReadout();
}

void ColorPickerOverlay::onPrevColorID(CCObject*) { this->stepColorID(-1); }
void ColorPickerOverlay::onNextColorID(CCObject*) { this->stepColorID(+1); }

void ColorPickerOverlay::stepColorID(int delta) {
    if (!m_idInput) return;
    int id = 0;
    auto txt = m_idInput->getString();
    if (!txt.empty()) {
        if (auto r = geode::utils::numFromString<int>(txt); r) id = r.unwrap();
    }
    id += delta;
    if (id < 0)    id = 0;
    if (id > 9999) id = 9999;
    m_idInput->setString(std::to_string(id));

    // If auto-apply is on, push the current selection to the newly selected
    // channel right away (force a re-apply even if the color didn't change).
    if (m_autoApply) {
        m_hasApplied = false;
        this->tryAutoApply();
    }
}

void ColorPickerOverlay::onCopy(CCObject*) {
    if (!m_ready) return;
    std::string s = this->currentValueString();
    geode::utils::clipboard::write(s);
    PaimonNotify::show(fmt::format("Copied {}", s), NotificationIcon::Success);
}

void ColorPickerOverlay::onToggleAuto(CCObject* sender) {
    m_autoApply = !static_cast<CCMenuItemToggler*>(sender)->isToggled();
    Mod::get()->setSavedValue<bool>("editor-cp-auto-apply", m_autoApply);
    if (m_autoApply) {
        m_autoNoIdWarned = false;
        this->tryAutoApply(); // push the current selection right away
    }
}

void ColorPickerOverlay::onCancel(CCObject*) {
    this->doClose();
}

void ColorPickerOverlay::onSave(CCObject*) {
    if (!m_ready) return;

    ccColor3B col = m_hasSelection ? m_selColor : m_liveColor;

    std::string s = this->currentValueString();
    geode::utils::clipboard::write(s);
    Mod::get()->setSavedValue<int64_t>("editor-cp-format", m_formatIndex);
    Mod::get()->setSavedValue<std::string>("editor-cp-last-color", formatColor(col, 2));

    int channelID = 0;
    if (m_idInput) {
        auto txt = m_idInput->getString();
        if (!txt.empty()) {
            if (auto r = geode::utils::numFromString<int>(txt); r) {
                channelID = r.unwrap();
            }
        }
    }

    if (channelID > 0) {
        const int       chId = channelID;
        const ccColor3B c    = col;
        this->doClose(); // close first so GD's popup is not hidden behind us
        Loader::get()->queueInMainThread([chId, c]() {
            if (paimon::isRuntimeShuttingDown()) return;
            auto* lel = LevelEditorLayer::get();
            if (lel && lel->m_levelSettings && lel->m_levelSettings->m_effectManager) {
                if (auto* action = lel->m_levelSettings->m_effectManager->getColorAction(chId)) {
                    if (auto* popup = ColorSelectPopup::create(action)) {
                        popup->show();
                        popup->selectColor(c);
                        PaimonNotify::show(
                            fmt::format("Loaded color into channel {} - confirm to apply", chId),
                            NotificationIcon::Info);
                        return;
                    }
                }
            }
            PaimonNotify::show("Color copied to clipboard.", NotificationIcon::Success);
        });
        return;
    }

    PaimonNotify::show(fmt::format("Saved & copied {}", s), NotificationIcon::Success);
    this->doClose();
}

void ColorPickerOverlay::tryAutoApply() {
    if (!m_hasSelection || !m_idInput) return;

    int id = 0;
    auto txt = m_idInput->getString();
    if (!txt.empty()) {
        if (auto r = geode::utils::numFromString<int>(txt); r) id = r.unwrap();
    }

    if (id <= 0) {
        // Auto is on but there is no channel to push to — nudge the user once.
        if (!m_autoNoIdWarned) {
            m_autoNoIdWarned = true;
            PaimonNotify::show("Auto-apply: type a Color ID to push picks live.",
                               NotificationIcon::Info);
        }
        return;
    }

    // Skip redundant work while dragging over a uniform area.
    if (m_hasApplied &&
        m_lastApplied.r == m_selColor.r &&
        m_lastApplied.g == m_selColor.g &&
        m_lastApplied.b == m_selColor.b) {
        return;
    }

    this->applyColorToChannel(m_selColor, id);
    m_lastApplied = m_selColor;
    m_hasApplied  = true;
}

void ColorPickerOverlay::applyColorToChannel(ccColor3B col, int channelID) {
    auto* lel = LevelEditorLayer::get();
    if (!lel || !lel->m_levelSettings || !lel->m_levelSettings->m_effectManager) return;
    auto* em = lel->m_levelSettings->m_effectManager;
    auto* action = em->getColorAction(channelID);
    if (!action) return;

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
        Mod::get()->setSavedValue<std::string>("editor-cp-channel-id", m_idInput->getString());
    }

    this->unscheduleAllSelectors();
    this->setTouchEnabled(false);
    if (s_instance == this) s_instance = nullptr;
    this->removeFromParent();
}

} // namespace paimon::editorcp
