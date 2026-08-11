#include "CaptureMiniPreview.hpp"
#include "CaptureUIConstants.hpp"
#include "../services/FramebufferCapture.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/Localization.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::capture {

namespace {
    constexpr float kRefreshDelay = 0.02f;
    constexpr float kBusyRetryDelay = 0.2f;
    constexpr int   kMaxBusyRetries = 4;
}

MiniPreview* MiniPreview::create(float width, float height) {
    auto* ret = new MiniPreview();
    if (ret->init(width, height)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool MiniPreview::init(float width, float height) {
    if (!CCNode::init()) return false;

    m_viewWidth  = width;
    m_viewHeight = height;

    this->setContentSize({width, height});
    this->setAnchorPoint({0.5f, 0.5f});
    this->ignoreAnchorPointForPosition(false);

    if (auto* bg = paimon::SpriteHelper::createColorPanel(
            width + 4.f, height + 4.f, {0, 0, 0}, 220, 4.f)) {
        bg->setPosition({-2.f, -2.f});
        this->addChild(bg, -2);
    }

    if (auto* border = paimon::SpriteHelper::createRoundedRectOutline(
            width + 4.f, height + 4.f, 4.f, {1.f, 1.f, 1.f, 0.18f}, 1.f)) {
        border->setPosition({-2.f, -2.f});
        this->addChild(border, 2);
    }

    m_sprite = CCSprite::create();
    m_sprite->setAnchorPoint({0.5f, 0.5f});
    m_sprite->setPosition({width * 0.5f, height * 0.5f});
    m_sprite->setVisible(false);
    this->addChild(m_sprite, 0);

    m_status = CCLabelBMFont::create(
        Localization::get().getString("preview.mini_loading").c_str(), "bigFont.fnt");
    m_status->setScale(0.28f);
    m_status->setOpacity(150);
    m_status->setPosition({width * 0.5f, height * 0.5f});
    this->addChild(m_status, 1);

    return true;
}

void MiniPreview::onEnter() {
    CCNode::onEnter();
    // First render happens here, not in init(): the owner gets a chance to
    // apply setPlayersHidden() first, and the scheduler is live by now.
    requestRefresh();
}

void MiniPreview::onExit() {
    this->unschedule(schedule_selector(MiniPreview::onRefreshTick));
    CCNode::onExit();
}

void MiniPreview::setPlayersHidden(bool hideP1, bool hideP2) {
    if (m_hideP1 == hideP1 && m_hideP2 == hideP2) return;
    m_hideP1 = hideP1;
    m_hideP2 = hideP2;
    requestRefresh();
}

void MiniPreview::requestRefresh() {
    if (m_pending) return;
    m_pending = true;
    m_busyRetries = 0;
    this->scheduleOnce(schedule_selector(MiniPreview::onRefreshTick), kRefreshDelay);
}

void MiniPreview::onRefreshTick(float) {
    m_pending = false;
    refreshNow();
}

void MiniPreview::showStatus(char const* text) {
    if (m_sprite) m_sprite->setVisible(false);
    if (m_status) {
        m_status->setString(text);
        m_status->setVisible(true);
    }
}

void MiniPreview::refreshNow() {
    namespace C = paimon::capture::preview;
    if (!m_sprite) return;

    auto* tex = FramebufferCapture::renderPreviewTexture(
        C::MINI_RT_WIDTH, C::MINI_RT_HEIGHT, m_hideP1, m_hideP2);

    if (!tex) {
        // The capture pipeline owns the GL state while a real capture runs;
        // come back for the frame after it finishes instead of showing nothing.
        if (m_busyRetries < kMaxBusyRetries) {
            ++m_busyRetries;
            m_pending = true;
            this->scheduleOnce(schedule_selector(MiniPreview::onRefreshTick), kBusyRetryDelay);
            if (!m_sprite->isVisible()) {
                showStatus(Localization::get().getString("preview.mini_loading").c_str());
            }
            return;
        }
        if (!m_sprite->isVisible()) {
            showStatus(Localization::get().getString("preview.mini_unavailable").c_str());
        }
        return;
    }

    m_busyRetries = 0;

    // The texture reports its size in pixels; sprites work in points, and GD
    // runs with a content scale factor of 4. Using the point size keeps the
    // texture rect (and therefore the UVs) matched to the whole image.
    auto sizeInPoints = tex->getContentSize();
    if (sizeInPoints.width <= 0.f || sizeInPoints.height <= 0.f) {
        showStatus(Localization::get().getString("preview.mini_unavailable").c_str());
        return;
    }

    m_sprite->setTexture(tex);
    m_sprite->setTextureRect({0.f, 0.f, sizeInPoints.width, sizeInPoints.height});
    m_sprite->setFlipY(true);
    m_sprite->setScale(std::min(m_viewWidth / sizeInPoints.width,
                                m_viewHeight / sizeInPoints.height));
    m_sprite->setPosition({m_viewWidth * 0.5f, m_viewHeight * 0.5f});
    m_sprite->setVisible(true);

    if (m_status) m_status->setVisible(false);
}

} // namespace paimon::capture
