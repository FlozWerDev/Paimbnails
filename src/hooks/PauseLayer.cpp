#include <Geode/modify/PauseLayer.hpp>
#include "../framework/HookConventions.hpp"
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
#include <sstream>

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
#include "../utils/LevelMetadata.hpp"
#include "../utils/ImageConverter.hpp"
#include "../utils/PaimonLoadingOverlay.hpp"
#include "../utils/FileDialog.hpp"
#include "../features/moderation/services/ModeratorUtils.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include "../utils/ThreadTracker.hpp"
#include <Geode/binding/LoadingCircle.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include "../utils/PaimonNotification.hpp"

#include "../utils/ActivePauseLayer.hpp"

#include "../utils/SpriteHelper.hpp"

using namespace geode::prelude;

namespace {
// #region agent log
// PERF: body gated behind PAIMON_DEBUG_AGENT347 (same as PlayLayer.cpp).
// No-op by default to avoid opening/closing the log file on every capture.
void agentLog347Pause(char const* loc, char const* msg, char const* hid, std::string const& data) {
#ifdef PAIMON_DEBUG_AGENT347
    std::ofstream f("debug-347aef.log", std::ios::app);
    if (!f) return;
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    f << "{\"sessionId\":\"347aef\",\"hypothesisId\":\"" << hid
      << "\",\"location\":\"" << loc << "\",\"message\":\"" << msg
      << "\",\"data\":" << data << ",\"timestamp\":" << ts << "}\n";
#else
    (void)loc; (void)msg; (void)hid; (void)data;
#endif
}
// #endregion
} // namespace

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

// PNG-encodes the accepted RGBA buffer (and optionally extracts the dominant
// colors) on a worker thread, then calls `onMainDone` back on the main thread.
// Doing this inline froze the frame ~300-500ms at 1080p when accepting.
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
                auto rgbBuf = convertRGBAtoRGB(buf.get(), w, h);
                auto pair = DominantColors::extract(rgbBuf.data(), w, h);
                A = {pair.first.r, pair.first.g, pair.first.b};
                B = {pair.second.r, pair.second.g, pair.second.b};
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
    };
    $override
    void customSetup() {
        PauseLayer::customSetup();
        paimon::setActivePauseLayer(this);

        // Ensure the zoom-hidden flag is clear when creating a new PauseLayer.
        // Otherwise, if a previous session left it true (crash, abrupt scene
        // change), the new PauseLayer would never render.
        paimon::setPauseZoomHidden(false);

        log::info("[PauseLayer] customSetup");

        auto playLayer = PlayLayer::get();
        if (!playLayer) {
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
            // Fallback: find a CCMenu on the correct side, but verify it
            // contains known PauseLayer buttons. Without that check, other mods'
            // menus in the same area (BetterEdit, Globed, etc.) could be picked
            // by accident.
            auto winSize = CCDirector::get()->getWinSize();
            // Button IDs we expect on each side of the PauseLayer.
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

                // Score by presence of known buttons. A menu with 2+ known
                // buttons is almost certainly the right one, guarding against
                // other mods' menus that only contain their own buttons.
                float score = 0.f;
                for (auto const* const* p = knownIDs; *p != nullptr; ++p) {
                    if (menu->getChildByID(*p)) score += 10.f;
                }
                // Tie-break by child count (GD menus usually have more buttons than mods').
                score += static_cast<float>(menu->getChildrenCount()) * 0.1f;

                if (!best || score > bestScore) {
                    best = menu;
                    bestScore = score;
                }
            }
            // If the "best" menu contains no known buttons, return nothing to
            // avoid adding our button to another mod's menu.
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

        if (!Mod::get()->getSettingValue<bool>("enable-thumbnail-taking")) {
            log::debug("Thumbnail taking disabled in settings");
            return;
        }

        // Idempotent: customSetup may run multiple times (other mods rebuild the
        // menu). Don't duplicate if the button already exists.
        if (rightMenu->getChildByID("thumbnail-capture-button"_spr)) {
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
            btn->setRotation(-90.f);
            rightMenu->addChild(btn);
            rightMenu->updateLayout();

            // Add the file-picker button (folder icon). The capture-button guard
            // above already filters reentry, but check here too defensively.
            if (rightMenu->getChildByID("thumbnail-select-button"_spr)) {
                // already added in a previous call
            } else {
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

            // Reconnect the native button; find camera items
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
                        // use class-name heuristic
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

            rewireScreenshotInMenu(findButtonMenu("right-button-menu", true));
            rewireScreenshotInMenu(findButtonMenu("left-button-menu", false));

            // don't call updateLayout, to keep positions
            log::info("Thumbnail capture + extra buttons added successfully");
    }

    // visit() is NOT overridden here: PauseLayer doesn't expose visit() in its
    // modify binding (only CCNode does), so $override void visit() inside this
    // $modify would hook nothing. The render-skip during zoom is done via
    // $modify(CCNode) in PlayLayer.cpp (PaimonPauseZoomVisitFilter), which hooks
    // CCNode::visit and filters by pointer to activePauseLayer when
    // paimon::isPauseZoomHidden is true. Applied to all nodes, but the check is
    // O(1) (pointer comparison), so the cost is negligible.

    void onScreenshot(CCObject*) {
        log::info("[PauseLayer] Capture button pressed; hiding pause menu");
        if (m_fields->m_captureInProgress) {
            log::warn("[PauseLayer] Capture already in progress, ignoring duplicate request");
            return;
        }

        // Mutual exclusion with path B (PlayLayer's capture keybind): if a
        // keybind capture is in progress, don't start another from the PauseLayer
        // button. Without this guard, both paths could clobber FramebufferCapture's
        // s_request and orphan one callback, leaving gCaptureInProgress /
        // paimon::isCaptureInProgress stuck true forever.
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

        // Temporarily hide the pause menu
        bool const visBefore = this->isVisible();
        this->setVisible(false);
        // Set a global flag so PauseZoomManager::update()'s ticker doesn't
        // restore PauseLayer visibility between setVisible(false) and the actual
        // capture in swapBuffers (~0.05s). Otherwise, with zoom at 1.0 and
        // autoShow on, the ticker would restore visibility and the PauseLayer
        // would appear in the capture.
        paimon::setCaptureInProgress(true);
        // #region agent log
        {
            std::ostringstream d;
            d << "{\"visBefore\":" << (visBefore ? "true" : "false")
              << ",\"visAfter\":" << (this->isVisible() ? "true" : "false")
              << ",\"selfPtr\":" << reinterpret_cast<uintptr_t>(this) << "}";
            agentLog347Pause("PauseLayer.cpp:onScreenshot", "hide_before_capture", "F", d.str());
        }
        // #endregion
        m_fields->m_captureInProgress = true;

        // Show loading overlay
        showLoadingOverlay();
        // Guard rail: restore UI if the callback never returns
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
        auto scene = CCDirector::get()->getRunningScene();
        if (!scene) return;
        if (auto existing = scene->getChildByID("paimon-loading-overlay"_spr)) {
            existing->removeFromParentAndCleanup(true);
        }

        auto overlay = PaimonLoadingOverlay::create("Loading...", 40.f);
        overlay->show(scene, 10000);
    }

    void reShowOverlay(float dt) {
        auto scene = CCDirector::get()->getRunningScene();
        if (!scene) return;
        auto overlay = scene->getChildByID("paimon-loading-overlay"_spr);
        if (overlay) overlay->setVisible(true);
    }

    void removeLoadingOverlay() {
        auto* director = CCDirector::get();
        if (!director || !director->getScheduler()) return;
        auto* scheduler = director->getScheduler();
        scheduler->unscheduleSelector(
            schedule_selector(PaimonPauseLayer::reShowOverlay), this
        );
        scheduler->unscheduleSelector(
            schedule_selector(PaimonPauseLayer::captureSafetyRestore), this
        );

        auto scene = CCDirector::get()->getRunningScene();
        if (!scene) return;
        if (auto overlay = typeinfo_cast<PaimonLoadingOverlay*>(scene->getChildByID("paimon-loading-overlay"_spr))) {
            overlay->dismiss();
        }
    }

    void captureSafetyRestore(float) {
        if (!m_fields->m_captureInProgress) return;
        // If we're no longer in the scene, do nothing; a later onExit() or the
        // destructor cleans up.
        if (!this->getParent()) {
            m_fields->m_captureInProgress = false;
            paimon::setCaptureInProgress(false);
            return;
        }
        log::warn("[PauseLayer] Capture watchdog restored UI state");
        m_fields->m_captureInProgress = false;
        paimon::setCaptureInProgress(false);
        removeLoadingOverlay();
        this->setVisible(true);
        PaimonNotify::create(Localization::get().getString("pause.capture_error").c_str(), NotificationIcon::Warning)->show();
    }

    void performCaptureAndRestore(float dt) {
        log::info("[PauseLayer] Performing capture");
        // #region agent log
        {
            std::ostringstream d;
            d << "{\"pauseVisible\":" << (this->isVisible() ? "true" : "false")
              << ",\"hasParent\":" << (this->getParent() ? "true" : "false")
              << ",\"selfPtr\":" << reinterpret_cast<uintptr_t>(this) << "}";
            agentLog347Pause("PauseLayer.cpp:performCaptureAndRestore", "capture_start", "F", d.str());
        }
        // #endregion
        CCDirector::get()->getScheduler()->unscheduleSelector(
            schedule_selector(PaimonPauseLayer::performCaptureAndRestore), this
        );

        // Essential guard: if this PauseLayer is no longer in the scene (e.g. the
        // user hit restart during the 0.05s wait), do nothing.
        if (!this->getParent()) {
            log::warn("[PauseLayer] performCaptureAndRestore called on orphaned PauseLayer");
            m_fields->m_captureInProgress = false;
            paimon::setCaptureInProgress(false);
            return;
        }

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

            // Pre-capture validations (High Graphics, LDM, death)
            auto validation = FramebufferCapture::validateCaptureConditions();
            if (!validation.canCapture) {
                log::info("[PauseLayer] Captura rechazada: {}", validation.reason);
                PaimonNotify::create(validation.reason.c_str(), NotificationIcon::Warning)->show();
                removeLoadingOverlay();
                this->setVisible(true);
                m_fields->m_captureInProgress = false;
                paimon::setCaptureInProgress(false);
                return;
            }

            int levelID = pl->m_level->m_levelID;

            // Hide overlay for a clean capture
            auto scene = CCDirector::get()->getRunningScene();
            if (scene) {
                auto overlay = scene->getChildByID("paimon-loading-overlay"_spr);
                if (overlay) overlay->setVisible(false);
            }

            // Show overlay next frame
            CCDirector::get()->getScheduler()->scheduleSelector(
                schedule_selector(PaimonPauseLayer::reShowOverlay),
                this, 0.0f, 0, 0.0f, false
            );

            // WeakRef instead of Ref: if the PauseLayer is destroyed (e.g.
            // restart/quit mid-capture) we don't want to revive it or run
            // callbacks on orphaned memory.
            geode::WeakRef<PauseLayer> weakRef = this;

            // Use FramebufferCapture
            FramebufferCapture::requestCapture(levelID, [weakRef, levelID](bool success, CCTexture2D* texture, std::shared_ptr<uint8_t> rgbData, int width, int height) {
                // Retain the texture with Ref<> so it survives until the queued
                // lambda runs next frame.
                Ref<CCTexture2D> texRef = texture;
                Loader::get()->queueInMainThread([weakRef, success, texRef, rgbData, width, height, levelID]() {
                    if (paimon::isRuntimeShuttingDown()) {
                        paimon::setCaptureInProgress(false);
                        return;
                    }
                    CCTexture2D* texture = texRef.data();
                    auto locked = weakRef.lock();
                    if (!locked) {
                        log::debug("[PauseLayer] Capture callback skipped: PauseLayer was destroyed");
                        // Still clear the global flag since the capture already finished
                        paimon::setCaptureInProgress(false);
                        return;
                    }
                    auto* self = static_cast<PaimonPauseLayer*>(locked.data());
                    // Double-check: even if the object exists, it may have lost
                    // its parent if onExit() already ran.
                    if (!self->getParent()) {
                        self->m_fields->m_captureInProgress = false;
                        paimon::setCaptureInProgress(false);
                        return;
                    }
                    self->removeLoadingOverlay();
                    self->m_fields->m_captureInProgress = false;
                    paimon::setCaptureInProgress(false);

                    if (success && texture && rgbData) {
                        log::info("[PauseLayer] Capture successful: {}x{}", width, height);

                        auto popup = CapturePreviewPopup::create(
                            texture,
                            levelID,
                            rgbData,
                            width,
                            height,
                            // On-accept callback: check moderator and upload
                            [](bool accepted, int lvlID, std::shared_ptr<uint8_t> buf, int w, int h, std::string mode, std::string replaceId) {
                                if (!accepted || !buf) {
                                    log::info("[PauseLayer] Thumbnail rejected or invalid buffer");
                                    return;
                                }

                                log::info("[PauseLayer] Thumbnail accepted for level {}", lvlID);

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

                                // Single upload — server handles mod check + routing (live vs pending)
                                PaimonNotify::create(Localization::get().getString("capture.uploading").c_str(), NotificationIcon::Info)->show();

                                // PNG encode on a worker; upload from the main thread.
                                processAcceptedCaptureAsync(buf, w, h, /*extractColors=*/false,
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
                                            if (success) {
                                                log::info("[PauseLayer] Upload result for level {}: {}", lvlID, msg);
                                            } else {
                                                log::error("[PauseLayer] Upload failed: {}", msg);
                                            }
                                        }, levelMeta);
                                    });
                            },
                            // Recapture callback
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

                    // Restore the pause menu
                    self->setVisible(true);
                    log::info("[PauseLayer] Pause menu restored after capture");
                });
            });

    }

    $override
    void onExit() {
        // Safety net: covers paths that skip onResume() (Esc/keyBackClicked,
        // quit, restart, scene transitions). Otherwise PauseZoomManager's ticker
        // could keep touching this node while it's destroyed.
        paimon::notifyPauseClosing();
        paimon::clearActivePauseLayer(this);
        // Clear the global capture flag in case it stayed set (e.g. quit/restart
        // while a capture was pending).
        paimon::setCaptureInProgress(false);
        // Clear the zoom-hidden flag too; otherwise, if pause closes abruptly
        // with zoom active, it stays true and the next PauseLayer never renders.
        paimon::setPauseZoomHidden(false);
        m_fields->m_captureInProgress = false;
        m_fields->m_fileDialogOpen = false;

        // Cancel any pending capture so async callbacks don't touch this
        // PauseLayer after it loses its parent.
        FramebufferCapture::cancelPending();

        // Unschedule ALL selectors on this PauseLayer to avoid use-after-free if
        // the scheduler runs them after onExit() but before the destructor.
        if (auto* director = CCDirector::get()) {
            if (auto* scheduler = director->getScheduler()) {
                scheduler->unscheduleAllForTarget(this);
            }
        }
        removeLoadingOverlay();
        PauseLayer::onExit();
    }

    void restorePauseMenu(float dt) {
        this->setVisible(true);
        log::info("[PauseLayer] Pause menu restored");
    }

    void processSelectedFile(std::filesystem::path selectedPath, int levelID) {
        log::info("[PauseLayer] Selected file: {}", geode::utils::string::pathToString(selectedPath));

        // Decide format by extension
        std::string ext = geode::utils::string::pathToString(selectedPath.extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        // MP4/MOV/M4V: mods/admins only
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

            // Validate MP4 magic bytes (ftyp at offset 4)
            if (mp4Data.size() < 8 ||
                !(mp4Data[4] == 'f' && mp4Data[5] == 't' && mp4Data[6] == 'y' && mp4Data[7] == 'p')) {
                log::error("[PauseLayer] Selected file is not a valid MP4/MOV");
                PaimonNotify::create(Localization::get().getString("pause.video_invalid").c_str(), NotificationIcon::Error)->show();
                return;
            }

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
            // Preview GIF and allow upload
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

                // Use CCImage from memory (RAII)
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

                // Get pixels via CCRenderTexture
                auto renderTex = CCRenderTexture::create(width, height, kCCTexture2DPixelFormat_RGBA8888);
                if (!renderTex) {
                    texture->release();
                    PaimonNotify::create(Localization::get().getString("pause.render_error").c_str(), NotificationIcon::Error)->show();
                    return;
                }

                renderTex->begin();
                auto sprite = CCSprite::createWithTexture(texture);
                sprite->setPosition(ccp(width/2, height/2));
                sprite->visit();
                renderTex->end();

                // Read RGBA data
                auto renderedImage = renderTex->newCCImage(false);
                if (!renderedImage) {
                    texture->release();
                    PaimonNotify::create(Localization::get().getString("pause.render_read_error").c_str(), NotificationIcon::Error)->show();
                    return;
                }

                auto imageData = renderedImage->getData();
                size_t rgbaSize = static_cast<size_t>(width) * height * 4;
                std::shared_ptr<uint8_t> rgbaData(new uint8_t[rgbaSize], std::default_delete<uint8_t[]>());

                // Copy RGBA directly
                std::memcpy(rgbaData.get(), imageData, rgbaSize);

                renderedImage->release();

                // The popup retains the texture via its Ref<>; autorelease drops
                // our reference (refcount=1 from new CCTexture2D) when the pool
                // drains, avoiding a permanent texture leak.
                texture->autorelease();

                // Show preview and upload the GIF if accepted
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

                        // Get user and check mod
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

                        // Dominant colors from the first frame, off the main
                        // thread (K-means in LAB is expensive at preview size).
                        if (buf && w > 0 && h > 0) {
                            paimon::ThreadTracker::get().spawn([lvlID, buf, w, h]() {
                                if (paimon::isRuntimeShuttingDown()) return;
                                auto rgbBuf = convertRGBAtoRGB(buf.get(), w, h);
                                auto pair = DominantColors::extract(rgbBuf.data(), w, h);
                                ccColor3B A{pair.first.r, pair.first.g, pair.first.b};
                                ccColor3B B{pair.second.r, pair.second.g, pair.second.b};
                                Loader::get()->queueInMainThread([lvlID, A, B]() {
                                    if (paimon::isRuntimeShuttingDown()) return;
                                    LevelColors::get().set(lvlID, A, B);
                                });
                            });
                        }

                        ThumbsRegistry::get().mark(ThumbKind::Level, lvlID, false);

                        // Single upload — server handles mod check + routing (live vs pending)
                        PaimonNotify::create(Localization::get().getString("pause.gif_uploading").c_str(), NotificationIcon::Loading)->show();
                        std::string levelMeta;
                        if (auto* pl = PlayLayer::get()) levelMeta = paimon::collectLevelMetadata(pl->m_level);
                        ThumbnailAPI::get().uploadGIF(lvlID, gifData, username, [lvlID, username](bool ok, std::string const& msg){
                            handleUploadResult(ok, msg, lvlID, username,
                                "pause.gif_uploaded", "pause.gif_upload_error");
                        }, levelMeta);
                    }
                );

                if (popup) {
                    popup->show();
                } else {
                    log::error("[PauseLayer] Failed to create GIF preview popup");
                }

            return; // Stop the PNG flow
        }

        // Read the full PNG into memory
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

        // Load image into CCImage (RAII)
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

        // Read image data
        int bpp = imgGuard->getBitsPerComponent();
        bool hasAlpha = imgGuard->hasAlpha();

        log::info("[PauseLayer] Image loaded {}x{} (BPP: {}, Alpha: {})",
                  width, height, bpp, hasAlpha);

        // Compute expected size
        int bytesPerPixel = hasAlpha ? 4 : 3;
        size_t expectedDataSize = static_cast<size_t>(width) * height * bytesPerPixel;

        // Convert if needed
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
                rgbaPixels[i*4 + 3] = 255;              // max opacity
            }
        }

        imgGuard.reset(); // Free CCImage early

        log::debug("[PauseLayer] RGBA data ready ({} bytes)", rgbaSize);

        // Create texture as in capture
        CCTexture2D* texture = new CCTexture2D();
        if (!texture) {
            log::error("[PauseLayer] Failed to create CCTexture2D");
            PaimonNotify::create(Localization::get().getString("pause.create_texture_error").c_str(), NotificationIcon::Error)->show();
            return;
        }

        // Init texture with data
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

        // Improve texture parameters
        texture->setAntiAliasTexParameters();

        // new CCTexture2D() gives refcount=1; the popup retains it via its Ref<>
        // m_texture. autorelease drops our reference when the pool drains (after
        // the popup retained), avoiding a permanent leak.
        texture->autorelease();

        log::info("[PauseLayer] Texture created successfully using FramebufferCapture method");

        // Wrapper for the data
        std::shared_ptr<uint8_t> rgbaData(new uint8_t[rgbaSize], std::default_delete<uint8_t[]>());
        std::memcpy(rgbaData.get(), rgbaPixels.data(), rgbaSize);

        log::info("[PauseLayer] Showing preview with RGBA data");

        // show popup
        auto popup = CapturePreviewPopup::create(
            texture,
            levelID,
            rgbaData,
            width,
            height,
            [levelID](bool accepted, int lvlID, std::shared_ptr<uint8_t> buf, int w, int h, std::string mode, std::string replaceId) {
                if (!accepted || !buf) {
                    log::info("[PauseLayer] User cancelled image preview");
                    return;
                }
                log::info("[PauseLayer] User accepted image loaded from disk");

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

                // Dominant colors + PNG encode on a worker; upload from main.
                processAcceptedCaptureAsync(buf, w, h, /*extractColors=*/true,
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

        if (popup) {
            popup->show();
        } else {
            log::error("[PauseLayer] Failed to create preview popup");
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

            // Mods/admins can upload MP4; normal users only images
            bool isMod = PaimonUtils::isUserModerator() && !HttpClient::get().getModCode().empty();
            if (isMod) {
                pt::pickMedia(pickerCb);
            } else {
                pt::pickImage(pickerCb);
            }
    }

    void onResume(CCObject* sender) {
        // Safety net: if PlayLayer::get() is null, calling PauseLayer::onResume
        // crashes (EXCEPTION_ACCESS_VIOLATION) in PlayLayer::resume by accessing
        // fields of a non-existent PlayLayer.
        if (!PlayLayer::get()) {
            log::warn("[PauseLayer] onResume called but PlayLayer::get() is null. Preventing crash.");
            // Don't removeFromParentAndCleanup here: if the PauseLayer is being
            // destroyed by a scene transition, forcing cleanup can double-free.
            // Safer to let the system handle destruction.
            return;
        }

        // Notify PauseZoomManager IMMEDIATELY when the user presses Resume, not
        // at the end of the close animation. Otherwise the manager's ticker keeps
        // running while the PauseLayer animates out and calls
        // setPauseMenuVisible(true)/showLayer() every frame, restarting the entry
        // animation and causing the "stuck menu" symptom on fast unpause. An
        // inconsistent transition can make the destructor run queued actions on
        // freed memory and crash.
        paimon::notifyPauseClosing();
        PauseLayer::onResume(sender);
    }

    void onRestart(CCObject* sender) {
        if (!PlayLayer::get()) {
            log::warn("[PauseLayer] onRestart called but PlayLayer::get() is null. Preventing crash.");
            return;
        }
        // Notify PauseZoomManager to clear zoom state so the next level doesn't
        // start with an invisible PauseLayer.
        paimon::notifyPauseClosing();
        PauseLayer::onRestart(sender);
    }

    void onRestartFull(CCObject* sender) {
        if (!PlayLayer::get()) {
            log::warn("[PauseLayer] onRestartFull called but PlayLayer::get() is null. Preventing crash.");
            return;
        }
        // Notify PauseZoomManager to clear zoom state.
        paimon::notifyPauseClosing();
        PauseLayer::onRestartFull(sender);
    }

    void onNormalMode(CCObject* sender) {
        if (!PlayLayer::get()) {
            log::warn("[PauseLayer] onNormalMode called but PlayLayer::get() is null. Preventing crash.");
            return;
        }
        // Notify PauseZoomManager to clear zoom state.
        paimon::notifyPauseClosing();
        PauseLayer::onNormalMode(sender);
    }

    void onPracticeMode(CCObject* sender) {
        if (!PlayLayer::get()) {
            log::warn("[PauseLayer] onPracticeMode called but PlayLayer::get() is null. Preventing crash.");
            return;
        }
        // Notify PauseZoomManager to clear zoom state.
        paimon::notifyPauseClosing();
        PauseLayer::onPracticeMode(sender);
    }
};
