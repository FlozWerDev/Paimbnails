#include <Geode/modify/PauseLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/loader/Event.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>

#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../features/capture/ui/CapturePreviewPopup.hpp"
#include "../features/thumbnails/services/ThumbsRegistry.hpp"
#include "../features/capture/services/FramebufferCapture.hpp"
#include "../utils/DominantColors.hpp"
#include "../features/thumbnails/services/LevelColors.hpp"
#include "../utils/Localization.hpp"
#include "../features/moderation/services/PendingQueue.hpp"
#include "../utils/Assets.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/ImageConverter.hpp"
#include "../utils/PaimonLoadingOverlay.hpp"
#include "../utils/FileDialog.hpp"
#include "../features/moderation/services/ModeratorUtils.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include <Geode/binding/LoadingCircle.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include "../utils/PaimonNotification.hpp"

#include "../utils/SpriteHelper.hpp"

using namespace geode::prelude;

static std::vector<uint8_t> convertRGBAtoRGB(const uint8_t* rgba, int w, int h) {
    const size_t pixelCount = static_cast<size_t>(w) * h;
    std::vector<uint8_t> rgb(pixelCount * 3);
    for (size_t i = 0; i < pixelCount; ++i) {
        rgb[i*3 + 0] = rgba[i*4 + 0];
        rgb[i*3 + 1] = rgba[i*4 + 1];
        rgb[i*3 + 2] = rgba[i*4 + 2];
    }
    return rgb;
}

static CCSprite* tryCreateIcon() {
    auto spr = CCSprite::createWithSpriteFrameName("GJ_everyplayBtn_001.png");
    if (!paimon::SpriteHelper::isValidSprite(spr)) {
        spr = CCSprite::create("paim_capturadora.png"_spr);
    }
    if (!paimon::SpriteHelper::isValidSprite(spr)) {
        spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_checkOn_001.png");
    }
    if (!spr) spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_button_01.png");
    if (spr) {
        constexpr float targetSize = 35.0f;
        float currentSize = std::max(spr->getContentSize().width, spr->getContentSize().height);
        if (currentSize > 0.0f) spr->setScale(targetSize / currentSize);
    }
    return spr;
}

class $modify(PaimonPauseLayer, PauseLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityAfterPost("PauseLayer::customSetup", "geode.node-ids");
    }

    struct Fields {
        bool m_fileDialogOpen = false;
        bool m_captureInProgress = false;
    };
    $override
    void customSetup() {
        PauseLayer::customSetup();

        log::info("[PauseLayer] customSetup");

        if (!Mod::get()->getSettingValue<bool>("enable-thumbnail-taking")) {
            log::debug("Thumbnail taking disabled in settings");
            return;
        }

        auto playLayer = PlayLayer::get();
        if (!playLayer) {
                        // si id falla, usa nombre de clase
            return;
        }

        if (!playLayer->m_level) {
            log::warn("Level not available in PlayLayer");
            return;
        }

        if (playLayer->m_level->m_levelID <= 0) {
            log::debug("Level ID is {} (not saving thumbnails for this level)", playLayer->m_level->m_levelID.value());
            return;
        }

        auto findButtonMenu = [this](char const* id, bool rightSide) -> CCMenu* {
            if (auto byId = typeinfo_cast<CCMenu*>(this->getChildByID(id))) {
                return byId;
            }
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            CCMenu* best = nullptr;
            float bestScore = 0.f;
            for (auto* node : CCArrayExt<CCNode*>(this->getChildren())) {
                auto menu = typeinfo_cast<CCMenu*>(node);
                if (!menu) continue;
                float x = menu->getPositionX();
                bool sideMatch = rightSide ? (x > winSize.width * 0.5f) : (x < winSize.width * 0.5f);
                if (!sideMatch) continue;
                float score = menu->getChildrenCount();
                if (!best || score > bestScore) {
                    best = menu;
                    bestScore = score;
                }
            }
            return best;
        };

        auto rightMenu = findButtonMenu("right-button-menu", true);
        if (!rightMenu) {
            log::error("Right button menu not found in PauseLayer (including fallback)");
            return;
        }

        auto spr = tryCreateIcon();
            if (!spr) {
                log::error("Failed to create button sprite");
                return;
            }

            auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(PaimonPauseLayer::onScreenshot));
            if (!btn) {
                log::error("Failed to create menu button");
                return;
            }

            btn->setID("thumbnail-capture-button"_spr);
            rightMenu->addChild(btn);
            rightMenu->updateLayout();

            // anade boton para elegir archivo (icono de carpeta)
            {
                auto selectSpr = Assets::loadButtonSprite(
                    "pause-select-file",
                    "frame:accountBtn_myLevels_001.png",
                    []() {
                        if (auto spr = paimon::SpriteHelper::safeCreateWithFrameName("accountBtn_myLevels_001.png")) return spr;
                        return paimon::SpriteHelper::safeCreateWithFrameName("GJ_button_01.png");
                    }
                );

                if (selectSpr) {
                    float targetSize = 30.0f;
                    float currentSize = std::max(selectSpr->getContentSize().width, selectSpr->getContentSize().height);

                    if (currentSize > 0) {
                        float scale = targetSize / currentSize;
                        selectSpr->setScale(scale);
                    }

                    auto selectBtn = CCMenuItemSpriteExtra::create(
                        selectSpr,
                        this,
                        menu_selector(PaimonPauseLayer::onSelectPNGFile)
                    );
                    if (selectBtn) {
                        selectBtn->setID("thumbnail-select-button"_spr);
                        rightMenu->addChild(selectBtn);
                        rightMenu->updateLayout();

                        log::debug("[PauseLayer] Select-file button added");
                    }
                }
            }

            // reconecta boton nativo
            // busca items de camara
            auto rewireScreenshotInMenu = [this](CCNode* menu){
                if (!menu) return;
                CCArray* arr = menu->getChildren();
                if (!arr) return;

                for (auto* obj : CCArrayExt<CCObject*>(arr)) {
                    auto* node = typeinfo_cast<CCNode*>(obj);
                    if (!node) continue;
                    std::string id = node->getID();
                    auto idL = geode::utils::string::toLower(id);
                    bool looksLikeCamera = (!idL.empty() && (idL.find("camera") != std::string::npos || idL.find("screenshot") != std::string::npos));
                    if (auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(node)) {
                        // usa heuristica de nombre de clase
                        if (!looksLikeCamera) {
                            if (auto* normal = item->getNormalImage()) {
                                auto cls = std::string(typeid(*normal).name());
                                auto clsL = geode::utils::string::toLower(cls);
                                if (clsL.find("camera") != std::string::npos || clsL.find("screenshot") != std::string::npos) {
                                    looksLikeCamera = true;
                                }
                            }
                        }

                        if (looksLikeCamera) {
                            log::info("[PauseLayer] Rewiring native capture button '{}' to onScreenshot", id);
                            item->setTarget(this, menu_selector(PaimonPauseLayer::onScreenshot));
                        }
                    }
                }
            };

            // prueba ambos menus
            rewireScreenshotInMenu(findButtonMenu("right-button-menu", true));
            rewireScreenshotInMenu(findButtonMenu("left-button-menu", false));

            // no llama updateLayout para mantener posiciones
            log::info("Thumbnail capture + extra buttons added successfully");
    }

    void onScreenshot(CCObject*) {
        log::info("[PauseLayer] Capture button pressed; hiding pause menu");
        if (m_fields->m_captureInProgress) {
            log::warn("[PauseLayer] Capture already in progress, ignoring duplicate request");
            return;
        }

        auto pl = PlayLayer::get();
        if (!pl) {
            log::error("[PauseLayer] PlayLayer not available");
            PaimonNotify::create(Localization::get().getString("pause.playlayer_error").c_str(), NotificationIcon::Error)->show();
            return;
        }

        // Oculta menu de pausa temporalmente
        this->setVisible(false);
        m_fields->m_captureInProgress = true;

        // Muestra circulo de carga
        showLoadingOverlay();
        // Guard rail: restaura UI si el callback no vuelve
        this->scheduleOnce(schedule_selector(PaimonPauseLayer::captureSafetyRestore), 8.0f);

        // Programa captura y restaura menu
        auto scheduler = CCDirector::sharedDirector()->getScheduler();
        scheduler->scheduleSelector(
            schedule_selector(PaimonPauseLayer::performCaptureAndRestore),
            this,
            0.05f,
            0,
            0.0f,
            false
        );
    }

    void showLoadingOverlay() {
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;
        if (auto existing = scene->getChildByID("paimon-loading-overlay"_spr)) {
            existing->removeFromParentAndCleanup(true);
        }

        auto overlay = PaimonLoadingOverlay::create("Loading...", 40.f);
        overlay->show(scene, 10000);
    }

    void reShowOverlay(float dt) {
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;
        auto overlay = scene->getChildByID("paimon-loading-overlay"_spr);
        if (overlay) overlay->setVisible(true);
    }

    void removeLoadingOverlay() {
        auto scheduler = CCDirector::sharedDirector()->getScheduler();
        scheduler->unscheduleSelector(
            schedule_selector(PaimonPauseLayer::reShowOverlay), this
        );
        scheduler->unscheduleSelector(
            schedule_selector(PaimonPauseLayer::captureSafetyRestore), this
        );

        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;
        if (auto overlay = typeinfo_cast<PaimonLoadingOverlay*>(scene->getChildByID("paimon-loading-overlay"_spr))) {
            overlay->dismiss();
        }
    }

    void captureSafetyRestore(float) {
        if (!m_fields->m_captureInProgress) return;
        log::warn("[PauseLayer] Capture watchdog restored UI state");
        m_fields->m_captureInProgress = false;
        removeLoadingOverlay();
        this->setVisible(true);
        PaimonNotify::create(Localization::get().getString("pause.capture_error").c_str(), NotificationIcon::Warning)->show();
    }

    void performCaptureAndRestore(float dt) {
        log::info("[PauseLayer] Performing capture");
        CCDirector::sharedDirector()->getScheduler()->unscheduleSelector(
            schedule_selector(PaimonPauseLayer::performCaptureAndRestore), this
        );

            auto* pl = PlayLayer::get();
            if (!pl || !pl->m_level) {
                log::error("[PauseLayer] PlayLayer or level not available for capture");
                PaimonNotify::create(Localization::get().getString("pause.capture_error").c_str(), NotificationIcon::Error)->show();
                removeLoadingOverlay();
                this->setVisible(true);
                return;
            }

            int levelID = pl->m_level->m_levelID;

            // Oculta overlay para captura limpia
            auto scene = CCDirector::sharedDirector()->getRunningScene();
            if (scene) {
                auto overlay = scene->getChildByID("paimon-loading-overlay"_spr);
                if (overlay) overlay->setVisible(false);
            }

            // Muestra overlay en siguiente frame
            CCDirector::sharedDirector()->getScheduler()->scheduleSelector(
                schedule_selector(PaimonPauseLayer::reShowOverlay),
                this, 0.0f, 0, 0.0f, false
            );

            // Ref<> para seguridad de memoria
            Ref<PauseLayer> safeRef = this;

            // Usa FramebufferCapture
            FramebufferCapture::requestCapture(levelID, [safeRef, levelID](bool success, CCTexture2D* texture, std::shared_ptr<uint8_t> rgbData, int width, int height) {
                Loader::get()->queueInMainThread([safeRef, success, texture, rgbData, width, height, levelID]() {
                    auto* self = static_cast<PaimonPauseLayer*>(safeRef.data());
                    if (!self->getParent()) {
                        self->m_fields->m_captureInProgress = false;
                        return;
                    }
                    self->removeLoadingOverlay();
                    self->m_fields->m_captureInProgress = false;

                    if (success && texture && rgbData) {
                        log::info("[PauseLayer] Capture successful: {}x{}", width, height);

                        // muestra popup de previsualizacion
                        auto popup = CapturePreviewPopup::create(
                            texture,
                            levelID,
                            rgbData,
                            width,
                            height,
                            // Callback al aceptar: verifica moderador y sube
                            [](bool accepted, int lvlID, std::shared_ptr<uint8_t> buf, int w, int h, std::string mode, std::string replaceId) {
                                if (!accepted || !buf) {
                                    log::info("[PauseLayer] Thumbnail rejected or invalid buffer");
                                    return;
                                }

                                log::info("[PauseLayer] Thumbnail accepted for level {}", lvlID);

                                // Obtiene nombre de usuario para subir
                                std::string username;
                                int accountID = 0;
                                auto* gm = GameManager::get();
                                if (gm) {
                                    username = gm->m_playerName;
                                    if (auto* am = GJAccountManager::get()) {
                                        accountID = am->m_accountID;
                                    }
                                }

                                if (username.empty()) {
                                    log::warn("[PauseLayer] No username available");
                                    PaimonNotify::create(Localization::get().getString("profile.username_error").c_str(), NotificationIcon::Error)->show();
                                    return;
                                }

                                // Convierte a PNG en memoria
                                std::vector<uint8_t> pngData;
                                if (!ImageConverter::rgbaToPngBuffer(buf.get(), w, h, pngData)) {
                                    log::error("[PauseLayer] Failed to encode PNG in memory");
                                    PaimonNotify::create(Localization::get().getString("capture.save_png_error").c_str(), NotificationIcon::Error)->show();
                                    return;
                                }

                                if (accountID <= 0) {
                                    PaimonNotify::create(Localization::get().getString("level.account_required").c_str(), NotificationIcon::Error)->show();
                                    return;
                                }

                                // Single upload — server handles mod check + routing (live vs pending)
                                PaimonNotify::create(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info)->show();

                                ThumbnailAPI::get().uploadThumbnail(lvlID, pngData, username, [lvlID, username](bool success, std::string const& msg) {
                                    if (success) {
                                        bool isPending = (msg.find("pending") != std::string::npos || msg.find("verification") != std::string::npos);
                                        if (isPending) {
                                            PendingQueue::get().addOrBump(lvlID, PendingCategory::Verify, username, {}, false);
                                            PaimonNotify::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Success)->show();
                                        } else {
                                            PendingQueue::get().removeForLevel(lvlID);
                                            PaimonNotify::create(Localization::get().getString("capture.upload_success").c_str(), NotificationIcon::Success)->show();
                                        }
                                        log::info("[PauseLayer] Upload result for level {}: {}", lvlID, msg);
                                    } else {
                                        PaimonNotify::create(Localization::get().getString("capture.upload_error").c_str(), NotificationIcon::Error)->show();
                                        log::error("[PauseLayer] Upload failed: {}", msg);
                                    }
                                });
                            },
                            // Callback de recaptura
                            nullptr,
                            false,
                            PaimonUtils::isUserModerator()
                        );

                        if (popup) {
                            popup->show();
                        }
                    } else {
                        log::error("[PauseLayer] Capture failed");
                        PaimonNotify::create(Localization::get().getString("pause.capture_error").c_str(), NotificationIcon::Error)->show();
                    }

                    // Restaura menu de pausa
                    safeRef->setVisible(true);
                    log::info("[PauseLayer] Pause menu restored after capture");
                });
            });

    }

    $override
    void onExit() {
        m_fields->m_captureInProgress = false;
        m_fields->m_fileDialogOpen = false;
        auto scheduler = CCDirector::sharedDirector()->getScheduler();
        scheduler->unscheduleSelector(
            schedule_selector(PaimonPauseLayer::performCaptureAndRestore), this
        );
        removeLoadingOverlay();
        PauseLayer::onExit();
    }

    void restorePauseMenu(float dt) {
        this->setVisible(true);
        log::info("[PauseLayer] Pause menu restored");
    }

    void processSelectedFile(std::filesystem::path selectedPath, int levelID) {
        log::info("[PauseLayer] Selected file: {}", geode::utils::string::pathToString(selectedPath));

        // Decide formato por extension
        std::string ext = geode::utils::string::pathToString(selectedPath.extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        // MP4/MOV/M4V: solo mods/admins
        if (ext == ".mp4" || ext == ".mov" || ext == ".m4v") {
            std::ifstream videoFile(selectedPath, std::ios::binary | std::ios::ate);
            if (!videoFile) {
                log::error("[PauseLayer] Could not open video file");
                PaimonNotify::create(Localization::get().getString("pause.video_open_error").c_str(), NotificationIcon::Error)->show();
                return;
            }
            size_t fileSize = static_cast<size_t>(videoFile.tellg());
            if (fileSize > 50 * 1024 * 1024) {
                PaimonNotify::create(Localization::get().getString("pause.video_too_large").c_str(), NotificationIcon::Error)->show();
                return;
            }
            videoFile.seekg(0, std::ios::beg);
            std::vector<uint8_t> mp4Data(fileSize);
            videoFile.read(reinterpret_cast<char*>(mp4Data.data()), fileSize);
            videoFile.close();

            log::info("[PauseLayer] Video file read ({} bytes)", fileSize);

            std::string username;
            int accountID = 0;
            if (auto* gm = GameManager::get()) {
                username = gm->m_playerName;
                if (auto* am = GJAccountManager::get()) accountID = am->m_accountID;
            }
            if (username.empty() || accountID <= 0) {
                PaimonNotify::create(Localization::get().getString("level.account_required").c_str(), NotificationIcon::Error)->show();
                return;
            }

            // Single upload — server handles mod check + routing (live vs pending)
            PaimonNotify::create(Localization::get().getString("pause.video_uploading").c_str(), NotificationIcon::Loading)->show();
            ThumbnailAPI::get().uploadVideo(levelID, mp4Data, username, [levelID, username](bool ok, std::string const& msg) {
                if (ok) {
                    bool isPending = (msg.find("pending") != std::string::npos || msg.find("verification") != std::string::npos);
                    if (isPending) {
                        PendingQueue::get().addOrBump(levelID, PendingCategory::Verify, username, {}, false);
                        PaimonNotify::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Success)->show();
                    } else {
                        PendingQueue::get().removeForLevel(levelID);
                        PaimonNotify::create(Localization::get().getString("pause.video_success").c_str(), NotificationIcon::Success)->show();
                    }
                } else {
                    PaimonNotify::create(Localization::get().getString("pause.video_upload_error").c_str(), NotificationIcon::Error)->show();
                    log::error("[PauseLayer] Video upload failed: {}", msg);
                }
            });
            return;
        }

        if (ext == ".gif") {
            // Previsualiza GIF y permite subir
            std::ifstream gifFile(selectedPath, std::ios::binary | std::ios::ate);
                if (!gifFile) {
                    log::error("[PauseLayer] Could not open GIF file");
                    PaimonNotify::create(Localization::get().getString("pause.gif_open_error").c_str(), NotificationIcon::Error)->show();
                    return;
                }
                size_t size = static_cast<size_t>(gifFile.tellg());
                gifFile.seekg(0, std::ios::beg);
                std::vector<uint8_t> gifData(size);
                gifFile.read(reinterpret_cast<char*>(gifData.data()), size);
                gifFile.close();

                // Usa CCImage desde memoria (RAII)
                auto ccRelease = [](CCImage* p) { if (p) p->release(); };
                auto imageGuard = std::unique_ptr<CCImage, decltype(ccRelease)>(new CCImage(), ccRelease);
                bool loaded = imageGuard->initWithImageData(
                    const_cast<void*>(static_cast<const void*>(gifData.data())),
                    gifData.size()
                );

                if (!loaded) {
                    PaimonNotify::create(Localization::get().getString("pause.gif_read_error").c_str(), NotificationIcon::Error)->show();
                    return;
                }

                int width = imageGuard->getWidth();
                int height = imageGuard->getHeight();

                if (width <= 0 || height <= 0) {
                    PaimonNotify::create(Localization::get().getString("pause.gif_read_error").c_str(), NotificationIcon::Error)->show();
                    return;
                }

                CCTexture2D* texture = new CCTexture2D();
                bool ok = texture->initWithImage(imageGuard.get());

                if (!ok) {
                    texture->release();
                    PaimonNotify::create(Localization::get().getString("pause.gif_texture_error").c_str(), NotificationIcon::Error)->show();
                    return;
                }
                texture->setAntiAliasTexParameters();

                // Obtiene pixeles con CCRenderTexture
                auto renderTex = CCRenderTexture::create(width, height, kCCTexture2DPixelFormat_RGBA8888);
                if (!renderTex) {
                    PaimonNotify::create(Localization::get().getString("pause.render_error").c_str(), NotificationIcon::Error)->show();
                    return;
                }

                renderTex->begin();
                auto sprite = CCSprite::createWithTexture(texture);
                sprite->setPosition(ccp(width/2, height/2));
                sprite->visit();
                renderTex->end();

                // Lee datos RGBA
                auto renderedImage = renderTex->newCCImage(false);
                if (!renderedImage) {
                    PaimonNotify::create(Localization::get().getString("pause.render_read_error").c_str(), NotificationIcon::Error)->show();
                    return;
                }

                auto imageData = renderedImage->getData();
                size_t rgbaSize = static_cast<size_t>(width) * height * 4;
                std::shared_ptr<uint8_t> rgbaData(new uint8_t[rgbaSize], std::default_delete<uint8_t[]>());

                // Copia RGBA directamente
                std::memcpy(rgbaData.get(), imageData, rgbaSize);

                renderedImage->release();

                // Muestra preview y sube GIF si acepta
                auto popup = CapturePreviewPopup::create(
                    texture,
                    levelID,
                    rgbaData,
                    width,
                    height,
                    [levelID, gifData = std::move(gifData)](bool accepted, int lvlID, std::shared_ptr<uint8_t> buf, int w, int h, std::string mode, std::string replaceId) mutable {
                        if (!accepted) {
                            log::info("[PauseLayer] User cancelled GIF preview");
                            return;
                        }

                        // Extrae colores dominantes del primer frame
                        auto rgbBuf = convertRGBAtoRGB(buf.get(), w, h);
                        auto pair = DominantColors::extract(rgbBuf.data(), w, h);
                        ccColor3B A{pair.first.r, pair.first.g, pair.first.b};
                        ccColor3B B{pair.second.r, pair.second.g, pair.second.b};
                        LevelColors::get().set(lvlID, A, B);

                        ThumbsRegistry::get().mark(ThumbKind::Level, lvlID, false);

                        // Obtiene usuario y verifica mod
                        std::string username;
                        int accountID = 0;
                        if (auto* gm = GameManager::get()) {
                            username = gm->m_playerName;
                            if (auto* am = GJAccountManager::get()) {
                                accountID = am->m_accountID;
                            }
                        }
                        if (username.empty()) {
                            PaimonNotify::create(Localization::get().getString("profile.username_error").c_str(), NotificationIcon::Error)->show();
                            return;
                        }
                        if (accountID <= 0) {
                            PaimonNotify::create(Localization::get().getString("level.account_required").c_str(), NotificationIcon::Error)->show();
                            return;
                        }

                        // Single upload — server handles mod check + routing (live vs pending)
                        PaimonNotify::create(Localization::get().getString("pause.gif_uploading").c_str(), NotificationIcon::Loading)->show();
                        ThumbnailAPI::get().uploadGIF(lvlID, gifData, username, [lvlID, username](bool ok, std::string const& msg){
                            if (ok) {
                                bool isPending = (msg.find("pending") != std::string::npos || msg.find("verification") != std::string::npos);
                                if (isPending) {
                                    PendingQueue::get().addOrBump(lvlID, PendingCategory::Verify, username, {}, false);
                                    PaimonNotify::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Success)->show();
                                } else {
                                    PendingQueue::get().removeForLevel(lvlID);
                                    PaimonNotify::create(Localization::get().getString("pause.gif_uploaded").c_str(), NotificationIcon::Success)->show();
                                }
                            } else {
                                PaimonNotify::create(Localization::get().getString("pause.gif_upload_error").c_str(), NotificationIcon::Error)->show();
                            }
                        });
                    }
                );

                if (popup) {
                    texture->autorelease();
                    popup->show();
                } else {
                    log::error("[PauseLayer] Failed to create GIF preview popup");
                    texture->release();
                }

            return; // Detiene flujo PNG
        }

        // Lee PNG completo en memoria
        std::ifstream pngFile(selectedPath, std::ios::binary | std::ios::ate);
        if (!pngFile) {
            log::error("[PauseLayer] Could not open PNG file");
            PaimonNotify::create(Localization::get().getString("pause.file_open_error").c_str(), NotificationIcon::Error)->show();
            return;
        }

        size_t fileSize = (size_t)pngFile.tellg();
        pngFile.seekg(0, std::ios::beg);
        std::vector<uint8_t> pngData(fileSize);
        pngFile.read(reinterpret_cast<char*>(pngData.data()), fileSize);
        pngFile.close();

        log::info("[PauseLayer] PNG file read ({} bytes)", fileSize);

        // Carga imagen en CCImage (RAII)
        auto ccRelease = [](CCImage* p) { if (p) p->release(); };
        auto imgGuard = std::unique_ptr<CCImage, decltype(ccRelease)>(new CCImage(), ccRelease);
        if (!imgGuard->initWithImageData(pngData.data(), fileSize)) {
            log::error("[PauseLayer] Failed to decode selected image file");
            PaimonNotify::create(Localization::get().getString("pause.png_invalid").c_str(), NotificationIcon::Error)->show();
            return;
        }

        int width = imgGuard->getWidth();
        int height = imgGuard->getHeight();
        unsigned char* imgData = imgGuard->getData();

        if (!imgData) {
            log::error("[PauseLayer] Failed to get image pixel data");
            PaimonNotify::create(Localization::get().getString("pause.process_image_error").c_str(), NotificationIcon::Error)->show();
            return;
        }

        // Lee datos de imagen
        int bpp = imgGuard->getBitsPerComponent();
        bool hasAlpha = imgGuard->hasAlpha();

        log::info("[PauseLayer] Image loaded {}x{} (BPP: {}, Alpha: {})",
                  width, height, bpp, hasAlpha);

        // Calcula tamano esperado
        int bytesPerPixel = hasAlpha ? 4 : 3;
        size_t expectedDataSize = static_cast<size_t>(width) * height * bytesPerPixel;

        // Convierte si es necesario
        size_t rgbaSize = static_cast<size_t>(width) * height * 4;
        std::vector<uint8_t> rgbaPixels(rgbaSize);

        if (hasAlpha) {
            memcpy(rgbaPixels.data(), imgData, std::min(rgbaSize, expectedDataSize));
            log::info("[PauseLayer] Alpha detected; copied {} bytes", expectedDataSize);
        } else {
            log::info("[PauseLayer] RGB detected; converting to RGBA ({} -> {} bytes)",
                      expectedDataSize, rgbaSize);
            for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
                rgbaPixels[i*4 + 0] = imgData[i*3 + 0]; // R
                rgbaPixels[i*4 + 1] = imgData[i*3 + 1]; // G
                rgbaPixels[i*4 + 2] = imgData[i*3 + 2]; // B
                rgbaPixels[i*4 + 3] = 255;              // opacidad maxima
            }
        }

        imgGuard.reset(); // Libera CCImage temprano

        log::debug("[PauseLayer] RGBA data ready ({} bytes)", rgbaSize);

        // Crea textura como en captura
        CCTexture2D* texture = new CCTexture2D();
        if (!texture) {
            log::error("[PauseLayer] Failed to create CCTexture2D");
            PaimonNotify::create(Localization::get().getString("pause.create_texture_error").c_str(), NotificationIcon::Error)->show();
            return;
        }

        // Inicia textura con datos
        if (!texture->initWithData(
            rgbaPixels.data(),
            kCCTexture2DPixelFormat_RGBA8888,
            width,
            height,
            CCSize(width, height)
        )) {
            log::error("[PauseLayer] Failed to initialize texture from data");
            texture->release();
            PaimonNotify::create(Localization::get().getString("pause.init_texture_error").c_str(), NotificationIcon::Error)->show();
            return;
        }

        // Mejora parametros de textura
        texture->setAntiAliasTexParameters();

        // new CCTexture2D() refcount=1; popup hace retain/release

        log::info("[PauseLayer] Texture created successfully using FramebufferCapture method");

        // Envoltorio para datos
        std::shared_ptr<uint8_t> rgbaData(new uint8_t[rgbaSize], std::default_delete<uint8_t[]>());
        std::memcpy(rgbaData.get(), rgbaPixels.data(), rgbaSize);

        log::info("[PauseLayer] Showing preview with RGBA data");

        // muestra popup
        auto popup = CapturePreviewPopup::create(
            texture,
            levelID,
            rgbaData,
            width,
            height,
            [levelID](bool accepted, int lvlID, std::shared_ptr<uint8_t> buf, int w, int h, std::string mode, std::string replaceId) {
                if (accepted) {
                    log::info("[PauseLayer] User accepted image loaded from disk");

                    auto rgbBuf = convertRGBAtoRGB(buf.get(), w, h);

                    // extrae colores dominantes
                    auto pair = DominantColors::extract(rgbBuf.data(), w, h);
                    ccColor3B A{pair.first.r, pair.first.g, pair.first.b};
                    ccColor3B B{pair.second.r, pair.second.g, pair.second.b};

                    LevelColors::get().set(lvlID, A, B);

                    ThumbsRegistry::get().mark(ThumbKind::Level, lvlID, false);

                    // Convierte a PNG en memoria
                    if (buf) {
                        std::vector<uint8_t> pngData;
                        if (!ImageConverter::rgbaToPngBuffer(buf.get(), w, h, pngData)) {
                            log::error("[PauseLayer] Failed to encode PNG in memory");
                            PaimonNotify::create(Localization::get().getString("capture.save_png_error").c_str(), NotificationIcon::Error)->show();
                        } else {

                                std::string username;
                                int accountID = 0;
                                if (auto gm = GameManager::get()) {
                                    username = gm->m_playerName;
                                    if (auto* am = GJAccountManager::get()) {
                                        accountID = am->m_accountID;
                                    }
                                }

                                if (!username.empty() && accountID > 0) {
                                    // Single upload — server handles mod check + routing (live vs pending)
                                    PaimonNotify::create(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info)->show();

                                    ThumbnailAPI::get().uploadThumbnail(lvlID, pngData, username, [lvlID, username](bool s, std::string const& msg){
                                        if (s) {
                                            bool isPending = (msg.find("pending") != std::string::npos || msg.find("verification") != std::string::npos);
                                            if (isPending) {
                                                PendingQueue::get().addOrBump(lvlID, PendingCategory::Verify, username, {}, false);
                                                PaimonNotify::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Success)->show();
                                            } else {
                                                PendingQueue::get().removeForLevel(lvlID);
                                                PaimonNotify::create(Localization::get().getString("capture.upload_success").c_str(), NotificationIcon::Success)->show();
                                            }
                                        } else {
                                            PaimonNotify::create(Localization::get().getString("capture.upload_error").c_str(), NotificationIcon::Error)->show();
                                        }
                                    });
                                } else {
                                    PaimonNotify::create(Localization::get().getString("level.account_required").c_str(), NotificationIcon::Error)->show();
                                }
                        }
                    }
                } else {
                    log::info("[PauseLayer] User cancelled image preview");
                }
            }
        );

        if (popup) {
            popup->show();
        } else {
            log::error("[PauseLayer] Failed to create preview popup");
            texture->release();
        }

    }

    void onSelectPNGFile(CCObject*) {
        log::info("[PauseLayer] Select file button pressed");

        if (m_fields->m_fileDialogOpen) {
            log::warn("[PauseLayer] File dialog already open, ignoring");
            return;
        }

        auto pl = PlayLayer::get();
            if (!pl || !pl->m_level) {
                log::error("[PauseLayer] PlayLayer or level not available");
                return;
            }

            int levelID = pl->m_level->m_levelID;

            m_fields->m_fileDialogOpen = true;
            WeakRef<PaimonPauseLayer> self = this;

            auto pickerCb = [self, levelID](geode::Result<std::optional<std::filesystem::path>> result) {
                auto layer = self.lock();
                if (!layer) return;
                layer->m_fields->m_fileDialogOpen = false;
                auto pathOpt = std::move(result).unwrapOr(std::nullopt);
                if (!pathOpt || pathOpt->empty()) return;
                layer->processSelectedFile(std::move(*pathOpt), levelID);
            };

            // Mods/admins pueden subir MP4; usuarios normales solo imagenes
            bool isMod = PaimonUtils::isUserModerator() && !HttpClient::get().getModCode().empty();
            if (isMod) {
                pt::pickMedia(pickerCb);
            } else {
                pt::pickImage(pickerCb);
            }
    }
};
