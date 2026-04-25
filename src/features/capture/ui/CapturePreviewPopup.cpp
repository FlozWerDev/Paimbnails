#include "CapturePreviewPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include <asp/time.hpp>
#include "CaptureLayerEditorPopup.hpp"
#include "CaptureAssetBrowserPopup.hpp"
#include "CaptureUIConstants.hpp"
#include "../../thumbnails/services/LocalThumbs.hpp"
#include "../../thumbnails/services/ThumbnailLoader.hpp"
#include "../../../utils/Localization.hpp"
#include "../services/FramebufferCapture.hpp"
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/cocos/platform/CCGL.h>
#include "../../../utils/PaimonButtonHighlighter.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PlayerToggleHelper.hpp"
#include "../../../utils/RenderTexture.hpp"
#include "../../../utils/ImageConverter.hpp"
#include "../../../managers/ThumbnailAPI.hpp"
#include <Geode/binding/FMODAudioEngine.hpp>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <future>
#include <mutex>
#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
std::mutex s_downloadWorkerMutex;
std::vector<std::future<void>> s_downloadWorkers;

CCSize getSpriteLogicalSize(CCSprite* sprite) {
    if (!sprite) return {0.f, 0.f};
    auto size = sprite->getContentSize();
    if (size.width > 0.f && size.height > 0.f) {
        return size;
    }
    if (auto* tex = sprite->getTexture()) {
        return {
            static_cast<float>(tex->getPixelsWide()),
            static_cast<float>(tex->getPixelsHigh())
        };
    }
    return {0.f, 0.f};
}

float computePreviewScale(CCSprite* sprite, float viewWidth, float viewHeight, bool fillMode) {
    auto size = getSpriteLogicalSize(sprite);
    if (viewWidth <= 0.f || viewHeight <= 0.f || size.width <= 0.f || size.height <= 0.f) {
        return 1.f;
    }
    float scaleX = viewWidth / size.width;
    float scaleY = viewHeight / size.height;
    float scale = fillMode ? std::max(scaleX, scaleY) : std::min(scaleX, scaleY);
    if (scale <= 0.f) return 1.f;
    return std::clamp(scale, 0.01f, 64.0f);
}

void spawnDownloadWorker(std::function<void()> job) {
    std::lock_guard<std::mutex> lock(s_downloadWorkerMutex);
    auto it = s_downloadWorkers.begin();
    while (it != s_downloadWorkers.end()) {
        if (!it->valid() || it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            it = s_downloadWorkers.erase(it);
        } else {
            ++it;
        }
    }
    s_downloadWorkers.emplace_back(std::async(std::launch::async, [job = std::move(job)]() mutable {
        job();
    }));
}
}

// ─── helpers ────────────────────────────────────────────────────────
float CapturePreviewPopup::clampF(float value, float mn, float mx) {
    return std::max(mn, std::min(mx, value));
}

// ─── factory ────────────────────────────────────────────────────────
CapturePreviewPopup* CapturePreviewPopup::create(
    CCTexture2D* texture, int levelID,
    std::shared_ptr<uint8_t> buffer, int width, int height,
    geode::CopyableFunction<void(bool, int, std::shared_ptr<uint8_t>, int, int, std::string, std::string)> callback,
    geode::CopyableFunction<void(bool, bool, CapturePreviewPopup*)> recaptureCallback,
    bool isPlayer1Hidden, bool isPlayer2Hidden, bool isModerator
) {
    if (!texture) return nullptr;
    log::info("[CapturePreview] create: levelID={} size={}x{}", levelID, width, height);

    auto ret = new CapturePreviewPopup();
    ret->m_texture = texture;
    ret->m_levelID = levelID;
    ret->m_buffer  = buffer;
    ret->m_width   = width;
    ret->m_height  = height;
    ret->m_callback          = std::move(callback);
    ret->m_recaptureCallback = std::move(recaptureCallback);
    ret->m_isPlayer1Hidden   = isPlayer1Hidden;
    ret->m_isPlayer2Hidden   = isPlayer2Hidden;
    ret->m_isModerator       = isModerator;

    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CapturePreviewPopup::~CapturePreviewPopup() {
    m_activeTouches.clear();
}

void CapturePreviewPopup::registerWithTouchDispatcher() {
    // Usar getTargetPrio() para participar en el force priority system de GD
    // en vez de hardcodear -502. Esto permite que popups de otros mods
    // se stackeen correctamente encima/debajo de este.
    auto* dispatcher = CCDirector::sharedDirector()->getTouchDispatcher();
    dispatcher->addTargetedDelegate(this, dispatcher->getTargetPrio() - 1, true);
}

void CapturePreviewPopup::onExit() {
    Popup::onExit();
}

// ─── updateContent ──────────────────────────────────────────────────
void CapturePreviewPopup::updateContent(CCTexture2D* texture,
    std::shared_ptr<uint8_t> buffer, int width, int height)
{
    if (!texture) return;
    log::debug("[CapturePreview] updateContent: {}x{}", width, height);

    m_texture  = texture;
    m_buffer   = buffer;
    m_width    = width;
    m_height   = height;
    m_isCropped = false;

    if (m_previewSprite) {
        m_previewSprite->removeFromParent();
        m_previewSprite = nullptr;
    }

    m_previewSprite = CCSprite::createWithTexture(m_texture);
    if (!m_previewSprite) return;

    m_previewSprite->setAnchorPoint({0.5f, 0.5f});
    m_previewSprite->setFlipY(false);

    ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    m_texture->setTexParameters(&params);

    m_previewSprite->setID("preview-sprite"_spr);

    if (m_clippingNode) {
        m_previewSprite->setPosition(
            ccp(m_clippingNode->getContentSize().width / 2,
                m_clippingNode->getContentSize().height / 2));
        m_clippingNode->addChild(m_previewSprite, 10);
        // Reset zoom/anchor state on content update
        m_wasZooming = false;
        m_activeTouches.clear();
        updatePreviewScale();
    }

    // Refresh resolution badge with new dimensions
    updateResBadge();
}

// ─── init ───────────────────────────────────────────────────────────
bool CapturePreviewPopup::init() {
    namespace C = paimon::capture::preview;

    if (!Popup::init(C::POPUP_WIDTH, C::POPUP_HEIGHT)) return false;

    this->setTitle(Localization::get().getString("preview.title").c_str());

    if (m_bgSprite) m_bgSprite->setVisible(false);

    auto content = m_mainLayer->getContentSize();

    if (!m_texture) return false;

    // ── area de preview ─────────────────────────────────────────
    float maxWidth  = content.width  - C::PREVIEW_PAD_X * 2;
    float maxHeight = content.height - C::PREVIEW_PAD_TOP - C::PREVIEW_PAD_BOT;
    m_viewWidth  = maxWidth;
    m_viewHeight = maxHeight;

    // Stencil geometrico — evita conflictos con HappyTextures/TextureLdr
    auto stencil = PaimonDrawNode::create();
    CCPoint rect[4] = { ccp(0,0), ccp(maxWidth,0), ccp(maxWidth,maxHeight), ccp(0,maxHeight) };
    ccColor4F white = {1,1,1,1};
    stencil->drawPolygon(rect, 4, white, 0, white);

    m_clippingNode = CCClippingNode::create(stencil);
    m_clippingNode->setContentSize({maxWidth, maxHeight});
    m_clippingNode->setAnchorPoint({0.5f, 0.5f});
    m_clippingNode->setPosition(ccp(content.width / 2, content.height / 2 + C::PREVIEW_OFFSET_Y));
    m_clippingNode->setID("preview-clip"_spr);
    m_mainLayer->addChild(m_clippingNode, 1);

    // Fondo oscuro dentro del clipping
    auto clippingBg = CCLayerColor::create(ccc4(0, 0, 0, C::CLIP_BG_ALPHA));
    clippingBg->setContentSize({maxWidth, maxHeight});
    clippingBg->ignoreAnchorPointForPosition(false);
    clippingBg->setAnchorPoint({0.5f, 0.5f});
    clippingBg->setPosition(ccp(maxWidth / 2, maxHeight / 2));
    m_clippingNode->addChild(clippingBg, -1);

    // Borde decorativo (GJ_square07 como en LocalThumbnailViewPopup)
    auto border = CCScale9Sprite::create("GJ_square07.png");
    if (border) {
        border->setContentSize({maxWidth + C::BORDER_MARGIN, maxHeight + C::BORDER_MARGIN});
        border->setPosition(ccp(content.width / 2, content.height / 2 + C::PREVIEW_OFFSET_Y));
        border->setID("preview-border"_spr);
        m_mainLayer->addChild(border, 2);
    }

    // ── sprite de preview ────────────────────────────────────────
    m_previewSprite = CCSprite::createWithTexture(m_texture);
    if (!m_previewSprite) return false;

    m_previewSprite->setAnchorPoint({0.5f, 0.5f});
    m_previewSprite->setFlipY(false);
    m_previewSprite->setID("preview-sprite"_spr);

    ccTexParams texParams{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    m_texture->setTexParameters(&texParams);

    m_previewSprite->setPosition(ccp(maxWidth / 2, maxHeight / 2));
    m_clippingNode->addChild(m_previewSprite, 10);
    updatePreviewScale();

    // ── resolution badge (top-right of preview area) ────────────
    {
        float badgeX = content.width / 2 + maxWidth / 2 - 4.f;
        float badgeY = content.height / 2 + C::PREVIEW_OFFSET_Y + maxHeight / 2 - 4.f;

        m_resBadgeLabel = CCLabelBMFont::create("1080p", "bigFont.fnt");
        m_resBadgeLabel->setScale(0.2f);
        m_resBadgeLabel->setAnchorPoint({1.f, 1.f});
        m_resBadgeLabel->setPosition(ccp(badgeX - 2.f, badgeY - 2.f));
        m_resBadgeLabel->setID("res-badge-label"_spr);
        m_mainLayer->addChild(m_resBadgeLabel, 16);

        m_resBadge = nullptr;
        updateResBadge();

        // Clickable badge area — cycle resolution on click
        auto badgeMenu = CCMenu::create();
        badgeMenu->setPosition(CCPointZero);
        badgeMenu->setID("res-badge-menu"_spr);

        auto clickArea = CCSprite::create();
        clickArea->setContentSize({50.f, 16.f});
        clickArea->setOpacity(0);
        auto badgeBtn = CCMenuItemSpriteExtra::create(
            clickArea, this, menu_selector(CapturePreviewPopup::onCycleResolution));
        badgeBtn->setContentSize({50.f, 16.f});
        badgeBtn->setAnchorPoint({1.f, 1.f});
        badgeBtn->setPosition(ccp(badgeX, badgeY));
        badgeBtn->setID("res-cycle-btn"_spr);
        badgeMenu->addChild(badgeBtn);
        m_mainLayer->addChild(badgeMenu, 17);
    }

    // ── boton X (reposicionar al borde del area preview) ────────
    if (m_closeBtn) {
        float topY  = (content.height / 2 + C::PREVIEW_OFFSET_Y) + (maxHeight / 2);
        float leftX = (content.width - maxWidth) / 2;
        m_closeBtn->setPosition(ccp(leftX - 3.f, topY + 3.f));
    }

    // ── Helper: normaliza un sprite para encajar en target ──────
    auto normalizeSprite = [](CCSprite* spr, float targetSize) {
        if (!spr) return;
        auto cs = spr->getContentSize();
        float maxDim = std::max(cs.width, cs.height);
        if (maxDim > 0.f) spr->setScale(targetSize / maxDim);
    };

    // ══════════════════════════════════════════════════════════════
    // TOOLBAR: edit controls (left) + separator + action buttons (right)
    // ══════════════════════════════════════════════════════════════
    const float toolbarW = content.width - C::PREVIEW_PAD_X * 2;
    const float toolbarY = C::TOOLBAR_Y + 33.f;

    // ── Edit controls (left group) ──────────────────────────────
    m_editMenu = CCMenu::create();
    m_editMenu->setID("edit-menu"_spr);

    // Helper: create a SimplePlayer icon with the user's colors and current gamemode
    auto createPlayerSpr = [&](bool isP2) -> CCNode* {
        auto* gm = GameManager::sharedState();
        if (!gm) return nullptr;

        // Detect current gamemode from PlayLayer player object
        int iconID = gm->m_playerFrame;
        IconType iconType = IconType::Cube;

        if (auto* pl = PlayLayer::get()) {
            auto* playerObj = isP2 ? pl->m_player2 : pl->m_player1;
            if (playerObj) {
                if (playerObj->m_isShip) {
                    iconID = gm->m_playerShip;
                    iconType = IconType::Ship;
                } else if (playerObj->m_isBall) {
                    iconID = gm->m_playerBall;
                    iconType = IconType::Ball;
                } else if (playerObj->m_isBird) {
                    iconID = gm->m_playerBird;
                    iconType = IconType::Ufo;
                } else if (playerObj->m_isDart) {
                    iconID = gm->m_playerDart;
                    iconType = IconType::Wave;
                } else if (playerObj->m_isRobot) {
                    iconID = gm->m_playerRobot;
                    iconType = IconType::Robot;
                } else if (playerObj->m_isSpider) {
                    iconID = gm->m_playerSpider;
                    iconType = IconType::Spider;
                } else if (playerObj->m_isSwing) {
                    iconID = gm->m_playerSwing;
                    iconType = IconType::Swing;
                }
            }
        }

        auto* player = SimplePlayer::create(iconID);
        if (!player) return nullptr;
        if (iconType != IconType::Cube) {
            player->updatePlayerFrame(iconID, iconType);
        }
        auto col1 = gm->colorForIdx(isP2 ? gm->m_playerColor2 : gm->m_playerColor);
        auto col2 = gm->colorForIdx(isP2 ? gm->m_playerColor : gm->m_playerColor2);
        player->setColor(col1);
        player->setSecondColor(col2);
        if (gm->m_playerGlow) {
            player->setGlowOutline(gm->colorForIdx(gm->m_playerGlowColor));
        } else {
            player->disableGlowOutline();
        }
        float maxDim = std::max(player->getContentSize().width, player->getContentSize().height);
        if (maxDim > 0) player->setScale(13.5f / maxDim);
        return player;
    };

    // Helper: wrap a sprite/node inside a CircleButtonSprite
    auto wrapInCircle = [&](CCSprite* inner, float innerScale = 1.0f) -> CCSprite* {
        auto bg = CCSprite::createWithSpriteFrameName("GJ_plainBtn_001.png");
        if (!bg) return inner;
        float maxDim = std::max(bg->getContentSize().width, bg->getContentSize().height);
        if (maxDim > 0) bg->setScale(C::BTN_TARGET_SIZE / maxDim);
        if (inner) {
            inner->setScale(innerScale);
            inner->setPosition(bg->getContentSize() / 2);
            inner->setAnchorPoint({0.5f, 0.5f});
            bg->addChild(inner, 1);
        }
        return bg;
    };

    // P1 toggle button — SimplePlayer inside button asset
    {
        auto* p1Player = createPlayerSpr(false);
        auto bg = CCSprite::createWithSpriteFrameName("GJ_plainBtn_001.png");
        if (bg && p1Player) {
            float maxDim = std::max(bg->getContentSize().width, bg->getContentSize().height);
            if (maxDim > 0) bg->setScale(C::BTN_TARGET_SIZE / maxDim);
            p1Player->setPosition(bg->getContentSize() / 2);
            p1Player->setAnchorPoint({0.5f, 0.5f});
            bg->addChild(p1Player, 1);
        }
        auto spr = bg ? static_cast<CCNode*>(bg) : static_cast<CCNode*>(CCSprite::createWithSpriteFrameName("GJ_plainBtn_001.png"));
        m_p1Btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(CapturePreviewPopup::onTogglePlayer1Btn));
        m_p1Btn->setID("p1-toggle"_spr);
        updatePlayerBtnVisual(m_p1Btn, m_isPlayer1Hidden);
        m_editMenu->addChild(m_p1Btn);
    }

    // P2 toggle button — SimplePlayer inside button asset
    {
        auto* p2Player = createPlayerSpr(true);
        auto bg = CCSprite::createWithSpriteFrameName("GJ_plainBtn_001.png");
        if (bg && p2Player) {
            float maxDim = std::max(bg->getContentSize().width, bg->getContentSize().height);
            if (maxDim > 0) bg->setScale(C::BTN_TARGET_SIZE / maxDim);
            p2Player->setPosition(bg->getContentSize() / 2);
            p2Player->setAnchorPoint({0.5f, 0.5f});
            bg->addChild(p2Player, 1);
        }
        auto spr = bg ? static_cast<CCNode*>(bg) : static_cast<CCNode*>(CCSprite::createWithSpriteFrameName("GJ_plainBtn_001.png"));
        m_p2Btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(CapturePreviewPopup::onTogglePlayer2Btn));
        m_p2Btn->setID("p2-toggle"_spr);
        updatePlayerBtnVisual(m_p2Btn, m_isPlayer2Hidden);
        m_editMenu->addChild(m_p2Btn);
    }

    // Crop button — GJ_backBtn_001.png (already a button asset)
    {
        auto cropSpr = CCSprite::createWithSpriteFrameName("GJ_backBtn_001.png");
        if (!cropSpr) cropSpr = paimon::SpriteHelper::safeCreateWithFrameName("edit_addCBtn_001.png");
        normalizeSprite(cropSpr, C::BTN_TARGET_SIZE);
        auto cropBtn = CCMenuItemSpriteExtra::create(
            cropSpr, this, menu_selector(CapturePreviewPopup::onCropBtn));
        cropBtn->setID("crop-button"_spr);
        m_editMenu->addChild(cropBtn);
    }

    // Fill/Fit toggle button
    {
        auto fillSpr = CCSprite::createWithSpriteFrameName("gj_findBtnOff_001.png");
        if (!fillSpr) fillSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_squareBtn_001.png");
        normalizeSprite(fillSpr, C::BTN_TARGET_SIZE);
        auto fillBtn = CCMenuItemSpriteExtra::create(
            fillSpr, this, menu_selector(CapturePreviewPopup::onToggleFillBtn));
        fillBtn->setID("fill-button"_spr);
        m_editMenu->addChild(fillBtn);
    }

    // Layer Editor button — inside button asset
    {
        auto innerLayer = paimon::SpriteHelper::safeCreateWithFrameName("GJ_editBtn_001.png");
        auto layerSpr = wrapInCircle(innerLayer, 0.55f);
        auto layerBtn = CCMenuItemSpriteExtra::create(
            layerSpr, this, menu_selector(CapturePreviewPopup::onLayerEditorBtn));
        layerBtn->setID("layers-button"_spr);
        m_editMenu->addChild(layerBtn);
    }

    // Asset Browser button — safe sprite creation with fallback
    {
        auto assetBg = paimon::SpriteHelper::safeCreateWithFrameName("GJ_longBtn06_001.png");
        if (!assetBg) assetBg = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plainBtn_001.png");
        if (assetBg) {
            normalizeSprite(assetBg, C::BTN_TARGET_SIZE);
            auto assetBtn = CCMenuItemSpriteExtra::create(
                assetBg, this, menu_selector(CapturePreviewPopup::onAssetBrowserBtn));
            assetBtn->setID("assets-button"_spr);
            m_editMenu->addChild(assetBtn);
        }
    }

    m_editMenu->ignoreAnchorPointForPosition(false);
    m_editMenu->setAnchorPoint({0.f, 0.5f});
    m_editMenu->setContentSize({toolbarW * 0.55f, C::TOOLBAR_HEIGHT});
    m_editMenu->setPosition(ccp(C::PREVIEW_PAD_X, toolbarY));
    m_editMenu->setLayout(
        RowLayout::create()
            ->setGap(C::TOOLBAR_EDIT_GAP)
            ->setAxisAlignment(AxisAlignment::Start)
            ->setCrossAxisAlignment(AxisAlignment::Center)
    );
    m_editMenu->updateLayout();
    m_mainLayer->addChild(m_editMenu, 10);

    // ── Action buttons (right group) ─────────────────────────────
    m_actionMenu = CCMenu::create();
    m_actionMenu->setID("action-menu"_spr);

    // Cancel button — GJ_deleteBtn_001.png
    auto cancelSpr = CCSprite::createWithSpriteFrameName("GJ_deleteBtn_001.png");
    if (!cancelSpr) cancelSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_deleteIcon_001.png");
    normalizeSprite(cancelSpr, C::BTN_TARGET_SIZE);
    auto cancelBtn = CCMenuItemSpriteExtra::create(
        cancelSpr, this, menu_selector(CapturePreviewPopup::onCancelBtn));
    cancelBtn->setID("cancel-button"_spr);
    m_actionMenu->addChild(cancelBtn);

    // Download button — GJ_downloadBtn_001.png
    auto downloadSpr = CCSprite::createWithSpriteFrameName("GJ_downloadBtn_001.png");
    if (!downloadSpr) downloadSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_03_001.png");
    normalizeSprite(downloadSpr, C::BTN_TARGET_SIZE);
    auto downloadBtn = CCMenuItemSpriteExtra::create(
        downloadSpr, this, menu_selector(CapturePreviewPopup::onDownloadBtn));
    downloadBtn->setID("download-button"_spr);
    m_actionMenu->addChild(downloadBtn);

    // Recenter button — GJ_zoomInBtn_001.png is already a button asset, no wrapping
    auto recenterSpr = CCSprite::createWithSpriteFrameName("GJ_zoomInBtn_001.png");
    if (!recenterSpr) recenterSpr = paimon::SpriteHelper::safeCreateWithFrameName("gj_findBtnOff_001.png");
    normalizeSprite(recenterSpr, C::BTN_TARGET_SIZE);
    auto recenterBtn = CCMenuItemSpriteExtra::create(
        recenterSpr, this, menu_selector(CapturePreviewPopup::onRecenterBtn));
    recenterBtn->setID("recenter-button"_spr);
    m_actionMenu->addChild(recenterBtn);

    // Accept button — green check inside GJ_plainBtn_001.png (30% larger)
    {
        constexpr float acceptSize = C::BTN_TARGET_SIZE * 1.3f;
        auto okBg = CCSprite::createWithSpriteFrameName("GJ_plainBtn_001.png");
        auto okCheck = paimon::SpriteHelper::safeCreateWithFrameName("GJ_completesIcon_001.png");
        if (!okCheck) okCheck = paimon::SpriteHelper::safeCreateWithFrameName("GJ_checkOn_001.png");
        if (okBg) {
            float maxDim = std::max(okBg->getContentSize().width, okBg->getContentSize().height);
            if (maxDim > 0) okBg->setScale(acceptSize / maxDim);
            if (okCheck) {
                okCheck->setScale(0.65f);
                okCheck->setPosition(okBg->getContentSize() / 2);
                okCheck->setAnchorPoint({0.5f, 0.5f});
                okBg->addChild(okCheck, 1);
            }
        }
        auto okSpr = okBg ? static_cast<CCSprite*>(okBg) : okCheck;
        auto okBtn = CCMenuItemSpriteExtra::create(
            okSpr, this, menu_selector(CapturePreviewPopup::onAcceptBtn));
        okBtn->setID("ok-button"_spr);
        m_actionMenu->addChild(okBtn);
    }

    m_actionMenu->ignoreAnchorPointForPosition(false);
    m_actionMenu->setAnchorPoint({1.f, 0.5f});
    m_actionMenu->setContentSize({toolbarW * 0.45f, C::TOOLBAR_HEIGHT});
    m_actionMenu->setPosition(ccp(content.width - C::PREVIEW_PAD_X, toolbarY));
    m_actionMenu->setLayout(
        RowLayout::create()
            ->setGap(C::TOOLBAR_GAP)
            ->setAxisAlignment(AxisAlignment::End)
            ->setCrossAxisAlignment(AxisAlignment::Center)
    );
    m_actionMenu->updateLayout();
    m_mainLayer->addChild(m_actionMenu, 10);

    // ── activar touch/scroll ────────────────────────────────────
    // Usar setTouchEnabled(true) en vez de addTargetedDelegate manual.
    // registerWithTouchDispatcher() se encarga de usar getTargetPrio()
    // para interoperar con el force priority system de GD.
    this->setTouchEnabled(true);
#if defined(GEODE_IS_WINDOWS)
    this->setMouseEnabled(true);
    this->setKeypadEnabled(true);
#endif

    paimon::markDynamicPopup(this);
    return true;
}

// ─── updatePreviewScale ────────────────────────────────────────────
void CapturePreviewPopup::updatePreviewScale() {
    namespace C = paimon::capture::preview;
    if (!m_previewSprite || m_viewWidth < 1.f || m_viewHeight < 1.f) return;

    float scale = computePreviewScale(m_previewSprite, m_viewWidth, m_viewHeight, m_fillMode);

    m_previewSprite->setScale(scale);
    m_previewSprite->setAnchorPoint({0.5f, 0.5f});

    m_initialScale = scale;
    m_minScale     = scale;
    m_maxScale     = std::max(C::ZOOM_MAX_BASE, scale * C::ZOOM_MAX_MULT);

    if (m_clippingNode) {
        m_previewSprite->setPosition(
            ccp(m_clippingNode->getContentSize().width / 2,
                m_clippingNode->getContentSize().height / 2));
    }
}

// ─── player button visual ──────────────────────────────────────────
void CapturePreviewPopup::updatePlayerBtnVisual(CCMenuItemSpriteExtra* btn, bool isHidden) {
    if (!btn) return;
    auto* node = btn->getNormalImage();
    if (!node) return;
    if (isHidden) {
        node->setVisible(true); // ensure visible even when toggled
        // Apply dimmed red tint to the button background
        if (auto* spr = typeinfo_cast<CCSprite*>(node)) {
            spr->setColor({200, 70, 70});
            spr->setOpacity(160);
        }
        // Also dim child nodes (SimplePlayer inside)
        auto* children = node->getChildren();
        if (children) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(child)) {
                    rgba->setOpacity(100);
                }
            }
        }
    } else {
        node->setVisible(true);
        if (auto* spr = typeinfo_cast<CCSprite*>(node)) {
            spr->setColor({255, 255, 255});
            spr->setOpacity(255);
        }
        auto* children = node->getChildren();
        if (children) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(child)) {
                    rgba->setOpacity(255);
                }
            }
        }
    }
}

// ─── button handlers ───────────────────────────────────────────────
void CapturePreviewPopup::onTogglePlayer1Btn(CCObject* sender) {
    if (!sender) return;
    m_isPlayer1Hidden = !m_isPlayer1Hidden;
    updatePlayerBtnVisual(m_p1Btn, m_isPlayer1Hidden);
    if (m_recaptureCallback) {
        m_recaptureCallback(m_isPlayer1Hidden, m_isPlayer2Hidden, this);
    } else {
        liveRecapture(true);
    }
}

void CapturePreviewPopup::onTogglePlayer2Btn(CCObject* sender) {
    if (!sender) return;
    m_isPlayer2Hidden = !m_isPlayer2Hidden;
    updatePlayerBtnVisual(m_p2Btn, m_isPlayer2Hidden);
    if (m_recaptureCallback) {
        m_recaptureCallback(m_isPlayer1Hidden, m_isPlayer2Hidden, this);
    } else {
        liveRecapture(true);
    }
}

void CapturePreviewPopup::onToggleFillBtn(CCObject* sender) {
    if (!sender) return;
    m_fillMode = !m_fillMode;
    updatePreviewScale();
    auto msg = m_fillMode
        ? Localization::get().getString("preview.fill_mode_active")
        : Localization::get().getString("preview.fit_mode_active");
    PaimonNotify::create(msg.c_str(), NotificationIcon::Info)->show();
}

void CapturePreviewPopup::onRecenterBtn(CCObject*) {
    if (!m_previewSprite) return;
    m_previewSprite->stopAllActions();
    m_previewSprite->setAnchorPoint({0.5f, 0.5f});
    updatePreviewScale();
}

void CapturePreviewPopup::onClose(CCObject* sender) {
    m_recapturePending = false;
    this->unschedule(schedule_selector(CapturePreviewPopup::onRecaptureTimeout));
    CaptureLayerEditorPopup::restoreAllLayers();

    // Cancel any pending recapture to avoid callbacks targeting a destroyed popup
    FramebufferCapture::cancelPending();

    // Reanudar musica solo si nosotros la pausamos (keybind capture)
    if (m_pausedMusic) {
        if (auto* engine = FMODAudioEngine::sharedEngine()) {
            if (engine->m_backgroundMusicChannel) {
                engine->m_backgroundMusicChannel->setPaused(false);
            }
        }
    }

    m_activeTouches.clear();

    // setTouchEnabled(true) + registerWithTouchDispatcher() se limpian
    // automaticamente via Popup::onClose → setTouchEnabled(false) → removeDelegate.
    // Ya no necesitamos limpieza manual del delegate.

    if (!m_callbackExecuted && m_callback) {
        m_callback(false, m_levelID, m_buffer, m_width, m_height, "", "");
        m_callbackExecuted = true;
    }
    Popup::onClose(sender);
}

void CapturePreviewPopup::recapture() {
    if (FramebufferCapture::hasPendingCapture()) {
        PaimonNotify::create(
            Localization::get().getString("layers.recapturing").c_str(),
            NotificationIcon::Warning)->show();
        return;
    }

    Ref<CapturePreviewPopup> safeRef = this;
    this->setVisible(false);
    m_recapturePending = true;
    this->scheduleOnce(schedule_selector(CapturePreviewPopup::onRecaptureTimeout),
        paimon::capture::preview::RECAPTURE_TIMEOUT_SEC);

    FramebufferCapture::requestCapture(m_levelID,
        [safeRef](bool success, CCTexture2D* texture,
               std::shared_ptr<uint8_t> rgbaData, int width, int height) {
            Loader::get()->queueInMainThread(
                [safeRef, success, texture, rgbaData, width, height]() {
                    safeRef->m_recapturePending = false;
                    safeRef->unschedule(schedule_selector(CapturePreviewPopup::onRecaptureTimeout));
                    if (!safeRef->getParent()) return;
                    if (success && texture && rgbaData) {
                        safeRef->updateContent(texture, rgbaData, width, height);
                    } else {
                        PaimonNotify::create(
                            Localization::get().getString("layers.recapture_error").c_str(),
                            NotificationIcon::Error)->show();
                    }
                    safeRef->setVisible(true);
                });
        });
}

void CapturePreviewPopup::onRecaptureTimeout(float) {
    if (!m_recapturePending) return;
    m_recapturePending = false;
    FramebufferCapture::cancelPending();
    this->setVisible(true);
    PaimonNotify::create(Localization::get().getString("layers.recapture_error").c_str(), NotificationIcon::Warning)->show();
}

void CapturePreviewPopup::liveRecapture(bool updateBuffer) {
    auto* pl = PlayLayer::get();
    if (!pl) return;

    // Use the same custom RenderTexture as captureScreenshot in PlayLayer
    // It properly adjusts m_fScaleX/Y, m_obScreenSize and design resolution,
    // which ensures ShaderLayer FBOs resolve correctly.
    auto* view = CCEGLView::sharedOpenGLView();
    if (!view) return;
    auto screenSize = view->getFrameSize();
    int screenW = static_cast<int>(screenSize.width);
    int screenH = static_cast<int>(screenSize.height);
    if (screenW <= 0 || screenH <= 0) return;

    // Respect configured capture resolution (same logic as FramebufferCapture)
    std::string res = geode::Mod::get()->getSettingValue<std::string>("capture-resolution");
    int targetW = 1920;
    if (res == "4k")         targetW = 3840;
    else if (res == "1440p") targetW = 2560;

    double aspect = static_cast<double>(screenW) / static_cast<double>(screenH);
    int w = targetW;
    int h = std::max(1, static_cast<int>(std::round(w / aspect)));

    // Hide UI layer
    bool uiWasVisible = false;
    if (pl->m_uiLayer && pl->m_uiLayer->isVisible()) {
        uiWasVisible = true;
        pl->m_uiLayer->setVisible(false);
    }

    PlayerVisState p1State, p2State;
    if (m_isPlayer1Hidden) {
        paimTogglePlayer(pl->m_player1, p1State, true);
    }
    if (m_isPlayer2Hidden) {
        paimTogglePlayer(pl->m_player2, p2State, true);
    }

    // The custom RenderTexture adjusts CCEGLView scale factors,
    // design resolution and viewport — making ShaderLayer render
    // correctly into our FBO.
    ::RenderTexture rt(w, h);
    rt.begin();
    pl->visit();
    rt.end();
    auto data = rt.getData();

    // Restore state
    if (m_isPlayer1Hidden) {
        paimTogglePlayer(pl->m_player1, p1State, false);
    }
    if (m_isPlayer2Hidden) {
        paimTogglePlayer(pl->m_player2, p2State, false);
    }
    if (uiWasVisible && pl->m_uiLayer) {
        pl->m_uiLayer->setVisible(true);
    }

    if (!data) return;

    // Vertical flip (glReadPixels reads bottom-to-top)
    int rowSize = w * 4;
    std::vector<uint8_t> tempRow(rowSize);
    uint8_t* buf = data.get();
    for (int y = 0; y < h / 2; ++y) {
        uint8_t* topRow = buf + y * rowSize;
        uint8_t* bottomRow = buf + (h - y - 1) * rowSize;
        std::memcpy(tempRow.data(), topRow, rowSize);
        std::memcpy(topRow, bottomRow, rowSize);
        std::memcpy(bottomRow, tempRow.data(), rowSize);
    }

    if (updateBuffer) {
        size_t dataSize = static_cast<size_t>(w) * h * 4;
        std::shared_ptr<uint8_t> buffer(new uint8_t[dataSize], std::default_delete<uint8_t[]>());
        memcpy(buffer.get(), data.get(), dataSize);

        auto* tex = new CCTexture2D();
        if (!tex->initWithData(buffer.get(), kCCTexture2DPixelFormat_RGBA8888,
                               w, h, CCSize(static_cast<float>(w), static_cast<float>(h)))) {
            tex->release();
            return;
        }
        tex->setAntiAliasTexParameters();
        tex->autorelease();
        updateContent(tex, buffer, w, h);
    } else {
        // Fast path: create texture from data directly for visual-only update
        auto* tex = new CCTexture2D();
        if (!tex->initWithData(data.get(), kCCTexture2DPixelFormat_RGBA8888,
                               w, h, CCSize(static_cast<float>(w), static_cast<float>(h)))) {
            tex->release();
            return;
        }
        tex->setAntiAliasTexParameters();

        if (m_previewSprite) {
            m_previewSprite->setTexture(tex);
            m_previewSprite->setTextureRect(CCRect(0, 0,
                static_cast<float>(w), static_cast<float>(h)));
            m_previewSprite->setFlipY(false);
            updatePreviewScale();
        }
        tex->release();
    }
}

void CapturePreviewPopup::onAcceptBtn(CCObject* sender) {
    if (!sender) return;
    m_callbackExecuted = true;
    ThumbnailLoader::get().invalidateLevel(m_levelID);

    // pone la miniatura aceptada en el cache de sesion para que
    // LevelInfoLayer pueda mostrarla de inmediato al volver del nivel
    // (antes de que el server propague la subida)
    if (m_buffer && m_width > 0 && m_height > 0) {
        auto* tex = new CCTexture2D();
        if (tex->initWithData(m_buffer.get(), kCCTexture2DPixelFormat_RGBA8888,
                m_width, m_height, CCSize((float)m_width, (float)m_height))) {
            tex->autorelease();
            ThumbnailLoader::get().updateSessionCache(m_levelID, tex);
        } else {
            tex->release();
        }
    }

    if (m_callback) m_callback(true, m_levelID, m_buffer, m_width, m_height, "", "");
    this->onClose(nullptr);
}

void CapturePreviewPopup::onLayerEditorBtn(CCObject* sender) {
    if (!sender) return;
    auto* editor = CaptureLayerEditorPopup::create(this);
    if (editor) {
        editor->show();
    }
}

void CapturePreviewPopup::onAssetBrowserBtn(CCObject* sender) {
    if (!sender) return;
    auto* browser = CaptureAssetBrowserPopup::create(this);
    if (browser) {
        browser->show();
    }
}

// ─── resolution badge ──────────────────────────────────────────────

void CapturePreviewPopup::updateResBadge() {
    if (!m_resBadgeLabel) return;

    std::string res = geode::Mod::get()->getSettingValue<std::string>("capture-resolution");
    std::string label;
    ccColor3B color;

    if (res == "4k") {
        label = "4K";
        color = {255, 200, 80}; // gold
    } else if (res == "1440p") {
        label = "1440p";
        color = {120, 220, 255}; // light blue
    } else {
        label = "1080p";
        color = {200, 255, 200}; // light green
    }

    // Append actual capture dimensions
    if (m_width > 0 && m_height > 0) {
        label += "  " + std::to_string(m_width) + "x" + std::to_string(m_height);
    }

    m_resBadgeLabel->setString(label.c_str());
    m_resBadgeLabel->setColor(color);

    auto labelSize = m_resBadgeLabel->getContentSize() * m_resBadgeLabel->getScale();
    float bgW = labelSize.width + 8.f;
    float bgH = labelSize.height + 5.f;

    // Recalcular posicion del badge (igual que en init)
    auto content = m_mainLayer->getContentSize();
    namespace C = paimon::capture::preview;
    float maxWidth  = content.width  - C::PREVIEW_PAD_X * 2;
    float maxHeight = content.height - C::PREVIEW_PAD_TOP - C::PREVIEW_PAD_BOT;
    float badgeX = content.width / 2 + maxWidth / 2 - 4.f;
    float badgeY = content.height / 2 + C::PREVIEW_OFFSET_Y + maxHeight / 2 - 4.f;

    if (m_resBadge) {
        m_resBadge->removeFromParent();
        m_resBadge = nullptr;
    }

    auto badgeBg = paimon::SpriteHelper::createColorPanel(bgW, bgH, {20, 20, 20}, 190, 4.f);
    if (badgeBg) {
        badgeBg->setAnchorPoint({1.f, 1.f});
        badgeBg->setPosition(ccp(badgeX, badgeY));
        badgeBg->setID("res-badge-bg"_spr);
        m_mainLayer->addChild(badgeBg, 15);
        m_resBadge = badgeBg;
    }

    // Ajustar hitbox del boton clickable al tamaño real del badge
    if (auto* menu = m_mainLayer->getChildByID("res-badge-menu"_spr)) {
        if (auto* btn = menu->getChildByID("res-cycle-btn"_spr)) {
            btn->setContentSize({bgW, bgH});
            btn->setPosition(ccp(badgeX, badgeY));
        }
    }
}

void CapturePreviewPopup::onCycleResolution(CCObject* sender) {
    std::string current = geode::Mod::get()->getSettingValue<std::string>("capture-resolution");

    std::string next;
    if (current == "1080p")      next = "1440p";
    else if (current == "1440p") next = "4k";
    else                         next = "1080p";

    geode::Mod::get()->setSettingValue<std::string>("capture-resolution", next);

    // Update badge immediately
    updateResBadge();

    // Notify user
    auto& loc = Localization::get();
    std::string msg = loc.getString("preview.res_changed");
    // Replace {} with the resolution name
    auto pos = msg.find("{}");
    if (pos != std::string::npos) msg.replace(pos, 2, next);
    PaimonNotify::create(msg.c_str(), NotificationIcon::Info)->show();

    // Trigger recapture at new resolution
    liveRecapture(true);
}

void CapturePreviewPopup::onCancelBtn(CCObject* sender) {
    if (!sender) return;
    m_callbackExecuted = true;
    if (m_callback) m_callback(false, m_levelID, m_buffer, m_width, m_height, "", "");
    this->onClose(nullptr);
}

// ─── crop ──────────────────────────────────────────────────────────
void CapturePreviewPopup::onCropBtn(CCObject* sender) {
    if (!sender) return;
    if (!m_buffer || m_width <= 0 || m_height <= 0) return;

    if (m_isCropped) {
        PaimonNotify::create(Localization::get().getString("preview.borders_removed").c_str(),
            NotificationIcon::Info)->show();
        return;
    }

    auto cropRect = detectBlackBorders();
    if (cropRect.width == m_width && cropRect.height == m_height) {
        PaimonNotify::create(Localization::get().getString("preview.no_borders").c_str(),
            NotificationIcon::Info)->show();
        return;
    }

    applyCrop(cropRect);
    m_isCropped = true;
    PaimonNotify::create(Localization::get().getString("preview.borders_deleted").c_str(),
        NotificationIcon::Success)->show();
}

CapturePreviewPopup::CropRect CapturePreviewPopup::detectBlackBorders() {
    namespace C = paimon::capture::preview;
    if (!m_buffer || m_width <= 0 || m_height <= 0)
        return {0, 0, m_width, m_height};

    const uint8_t* data = m_buffer.get();

    auto isBlackPixel = [&](int x, int y) -> bool {
        int idx = (y * m_width + x) * 4;
        return data[idx] <= C::CROP_BLACK_THRESHOLD && data[idx+1] <= C::CROP_BLACK_THRESHOLD && data[idx+2] <= C::CROP_BLACK_THRESHOLD;
    };
    auto isBlackLine = [&](int linePos, bool isHorizontal) -> bool {
        int blackCount = 0, totalSamples = 0;
        if (isHorizontal) {
            for (int x = 0; x < m_width; x += C::CROP_SAMPLE_STEP) {
                if (isBlackPixel(x, linePos)) blackCount++;
                totalSamples++;
            }
        } else {
            for (int y = 0; y < m_height; y += C::CROP_SAMPLE_STEP) {
                if (isBlackPixel(linePos, y)) blackCount++;
                totalSamples++;
            }
        }
        return static_cast<float>(blackCount) / totalSamples >= C::CROP_BLACK_PERCENTAGE;
    };

    int top = 0, bottom = m_height - 1, left = 0, right = m_width - 1;
    for (int y = 0; y < m_height / 2; ++y) { if (!isBlackLine(y, true)) { top = y; break; } }
    for (int y = m_height - 1; y >= m_height / 2; --y) { if (!isBlackLine(y, true)) { bottom = y; break; } }
    for (int x = 0; x < m_width / 2; ++x) { if (!isBlackLine(x, false)) { left = x; break; } }
    for (int x = m_width - 1; x >= m_width / 2; --x) { if (!isBlackLine(x, false)) { right = x; break; } }

    int cropW = right - left + 1;
    int cropH = bottom - top + 1;
    float cropRatio = static_cast<float>(cropW * cropH) / (m_width * m_height);
    if (cropRatio < C::CROP_MIN_RATIO || cropRatio > C::CROP_MAX_RATIO) return {0, 0, m_width, m_height};
    return {left, top, cropW, cropH};
}

void CapturePreviewPopup::applyCrop(const CropRect& rect) {
    size_t newSize = static_cast<size_t>(rect.width) * rect.height * 4;
    std::shared_ptr<uint8_t> croppedBuffer(new uint8_t[newSize], std::default_delete<uint8_t[]>());
    const uint8_t* srcData = m_buffer.get();
    uint8_t* dstData = croppedBuffer.get();

    for (int y = 0; y < rect.height; ++y) {
        int srcY = rect.y + y;
        const uint8_t* srcRow = srcData + (srcY * m_width + rect.x) * 4;
        uint8_t* dstRow = dstData + y * rect.width * 4;
        memcpy(dstRow, srcRow, rect.width * 4);
    }

    auto* newTexture = new CCTexture2D();
    if (newTexture->initWithData(croppedBuffer.get(), kCCTexture2DPixelFormat_RGBA8888,
            rect.width, rect.height,
            CCSize(static_cast<float>(rect.width), static_cast<float>(rect.height)))) {
        newTexture->setAntiAliasTexParameters();
        newTexture->autorelease();
        updateContent(newTexture, croppedBuffer, rect.width, rect.height);
    } else {
        newTexture->release();
    }
}

// ─── download ──────────────────────────────────────────────────────
void CapturePreviewPopup::onDownloadBtn(CCObject* sender) {
    if (!sender) return;
    if (!m_buffer || m_width <= 0 || m_height <= 0) {
        PaimonNotify::create(Localization::get().getString("preview.no_image").c_str(),
            NotificationIcon::Error)->show();
        return;
    }

    auto downloadDir = Mod::get()->getSaveDir() / "downloaded_thumbnails";
    std::error_code ec;
    if (!std::filesystem::exists(downloadDir, ec)) {
        std::filesystem::create_directory(downloadDir, ec);
        if (ec) {
            PaimonNotify::create(Localization::get().getString("preview.folder_error").c_str(),
                NotificationIcon::Error)->show();
            return;
        }
    }

    auto now = asp::time::SystemTime::now();
    auto tmBuf = asp::localtime(now.to_time_t());
    std::stringstream ss;
    ss << "thumbnail_" << m_levelID << "_" << std::put_time(&tmBuf, "%Y%m%d_%H%M%S") << ".png";
    auto filePath = downloadDir / ss.str();

    // Copiamos el buffer para el hilo de fondo
    size_t dataSize = static_cast<size_t>(m_width) * m_height * 4;
    std::shared_ptr<uint8_t> bufCopy(new uint8_t[dataSize], std::default_delete<uint8_t[]>());
    std::memcpy(bufCopy.get(), m_buffer.get(), dataSize);
    int w = m_width, h = m_height;
    int levelID = m_levelID;

    // ImageConverter::saveRGBAToPNG (imageplus + stb fallback) + std::ofstream(path) = Unicode-safe en Windows
    spawnDownloadWorker([bufCopy, w, h, filePath, levelID]() {
        if (ImageConverter::saveRGBAToPNG(bufCopy.get(), w, h, filePath)) {
            geode::Loader::get()->queueInMainThread([filePath, levelID]() {
                PaimonNotify::create(Localization::get().getString("preview.downloaded").c_str(),
                    geode::NotificationIcon::Success)->show();
                ThumbnailLoader::get().invalidateLevel(levelID);
            });
        } else {
            geode::Loader::get()->queueInMainThread([]() {
                PaimonNotify::create(Localization::get().getString("preview.save_error").c_str(),
                    geode::NotificationIcon::Error)->show();
            });
        }
    });
}

// ═══════════════════════════════════════════════════════════════════
// ZOOM / PAN (copied from LocalThumbnailViewPopup pattern)
// ═══════════════════════════════════════════════════════════════════

void CapturePreviewPopup::clampSpritePosition() {
    if (!m_previewSprite || m_viewWidth <= 0 || m_viewHeight <= 0) return;

    float scale   = m_previewSprite->getScale();
    float spriteW = m_previewSprite->getContentSize().width  * scale;
    float spriteH = m_previewSprite->getContentSize().height * scale;

    CCPoint pos    = m_previewSprite->getPosition();
    CCPoint anchor = m_previewSprite->getAnchorPoint();

    float spriteLeft   = pos.x - spriteW * anchor.x;
    float spriteRight  = pos.x + spriteW * (1.f - anchor.x);
    float spriteBottom = pos.y - spriteH * anchor.y;
    float spriteTop    = pos.y + spriteH * (1.f - anchor.y);

    float newX = pos.x, newY = pos.y;

    if (spriteW <= m_viewWidth) {
        newX = m_viewWidth / 2;
    } else {
        if (spriteLeft  > 0)           newX = spriteW * anchor.x;
        if (spriteRight < m_viewWidth) newX = m_viewWidth - spriteW * (1.f - anchor.x);
    }
    if (spriteH <= m_viewHeight) {
        newY = m_viewHeight / 2;
    } else {
        if (spriteBottom > 0)            newY = spriteH * anchor.y;
        if (spriteTop    < m_viewHeight) newY = m_viewHeight - spriteH * (1.f - anchor.y);
    }

    m_previewSprite->setPosition(ccp(newX, newY));
}

void CapturePreviewPopup::clampSpritePositionAnimated() {
    if (!m_previewSprite || m_viewWidth <= 0 || m_viewHeight <= 0) return;

    float scale   = m_previewSprite->getScale();
    float spriteW = m_previewSprite->getContentSize().width  * scale;
    float spriteH = m_previewSprite->getContentSize().height * scale;

    CCPoint pos    = m_previewSprite->getPosition();
    CCPoint anchor = m_previewSprite->getAnchorPoint();

    float spriteLeft   = pos.x - spriteW * anchor.x;
    float spriteRight  = pos.x + spriteW * (1.f - anchor.x);
    float spriteBottom = pos.y - spriteH * anchor.y;
    float spriteTop    = pos.y + spriteH * (1.f - anchor.y);

    float newX = pos.x, newY = pos.y;
    bool needsAnim = false;

    if (spriteW <= m_viewWidth) {
        if (std::abs(newX - m_viewWidth / 2) > 0.5f) { newX = m_viewWidth / 2; needsAnim = true; }
    } else {
        if (spriteLeft  > 0)           { newX = spriteW * anchor.x;                      needsAnim = true; }
        if (spriteRight < m_viewWidth) { newX = m_viewWidth - spriteW * (1.f - anchor.x); needsAnim = true; }
    }
    if (spriteH <= m_viewHeight) {
        if (std::abs(newY - m_viewHeight / 2) > 0.5f) { newY = m_viewHeight / 2; needsAnim = true; }
    } else {
        if (spriteBottom > 0)            { newY = spriteH * anchor.y;                        needsAnim = true; }
        if (spriteTop    < m_viewHeight) { newY = m_viewHeight - spriteH * (1.f - anchor.y); needsAnim = true; }
    }

    if (needsAnim) {
        m_previewSprite->stopAllActions();
        m_previewSprite->runAction(CCEaseBackOut::create(CCMoveTo::create(0.15f, ccp(newX, newY))));
    }
}

bool CapturePreviewPopup::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    if (!this->isVisible()) return false;

    // Buscar si el toque cae sobre algun item de los menus del popup
    auto findTouchedItem = [](CCMenu* menu, CCTouch* t) -> CCMenuItem* {
        if (!menu || !menu->isVisible()) return nullptr;
        auto point = menu->convertTouchToNodeSpace(t);
        for (auto* obj : CCArrayExt<CCObject*>(menu->getChildren())) {
            auto item = typeinfo_cast<CCMenuItem*>(obj);
            if (item && item->isVisible() && item->isEnabled() &&
                item->boundingBox().containsPoint(point))
                return item;
        }
        return nullptr;
    };

    // Si toca un item de menu, activarlo manualmente y tragar el toque
    if (auto* item = findTouchedItem(m_editMenu, touch)) {
        m_activatedItem = item;
        item->selected();
        return true;
    }
    if (auto* item = findTouchedItem(m_actionMenu, touch)) {
        m_activatedItem = item;
        item->selected();
        return true;
    }

    // Dejar que m_buttonMenu (close btn) maneje por su propia prioridad
    if (m_buttonMenu && m_buttonMenu->isVisible()) {
        auto point = m_buttonMenu->convertTouchToNodeSpace(touch);
        for (auto* obj : CCArrayExt<CCObject*>(m_buttonMenu->getChildren())) {
            auto item = typeinfo_cast<CCMenuItem*>(obj);
            if (item && item->isVisible() && item->isEnabled() &&
                item->boundingBox().containsPoint(point)) {
                m_activatedItem = item;
                item->selected();
                return true;
            }
        }
    }

    // Toques fuera del contenido del popup se tragan para bloquear fondo
    auto nodePos = m_mainLayer->convertToNodeSpace(touch->getLocation());
    auto size    = m_mainLayer->getContentSize();
    if (!CCRect(0, 0, size.width, size.height).containsPoint(nodePos)) return true;

    // Segundo dedo → pinch zoom
    if (m_activeTouches.size() == 1) {
        auto firstTouch = *m_activeTouches.begin();
        if (firstTouch == touch) return true;

        auto firstLoc  = firstTouch->getLocation();
        auto secondLoc = touch->getLocation();
        m_touchMidPoint   = (firstLoc + secondLoc) / 2.0f;
        m_savedScale      = m_previewSprite ? m_previewSprite->getScale() : m_initialScale;
        m_initialDistance  = firstLoc.getDistance(secondLoc);

        if (m_previewSprite) {
            auto oldAnchor  = m_previewSprite->getAnchorPoint();
            auto worldPos   = m_previewSprite->convertToWorldSpace(ccp(0, 0));
            float newAX = clampF((m_touchMidPoint.x - worldPos.x) / m_previewSprite->getScaledContentSize().width,  0, 1);
            float newAY = clampF((m_touchMidPoint.y - worldPos.y) / m_previewSprite->getScaledContentSize().height, 0, 1);
            m_previewSprite->setAnchorPoint(ccp(newAX, newAY));
            m_previewSprite->setPosition(ccp(
                m_previewSprite->getPositionX() + m_previewSprite->getScaledContentSize().width  * -(oldAnchor.x - newAX),
                m_previewSprite->getPositionY() + m_previewSprite->getScaledContentSize().height * -(oldAnchor.y - newAY)
            ));
        }
    }

    m_activeTouches.insert(touch);
    return true;
}

void CapturePreviewPopup::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    // Si estamos rastreando un item de menu, cancelar si el dedo se mueve lejos
    if (m_activatedItem) {
        auto* parent = m_activatedItem->getParent();
        if (auto* menu = typeinfo_cast<CCMenu*>(parent)) {
            auto point = menu->convertTouchToNodeSpace(touch);
            if (!m_activatedItem->boundingBox().containsPoint(point)) {
                m_activatedItem->unselected();
                m_activatedItem = nullptr;
            }
        }
        return;
    }

    if (!m_previewSprite) return;

    if (m_activeTouches.size() == 1) {
        auto delta = touch->getDelta();
        m_previewSprite->setPosition(ccp(
            m_previewSprite->getPositionX() + delta.x,
            m_previewSprite->getPositionY() + delta.y));
        clampSpritePosition();
    } else if (m_activeTouches.size() == 2) {
        m_wasZooming = true;
        auto it = m_activeTouches.begin();
        auto firstTouch  = *it; ++it;
        auto secondTouch = *it;

        auto firstLoc  = firstTouch->getLocation();
        auto secondLoc = secondTouch->getLocation();
        auto center    = (firstLoc + secondLoc) / 2.0f;
        float distNow  = std::max(0.1f, firstLoc.getDistance(secondLoc));

        float mult = std::max(0.0001f, m_initialDistance / distNow);
        float zoom = clampF(m_savedScale / mult, m_minScale, m_maxScale);
        m_previewSprite->setScale(zoom);

        auto centerDiff = m_touchMidPoint - center;
        m_previewSprite->setPosition(m_previewSprite->getPosition() - centerDiff);
        m_touchMidPoint = center;
        clampSpritePosition();
    }
}

void CapturePreviewPopup::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    // Si habia un item de menu activado, dispararlo
    if (m_activatedItem) {
        m_activatedItem->unselected();
        m_activatedItem->activate();
        m_activatedItem = nullptr;
        return;
    }

    m_activeTouches.erase(touch);
    if (!m_previewSprite) return;

    if (m_wasZooming && m_activeTouches.size() == 1) {
        float scale = m_previewSprite->getScale();
        if (scale < m_minScale)
            m_previewSprite->runAction(CCEaseSineInOut::create(CCScaleTo::create(0.3f, m_minScale)));
        else if (scale > m_maxScale)
            m_previewSprite->runAction(CCEaseSineInOut::create(CCScaleTo::create(0.3f, m_maxScale)));
        m_wasZooming = false;
    }
    if (m_activeTouches.empty()) clampSpritePositionAnimated();
}

void CapturePreviewPopup::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    if (m_activatedItem) {
        m_activatedItem->unselected();
        m_activatedItem = nullptr;
    }
    m_activeTouches.erase(touch);
    m_wasZooming = false;
}

void CapturePreviewPopup::scrollWheel(float x, float y) {
    namespace C = paimon::capture::preview;
    if (!m_previewSprite || !m_previewSprite->getParent()) return;

    float scrollAmount = y;
    if (std::abs(y) < 0.001f) scrollAmount = -x;

    float zoomFactor  = scrollAmount > 0 ? C::SCROLL_ZOOM_IN : C::SCROLL_ZOOM_OUT;
    float curScale    = m_previewSprite->getScale();
    float newScale    = clampF(curScale * zoomFactor, m_minScale, m_maxScale);
    if (std::abs(newScale - curScale) < 0.001f) return;

    m_previewSprite->setScale(newScale);
    clampSpritePosition();
}
