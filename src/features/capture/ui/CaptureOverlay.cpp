#include "CaptureOverlay.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/ImageConverter.hpp"
#include "../../../utils/ClipboardImage.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../services/FramebufferCapture.hpp"
#include "../../volume-scroll/ui/ScrollKeybindsPopup.hpp"

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
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
    auto* scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return;

    if (s_instance) {
        // Toggle behavior: if already open, close it
        s_instance->onClose(nullptr);
        return;
    }

    auto* overlay = CaptureOverlay::create();
    overlay->setID("CaptureOverlay");
    scene->addChild(overlay, 99999);
}

void CaptureOverlay::hideOverlay() {
    if (s_instance) {
        s_instance->onClose(nullptr);
    }
}

bool CaptureOverlay::init() {
    if (!CCLayer::init()) return false;
    s_instance = this;

    this->setTouchEnabled(true);
    this->setKeypadEnabled(true);

    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // Semi-transparent dim background
    auto* bg = CCLayerColor::create({0, 0, 0, 120});
    this->addChild(bg);

    // Create the floating card in the center
    m_centerCard = CCNode::create();
    m_centerCard->setPosition(winSize / 2);
    this->addChild(m_centerCard);

    // Solid dark card background — Geode-canónico: NineSlice GJ_square01
    // tintado oscuro, con fallback automático a CCScale9Sprite y CCDrawNode
    // si HappyTextures u otro pack remueve el frame.
    cocos2d::CCNodeRGBA* cardBg = paimon::SpriteHelper::safeCreateNineSliceFromFile("GJ_square01.png");
    if (!cardBg) cardBg = paimon::SpriteHelper::safeCreateScale9("GJ_square01.png");
    if (!cardBg) cardBg = paimon::SpriteHelper::createDarkPanel(200.f, 130.f, 220, 6.f);
    cardBg->setContentSize({200.f, 130.f});
    cardBg->setColor({20, 20, 25});
    cardBg->setOpacity(220);
    m_centerCard->addChild(cardBg);

    // Capture Button centered (top half)
    auto* captureBtnSpr = ButtonSprite::create("Capturar", "goldFont.fnt", "GJ_button_01.png", .8f);
    auto* captureBtn = CCMenuItemSpriteExtra::create(
        captureBtnSpr, this, menu_selector(CaptureOverlay::onCapture)
    );
    captureBtn->setPosition({0.f, 5.f});

    // Shortcuts Button (bottom half) — abre el popup de configuracion de
    // modificadores para el scroll de volumen (Ctrl/Shift/Alt + scroll).
    auto* shortcutsBtnSpr = ButtonSprite::create("Atajos", "bigFont.fnt", "GJ_button_04.png", .65f);
    auto* shortcutsBtn = CCMenuItemSpriteExtra::create(
        shortcutsBtnSpr, this, menu_selector(CaptureOverlay::onOpenShortcuts)
    );
    shortcutsBtn->setPosition({0.f, -32.f});

    // Close Button (Small X in top right of card)
    auto* closeSpr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
    auto* closeBtn = CCMenuItemSpriteExtra::create(
        closeSpr, this, menu_selector(CaptureOverlay::onClose)
    );
    closeBtn->setPosition({90.f, 55.f});
    closeBtn->setScale(0.7f);

    // Title label
    auto* titleLabel = CCLabelBMFont::create("Captura de Pantalla", "bigFont.fnt");
    titleLabel->setScale(0.4f);
    titleLabel->setPosition({0.f, 43.f});
    m_centerCard->addChild(titleLabel);

    m_centerMenu = CCMenu::create();
    m_centerMenu->setPosition({0.f, 0.f});
    m_centerMenu->addChild(captureBtn);
    m_centerMenu->addChild(shortcutsBtn);
    m_centerMenu->addChild(closeBtn);
    m_centerCard->addChild(m_centerMenu);

    // ── Entrance animation: scale from 0.5 to 1.0 with bounce ──
    m_centerCard->setScale(0.5f);
    m_centerCard->runAction(CCEaseBackOut::create(CCScaleTo::create(0.35f, 1.0f)));
    // Fade in the bg
    bg->setOpacity(0);
    bg->runAction(CCFadeTo::create(0.25f, 120));

    return true;
}

void CaptureOverlay::onExit() {
    CCLayer::onExit();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

void CaptureOverlay::registerWithTouchDispatcher() {
    CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this, -505, true);
}

bool CaptureOverlay::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    auto touchPos = touch->getLocation();

    if (m_centerCard && m_centerCard->isVisible()) {
        auto localPos = m_centerCard->convertToNodeSpace(touchPos);
        // Card es 200x130, anchorPoint en el centro → bounds [-100,-65 .. 100,65]
        CCRect rect = CCRect{-100.f, -65.f, 200.f, 130.f};
        if (!rect.containsPoint(localPos)) {
            this->onClose(nullptr);
        }
    } else if (m_previewCard && m_previewCard->isVisible()) {
        auto localPos = m_previewCard->convertToNodeSpace(touchPos);
        auto cardSize = m_previewCard->getContentSize();
        // Extend rect upward to include buttons above card (kBtnRowH + margin)
        constexpr float kBtnRowH = 22.f;
        CCRect rect = CCRect{-5.f, -5.f, cardSize.width + 10.f, cardSize.height + kBtnRowH + 10.f};
        if (!rect.containsPoint(localPos)) {
            this->onClose(nullptr);
        }
    }
    return true;
}

void CaptureOverlay::onClose(CCObject* sender) {
    if (m_isClosing) return;
    m_isClosing = true;

    // Disable touch so user can't interact during close animation
    this->setTouchEnabled(false);

    // ── Case 1: Preview card is showing (post-capture state) ──
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

        // Card shrinks and slides slightly down-right, then remove
        auto* scaleDown = CCScaleTo::create(0.35f, 0.3f);
        auto* moveOff = CCMoveBy::create(0.35f, {20.f, -15.f});
        auto* spawn = CCSpawn::create(scaleDown, moveOff, nullptr);
        auto* ease = CCEaseExponentialIn::create(spawn);
        auto* callback = CCCallFunc::create(this, callfunc_selector(CaptureOverlay::finishClose));

        m_previewCard->runAction(CCSequence::create(ease, callback, nullptr));
        return;
    }

    // ── Case 2: Center card is showing (initial capture dialog) ──
    if (m_centerCard && m_centerCard->isVisible()) {
        auto* scaleDown = CCScaleTo::create(0.2f, 0.0f);
        auto* ease = CCEaseBackIn::create(scaleDown);
        auto* callback = CCCallFunc::create(this, callfunc_selector(CaptureOverlay::finishClose));

        m_centerCard->runAction(CCSequence::create(ease, callback, nullptr));

        // Also fade out the dim background
        auto* children = this->getChildren();
        if (children) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (auto* layerColor = typeinfo_cast<CCLayerColor*>(child)) {
                    layerColor->runAction(CCFadeTo::create(0.2f, 0));
                }
            }
        }
        return;
    }

    // ── Fallback: no card visible, just remove ──
    finishClose();
}

void CaptureOverlay::finishClose() {
    this->removeFromParent();
}

void CaptureOverlay::onCapture(CCObject* sender) {
    // Hide overlay so it's not captured in the screen
    this->setVisible(false);

    // Queue in main thread to ensure rendering loop has completed the hide pass.
    // Use WeakRef to avoid dangling `this` if the user exits the scene before the
    // queued callback runs.
    geode::WeakRef<CaptureOverlay> weakSelf = this;
    geode::Loader::get()->queueInMainThread([weakSelf]() {
        if (auto self = weakSelf.lock()) {
            self->triggerCaptureProcess();
        }
    });
}

void CaptureOverlay::triggerCaptureProcess() {
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

                // Copiar la captura al portapapeles para que el usuario
                // pueda pegarla directamente en Discord, navegador, etc.
                // La conversion RGBA→BGR + escritura al clipboard se hace
                // en un thread aparte para no bloquear el frame, y notificamos
                // de vuelta en el main thread cuando termine.
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

                // Hide the central selection menu
                if (self->m_centerCard) {
                    self->m_centerCard->setVisible(false);
                }

                // Run flying/scale transition
                self->playFlyToBottomRightAnimation();
            } else {
                FLAlertLayer::create("Error", "No se pudo realizar la captura de pantalla.", "OK")->show();
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

    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // ── Card dimensions ──
    constexpr float kCardW = 190.f;
    constexpr float kCardH = 110.f;
    constexpr float kMargin = 15.f;
    constexpr float kCornerR = 8.f;

    // ── Create preview card container at final position ──
    m_previewCard = CCNode::create();
    m_previewCard->setContentSize({kCardW, kCardH});
    m_previewCard->setAnchorPoint({1.f, 0.f});
    m_previewCard->ignoreAnchorPointForPosition(false);
    m_previewCard->setPosition({winSize.width - kMargin, kMargin});
    this->addChild(m_previewCard, 5);

    // ── Solid dark background with rounded corners (starts transparent, fades in) ──
    // NineSlice canónico via createColorPanel — pasa NineSlice → Scale9 →
    // CCDrawNode automáticamente.
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

    // ── Clipped preview image with rounded corners (uses FULL card area) ──
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

        // Create the preview sprite inside the clipping node
        auto* cardPreviewSpr = CCSprite::createWithTexture(m_capturedTexture.data());
        if (cardPreviewSpr) {
            float imgW = m_capturedTexture->getContentSize().width;
            float imgH = m_capturedTexture->getContentSize().height;

            // Cover: scale to fill entire clip area
            float coverScale = std::max(clipW / imgW, clipH / imgH);
            cardPreviewSpr->setScale(coverScale);
            cardPreviewSpr->setAnchorPoint({0.5f, 0.5f});
            cardPreviewSpr->setPosition({clipW / 2.f, clipH / 2.f});
            m_flyClipNode->addChild(cardPreviewSpr);
        }
    }

    // ── Start state: card scaled up to fill screen, centered ──

    // The card's anchor is bottom-right, its world center when at final position:
    float cardWorldCX = winSize.width - kMargin - kCardW / 2.f;
    float cardWorldCY = kMargin + kCardH / 2.f;

    // Scale the card so it covers the entire window
    float startScale = std::max(winSize.width / kCardW, winSize.height / kCardH) * 1.1f;

    // Translate so the center of the scaled card aligns with screen center
    float targetX = winSize.width - kMargin;
    float targetY = kMargin;

    // Starting position: offset so the card center appears at screen center
    float startX = targetX + (winSize.width / 2.f - cardWorldCX) * 1.0f;
    float startY = targetY + (winSize.height / 2.f - cardWorldCY) * 1.0f;

    m_previewCard->setPosition({startX, startY});
    m_previewCard->setScale(startScale);

    // Also hide the flying preview sprite since we use the clipped version
    if (m_previewSprite) {
        m_previewSprite->removeFromParent();
        m_previewSprite = nullptr;
    }

    // ── Animate to final position with easing ──
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

    // ── Buttons OUTSIDE the card frame (above it) ──
    m_previewMenu = CCMenu::create();
    m_previewMenu->setPosition({0.f, 0.f});
    m_previewCard->addChild(m_previewMenu, 10);

    // Start all buttons invisible — set opacity on the sprite inside
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

    // ── Fade in all buttons smoothly ──
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

    // Show immediate feedback so user knows the save started
    PaimonNotify::create("Guardando captura...", NotificationIcon::Info)->show();

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
    auto capturesDir = Mod::get()->getSaveDir() / "captures";
    std::error_code ec;
    if (!std::filesystem::exists(capturesDir, ec)) {
        std::filesystem::create_directories(capturesDir, ec);
    }

    auto pathStr = capturesDir.string();

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

void CaptureOverlay::onOpenShortcuts(CCObject* sender) {
    // Cerramos el overlay y abrimos el popup de configuracion de atajos.
    // Lo encolamos en el main thread para evitar correr el create() en el
    // medio del touch dispatch (al cerrar el overlay se modifican children
    // de la escena, lo que podria invalidar iteradores en cocos).
    geode::Loader::get()->queueInMainThread([]() {
        if (auto* popup = paimon::volscroll::ScrollKeybindsPopup::create()) {
            popup->show();
        }
    });
    this->onClose(nullptr);
}
