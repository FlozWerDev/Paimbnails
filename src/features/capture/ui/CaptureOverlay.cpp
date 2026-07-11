#include "CaptureOverlay.hpp"
#include <Geode/utils/string.hpp>
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/ImageConverter.hpp"
#include "../../../utils/ClipboardImage.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../services/FramebufferCapture.hpp"

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/OverlayManager.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>
#include "../../../utils/ThreadTracker.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#ifdef GEODE_IS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

using namespace cocos2d;
using namespace cocos2d::extension;
using namespace geode::prelude;

CaptureOverlay* CaptureOverlay::s_instance = nullptr;

void CaptureOverlay::show() {
    if (s_instance) {
    s_instance->onClose(nullptr);
        return;
    }

    auto* overlay = CaptureOverlay::create();
    overlay->setID("CaptureOverlay");

    // Use OverlayManager to render above the scene, below the custom cursor.
    if (auto* host = geode::OverlayManager::get()) {
        host->addChild(overlay, 999000);
    } else if (auto* scene = cocos2d::CCDirector::get()->getRunningScene()) {
        scene->addChild(overlay, 99999);
    }
}

void CaptureOverlay::hideOverlay() {
    if (s_instance) {
        s_instance->onClose(nullptr);
    }
}

namespace {
// The docked preview behaves like a toast: it dismisses itself so it never
// lingers on top of popups the user opens afterwards.
constexpr float kAutoDismissSeconds = 6.f;
constexpr float kSceneCheckInterval = 0.25f;
}

bool CaptureOverlay::init() {
    if (!CCLayer::init()) return false;
    s_instance = this;

    this->setTouchEnabled(true);
    this->setKeypadEnabled(true);

    // OverlayManager outlives scene transitions; if the scene changes while the
    // preview card is up, remove it instead of floating over the new scene.
    if (auto* director = CCDirector::get()) {
        m_ownerScene = director->getRunningScene();
        if (auto* scheduler = director->getScheduler()) {
            scheduler->scheduleSelector(
                schedule_selector(CaptureOverlay::checkSceneChanged),
                this, kSceneCheckInterval, false
            );
        }
    }

    // Dim background (hidden during capture so it never appears in the screenshot)
    m_dimBg = CCLayerColor::create({0, 0, 0, 0});
    if (m_dimBg) {
        this->addChild(m_dimBg);
    }

    // Capture flow: hide self, capture framebuffer, animate preview.
    this->setVisible(false);
    if (auto* director = CCDirector::get(); director && director->getScheduler()) {
        director->getScheduler()->scheduleSelector(
            schedule_selector(CaptureOverlay::triggerCaptureProcess),
            this,
            0.05f,
            0,
            0.0f,
            false
        );
    } else {
        triggerCaptureProcess(0.f);
    }

    return true;
}

void CaptureOverlay::onExit() {
    if (auto* director = CCDirector::get(); director && director->getScheduler()) {
        auto* scheduler = director->getScheduler();
        scheduler->unscheduleSelector(
            schedule_selector(CaptureOverlay::checkSceneChanged), this);
        scheduler->unscheduleSelector(
            schedule_selector(CaptureOverlay::onAutoDismiss), this);
        scheduler->unscheduleSelector(
            schedule_selector(CaptureOverlay::triggerCaptureProcess), this);
    }
    CCLayer::onExit();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

void CaptureOverlay::startAutoDismissTimer() {
    auto* director = CCDirector::get();
    if (!director || !director->getScheduler()) return;
    auto* scheduler = director->getScheduler();
    scheduler->unscheduleSelector(
        schedule_selector(CaptureOverlay::onAutoDismiss), this);
    scheduler->scheduleSelector(
        schedule_selector(CaptureOverlay::onAutoDismiss),
        this, kAutoDismissSeconds, 0, 0.f, false
    );
}

void CaptureOverlay::onAutoDismiss(float) {
    this->onClose(nullptr);
}

namespace {
void collectVisibleAlerts(cocos2d::CCScene* scene, std::vector<CCNode*>& out) {
    if (!scene) return;
    for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
        if (!child) continue;
        auto* alert = typeinfo_cast<FLAlertLayer*>(child);
        if (alert && alert->isVisible()) out.push_back(child);
    }
}
}

void CaptureOverlay::checkSceneChanged(float) {
    auto* director = CCDirector::get();
    if (!director) return;
    auto* running = director->getRunningScene();
    // Pointer identity only; the old scene may already be freed.
    if (running != m_ownerScene) {
        m_isClosing = true;
        this->removeFromParent();
        return;
    }

    // A popup opened after the card docked: get out of its way immediately
    // instead of floating on top of it until the auto-dismiss fires.
    if (m_docked && !m_isClosing) {
        std::vector<CCNode*> alerts;
        collectVisibleAlerts(running, alerts);
        for (auto* a : alerts) {
            if (std::find(m_alertsAtDock.begin(), m_alertsAtDock.end(), a)
                    == m_alertsAtDock.end()) {
                this->onClose(nullptr);
                return;
            }
        }
    }
}

void CaptureOverlay::registerWithTouchDispatcher() {
    CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, -505, true);
}

bool CaptureOverlay::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    if (m_isClosing) return false;
    auto touchPos = touch->getLocation();

    if (m_previewCard && m_previewCard->isVisible()) {
        auto localPos = m_previewCard->convertToNodeSpace(touchPos);
        auto cardSize = m_previewCard->getContentSize();
        // Extend rect upward to include buttons above card (kBtnRowH + margin)
        constexpr float kBtnRowH = 22.f;
        CCRect rect = CCRect{-5.f, -5.f, cardSize.width + 10.f, cardSize.height + kBtnRowH + 10.f};
        if (!rect.containsPoint(localPos)) {
            this->onClose(nullptr);
            // Don't swallow: the click that dismisses the card must still reach
            // whatever UI is underneath (popups opened after the capture, etc).
            return false;
        }
        return true;
    }
    // Capture still in flight / fly animation: keep swallowing briefly.
    return true;
}

void CaptureOverlay::onClose(CCObject* sender) {
    if (m_isClosing) return;
    m_isClosing = true;

    if (auto* director = CCDirector::get(); director && director->getScheduler()) {
        director->getScheduler()->unscheduleSelector(
            schedule_selector(CaptureOverlay::onAutoDismiss), this);
    }

    // Disable touch so user can't interact during close animation
    this->setTouchEnabled(false);

    // Case 1: Preview card is showing (post-capture state)
    if (m_previewCard && m_previewCard->isVisible()) {
        // Fade out buttons first (fast)
        if (m_previewMenu) {
            auto* children = m_previewMenu->getChildren();
            if (children) {
                for (auto* child : CCArrayExt<CCNode*>(children)) {
                    if (auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(child)) {
                        if (auto* img = btn->getNormalImage()) {
                            img->runAction(CCFadeTo::create(0.15f, 0));
                        }
                    }
                }
            }
        }

        // Fade out the card background
        if (m_flyCardBg) {
            m_flyCardBg->runAction(CCFadeTo::create(0.3f, 0));
        }

        // Also fade out the dim backdrop alongside the card.
        if (m_dimBg) {
            m_dimBg->runAction(CCFadeTo::create(0.3f, 0));
        }

        // Card shrinks and slides slightly down-right, then remove
        auto* scaleDown = CCScaleTo::create(0.35f, 0.3f);
        auto* moveOff = CCMoveBy::create(0.35f, {20.f, -15.f});
        auto* spawn = CCSpawn::create(scaleDown, moveOff, nullptr);
        auto* ease = CCEaseExponentialIn::create(spawn);
        auto* callback = CCCallFunc::create(this, callfunc_selector(CaptureOverlay::finishClose));

        m_previewCard->runAction(CCSequence::create(ease, callback, nullptr));
        return;
    }

    // Case 2: No preview yet (e.g. capture failed). Fade the dim and remove.
    if (m_dimBg) {
        m_dimBg->runAction(CCFadeTo::create(0.2f, 0));
    }
    finishClose();
}

void CaptureOverlay::finishClose() {
    this->removeFromParent();
}

void CaptureOverlay::triggerCaptureProcess(float) {
    if (auto* director = CCDirector::get(); director && director->getScheduler()) {
        director->getScheduler()->unscheduleSelector(
            schedule_selector(CaptureOverlay::triggerCaptureProcess), this
        );
    }

    geode::WeakRef<CaptureOverlay> weakSelf = this;
    FramebufferCapture::requestCapture(
        0,
        [weakSelf](bool success, cocos2d::CCTexture2D* texture, std::shared_ptr<uint8_t> rgba, int w, int h) {
            auto self = weakSelf.lock();
            if (!self) return;  // overlay destroyed during capture
            // Restore visibility of overlay
            self->setVisible(true);

            if (success && texture) {
                self->m_capturedTexture = texture;
                self->m_rgbaBuffer = rgba;
                self->m_captureWidth = w;
                self->m_captureHeight = h;

                // Copy capture to clipboard in background thread
                {
                    auto rgbaCopy = rgba;
                    int const cw = w;
                    int const ch = h;
                    paimon::ThreadTracker::get().spawn([rgbaCopy, cw, ch]() {
                        geode::utils::thread::setName("Paimon Clipboard Image");
                        if (paimon::isRuntimeShuttingDown()) return;
                        bool const ok = paimon::copyRGBAToClipboard(rgbaCopy.get(), cw, ch);
                        Loader::get()->queueInMainThread([ok]() {
                            if (paimon::isRuntimeShuttingDown()) return;
                            if (ok) {
                                PaimonNotify::create(
                                    "Captura copiada al portapapeles",
                                    NotificationIcon::Success
                                )->show();
                            } else {
                                PaimonNotify::create(
                                    "No se pudo copiar al portapapeles",
                                    NotificationIcon::Warning
                                )->show();
                            }
                        });
                    });
                }

                // Fade in dim, then run the fly/scale transition.
                if (self->m_dimBg) {
                    self->m_dimBg->runAction(CCFadeTo::create(0.25f, 120));
                }

                // Run flying/scale transition
                self->playFlyToBottomRightAnimation();
            } else {
                PopupManager::get().alert(
                    "Error",
                    "No se pudo realizar la captura de pantalla."
                ).showInstant();
                self->onClose(nullptr);
            }
        },
        nullptr, // Entire screen
        false,   // hidePlayer1
        false    // hidePlayer2
    );
}

void CaptureOverlay::playFlyToBottomRightAnimation() {
    if (!m_capturedTexture) return;

    auto winSize = CCDirector::get()->getWinSize();

    // Card dimensions
    constexpr float kCardW = 190.f;
    constexpr float kCardH = 110.f;
    constexpr float kMargin = 15.f;
    constexpr float kCornerR = 8.f;

    // Create preview card at final position
    m_previewCard = CCNode::create();
    m_previewCard->setContentSize({kCardW, kCardH});
    m_previewCard->setAnchorPoint({1.f, 0.f});
    m_previewCard->ignoreAnchorPointForPosition(false);
    m_previewCard->setPosition({winSize.width - kMargin, kMargin});
    this->addChild(m_previewCard, 5);

    // Solid dark background with rounded corners (starts transparent, fades in)
    m_flyCardBg = paimon::SpriteHelper::createColorPanel(
        kCardW, kCardH, cocos2d::ccColor3B{20, 20, 25}, 230, kCornerR
    );
    if (m_flyCardBg) {
        m_flyCardBg->setPosition({0.f, 0.f});
        m_flyCardBg->setOpacity(0);
        m_previewCard->addChild(m_flyCardBg, 0);
        // Fade in background during the fly animation
        m_flyCardBg->runAction(CCFadeTo::create(0.5f, 255));
    }

    // Clipped preview image with rounded corners
    if (m_capturedTexture) {
        float clipW = kCardW - 6.f;
        float clipH = kCardH - 6.f;

        auto* stencil = paimon::SpriteHelper::createRoundedRectStencil(
            clipW, clipH, kCornerR
        );

        m_flyClipNode = CCClippingNode::create(stencil);
        m_flyClipNode->setAlphaThreshold(0.05f);
        m_flyClipNode->setContentSize({clipW, clipH});
        m_flyClipNode->setAnchorPoint({0.5f, 0.5f});
        m_flyClipNode->ignoreAnchorPointForPosition(false);
        m_flyClipNode->setPosition({kCardW / 2.f, kCardH / 2.f});
        m_previewCard->addChild(m_flyClipNode, 1);

        // Preview sprite inside clipping node
        auto* cardPreviewSpr = CCSprite::createWithTexture(m_capturedTexture.data());
        if (cardPreviewSpr) {
            float imgW = m_capturedTexture->getContentSize().width;
            float imgH = m_capturedTexture->getContentSize().height;

            // Scale to fill clip area
            float coverScale = std::max(clipW / imgW, clipH / imgH);
            cardPreviewSpr->setScale(coverScale);
            cardPreviewSpr->setAnchorPoint({0.5f, 0.5f});
            cardPreviewSpr->setPosition({clipW / 2.f, clipH / 2.f});
            m_flyClipNode->addChild(cardPreviewSpr);
        }
    }

    // Card center at final position
    float cardWorldCX = winSize.width - kMargin - kCardW / 2.f;
    float cardWorldCY = kMargin + kCardH / 2.f;

    float startScale = std::max(winSize.width / kCardW, winSize.height / kCardH) * 1.1f;

    float targetX = winSize.width - kMargin;
    float targetY = kMargin;

    // Offset start so card center aligns with screen center
    float startX = targetX + (winSize.width / 2.f - cardWorldCX) * 1.0f;
    float startY = targetY + (winSize.height / 2.f - cardWorldCY) * 1.0f;

    m_previewCard->setPosition({startX, startY});
    m_previewCard->setScale(startScale);

    // Hide flying preview sprite (using clipped version instead)
    if (m_previewSprite) {
        m_previewSprite->removeFromParent();
        m_previewSprite = nullptr;
    }

    // Animate to final position
    auto* moveTo = CCMoveTo::create(0.7f, {targetX, targetY});
    auto* scaleTo = CCScaleTo::create(0.7f, 1.0f);
    auto* spawn = CCSpawn::create(moveTo, scaleTo, nullptr);
    auto* ease = CCEaseExponentialOut::create(spawn);

    auto* callback = CCCallFunc::create(this, callfunc_selector(CaptureOverlay::revealPreviewControls));

    m_previewCard->runAction(CCSequence::create(ease, callback, nullptr));
}

void CaptureOverlay::revealPreviewControls() {
    if (!m_previewCard) return;

    constexpr float kCardW = 190.f;
    constexpr float kCardH = 110.f;
    constexpr float kBtnRowH = 22.f;

    // Buttons above the card frame
    m_previewMenu = CCMenu::create();
    m_previewMenu->setPosition({0.f, 0.f});
    m_previewCard->addChild(m_previewMenu, 10);

    // Start all buttons invisible
    auto setButtonInvisible = [](CCMenuItemSpriteExtra* btn) {
        if (!btn) return;
        if (auto* img = btn->getNormalImage()) {
            if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(img)) {
                rgba->setOpacity(0);
            }
        }
    };

    // Download Button (above card, right area)
    auto* dlSpr = CCSprite::createWithSpriteFrameName("GJ_downloadBtn_001.png");
    dlSpr->setScale(0.65f);
    auto* dlBtn = CCMenuItemSpriteExtra::create(
        dlSpr, this, menu_selector(CaptureOverlay::onDownload)
    );
    dlBtn->setPosition({kCardW - 60.f, kCardH + kBtnRowH / 2.f + 2.f});
    setButtonInvisible(dlBtn);
    m_previewMenu->addChild(dlBtn);

    // Folder Button
    auto* folderSpr = CCSprite::createWithSpriteFrameName("gj_folderBtn_001.png");
    folderSpr->setScale(0.6f);
    auto* folderCircle = CircleButtonSprite::create(
        folderSpr,
        CircleBaseColor::Green,
        CircleBaseSize::Medium
    );
    CCNode* folderBtnSpr = folderCircle ? static_cast<CCNode*>(folderCircle) : static_cast<CCNode*>(folderSpr);
    folderBtnSpr->setScale(0.5f);
    auto* folderBtn = CCMenuItemSpriteExtra::create(
        folderBtnSpr, this, menu_selector(CaptureOverlay::onOpenFolder)
    );
    folderBtn->setPosition({kCardW - 25.f, kCardH + kBtnRowH / 2.f + 2.f});
    setButtonInvisible(folderBtn);
    m_previewMenu->addChild(folderBtn);

    // Close button (above card, left area)
    auto* dismissSpr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
    dismissSpr->setScale(0.4f);
    auto* dismissBtn = CCMenuItemSpriteExtra::create(
        dismissSpr, this, menu_selector(CaptureOverlay::onClose)
    );
    dismissBtn->setPosition({12.f, kCardH + kBtnRowH / 2.f + 2.f});
    setButtonInvisible(dismissBtn);
    m_previewMenu->addChild(dismissBtn);

    // Fade in buttons
    float fadeDelay = 0.05f;
    float fadeDuration = 0.3f;

    auto fadeInBtn = [&](CCMenuItemSpriteExtra* btn, float delay) {
        if (!btn) return;
        if (auto* img = btn->getNormalImage()) {
            if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(img)) {
                rgba->setOpacity(0);
            }
            img->runAction(CCSequence::create(
                CCDelayTime::create(delay),
                CCFadeTo::create(fadeDuration, 255),
                nullptr
            ));
        }
    };

    fadeInBtn(dismissBtn, fadeDelay);
    fadeInBtn(dlBtn, fadeDelay + 0.05f);
    fadeInBtn(folderBtn, fadeDelay + 0.1f);

    // Card is docked: release the fullscreen dim so the game stays fully
    // visible and the preview behaves like a transient toast.
    if (m_dimBg) {
        m_dimBg->stopAllActions();
        m_dimBg->runAction(CCFadeTo::create(0.3f, 0));
    }

    m_docked = true;
    m_alertsAtDock.clear();
    if (auto* director = CCDirector::get()) {
        collectVisibleAlerts(director->getRunningScene(), m_alertsAtDock);
    }
    startAutoDismissTimer();
}

void CaptureOverlay::onDownload(CCObject* sender) {
    if (!m_rgbaBuffer) {
        PaimonNotify::create("No hay datos de captura disponibles.", NotificationIcon::Error)->show();
        return;
    }

    auto capturesDir = Mod::get()->getSaveDir() / "captures";
    std::error_code ec;
    if (!std::filesystem::exists(capturesDir, ec)) {
        std::filesystem::create_directories(capturesDir, ec);
        if (ec) {
            PaimonNotify::create("Error al crear carpeta de capturas.", NotificationIcon::Error)->show();
            return;
        }
    }

    // Generate filename using timestamp
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf;
    #ifdef GEODE_IS_WINDOWS
    localtime_s(&tmBuf, &in_time_t);
    #else
    localtime_r(&in_time_t, &tmBuf);
    #endif

    std::stringstream ss;
    ss << "screenshot_" << std::put_time(&tmBuf, "%Y%m%d_%H%M%S") << ".png";
    auto filePath = capturesDir / ss.str();

    // Copy buffer for background thread
    size_t dataSize = static_cast<size_t>(m_captureWidth) * m_captureHeight * 4;
    std::shared_ptr<uint8_t> bufCopy(new uint8_t[dataSize], std::default_delete<uint8_t[]>());
    std::memcpy(bufCopy.get(), m_rgbaBuffer.get(), dataSize);
    int w = m_captureWidth, h = m_captureHeight;

    // Show feedback
    PaimonNotify::create("Guardando captura...", NotificationIcon::Info)->show();

    // The user is interacting with the card; give it a fresh dismiss window.
    startAutoDismissTimer();

    // Save PNG in background thread to avoid stutters
    paimon::ThreadTracker::get().spawn([bufCopy, w, h, filePath]() {
        geode::utils::thread::setName("Paimon Capture Save");
        if (paimon::isRuntimeShuttingDown()) return;
        if (ImageConverter::saveRGBAToPNG(bufCopy.get(), w, h, filePath)) {
            if (paimon::isRuntimeShuttingDown()) return;
            Loader::get()->queueInMainThread([filePath]() {
                if (paimon::isRuntimeShuttingDown()) return;
                PaimonNotify::create("Captura de pantalla guardada con exito!", NotificationIcon::Success)->show();
            });
        } else {
            if (paimon::isRuntimeShuttingDown()) return;
            Loader::get()->queueInMainThread([]() {
                if (paimon::isRuntimeShuttingDown()) return;
                PaimonNotify::create("Error al guardar la captura.", NotificationIcon::Error)->show();
            });
        }
    });
}

void CaptureOverlay::onOpenFolder(CCObject* sender) {
    startAutoDismissTimer();

    auto capturesDir = Mod::get()->getSaveDir() / "captures";
    std::error_code ec;
    if (!std::filesystem::exists(capturesDir, ec)) {
        std::filesystem::create_directories(capturesDir, ec);
    }

    auto pathStr = geode::utils::string::pathToString(capturesDir);

#ifdef GEODE_IS_WINDOWS
    ShellExecuteA(nullptr, "open", pathStr.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(GEODE_IS_MACOS)
    std::string cmd = "open \"" + pathStr + "\"";
    std::system(cmd.c_str());
#else
    PaimonNotify::create("Carpeta: " + pathStr, NotificationIcon::Info)->show();
    return;
#endif

    PaimonNotify::create("Carpeta de capturas abierta.", NotificationIcon::Success)->show();
}
