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
        PaimonNotify::create(Localization::get().getString(errorKey).c_str(), NotificationIcon::Error)->show();
        return;
    }
    bool isPending = msg.find("pending") != std::string::npos
                  || msg.find("verification") != std::string::npos;
    if (isPending) {
        PendingQueue::get().addOrBump(levelID, PendingCategory::Verify, username, {}, false);
        PaimonNotify::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Success)->show();
    } else {
        PendingQueue::get().removeForLevel(levelID);
        PaimonNotify::create(Localization::get().getString(successKey).c_str(), NotificationIcon::Success)->show();
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
        bool m_closing = false;
        uint64_t m_captureGeneration = 0;
        WeakRef<PlayLayer> m_playLayer;
        WeakRef<PaimonLoadingOverlay> m_loadingOverlay;
    };

    void onPauseEnter() {
        m_fields->m_closing = false;
        paimon::setPauseLayerClosing(this, false);
        paimon::setActivePauseLayer(this);
    }

    $override
    void customSetup() {
        PauseLayer::customSetup();

        auto playLayer = PlayLayer::get();
        m_fields->m_playLayer = playLayer;
        if (!playLayer) return;

        paimon::setPauseZoomHidden(false);

        if (!playLayer->m_level) {
            log::warn("Level not available in PlayLayer");
            return;
        }

        if (playLayer->m_level->m_levelID <= 0) return;

        auto findButtonMenu = [this](char const* id, bool rightSide) -> CCMenu* {
            if (auto byId = typeinfo_cast<CCMenu*>(this->getChildByID(id))) {
                return byId;
            }
            // fallback to the side menu containing known pauselayer buttons
            auto winSize = CCDirector::get()->getWinSize();
            static char const* const kRightSideKnownIDs[] = {
                "resume-button", "practice-button", "quit-button", nullptr
            };
            static char const* const kLeftSideKnownIDs[] = {
                "options-button", "restart-button", nullptr
            };
            char const* const* knownIDs = rightSide ? kRightSideKnownIDs : kLeftSideKnownIDs;

            CCMenu* best = nullptr;
            float bestScore = 0.f;
            for (auto* node : CCArrayExt<CCNode*>(this->getChildren())) {
                auto menu = typeinfo_cast<CCMenu*>(node);
                if (!menu) continue;
                float x = menu->getPositionX();
                bool sideMatch = rightSide ? (x > winSize.width * 0.5f) : (x < winSize.width * 0.5f);
                if (!sideMatch) continue;

                float score = 0.f;
                for (auto const* const* p = knownIDs; *p != nullptr; ++p) {
                    if (menu->getChildByID(*p)) score += 10.f;
                }
                score += static_cast<float>(menu->getChildrenCount()) * 0.1f;

                if (!best || score > bestScore) {
                    best = menu;
                    bestScore = score;
                }
            }
            if (best && bestScore < 5.f) {
                log::warn("PauseLayer fallback menu found but contains no known buttons; skipping to avoid foreign-mod menu pollution");
                return nullptr;
            }
            return best;
        };

        auto rightMenu = findButtonMenu("right-button-menu", true);
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

        // customsetup may repeat; do not duplicate the button
        if (rightMenu->getChildByID("thumbnail-capture-button"_spr)) return;

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
                            item->setTarget(this, menu_selector(PaimonPauseLayer::onScreenshot));
                        }
                    }
                }
            };

            rewireScreenshotInMenu(findButtonMenu("right-button-menu", true));
            rewireScreenshotInMenu(findButtonMenu("left-button-menu", false));
    }

    bool canUsePauseLayer() {
        auto play = m_fields->m_playLayer.lock();
        return !m_fields->m_closing && paimon::isPauseLayerOpen(this)
            && play && play.data() == PlayLayer::get() && play->m_isPaused
            && paimon::findOpenPauseLayerInScene() == this;
    }

    void stopPauseWork() {
        ++m_fields->m_captureGeneration;
        m_fields->m_fileDialogOpen = false;
        bool const ownsCapture = m_fields->m_captureInProgress || m_fields->m_feedbackCaptureInProgress;
        m_fields->m_captureInProgress = false;
        m_fields->m_feedbackCaptureInProgress = false;
        if (ownsCapture) {
            FramebufferCapture::cancelPending();
            paimon::setCaptureInProgress(false);
        }
        if (auto* director = CCDirector::get()) {
            if (auto* scheduler = director->getScheduler()) scheduler->unscheduleAllForTarget(this);
        }
        removeLoadingOverlay();
    }

    void markClosing() {
        m_fields->m_closing = true;
        paimon::setPauseLayerClosing(this, true);
        if (paimon::getActivePauseLayer() == this) {
            paimon::clearActivePauseLayer(this);
            paimon::notifyPauseClosing();
        }
    }

    bool beginPauseAction(char const* action) {
        if (m_fields->m_closing) return false;
        bool const canUse = canUsePauseLayer();
        markClosing();
        stopPauseWork();
        setVisible(false);
        setTouchEnabled(false);
        setKeypadEnabled(false);
        setKeyboardEnabled(false);
        if (!canUse) {
            log::warn("[PauseLayer] Ignoring {} on an inactive pause menu", action);
            removeFromParentAndCleanup(true);
        }
        return canUse;
    }

    void finishPauseAction() {
        if (!getParent()) return;
        auto play = m_fields->m_playLayer.lock();
        if (!play || play.data() != PlayLayer::get() || !play->m_isPaused
            || paimon::findOpenPauseLayerInScene()) {
            removeFromParentAndCleanup(true);
            return;
        }
        // Keep the menu usable if another hook rejects the action.
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
        if (!request || m_fields->m_feedbackCaptureInProgress || m_fields->m_captureInProgress
            || paimon::isCaptureInProgress()) return;
        auto validation = FramebufferCapture::validateCaptureConditions();
        if (!validation.canCapture) {
            PaimonNotify::create(validation.reason.c_str(), NotificationIcon::Warning)->show();
            return;
        }
        m_fields->m_feedbackCaptureInProgress = true;
        ++m_fields->m_captureGeneration;
        paimon::setCaptureInProgress(true);
        setVisible(false);
        showLoadingOverlay();
        scheduleOnce(schedule_selector(PaimonPauseLayer::performWebRequestCapture), .05f);
        scheduleOnce(schedule_selector(PaimonPauseLayer::webRequestCaptureTimeout), 8.f);
    }

    void webRequestCaptureTimeout(float) {
        if (!m_fields->m_feedbackCaptureInProgress) return;
        ++m_fields->m_captureGeneration;
        m_fields->m_feedbackCaptureInProgress = false;
        FramebufferCapture::cancelPending();
        paimon::setCaptureInProgress(false);
        if (canUsePauseLayer()) setVisible(true);
        removeLoadingOverlay();
        PaimonNotify::create("La captura no respondio", NotificationIcon::Warning)->show();
    }

    void performWebRequestCapture(float) {
        auto* play = PlayLayer::get();
        auto request = play && play->m_level
            ? feedbackRequestForLevel(play->m_level->m_levelID) : std::nullopt;
        if (!m_fields->m_feedbackCaptureInProgress || !canUsePauseLayer() || !request) {
            webRequestCaptureTimeout(0.f);
            return;
        }
        if (auto overlay = m_fields->m_loadingOverlay.lock()) overlay->setVisible(false);
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
                        || !self->m_fields->m_feedbackCaptureInProgress || !self->canUsePauseLayer()) return;
                    self->m_fields->m_feedbackCaptureInProgress = false;
                    self->unschedule(schedule_selector(PaimonPauseLayer::webRequestCaptureTimeout));
                    paimon::setCaptureInProgress(false);
                    self->removeLoadingOverlay();
                    self->setVisible(true);
                    if (!success || !held || !rgba) {
                        PaimonNotify::create("No se pudo capturar el nivel", NotificationIcon::Error)->show();
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
        if (m_fields->m_captureInProgress) {
            log::warn("[PauseLayer] Capture already in progress, ignoring duplicate request");
            return;
        }

        // avoid racing playlayer's capture keybind or orphaning its callback
        if (paimon::isCaptureInProgress()) {
            log::warn("[PauseLayer] Captura por keybind ya en curso, ignorando boton");
            PaimonNotify::create(
                Localization::get().getString("pause.capture_busy").c_str(),
                NotificationIcon::Warning
            )->show();
            return;
        }

        auto pl = PlayLayer::get();
        if (!pl) {
            log::error("[PauseLayer] PlayLayer not available");
            PaimonNotify::create(Localization::get().getString("pause.playlayer_error").c_str(), NotificationIcon::Error)->show();
            return;
        }

        bool const visBefore = this->isVisible();
        this->setVisible(false);
        // prevent the zoom ticker from restoring it before swapbuffers().
        paimon::setCaptureInProgress(true);
        m_fields->m_captureInProgress = true;
        ++m_fields->m_captureGeneration;

        showLoadingOverlay();
        // restore the ui if the callback never returns
        this->scheduleOnce(schedule_selector(PaimonPauseLayer::captureSafetyRestore), 8.0f);

        auto* director = CCDirector::get();
        if (!director || !director->getScheduler()) {
            m_fields->m_captureInProgress = false;
            paimon::setCaptureInProgress(false);
            this->setVisible(visBefore);
            removeLoadingOverlay();
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
        if (auto existing = scene->getChildByID("paimon-loading-overlay"_spr)) {
            existing->removeFromParentAndCleanup(true);
        }

        if (auto* overlay = PaimonLoadingOverlay::create("Loading...", 40.f)) {
            m_fields->m_loadingOverlay = overlay;
            overlay->show(scene, 10000);
        }
    }

    void reShowOverlay(float) {
        if (!canUsePauseLayer() || !m_fields->m_captureInProgress) return;
        if (auto overlay = m_fields->m_loadingOverlay.lock()) overlay->setVisible(true);
    }

    void removeLoadingOverlay() {
        auto* director = CCDirector::get();
        if (!director) return;
        if (auto* scheduler = director->getScheduler()) {
            scheduler->unscheduleSelector(
                schedule_selector(PaimonPauseLayer::reShowOverlay), this
            );
            scheduler->unscheduleSelector(
                schedule_selector(PaimonPauseLayer::captureSafetyRestore), this
            );
        }

        if (auto overlay = m_fields->m_loadingOverlay.lock()) {
            if (m_fields->m_closing) {
                overlay->setVisible(false);
                overlay->setTouchEnabled(false);
                // Scene teardown may be iterating the overlay's siblings.
                Loader::get()->queueInMainThread([overlay] {
                    if (overlay->getParent()) overlay->removeFromParentAndCleanup(true);
                });
            } else {
                overlay->dismiss();
            }
        }
        m_fields->m_loadingOverlay = nullptr;
    }

    void captureSafetyRestore(float) {
        if (!m_fields->m_captureInProgress) return;
        bool const canRestore = canUsePauseLayer();
        ++m_fields->m_captureGeneration;
        log::warn("[PauseLayer] Capture watchdog restored UI state");
        m_fields->m_captureInProgress = false;
        FramebufferCapture::cancelPending();
        paimon::setCaptureInProgress(false);
        removeLoadingOverlay();
        if (!canRestore) return;
        this->setVisible(true);
        PaimonNotify::create(Localization::get().getString("pause.capture_error").c_str(), NotificationIcon::Warning)->show();
    }

    void performCaptureAndRestore(float) {
        if (!m_fields->m_captureInProgress || !canUsePauseLayer()) return;
        auto* director = CCDirector::get();
        auto* scheduler = director ? director->getScheduler() : nullptr;
        if (!scheduler) {
            log::warn("[PauseLayer] Capture cancelled: scheduler unavailable");
            m_fields->m_captureInProgress = false;
            paimon::setCaptureInProgress(false);
            removeLoadingOverlay();
            this->setVisible(true);
            return;
        }

        scheduler->unscheduleSelector(
            schedule_selector(PaimonPauseLayer::performCaptureAndRestore), this
        );

        auto* pl = PlayLayer::get();
        if (!pl || !pl->m_level) {
            log::error("[PauseLayer] PlayLayer or level not available for capture");
            PaimonNotify::create(Localization::get().getString("pause.capture_error").c_str(), NotificationIcon::Error)->show();
            removeLoadingOverlay();
            this->setVisible(true);
            m_fields->m_captureInProgress = false;
            paimon::setCaptureInProgress(false);
            return;
        }

        auto validation = FramebufferCapture::validateCaptureConditions();
        if (!validation.canCapture) {
            PaimonNotify::create(validation.reason.c_str(), NotificationIcon::Warning)->show();
            removeLoadingOverlay();
            this->setVisible(true);
            m_fields->m_captureInProgress = false;
            paimon::setCaptureInProgress(false);
            return;
        }

        int levelID = pl->m_level->m_levelID;

        if (auto overlay = m_fields->m_loadingOverlay.lock()) overlay->setVisible(false);

        scheduler->scheduleSelector(
            schedule_selector(PaimonPauseLayer::reShowOverlay),
            this, 0.0f, 0, 0.0f, false
        );

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
                        || !self->m_fields->m_captureInProgress || !self->canUsePauseLayer()) return;
                    self->removeLoadingOverlay();
                    self->m_fields->m_captureInProgress = false;
                    paimon::setCaptureInProgress(false);

                    if (success && texture && rgbData) {
                        auto popup = CapturePreviewPopup::create(
                            texture,
                            levelID,
                            rgbData,
                            width,
                            height,
                            [](bool accepted, int lvlID, std::shared_ptr<uint8_t> buf, int w, int h, std::string mode, std::string replaceId) {
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
                                    PaimonNotify::create(Localization::get().getString("profile.username_error").c_str(), NotificationIcon::Error)->show();
                                    return;
                                }
                                if (accountID <= 0) {
                                    PaimonNotify::create(Localization::get().getString("level.account_required").c_str(), NotificationIcon::Error)->show();
                                    return;
                                }

                                PaimonNotify::create(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info)->show();

                                processAcceptedCaptureAsync(buf, w, h, /*extractcolors=*/false,
                                    [lvlID, username](bool encoded, std::vector<uint8_t> pngData, ccColor3B, ccColor3B) {
                                        if (!encoded) {
                                            log::error("[PauseLayer] Failed to encode PNG in memory");
                                            PaimonNotify::create(Localization::get().getString("capture.save_png_error").c_str(), NotificationIcon::Error)->show();
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
                        PaimonNotify::create(Localization::get().getString("pause.capture_error").c_str(), NotificationIcon::Error)->show();
                    }

                    self->setVisible(true);
                });
            });

    }

    void onPauseExit() {
        markClosing();
        stopPauseWork();
    }

    void processSelectedFile(std::filesystem::path selectedPath, int levelID) {
        std::string ext = geode::utils::string::toLower(
            geode::utils::string::pathToString(selectedPath.extension()));

        if (ext == ".mp4" || ext == ".mov" || ext == ".m4v") {
            std::error_code fileError;
            auto const fileSize = std::filesystem::file_size(selectedPath, fileError);
            if (fileError) {
                log::error("[PauseLayer] Could not open video file");
                PaimonNotify::create(Localization::get().getString("pause.video_open_error").c_str(), NotificationIcon::Error)->show();
                return;
            }
            if (fileSize > 50 * 1024 * 1024) {
                PaimonNotify::create(Localization::get().getString("pause.video_too_large").c_str(), NotificationIcon::Error)->show();
                return;
            }
            auto mp4Data = ImageLoadHelper::readBinaryFile(selectedPath, 50);
            if (mp4Data.empty()) {
                log::error("[PauseLayer] Could not read video file");
                PaimonNotify::create(Localization::get().getString("pause.video_open_error").c_str(), NotificationIcon::Error)->show();
                return;
            }

            if (!paimon::format::isMp4(mp4Data.data(), mp4Data.size())) {
                log::error("[PauseLayer] Selected file is not a valid MP4/MOV");
                PaimonNotify::create(Localization::get().getString("pause.video_invalid").c_str(), NotificationIcon::Error)->show();
                return;
            }


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

            PaimonNotify::create(Localization::get().getString("pause.video_uploading").c_str(), NotificationIcon::Loading)->show();
            std::string levelMeta;
            if (auto* pl = PlayLayer::get()) levelMeta = paimon::collectLevelMetadata(pl->m_level);
            ThumbnailAPI::get().uploadVideo(levelID, mp4Data, username, [levelID, username](bool ok, std::string const& msg) {
                handleUploadResult(ok, msg, levelID, username,
                    "pause.video_success", "pause.video_upload_error");
                if (!ok) {
                    log::error("[PauseLayer] Video upload failed: {}", msg);
                }
            }, levelMeta);
            return;
        }

        if (ext == ".gif") {
            auto gifData = ImageLoadHelper::readBinaryFile(selectedPath, 50);
            if (gifData.empty()) {
                log::error("[PauseLayer] Could not read GIF file");
                PaimonNotify::create(Localization::get().getString("pause.gif_open_error").c_str(), NotificationIcon::Error)->show();
                return;
            }

            auto preview = ImageLoadHelper::loadWithSTBFromMemory(gifData.data(), gifData.size());
            if (!preview.success || !preview.texture || !preview.buffer) {
                log::error("[PauseLayer] Could not decode GIF preview: {}", preview.error);
                PaimonNotify::create(Localization::get().getString("pause.gif_read_error").c_str(), NotificationIcon::Error)->show();
                return;
            }

            auto popup = CapturePreviewPopup::create(
                preview.texture,
                levelID,
                preview.buffer,
                preview.width,
                preview.height,
                [levelID, gifData = std::move(gifData)](bool accepted, int lvlID, std::shared_ptr<uint8_t> buf, int w, int h, std::string mode, std::string replaceId) mutable {
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
                        PaimonNotify::create(Localization::get().getString("profile.username_error").c_str(), NotificationIcon::Error)->show();
                        return;
                    }
                    if (accountID <= 0) {
                        PaimonNotify::create(Localization::get().getString("level.account_required").c_str(), NotificationIcon::Error)->show();
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

                    PaimonNotify::create(Localization::get().getString("pause.gif_uploading").c_str(), NotificationIcon::Loading)->show();
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

            return;
        }

        auto image = ImageLoadHelper::loadStaticImage(selectedPath, 50);
        if (!image.success || !image.texture || !image.buffer) {
            log::error("[PauseLayer] Failed to load selected image: {}", image.error);
            PaimonNotify::create(Localization::get().getString("pause.png_invalid").c_str(), NotificationIcon::Error)->show();
            return;
        }


        auto popup = CapturePreviewPopup::create(
            image.texture,
            levelID,
            image.buffer,
            image.width,
            image.height,
            [levelID](bool accepted, int lvlID, std::shared_ptr<uint8_t> buf, int w, int h, std::string mode, std::string replaceId) {
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
                    PaimonNotify::create(Localization::get().getString("level.account_required").c_str(), NotificationIcon::Error)->show();
                    return;
                }

                PaimonNotify::create(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info)->show();

                processAcceptedCaptureAsync(buf, w, h, /*extractcolors=*/true,
                    [lvlID, username](bool encoded, std::vector<uint8_t> pngData, ccColor3B A, ccColor3B B) {
                        LevelColors::get().set(lvlID, A, B);
                        ThumbsRegistry::get().mark(ThumbKind::Level, lvlID, false);

                        if (!encoded) {
                            log::error("[PauseLayer] Failed to encode PNG in memory");
                            PaimonNotify::create(Localization::get().getString("capture.save_png_error").c_str(), NotificationIcon::Error)->show();
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

    void onSelectPNGFile(CCObject*) {
        if (!canUsePauseLayer()) return;
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
            if (!layer->canUsePauseLayer()) return;
            auto pathOpt = std::move(result).unwrapOr(std::nullopt);
            if (!pathOpt || pathOpt->empty()) return;
            layer->processSelectedFile(std::move(*pathOpt), levelID);
        };

        bool isMod = PaimonUtils::isUserModerator() && !HttpClient::get().getModCode().empty();
        if (isMod) {
            pt::pickMedia(pickerCb);
        } else {
            pt::pickImage(pickerCb);
        }
    }

    void onResume(CCObject* sender) {
        Ref<PaimonPauseLayer> keepAlive = this;
        if (!beginPauseAction("onResume")) return;
        PauseLayer::onResume(sender);
        finishPauseAction();
    }

    void onRestart(CCObject* sender) {
        Ref<PaimonPauseLayer> keepAlive = this;
        if (!beginPauseAction("onRestart")) return;
        PauseLayer::onRestart(sender);
        finishPauseAction();
    }

    void onRestartFull(CCObject* sender) {
        Ref<PaimonPauseLayer> keepAlive = this;
        if (!beginPauseAction("onRestartFull")) return;
        PauseLayer::onRestartFull(sender);
        finishPauseAction();
    }

    void onNormalMode(CCObject* sender) {
        Ref<PaimonPauseLayer> keepAlive = this;
        if (!beginPauseAction("onNormalMode")) return;
        PauseLayer::onNormalMode(sender);
        finishPauseAction();
    }

    void onPracticeMode(CCObject* sender) {
        Ref<PaimonPauseLayer> keepAlive = this;
        if (!beginPauseAction("onPracticeMode")) return;
        PauseLayer::onPracticeMode(sender);
        finishPauseAction();
    }

    void onQuit(CCObject* sender) {
        Ref<PaimonPauseLayer> keepAlive = this;
        if (!beginPauseAction("onQuit")) return;
        PauseLayer::onQuit(sender);
    }
};

// PauseLayer inherits these methods; its modify bindings do not hook them.
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
