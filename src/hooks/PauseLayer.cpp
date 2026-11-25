#include "../utils/FilesystemPath.hpp"
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/CCLayer.hpp>
#include "../framework/HookConventions.hpp"
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/loader/Event.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <filesystem>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include <utility>
#include <vector>

#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../features/capture/ui/CapturePreviewPopup.hpp"
#include "../features/thumbnails/services/ThumbsRegistry.hpp"
#include "../features/capture/services/FramebufferCapture.hpp"
#include "../features/twitch-requests/TwitchRequestManager.hpp"
#include "../features/twitch-requests/ui/WebFeedbackPopup.hpp"
#include "../utils/DominantColors.hpp"
#include "../features/thumbnails/services/LevelColors.hpp"
#include "../utils/Localization.hpp"
#include "../features/moderation/services/PendingQueue.hpp"
#include "../utils/Assets.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/LevelMetadata.hpp"
#include "../utils/ImageConverter.hpp"
#include "../utils/FormatDetect.hpp"
#include "../utils/ImageLoadHelper.hpp"
#include "../utils/PaimonLoadingOverlay.hpp"
#include "../utils/FileDialog.hpp"
#include "../features/moderation/services/ModeratorUtils.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include "../utils/ThreadTracker.hpp"
#include "../utils/HttpClient.hpp"
#include <Geode/binding/LoadingCircle.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include "../utils/PaimonNotification.hpp"

#include "../utils/ActivePauseLayer.hpp"
#include "../utils/PauseMenuNodes.hpp"
#include "../utils/SceneIdentity.hpp"

#include "../utils/SpriteHelper.hpp"

using namespace geode::prelude;

namespace {
std::pair<ccColor3B, ccColor3B> dominantPair(uint8_t const* rgba, int w, int h) {
    size_t const pixelCount = static_cast<size_t>(w) * h;
    std::vector<uint8_t> rgbBuf(pixelCount * 3);
    ImageConverter::rgbaToRgbFast(rgba, rgbBuf.data(), pixelCount);
    auto pair = DominantColors::extract(rgbBuf.data(), w, h);
    return {{pair.first.r, pair.first.g, pair.first.b}, {pair.second.r, pair.second.g, pair.second.b}};
}

std::optional<paimon::twitch::LevelRequest> feedbackRequestForLevel(int levelID) {
    for (auto const& request : paimon::twitch::TwitchRequestManager::get().requests()) {
        if (request.levelID == levelID && request.platform == paimon::twitch::Platform::Web
            && request.requesterVerified && request.requesterAccountID > 0
            && !request.webRequestID.empty()) return request;
    }
    return std::nullopt;
}
}

// encode and analyze off-thread; call onmaindone on the main thread.
static void processAcceptedCaptureAsync(
    std::shared_ptr<uint8_t> buf, int w, int h, bool extractColors,
    std::function<void(bool encoded, std::vector<uint8_t> pngData,
                       ccColor3B colorA, ccColor3B colorB)> onMainDone)
{
    paimon::ThreadTracker::get().spawn(
        [buf = std::move(buf), w, h, extractColors,
         onMainDone = std::move(onMainDone)]() mutable {
            geode::utils::thread::setName("Paimon Capture Upload");
            if (paimon::isRuntimeShuttingDown()) return;

            ccColor3B A{255, 255, 255}, B{255, 255, 255};
            if (extractColors) {
                auto colors = dominantPair(buf.get(), w, h);
                A = colors.first;
                B = colors.second;
            }

            std::vector<uint8_t> pngData;
            bool encoded = ImageConverter::rgbaToPngBuffer(
                buf.get(), static_cast<uint32_t>(w), static_cast<uint32_t>(h), pngData);

            Loader::get()->queueInMainThread(
                [encoded, pngData = std::move(pngData), A, B,
                 onMainDone = std::move(onMainDone)]() mutable {
                    if (paimon::isRuntimeShuttingDown()) return;
                    if (onMainDone) onMainDone(encoded, std::move(pngData), A, B);
                });
        });
}

static void handleUploadResult(bool success, std::string const& msg, int levelID,
                               std::string const& username,
                               char const* successKey, char const* errorKey);

static void showStaticImagePreview(std::vector<uint8_t> bytes, int levelID) {
    auto image = ImageLoadHelper::loadWithSTBFromMemory(bytes.data(), bytes.size());
    if (!image.success || !image.texture || !image.buffer) {
        log::error("[PauseLayer] Failed to load selected image: {}", image.error);
        PaimonNotify::show(Localization::get().getString("pause.png_invalid").c_str(), NotificationIcon::Error);
        return;
    }

    auto popup = CapturePreviewPopup::create(
        image.texture,
        levelID,
        image.buffer,
        image.width,
        image.height,
        [](bool accepted, int lvlID, std::shared_ptr<uint8_t> buf, int w, int h, std::string, std::string) {
            if (!accepted || !buf) {
                return;
            }

            std::string username;
            int accountID = 0;
            if (auto gm = GameManager::get()) {
                username = gm->m_playerName;
                if (auto* am = GJAccountManager::get()) {
                    accountID = am->m_accountID;
                }
            }
            if (username.empty() || accountID <= 0) {
                PaimonNotify::show(Localization::get().getString("level.account_required").c_str(), NotificationIcon::Error);
                return;
            }

            PaimonNotify::show(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info);

            processAcceptedCaptureAsync(buf, w, h, /*extractcolors=*/true,
                [lvlID, username](bool encoded, std::vector<uint8_t> pngData, ccColor3B A, ccColor3B B) {
                    LevelColors::get().set(lvlID, A, B);
                    ThumbsRegistry::get().mark(ThumbKind::Level, lvlID, false);

                    if (!encoded) {
                        log::error("[PauseLayer] Failed to encode PNG in memory");
                        PaimonNotify::show(Localization::get().getString("capture.save_png_error").c_str(), NotificationIcon::Error);
                        return;
                    }
                    std::string levelMeta;
                    if (auto* pl = PlayLayer::get()) levelMeta = paimon::collectLevelMetadata(pl->m_level);
                    ThumbnailAPI::get().uploadThumbnail(lvlID, pngData, username, [lvlID, username](bool s, std::string const& msg){
                        handleUploadResult(s, msg, lvlID, username,
                            "capture.upload_success", "capture.upload_error");
                    }, levelMeta);
                });
        }
    );

    image.texture->release();

    if (popup) {
        popup->show();
    } else {
        log::error("[PauseLayer] Failed to create preview popup");
    }
}

static CCSprite* tryCreateIcon() {
    auto spr = CCSprite::create("paim_capturadora.png"_spr);
    if (!paimon::SpriteHelper::isValidSprite(spr)) {
        spr = CCSprite::createWithSpriteFrameName("GJ_everyplayBtn_001.png");
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

static void handleUploadResult(bool success, std::string const& msg, int levelID,
                               std::string const& username,
                               char const* successKey, char const* errorKey) {
    if (!success) {
        PaimonNotify::show(Localization::get().getString(errorKey).c_str(), NotificationIcon::Error);
        return;
    }
    bool isPending = msg.find("pending") != std::string::npos
                  || msg.find("verification") != std::string::npos;
    if (isPending) {
        PendingQueue::get().addOrBump(levelID, PendingCategory::Verify, username, {}, false);
        PaimonNotify::show(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Success);
    } else {
        PendingQueue::get().removeForLevel(levelID);
        PaimonNotify::show(Localization::get().getString(successKey).c_str(), NotificationIcon::Success);
    }
}

class $modify(PaimonPauseLayer, PauseLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "PauseLayer::customSetup");
    }

    struct Fields {
        bool m_fileDialogOpen = false;
        bool m_captureInProgress = false;
        bool m_feedbackCaptureInProgress = false;
        bool m_capturePauseWasVisible = true;
        bool m_capturePauseTouchEnabled = true;
        bool m_closing = false;
        uint64_t m_captureGeneration = 0;
        paimon::NodeIdentity<PlayLayer> m_playLayer;
        WeakRef<PaimonLoadingOverlay> m_loadingOverlay;
    };

    void onPauseEnter() {
        m_fields->m_closing = false;
        incrementForcePrio();
        paimon::setPauseLayerClosing(this, false);
        paimon::setActivePauseLayer(this);
        schedule(schedule_selector(PaimonPauseLayer::synchronizePauseState));
    }

    $override
    void customSetup() {
        PauseLayer::customSetup();

        auto playLayer = PlayLayer::get();
        m_fields->m_playLayer = paimon::NodeIdentity(playLayer);
        if (!playLayer) return;

        paimon::setPauseZoomHidden(false);

        if (!playLayer->m_level) {
            log::warn("Level not available in PlayLayer");
            return;
        }

        if (playLayer->m_level->m_levelID <= 0) return;

        auto* rightMenu = paimon::findPauseButtonMenu(this, "right-button-menu");
        if (!rightMenu) {
            log::error("Right button menu not found in PauseLayer (including fallback)");
            return;
        }

        if (feedbackRequestForLevel(playLayer->m_level->m_levelID)
            && !rightMenu->getChildByID("web-request-feedback-button"_spr)) {
            auto* sprite = ButtonSprite::create("Feedback", "goldFont.fnt", "GJ_button_01.png", .8f);
            if (sprite) {
                sprite->setScale(.45f);
                auto* button = CCMenuItemSpriteExtra::create(sprite, this,
                    menu_selector(PaimonPauseLayer::onWebRequestFeedback));
                button->setID("web-request-feedback-button"_spr);
                rightMenu->addChild(button);
                rightMenu->updateLayout();
            }
        }

        if (!Mod::get()->getSettingValue<bool>("enable-thumbnail-taking")) return;

        // customSetup may repeat; repair either missing button independently.
        if (!rightMenu->getChildByID("thumbnail-capture-button"_spr)) {
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
            btn->setRotation(-90.f);
            rightMenu->addChild(btn);
            rightMenu->updateLayout();
        }

        if (!rightMenu->getChildByID("thumbnail-select-button"_spr)) {
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

                if (currentSize > 0) selectSpr->setScale(targetSize / currentSize);

                auto selectBtn = CCMenuItemSpriteExtra::create(
                    selectSpr,
                    this,
                    menu_selector(PaimonPauseLayer::onSelectPNGFile)
                );
                if (selectBtn) {
                    selectBtn->setID("thumbnail-select-button"_spr);
                    rightMenu->addChild(selectBtn);
                    rightMenu->updateLayout();
                }
            }
        }

    }

    bool canUsePauseLayer() {
        auto* play = PlayLayer::get();
        return !m_fields->m_closing && paimon::isPauseLayerOpen(this)
            && m_fields->m_playLayer.matches(play) && play->m_isPaused
            && paimon::findOpenPauseLayerInScene() == this;
    }

    bool canPresentCapturePopup() {
        if (!canUsePauseLayer()) return false;
        auto* scene = CCDirector::get()->getRunningScene();
        for (auto* node : CCArrayExt<CCNode*>(scene->getChildren())) {
            if (node == this || !node->isVisible()) continue;
            if (typeinfo_cast<FLAlertLayer*>(node) || typeinfo_cast<CCBlockLayer*>(node)
                || typeinfo_cast<DialogLayer*>(node)) return false;
        }
        return true;
    }

    void finishPauseCapture(bool cancelRequest, bool restoreUI) {
        ++m_fields->m_captureGeneration;
        bool const ownsCapture = m_fields->m_captureInProgress || m_fields->m_feedbackCaptureInProgress;
        m_fields->m_captureInProgress = false;
        m_fields->m_feedbackCaptureInProgress = false;
        if (ownsCapture) {
            if (cancelRequest) FramebufferCapture::cancelPending();
            paimon::setCaptureInProgress(false);
        }
        // leave native selectors and other mods' work on this layer intact.
        unschedule(schedule_selector(PaimonPauseLayer::performCaptureAndRestore));
        unschedule(schedule_selector(PaimonPauseLayer::performWebRequestCapture));
        unschedule(schedule_selector(PaimonPauseLayer::captureSafetyRestore));
        unschedule(schedule_selector(PaimonPauseLayer::webRequestCaptureTimeout));
        removeLoadingOverlay();
        if (ownsCapture && restoreUI && canUsePauseLayer()) {
            setVisible(m_fields->m_capturePauseWasVisible);
            setTouchEnabled(m_fields->m_capturePauseTouchEnabled);
        }
    }

    void stopPauseWork(bool restoreUI = false) {
        m_fields->m_fileDialogOpen = false;
        finishPauseCapture(true, restoreUI);
    }

    void hidePauseForCapture() {
        m_fields->m_capturePauseWasVisible = isVisible();
        m_fields->m_capturePauseTouchEnabled = isTouchEnabled();
        setVisible(false);
        setTouchEnabled(false);
    }

    void markClosing() {
        if (m_fields->m_closing) return;
        m_fields->m_closing = true;
        paimon::setPauseLayerClosing(this, true);
        unschedule(schedule_selector(PaimonPauseLayer::synchronizePauseState));
        // capture cancellation restores its saved playlayer transform. do this
        // before pause zoom resets it, or gameplay can resume with the old zoom.
        stopPauseWork();
        setVisible(false);
        setTouchEnabled(false);
        setKeypadEnabled(false);
        setKeyboardEnabled(false);
        if (paimon::getActivePauseLayer() == this) {
            paimon::clearActivePauseLayer(this);
            paimon::notifyPauseClosing();
        }
    }

    void synchronizePauseState(float) {
        if (m_fields->m_closing || !getParent() || !isRunning()) return;
        auto* director = CCDirector::get();
        if (!director || director->getNextScene()) return;
        auto* play = PlayLayer::get();
        if (m_fields->m_playLayer.matches(play) && play->m_isPaused) return;

        Ref<PaimonPauseLayer> keepAlive = this;
        markClosing();
        removeFromParentAndCleanup(true);
    }

    bool belongsToPlayLayer(PlayLayer* playLayer) {
        return m_fields->m_playLayer.matches(playLayer);
    }

    bool beginPauseAction(char const* action) {
        if (m_fields->m_closing) return false;
        bool const canUse = canUsePauseLayer();
        markClosing();
        if (!canUse) {
            log::warn("[PauseLayer] Ignoring {} on an inactive pause menu", action);
            removeFromParentAndCleanup(true);
        }
        return canUse;
    }

    void finishPauseAction() {
        if (!getParent()) return;
        auto* director = CCDirector::get();
        if (!director || director->getNextScene()) return;
        auto* play = PlayLayer::get();
        if (!m_fields->m_playLayer.matches(play) || !play->m_isPaused
            || paimon::findOpenPauseLayerInScene()) {
            removeFromParentAndCleanup(true);
            return;
        }
        // keep the menu usable if another hook rejects the action.
        onPauseEnter();
        setVisible(true);
        setTouchEnabled(true);
        setKeypadEnabled(true);
        setKeyboardEnabled(true);
    }

    void onWebRequestFeedback(CCObject*) {
        if (!canUsePauseLayer()) return;
        auto* play = PlayLayer::get();
        auto request = play && play->m_level
            ? feedbackRequestForLevel(play->m_level->m_levelID) : std::nullopt;
        if (!request || m_fields->m_fileDialogOpen || m_fields->m_feedbackCaptureInProgress || m_fields->m_captureInProgress
            || paimon::isCaptureInProgress()) return;
        auto validation = FramebufferCapture::validateCaptureConditions();
        if (!validation.canCapture) {
            PaimonNotify::show(validation.reason.c_str(), NotificationIcon::Warning);
            return;
        }
        m_fields->m_feedbackCaptureInProgress = true;
        ++m_fields->m_captureGeneration;
        paimon::setCaptureInProgress(true);
        hidePauseForCapture();
        showLoadingOverlay();
        scheduleOnce(schedule_selector(PaimonPauseLayer::performWebRequestCapture), .05f);
        scheduleOnce(schedule_selector(PaimonPauseLayer::webRequestCaptureTimeout), 8.f);
    }

    void webRequestCaptureTimeout(float) {
        if (!m_fields->m_feedbackCaptureInProgress) return;
        bool const canNotify = canUsePauseLayer();
        finishPauseCapture(true, true);
        if (canNotify) PaimonNotify::show("La captura no respondio", NotificationIcon::Warning);
    }

    void performWebRequestCapture(float) {
        auto* play = PlayLayer::get();
        auto request = play && play->m_level
            ? feedbackRequestForLevel(play->m_level->m_levelID) : std::nullopt;
        if (!m_fields->m_feedbackCaptureInProgress || !canUsePauseLayer() || !request) {
            webRequestCaptureTimeout(0.f);
            return;
        }
        // the capture service hides and restores the overlay at the render boundary.
        WeakRef<PauseLayer> weak = this;
        auto const generation = m_fields->m_captureGeneration;
        FramebufferCapture::requestCapture(request->levelID,
            [weak, generation, request = *request](bool success, CCTexture2D* texture,
                std::shared_ptr<uint8_t> rgba, int width, int height) mutable {
                Ref<CCTexture2D> held = texture;
                Loader::get()->queueInMainThread([weak, generation, request = std::move(request), success,
                    held, rgba, width, height]() mutable {
                    if (paimon::isRuntimeShuttingDown()) return;
                    auto locked = weak.lock();
                    if (!locked) return;
                    auto* self = static_cast<PaimonPauseLayer*>(locked.data());
                    if (generation != self->m_fields->m_captureGeneration
                        || !self->m_fields->m_feedbackCaptureInProgress) return;
                    self->finishPauseCapture(false, true);
                    if (!self->canPresentCapturePopup()) return;
                    if (!success || !held || !rgba) {
                        PaimonNotify::show("No se pudo capturar el nivel", NotificationIcon::Error);
                        return;
                    }
                    auto* popup = paimon::twitch::WebFeedbackPopup::create(
                        std::move(request), held.data(), rgba, width, height);
                    if (popup) popup->show();
                });
            });
    }

    void onScreenshot(CCObject*) {
        if (!canUsePauseLayer()) return;
        if (m_fields->m_fileDialogOpen || m_fields->m_feedbackCaptureInProgress || m_fields->m_captureInProgress) {
            log::warn("[PauseLayer] Capture already in progress, ignoring duplicate request");
            return;
        }

        // avoid racing playlayer's capture keybind or orphaning its callback
        if (paimon::isCaptureInProgress()) {
            log::warn("[PauseLayer] Captura por keybind ya en curso, ignorando boton");
            PaimonNotify::show(
                Localization::get().getString("pause.capture_busy").c_str(),
                NotificationIcon::Warning
            );
            return;
        }

        auto pl = PlayLayer::get();
        if (!pl) {
            log::error("[PauseLayer] PlayLayer not available");
            PaimonNotify::show(Localization::get().getString("pause.playlayer_error").c_str(), NotificationIcon::Error);
            return;
        }

        auto validation = FramebufferCapture::validateCaptureConditions();
        if (!validation.canCapture) {
            PaimonNotify::show(validation.reason.c_str(), NotificationIcon::Warning);
            return;
        }
        hidePauseForCapture();
        // prevent the zoom ticker from restoring it before swapbuffers().
        paimon::setCaptureInProgress(true);
        m_fields->m_captureInProgress = true;
        ++m_fields->m_captureGeneration;

        showLoadingOverlay();
        // restore the ui if the callback never returns
        this->scheduleOnce(schedule_selector(PaimonPauseLayer::captureSafetyRestore), 8.0f);

        auto* director = CCDirector::get();
        if (!director || !director->getScheduler()) {
            finishPauseCapture(true, true);
            return;
        }
        director->getScheduler()->scheduleSelector(
            schedule_selector(PaimonPauseLayer::performCaptureAndRestore),
            this,
            0.05f,
            0,
            0.0f,
            false
        );
    }

    void showLoadingOverlay() {
        auto* director = CCDirector::get();
        auto* scene = director ? director->getRunningScene() : nullptr;
        if (!scene) return;
        removeLoadingOverlay();
        if (auto* overlay = PaimonLoadingOverlay::create("Loading...", 40.f)) {
            m_fields->m_loadingOverlay = overlay;
            overlay->show(scene, 10000);
        }
    }

    void removeLoadingOverlay() {
        auto* director = CCDirector::get();
        if (!director) return;

        if (auto overlay = m_fields->m_loadingOverlay.lock()) {
            if (m_fields->m_closing) {
                overlay->setVisible(false);
                overlay->setTouchEnabled(false);
                // scene teardown may be iterating the overlay's siblings.
                if (!paimon::isRuntimeShuttingDown()) {
                    Loader::get()->queueInMainThread([overlay] {
                        if (overlay->getParent()) overlay->removeFromParentAndCleanup(true);
                    });
                }
            } else {
                overlay->dismiss();
            }
        }
        m_fields->m_loadingOverlay = nullptr;
    }

    void captureSafetyRestore(float) {
        if (!m_fields->m_captureInProgress) return;
        bool const canRestore = canUsePauseLayer();
        log::warn("[PauseLayer] Capture watchdog restored UI state");
        finishPauseCapture(true, true);
        if (!canRestore) return;
        PaimonNotify::show(Localization::get().getString("pause.capture_error").c_str(), NotificationIcon::Warning);
    }

    void performCaptureAndRestore(float) {
        if (!m_fields->m_captureInProgress) return;
        if (!canUsePauseLayer()) {
            finishPauseCapture(true, false);
            return;
        }
        auto* director = CCDirector::get();
        auto* scheduler = director ? director->getScheduler() : nullptr;
        if (!scheduler) {
            log::warn("[PauseLayer] Capture cancelled: scheduler unavailable");
            finishPauseCapture(true, true);
            return;
        }

        scheduler->unscheduleSelector(
            schedule_selector(PaimonPauseLayer::performCaptureAndRestore), this
        );

        auto* pl = PlayLayer::get();
        if (!pl || !pl->m_level) {
            log::error("[PauseLayer] PlayLayer or level not available for capture");
            PaimonNotify::show(Localization::get().getString("pause.capture_error").c_str(), NotificationIcon::Error);
            finishPauseCapture(true, true);
            return;
        }

        auto validation = FramebufferCapture::validateCaptureConditions();
        if (!validation.canCapture) {
            PaimonNotify::show(validation.reason.c_str(), NotificationIcon::Warning);
            finishPauseCapture(true, true);
            return;
        }

        int levelID = pl->m_level->m_levelID;

        // overlay visibility belongs to framebufferscapture until the shot completes.

        WeakRef<PauseLayer> weakRef = this;
        auto const generation = m_fields->m_captureGeneration;

            FramebufferCapture::requestCapture(levelID, [weakRef, generation, levelID](bool success, CCTexture2D* texture, std::shared_ptr<uint8_t> rgbData, int width, int height) {
                Ref<CCTexture2D> texRef = texture;
                Loader::get()->queueInMainThread([weakRef, generation, success, texRef, rgbData, width, height, levelID]() {
                    if (paimon::isRuntimeShuttingDown()) return;
                    CCTexture2D* texture = texRef.data();
                    auto locked = weakRef.lock();
                    if (!locked) return;
                    auto* self = static_cast<PaimonPauseLayer*>(locked.data());
                    if (generation != self->m_fields->m_captureGeneration
                        || !self->m_fields->m_captureInProgress) return;
                    self->finishPauseCapture(false, true);
                    if (!self->canPresentCapturePopup()) return;

                    if (success && texture && rgbData) {
                        auto popup = CapturePreviewPopup::create(
                            texture,
                            levelID,
                            rgbData,
                            width,
                            height,
                            [](bool accepted, int lvlID, std::shared_ptr<uint8_t> buf, int w, int h, std::string, std::string) {
                                if (!accepted || !buf) return;

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
                                    PaimonNotify::show(Localization::get().getString("profile.username_error").c_str(), NotificationIcon::Error);
                                    return;
                                }
                                if (accountID <= 0) {
                                    PaimonNotify::show(Localization::get().getString("level.account_required").c_str(), NotificationIcon::Error);
                                    return;
                                }

                                PaimonNotify::show(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info);

                                processAcceptedCaptureAsync(buf, w, h, /*extractcolors=*/false,
                                    [lvlID, username](bool encoded, std::vector<uint8_t> pngData, ccColor3B, ccColor3B) {
                                        if (!encoded) {
                                            log::error("[PauseLayer] Failed to encode PNG in memory");
                                            PaimonNotify::show(Localization::get().getString("capture.save_png_error").c_str(), NotificationIcon::Error);
                                            return;
                                        }
                                        std::string levelMeta;
                                        if (auto* pl = PlayLayer::get()) levelMeta = paimon::collectLevelMetadata(pl->m_level);
                                        ThumbnailAPI::get().uploadThumbnail(lvlID, pngData, username, [lvlID, username](bool success, std::string const& msg) {
                                            handleUploadResult(success, msg, lvlID, username,
                                                "capture.upload_success", "capture.upload_error");
                                            if (!success) {
                                                log::error("[PauseLayer] Upload failed: {}", msg);
                                            }
                                        }, levelMeta);
                                    });
                            },
                            nullptr,
                            false,
                            PaimonUtils::isUserModerator()
                        );

                        if (popup) {
                            popup->show();
                        }
                    } else {
                        log::error("[PauseLayer] Capture failed");
                        PaimonNotify::show(Localization::get().getString("pause.capture_error").c_str(), NotificationIcon::Error);
                    }

                });
            });

    }

    void onPauseExit() {
        markClosing();
        // async capture/file callbacks can retain a detached menu. do not keep
        // its forced touch priority registered until the destructor finally runs.
        decrementForcePrio();
    }

    void processSelectedFile(std::filesystem::path selectedPath, int levelID) {
        std::string ext = geode::utils::string::toLower(
            paimon::paths::toUtf8(selectedPath.extension()));

        if (ext == ".mp4" || ext == ".mov" || ext == ".m4v") {
            std::error_code fileError;
            auto const fileSize = std::filesystem::file_size(selectedPath, fileError);
            if (fileError) {
                log::error("[PauseLayer] Could not open video file");
                PaimonNotify::show(Localization::get().getString("pause.video_open_error").c_str(), NotificationIcon::Error);
                return;
            }
            if (fileSize > 50 * 1024 * 1024) {
                PaimonNotify::show(Localization::get().getString("pause.video_too_large").c_str(), NotificationIcon::Error);
                return;
            }
            // up to 50MB: read off the main thread; the rest needs no `this`.
            paimon::ThreadTracker::get().spawn([selectedPath, levelID] {
                geode::utils::thread::setName("PaimonPauseMediaRead");
                auto mp4Data = ImageLoadHelper::readBinaryFile(selectedPath, 50);
                Loader::get()->queueInMainThread([mp4Data = std::move(mp4Data), levelID]() mutable {
                    if (paimon::isRuntimeShuttingDown()) return;
                    if (mp4Data.empty()) {
                        log::error("[PauseLayer] Could not read video file");
                        PaimonNotify::show(Localization::get().getString("pause.video_open_error").c_str(), NotificationIcon::Error);
                        return;
                    }

                    if (!paimon::format::isMp4(mp4Data.data(), mp4Data.size())) {
                        log::error("[PauseLayer] Selected file is not a valid MP4/MOV");
                        PaimonNotify::show(Localization::get().getString("pause.video_invalid").c_str(), NotificationIcon::Error);
                        return;
                    }

                    std::string username;
                    int accountID = 0;
                    if (auto* gm = GameManager::get()) {
                        username = gm->m_playerName;
                        if (auto* am = GJAccountManager::get()) accountID = am->m_accountID;
                    }
                    if (username.empty() || accountID <= 0) {
                        PaimonNotify::show(Localization::get().getString("level.account_required").c_str(), NotificationIcon::Error);
                        return;
                    }

                    PaimonNotify::show(Localization::get().getString("pause.video_uploading").c_str(), NotificationIcon::Loading);
                    std::string levelMeta;
                    if (auto* pl = PlayLayer::get()) levelMeta = paimon::collectLevelMetadata(pl->m_level);
                    ThumbnailAPI::get().uploadVideo(levelID, mp4Data, username, [levelID, username](bool ok, std::string const& msg) {
                        handleUploadResult(ok, msg, levelID, username,
                            "pause.video_success", "pause.video_upload_error");
                        if (!ok) {
                            log::error("[PauseLayer] Video upload failed: {}", msg);
                        }
                    }, levelMeta);
                });
            });
            return;
        }

        if (ext == ".gif") {
            // up to 50MB: read off the main thread; the stb decode (texture
            // creation) stays on the main thread below.
            paimon::ThreadTracker::get().spawn([selectedPath, levelID] {
                geode::utils::thread::setName("PaimonPauseMediaRead");
                auto gifData = ImageLoadHelper::readBinaryFile(selectedPath, 50);
                Loader::get()->queueInMainThread([gifData = std::move(gifData), levelID]() mutable {
                    if (paimon::isRuntimeShuttingDown()) return;
                    if (gifData.empty()) {
                        log::error("[PauseLayer] Could not read GIF file");
                        PaimonNotify::show(Localization::get().getString("pause.gif_open_error").c_str(), NotificationIcon::Error);
                        return;
                    }

                    auto preview = ImageLoadHelper::loadWithSTBFromMemory(gifData.data(), gifData.size());
                    if (!preview.success || !preview.texture || !preview.buffer) {
                        log::error("[PauseLayer] Could not decode GIF preview: {}", preview.error);
                        PaimonNotify::show(Localization::get().getString("pause.gif_read_error").c_str(), NotificationIcon::Error);
                        return;
                    }

                    auto popup = CapturePreviewPopup::create(
                        preview.texture,
                        levelID,
                        preview.buffer,
                        preview.width,
                        preview.height,
                        [gifData = std::move(gifData)](bool accepted, int lvlID, std::shared_ptr<uint8_t> buf, int w, int h, std::string, std::string) mutable {
                            if (!accepted) {
                                return;
                            }

                            std::string username;
                            int accountID = 0;
                            if (auto* gm = GameManager::get()) {
                                username = gm->m_playerName;
                                if (auto* am = GJAccountManager::get()) {
                                    accountID = am->m_accountID;
                                }
                            }
                            if (username.empty()) {
                                PaimonNotify::show(Localization::get().getString("profile.username_error").c_str(), NotificationIcon::Error);
                                return;
                            }
                            if (accountID <= 0) {
                                PaimonNotify::show(Localization::get().getString("level.account_required").c_str(), NotificationIcon::Error);
                                return;
                            }

                            // extract dominant colors off-thread; lab clustering is expensive
                            if (buf && w > 0 && h > 0) {
                                paimon::ThreadTracker::get().spawn([lvlID, buf, w, h]() {
                                    geode::utils::thread::setName("PaimonDominantColors");
                                    if (paimon::isRuntimeShuttingDown()) return;
                                    auto dominant = dominantPair(buf.get(), w, h);
                                    ccColor3B A = dominant.first;
                                    ccColor3B B = dominant.second;
                                    Loader::get()->queueInMainThread([lvlID, A, B]() {
                                        if (paimon::isRuntimeShuttingDown()) return;
                                        LevelColors::get().set(lvlID, A, B);
                                    });
                                });
                            }

                            ThumbsRegistry::get().mark(ThumbKind::Level, lvlID, false);

                            PaimonNotify::show(Localization::get().getString("pause.gif_uploading").c_str(), NotificationIcon::Loading);
                            std::string levelMeta;
                            if (auto* pl = PlayLayer::get()) levelMeta = paimon::collectLevelMetadata(pl->m_level);
                            ThumbnailAPI::get().uploadGIF(lvlID, gifData, username, [lvlID, username](bool ok, std::string const& msg){
                                handleUploadResult(ok, msg, lvlID, username,
                                    "pause.gif_uploaded", "pause.gif_upload_error");
                            }, levelMeta);
                        }
                    );

                    preview.texture->release();

                    if (popup) {
                        popup->show();
                    } else {
                        log::error("[PauseLayer] Failed to create GIF preview popup");
                    }
                });
            });
            return;
        }

        paimon::ThreadTracker::get().spawn([selectedPath, levelID] {
            geode::utils::thread::setName("PaimonPauseMediaRead");
            auto bytes = ImageLoadHelper::readBinaryFile(selectedPath, 50);
            Loader::get()->queueInMainThread([bytes = std::move(bytes), levelID]() mutable {
                if (paimon::isRuntimeShuttingDown()) return;
                if (bytes.empty()) {
                    log::error("[PauseLayer] Could not read image file");
                    PaimonNotify::show(Localization::get().getString("pause.png_invalid").c_str(), NotificationIcon::Error);
                    return;
                }
                showStaticImagePreview(std::move(bytes), levelID);
            });
        });
    }

    void onSelectPNGFile(CCObject*) {
        if (!canUsePauseLayer()) return;
        if (m_fields->m_fileDialogOpen || m_fields->m_captureInProgress
            || m_fields->m_feedbackCaptureInProgress || paimon::isCaptureInProgress()) {
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
        auto const generation = ++m_fields->m_captureGeneration;
        WeakRef<PaimonPauseLayer> self = this;

        auto pickerCb = [self, levelID, generation](geode::Result<std::optional<std::filesystem::path>> result) {
            if (paimon::isRuntimeShuttingDown()) return;
            auto layer = self.lock();
            if (!layer) return;
            if (generation != layer->m_fields->m_captureGeneration || !layer->m_fields->m_fileDialogOpen) return;
            layer->m_fields->m_fileDialogOpen = false;
            if (!layer->canPresentCapturePopup()) return;
            auto pathOpt = std::move(result).unwrapOr(std::nullopt);
            if (!pathOpt || pathOpt->empty()) return;
            layer->processSelectedFile(std::move(*pathOpt), levelID);
        };

        bool isMod = PaimonUtils::isUserModerator()
            && (HttpClient::get().hasSessionToken() || !HttpClient::get().getModCode().empty());
        if (isMod) {
            pt::pickMedia(pickerCb);
        } else {
            pt::pickImage(pickerCb);
        }
    }

    // the overrides below share keepalive + begin/finish; only the base
    // call differs, and it must name PauseLayer:: explicitly (a member
    // pointer would dispatch virtually back into the override).
    template <typename Base>
    void forwardPauseAction(char const* name, CCObject* sender, Base&& base) {
        Ref<PaimonPauseLayer> keepAlive = this;
        if (!beginPauseAction(name)) return;
        base(sender);
        finishPauseAction();
    }

    void onResume(CCObject* sender) {
        forwardPauseAction("onResume", sender, [this](CCObject* s) { PauseLayer::onResume(s); });
    }

    void onRestart(CCObject* sender) {
        forwardPauseAction("onRestart", sender, [this](CCObject* s) { PauseLayer::onRestart(s); });
    }

    void onRestartFull(CCObject* sender) {
        forwardPauseAction("onRestartFull", sender, [this](CCObject* s) { PauseLayer::onRestartFull(s); });
    }

    void onNormalMode(CCObject* sender) {
        forwardPauseAction("onNormalMode", sender, [this](CCObject* s) { PauseLayer::onNormalMode(s); });
    }

    void onPracticeMode(CCObject* sender) {
        forwardPauseAction("onPracticeMode", sender, [this](CCObject* s) { PauseLayer::onPracticeMode(s); });
    }

    void onQuit(CCObject* sender) {
        forwardPauseAction("onQuit", sender, [this](CCObject* s) { PauseLayer::onQuit(s); });
    }

    void tryQuit(CCObject* sender) {
        Ref<PaimonPauseLayer> keepAlive = this;
        if (!canUsePauseLayer()) return;
        stopPauseWork(true);
        PauseLayer::tryQuit(sender);
    }

    void onTryEdit(CCObject* sender) {
        Ref<PaimonPauseLayer> keepAlive = this;
        if (!canUsePauseLayer()) return;
        stopPauseWork(true);
        PauseLayer::onTryEdit(sender);
    }

    void onEdit(CCObject* sender) {
        Ref<PaimonPauseLayer> keepAlive = this;
        if (!canUsePauseLayer()) return;
        stopPauseWork(true);
        // onEdit schedules goEdit as an action; do not treat it as a synchronous close.
        PauseLayer::onEdit(sender);
    }

    void goEdit() {
        Ref<PaimonPauseLayer> keepAlive = this;
        if (!canUsePauseLayer()) return;
        markClosing();
        PauseLayer::goEdit();
    }
};

namespace paimon {
void synchronizePauseLayers(PlayLayer* playLayer) {
    if (!playLayer || playLayer != PlayLayer::get() || playLayer->m_isPaused) return;
    auto* director = CCDirector::get();
    auto* scene = director ? director->getRunningScene() : nullptr;
    if (!scene || director->getNextScene()) return;

    // removal runs onexit hooks, which may mutate the scene or open a new pause.
    std::vector<Ref<PauseLayer>> pauses;
    for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
        if (auto* pause = typeinfo_cast<PauseLayer*>(child)) pauses.emplace_back(pause);
    }
    for (auto const& pause : pauses) {
        auto* layer = static_cast<PaimonPauseLayer*>(pause.data());
        if (layer->belongsToPlayLayer(playLayer)) layer->synchronizePauseState(0.f);
    }
}
}

// pauselayer inherits these methods; its modify bindings do not hook them.
class $modify(PaimonPauseLifecycleLayer, CCLayer) {
    void onEnter() {
        CCLayer::onEnter();
        if (auto* pause = typeinfo_cast<PauseLayer*>(this)) {
            static_cast<PaimonPauseLayer*>(pause)->onPauseEnter();
        }
    }

    void onExit() {
        if (auto* pause = typeinfo_cast<PauseLayer*>(this)) {
            static_cast<PaimonPauseLayer*>(pause)->onPauseExit();
        }
        CCLayer::onExit();
    }
};
