#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCNode.hpp>
#include "../utils/PaimonNotification.hpp"
#include <Geode/loader/SettingV3.hpp>
#include <Geode/utils/Keyboard.hpp>
#include "../features/capture/ui/CapturePreviewPopup.hpp"
#include "../features/capture/ui/CaptureLayerEditorPopup.hpp"
#include "../features/capture/ui/CaptureAssetBrowserPopup.hpp"
#include "../features/capture/services/FramebufferCapture.hpp"
#include "../features/capture/services/CaptureDeathTracker.hpp"

#include "../utils/PlayerToggleHelper.hpp"
#include "../utils/Localization.hpp"
#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../features/moderation/services/PendingQueue.hpp"
#include "../utils/ImageConverter.hpp"
#include "../features/moderation/services/ModeratorUtils.hpp"
#include <Geode/cocos/robtop/keyboard_dispatcher/CCKeyboardDispatcher.h>

#include "../utils/DominantColors.hpp"
#include "../utils/LevelMetadata.hpp"
#include "../features/thumbnails/services/LevelColors.hpp"
#include "../features/audio/services/AudioContextCoordinator.hpp"
#include "../features/foryou/services/ForYouTracker.hpp"
#include "../features/foryou/services/LevelTagsIntegration.hpp"
#include "../framework/compat/ModCompat.hpp"
#include "../features/smooth-scroll/services/SmoothScrollController.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include "../utils/ActivePauseLayer.hpp"
#include "../utils/ThreadTracker.hpp"
#include <algorithm>
#include <cstring>
#include <memory>
#include <chrono>
#include <fstream>
#include <sstream>

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#endif

#include "../features/dynamic-songs/services/DynamicSongManager.hpp"
#include "../features/menu-loop/services/MenuLoopManager.hpp"
#include "../features/menu-loop/services/MenuLoopControl.hpp"

using namespace geode::prelude;

namespace {
    // #region agent log
    // PERF: the body opened/wrote/closed debug-347aef.log every call (50-200us
    // on Windows). Since onScroll invokes it per scroll event during pause-zoom,
    // that added 1-10ms of jitter per frame. Now a no-op. Define
    // PAIMON_DEBUG_AGENT347 in CMake to re-enable instrumentation.
    void agentLog347(char const* loc, char const* msg, char const* hid, std::string const& data) {
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

    std::atomic_bool gCaptureInProgress{false};
    constexpr float kPauseZoomStep = 0.18f;
    constexpr float kPauseZoomMin = 1.0f;
    constexpr float kPauseZoomMax = 4.0f;

    float getPauseZoomSensitivity() {
        return static_cast<float>(Mod::get()->getSavedValue<double>("zoom-sensitivity", 1.0));
    }

    bool pauseZoomAutoHideMenu() {
        return Mod::get()->getSavedValue<bool>("zoom-auto-hide-menu", true);
    }

    bool pauseZoomAutoShowMenu() {
        return Mod::get()->getSavedValue<bool>("zoom-auto-show-menu", true);
    }

    bool pauseZoomAltDisablesScroll() {
        return Mod::get()->getSavedValue<bool>("zoom-alt-disables-scroll", true);
    }

    float clampZoomValue(float value, float minValue, float maxValue);
    CCSize getGameplayScreenSize();
    void resetPlayLayerZoom(CCNode* playLayer);
    void clampPlayLayerZoomPosition(CCNode* playLayer);

    // The PauseLayer may not exist in the scene tree for the first few frames
    // after pausing (GD's entry animation takes ~0.3s). update() must not call
    // onResume() when it's missing — that would set m_isPaused=false permanently
    // and break zoom (the !m_isPaused guards). Instead, m_pauseLayerMissingFrames
    // counts consecutive missing frames; only assume pause was cancelled after
    // ~2s (~120 frames at 60fps). Normally the PauseLayer appears by frame 2-3.

    class PauseZoomManager {
    public:
        static PauseZoomManager& get() {
            static PauseZoomManager instance;
            return instance;
        }

        void onPause() {
            if (m_isPaused) return;
            m_isPaused = true;
            log::debug("[PauseZoom] onPause() called — m_isPaused=true");
            m_isPanning = false;
            m_menuForcedHidden = false;
            m_pauseLayerMissingFrames = 0;
            m_lastMousePos = cocos::getMousePos();
            m_deltaMousePos = ccp(0.f, 0.f);
        }

        void onResume() {
            if (!m_isPaused) return;
            log::debug("[PauseZoom] onResume() called — m_isPaused=false (was paused)");
            if (auto* playLayer = PlayLayer::get()) {
                resetPlayLayerZoom(playLayer);
            }
            restorePauseMenuVisible();
            // Extra guarantee: if restorePauseMenuVisible didn't find the
            // pauseLayer (already destroyed by a scene change), still clear the
            // global flag so state doesn't leak into the next session.
            paimon::setPauseZoomHidden(false);
            m_isPaused = false;
            m_isPanning = false;
            m_menuForcedHidden = false;
            m_pauseLayerMissingFrames = 0;
        }

        void resetForNewLevel() {
            m_isPaused = false;
            m_isPanning = false;
            m_menuForcedHidden = false;
            m_pauseLayerMissingFrames = 0;
            // Defensive reset: if a previous level left the flag true (crash,
            // abrupt exit), the next level won't end up with an invisible PauseLayer.
            paimon::setPauseZoomHidden(false);
        }

        void update(float dt) {
            // Detect pause by the PauseLayer's presence in the scene (not
            // pauseGame(), since mods like compact-pause-menu replace the pause
            // logic and never call pauseGame(true)).
            auto* playLayer = PlayLayer::get();
            auto* pauseLayer = getPauseLayer();
            bool pauseLayerPresent = (playLayer && pauseLayer);

            if (pauseLayerPresent && !m_isPaused) {
                this->onPause();
            }
            if (!pauseLayerPresent && m_isPaused) {
                // If the PauseLayer has been gone for over 2 seconds, assume
                // pause was cancelled.
                m_pauseLayerMissingFrames++;
                if (m_pauseLayerMissingFrames > 120) {
                    log::debug("[PauseZoom] update auto-resume: pauseLayer missing for {} frames", m_pauseLayerMissingFrames);
                    this->onResume();
                }
                return;
            }
            m_pauseLayerMissingFrames = 0;

            if (!m_isPaused) return;

            if (hasBlockingPopup()) {
                m_isPanning = false;
                return;
            }

#ifdef GEODE_IS_WINDOWS
            auto mousePos = cocos::getMousePos();
            m_deltaMousePos = ccp(mousePos.x - m_lastMousePos.x, mousePos.y - m_lastMousePos.y);
            m_lastMousePos = mousePos;
            m_isPanning = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
#else
            m_deltaMousePos = CCPointZero;
            m_isPanning = false;
#endif

            if (m_isPanning && playLayer->getScale() > 1.0f) {
                playLayer->setPosition(playLayer->getPosition() + m_deltaMousePos);
                clampPlayLayerZoomPosition(playLayer);
            }

            if (!paimon::isCaptureInProgress() && pauseLayer) {
                float scale = playLayer->getScale();
                bool const autoHide = pauseZoomAutoHideMenu();
                bool const autoShow = pauseZoomAutoShowMenu();

                if (autoHide && scale > 1.01f && pauseLayer->isVisible()) {
                    log::debug("[PauseZoom] update: auto-hiding PauseLayer (scale={:.3f})", scale);
                    hidePauseMenu();
                } else if (autoShow && scale <= 1.01f && !pauseLayer->isVisible() && m_menuForcedHidden) {
                    log::debug("[PauseZoom] update: auto-restoring PauseLayer (scale={:.3f})", scale);
                    restorePauseMenuVisible();
                }
            }
        }

        void onScroll(float y, float) {
            if (!m_isPaused) {
                // #region agent log
                agentLog347("PlayLayer.cpp:onScroll", "blocked_not_paused", "E", "{}");
                // #endregion
                log::debug("[PauseZoom] onScroll blocked: !m_isPaused");
                return;
            }

            auto* playLayer = PlayLayer::get();
            auto* pauseLayer = getPauseLayer();
            auto* activePause = paimon::getActivePauseLayer();
            if (!playLayer || !pauseLayer) {
                // #region agent log
#ifdef PAIMON_DEBUG_AGENT347
                {
                    std::ostringstream d;
                    d << "{\"playLayer\":" << (playLayer ? "true" : "false")
                      << ",\"pauseLayer\":" << (pauseLayer ? "true" : "false")
                      << ",\"activePause\":" << (activePause ? "true" : "false") << "}";
                    agentLog347("PlayLayer.cpp:onScroll", "blocked_missing_layer", "B", d.str());
                }
#endif
                // #endregion
                log::debug("[PauseZoom] onScroll blocked: playLayer={} pauseLayer={}", (void*)playLayer, (void*)pauseLayer);
                return;  // No auto-resume on scroll; wait for the ticker
            }

            if (hasBlockingPopup()) {
                // #region agent log
                agentLog347("PlayLayer.cpp:onScroll", "blocked_popup", "E", "{}");
                // #endregion
                log::debug("[PauseZoom] onScroll blocked: hasBlockingPopup=true");
                return;
            }

            if (pauseZoomAltDisablesScroll()) {
                if (auto* kb = CCKeyboardDispatcher::get(); kb && kb->getAltKeyPressed()) {
                    log::debug("[PauseZoom] onScroll blocked: Alt key held (alt-disables-scroll)");
                    return;
                }
            }

            float zoomDelta = getPauseZoomSensitivity() * 0.1f;
            if (paimon::smoothscroll::shouldUseSmoothPauseZoom()) {
                zoomAtMouse(-y * zoomDelta * 0.1f);
            } else if (y > 0.f) {
                zoomAtMouse(-zoomDelta);
            } else if (y < 0.f) {
                zoomAtMouse(zoomDelta);
            }

            float scale = playLayer->getScale();
            bool const autoHide = pauseZoomAutoHideMenu();
            bool const autoShow = pauseZoomAutoShowMenu();
            if (y > 0.f) {
                if (autoShow && scale <= 1.01f) {
                    restorePauseMenuVisible();
                }
            } else if (y < 0.f) {
                if (autoHide && scale > 1.01f) {
                    hidePauseMenu();
                }
            }
            // #region agent log
#ifdef PAIMON_DEBUG_AGENT347
            {
                std::ostringstream d;
                d << "{\"y\":" << y << ",\"scale\":" << scale
                  << ",\"autoHide\":" << (autoHide ? "true" : "false")
                  << ",\"autoShow\":" << (autoShow ? "true" : "false")
                  << ",\"pauseVisible\":" << (pauseLayer->isVisible() ? "true" : "false")
                  << ",\"pausePtr\":" << reinterpret_cast<uintptr_t>(pauseLayer)
                  << ",\"activePtr\":" << reinterpret_cast<uintptr_t>(activePause)
                  << ",\"ptrMatch\":" << (pauseLayer == activePause ? "true" : "false") << "}";
                agentLog347("PlayLayer.cpp:onScroll", "scroll_zoom_done", "A", d.str());
            }
#endif
            // #endregion
        }

        void togglePauseMenu() {
            if (!m_isPaused) return;
            if (hasBlockingPopup()) return;
            auto* pauseLayer = getPauseLayer();
            if (!pauseLayer) return;

            if (pauseLayer->isVisible()) {
                hidePauseMenu();
            } else {
                restorePauseMenuVisible();
            }
        }

        void zoomInStep() {
            if (!m_isPaused) return;
            if (hasBlockingPopup()) return;
            float scaleBefore = 1.f;
            if (auto* pl = PlayLayer::get()) scaleBefore = pl->getScale();
            zoomAtMouse(kPauseZoomStep);
            if (pauseZoomAutoHideMenu()) {
                if (auto* playLayer = PlayLayer::get(); playLayer && playLayer->getScale() > 1.01f) {
                    hidePauseMenu();
                }
            }
            // #region agent log
#ifdef PAIMON_DEBUG_AGENT347
            if (auto* playLayer = PlayLayer::get()) {
                auto* pauseLayer = getPauseLayer();
                std::ostringstream d;
                d << "{\"scaleBefore\":" << scaleBefore << ",\"scaleAfter\":" << playLayer->getScale()
                  << ",\"pauseVisible\":" << (pauseLayer && pauseLayer->isVisible() ? "true" : "false") << "}";
                agentLog347("PlayLayer.cpp:zoomInStep", "keybind_zoom", "A", d.str());
            }
#else
            (void)scaleBefore;
#endif
            // #endregion
        }

        void zoomOutStep() {
            if (!m_isPaused) return;
            if (hasBlockingPopup()) return;
            zoomAtMouse(-kPauseZoomStep);
            if (pauseZoomAutoShowMenu()) {
                if (auto* playLayer = PlayLayer::get(); playLayer && playLayer->getScale() <= 1.01f) {
                    restorePauseMenuVisible();
                }
            }
        }

        void reset() {
            if (m_isPaused && hasBlockingPopup()) return;
            if (auto* playLayer = PlayLayer::get()) {
                resetPlayLayerZoom(playLayer);
            }
            restorePauseMenuVisible();
        }

    private:
        bool m_isPaused = false;
        bool m_isPanning = false;
        bool m_menuForcedHidden = false;
        int m_pauseLayerMissingFrames = 0;
        CCPoint m_lastMousePos = ccp(0.f, 0.f);
        CCPoint m_deltaMousePos = ccp(0.f, 0.f);

        // Always resolve the PauseLayer fresh from the running scene. We must NOT
        // cache a raw PauseLayer* here: the PauseLayer is destroyed on resume /
        // level exit, and the per-frame ticker (PauseZoomTickerNode::update) would
        // then dereference the stale pointer. Even the "is it still valid?" check
        // (cached->getParent()) is itself a use-after-free that can read garbage
        // and return the dangling pointer, after which a virtual call such as
        // isVisible() jumps through a freed vtable — exactly the
        // EXCEPTION_ACCESS_VIOLATION reported inside libcocos2d. The lookup below
        // is a cheap scan of the scene's direct children, run at most once/frame.
        PauseLayer* getPauseLayer() {
            auto* scene = CCDirector::get() ? CCDirector::get()->getRunningScene() : nullptr;
            if (!scene) return nullptr;

            if (auto* byID = typeinfo_cast<PauseLayer*>(scene->getChildByID("PauseLayer"))) {
                return byID;
            }

            auto* children = scene->getChildren();
            if (!children) return nullptr;

            for (auto* obj : CCArrayExt<CCObject*>(children)) {
                if (auto* pl = typeinfo_cast<PauseLayer*>(obj)) {
                    return pl;
                }
            }

            return nullptr;
        }

        bool hasBlockingPopup() {
            auto* scene = CCDirector::get() ? CCDirector::get()->getRunningScene() : nullptr;
            if (!scene) return false;
            auto* pauseLayer = getPauseLayer();

            for (auto* obj : CCArrayExt<CCObject*>(scene->getChildren())) {
                auto* node = typeinfo_cast<CCNode*>(obj);
                if (!node || !node->isVisible()) continue;
                if (node == pauseLayer) continue;

                if (typeinfo_cast<FLAlertLayer*>(node)) {
                    return true;
                }

                std::string_view id = node->getID();
                if (!id.empty()) {
                    auto containsCI = [](std::string_view haystack, std::string_view needle) {
                        return std::search(haystack.begin(), haystack.end(),
                            needle.begin(), needle.end(),
                            [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; }
                        ) != haystack.end();
                    };
                    if (containsCI(id, "popup") || containsCI(id, "alert")) {
                        return true;
                    }
                }
            }

            return false;
        }

        void hidePauseMenu() {
            auto* pauseLayer = getPauseLayer();
            auto* activePause = paimon::getActivePauseLayer();
            bool visBefore = pauseLayer ? pauseLayer->isVisible() : false;
            if (pauseLayer) {
                if (pauseLayer->isVisible()) {
                    pauseLayer->setVisible(false);
                }
                // Set the GLOBAL zoom-hidden flag; the visit() filter checks it
                // and returns early. Needed because setVisible(false) alone isn't
                // persistent: GD/CCBlockLayer/another mod restores m_bVisible
                // every frame (verified in debug-347aef.log).
                paimon::setPauseZoomHidden(true);
                // Also disable touch so clicks where the menu would be don't
                // trigger invisible buttons.
                pauseLayer->setTouchEnabled(false);
                m_menuForcedHidden = true;
            }
            // #region agent log
#ifdef PAIMON_DEBUG_AGENT347
            {
                std::ostringstream d;
                d << "{\"visBefore\":" << (visBefore ? "true" : "false")
                  << ",\"visAfter\":" << (pauseLayer && pauseLayer->isVisible() ? "true" : "false")
                  << ",\"pausePtr\":" << reinterpret_cast<uintptr_t>(pauseLayer)
                  << ",\"activePtr\":" << reinterpret_cast<uintptr_t>(activePause)
                  << ",\"ptrMatch\":" << (pauseLayer == activePause ? "true" : "false") << "}";
                agentLog347("PlayLayer.cpp:hidePauseMenu", "hide_called", "B", d.str());
            }
#else
            (void)visBefore;
            (void)activePause;
#endif
            // #endregion
        }

        void restorePauseMenuVisible() {
            auto* pauseLayer = getPauseLayer();
            bool visBefore = pauseLayer ? pauseLayer->isVisible() : false;
            if (pauseLayer) {
                if (pauseLayer->getParent() && !pauseLayer->isVisible()) {
                    pauseLayer->setVisible(true);
                }
                // Re-enable touch (disabled in hidePauseMenu).
                pauseLayer->setTouchEnabled(true);
            }
            // Clear the zoom-hidden flag; visit() will render the PauseLayer normally again.
            paimon::setPauseZoomHidden(false);
            m_menuForcedHidden = false;
            // #region agent log
#ifdef PAIMON_DEBUG_AGENT347
            {
                std::ostringstream d;
                d << "{\"visBefore\":" << (visBefore ? "true" : "false")
                  << ",\"visAfter\":" << (pauseLayer && pauseLayer->isVisible() ? "true" : "false") << "}";
                agentLog347("PlayLayer.cpp:restorePauseMenuVisible", "restore_called", "D", d.str());
            }
#else
            (void)visBefore;
#endif
            // #endregion
        }

        void zoomAtMouse(float delta) {
            auto* playLayer = PlayLayer::get();
            if (!playLayer) return;

            auto contentSize = playLayer->getContentSize();
            auto screenSize = getGameplayScreenSize();
            if (contentSize.width <= 0.0f || contentSize.height <= 0.0f) return;
            if (screenSize.width <= 0.0f || screenSize.height <= 0.0f) return;

            float oldScale = std::max(playLayer->getScale(), 0.001f);
            float newScale = oldScale;
            if (delta < 0.0f) {
                newScale = oldScale / (1.0f - delta);
            } else if (delta > 0.0f) {
                newScale = oldScale * (1.0f + delta);
            }
            newScale = clampZoomValue(newScale, kPauseZoomMin, kPauseZoomMax);

            CCPoint mousePos = cocos::getMousePos();
            CCPoint anchorPoint = {
                mousePos.x - contentSize.width * 0.5f,
                mousePos.y - contentSize.height * 0.5f
            };

            CCPoint deltaFromAnchor = playLayer->getPosition() - anchorPoint;
            playLayer->setPosition(anchorPoint);
            playLayer->setScale(newScale);
            playLayer->setPosition(anchorPoint + deltaFromAnchor * (newScale / oldScale));
            clampPlayLayerZoomPosition(playLayer);
        }
    };

    float clampZoomValue(float value, float minValue, float maxValue) {
        return std::max(minValue, std::min(value, maxValue));
    }

    CCSize getGameplayScreenSize() {
        auto* director = CCDirector::get();
        if (!director) return { 0.0f, 0.0f };
        return director->getWinSize();
    }

    void resetPlayLayerZoom(CCNode* playLayer) {
        if (!playLayer) return;
        playLayer->setScale(1.0f);
        playLayer->setPosition({ 0.0f, 0.0f });
    }

    void clampPlayLayerZoomPosition(CCNode* playLayer) {
        if (!playLayer) return;

        auto screenSize = getGameplayScreenSize();
        auto contentSize = playLayer->getContentSize();
        if (screenSize.width <= 0.0f || screenSize.height <= 0.0f) return;
        if (contentSize.width <= 0.0f || contentSize.height <= 0.0f) return;

        auto pos = playLayer->getPosition();
        float scale = std::max(playLayer->getScale(), kPauseZoomMin);
        float xLimit = std::max(0.0f, (contentSize.width * scale - screenSize.width) * 0.5f);
        float yLimit = std::max(0.0f, (contentSize.height * scale - screenSize.height) * 0.5f);

        pos.x = clampZoomValue(pos.x, -xLimit, xLimit);
        pos.y = clampZoomValue(pos.y, -yLimit, yLimit);
        playLayer->setPosition(pos);
    }

    bool isNonGameplayOverlay(CCNode* node, bool checkZ) {
        if (!node) return false;

        if (typeinfo_cast<PlayerObject*>(node)) return false;

        if (checkZ && node->getZOrder() >= 10) return true;

        if (typeinfo_cast<UILayer*>(node)) return true;
        if (typeinfo_cast<PauseLayer*>(node)) return true;
        if (typeinfo_cast<CCMenu*>(node)) return true;
        if (typeinfo_cast<FLAlertLayer*>(node)) return true;
        if (typeinfo_cast<EditorPauseLayer*>(node)) return true;
        if (typeinfo_cast<CCLabelBMFont*>(node)) {
            if (checkZ && node->getZOrder() >= 10) return true;
        }

        std::string_view id = node->getID();
        if (!id.empty()) {
            static constexpr std::string_view patterns[] = {
                "ui", "uilayer", "pause", "menu", "dialog", "popup", "editor",
                "notification", "btn", "button", "overlay", "checkpoint",
                "fps", "debug", "attempt", "percent", "progress", "bar",
                "score", "practice", "hitbox", "trajectory", "status"
            };
            auto containsCI = [](std::string_view haystack, std::string_view needle) {
                return std::search(haystack.begin(), haystack.end(),
                    needle.begin(), needle.end(),
                    [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; }
                ) != haystack.end();
            };
            for (auto p : patterns) {
                if (containsCI(id, p)) return true;
            }
        }

        return false;
    }

    void hideNonGameplayDescendants(CCNode* root, std::vector<CCNode*>& hidden, bool checkZ, PlayLayer* pl) {
        if (!root) return;
        auto* children = root->getChildren();
        if (!children) return;

        for (auto* obj : CCArrayExt<CCObject*>(children)) {
            auto* node = typeinfo_cast<CCNode*>(obj);
            if (!node) continue;

            if (pl) {
                if (node == pl->m_player1 || node == pl->m_player2) continue;
            }

            if (node->isVisible() && isNonGameplayOverlay(node, checkZ)) {
                node->setVisible(false);
                hidden.push_back(node);
            }
            else {
                std::string cls = typeid(*node).name();
                if (cls.find("CCNode") != std::string::npos || cls.find("Layer") != std::string::npos) {
                    if (cls.find("GameLayer") == std::string::npos) {
                        hideNonGameplayDescendants(node, hidden, false, pl);
                    }
                }
            }
        }
    }
}

namespace paimon {
    void notifyPauseClosing() {
        log::debug("[PauseZoom] notifyPauseClosing() → onResume()");
        PauseZoomManager::get().onResume();
    }
}

// Processes the captured RGBA buffer and uploads the thumbnail: extracts
// dominant colors, encodes PNG, and routes the upload (pending vs live).
// Shared by the keybind capture path (init) and captureScreenshot(). Caller
// validates W/H and okSave/buf first. The heavy CPU work (color K-means +
// PNG encode, ~300-500ms at 1080p) runs on a worker thread — doing it inline
// froze the frame every time the user accepted a capture.
static void uploadCapturedThumbnail(int levelID, std::shared_ptr<uint8_t> const& buf, int W, int H) {
    std::string username;
    int accountID = 0;
    if (auto* gm = GameManager::sharedState()) {
        username = gm->m_playerName;
        if (auto* am = GJAccountManager::get()) accountID = am->m_accountID;
    }
    if (username.empty()) username = "unknown";
    if (accountID <= 0) {
        PaimonNotify::create(Localization::get().getString("level.account_required"), NotificationIcon::Error)->show();
        return;
    }

    // Collect the full level metadata on the main thread (GJGameLevel bindings)
    // so it can be attached to the upload.
    std::string levelMeta;
    if (auto* pl = PlayLayer::get()) {
        levelMeta = paimon::collectLevelMetadata(pl->m_level);
    }

    PaimonNotify::show(Localization::get().getString("capture.uploading"), geode::NotificationIcon::Info);

    paimon::ThreadTracker::get().spawn([levelID, buf, W, H, username, levelMeta]() {
        geode::utils::thread::setName("Paimon Capture Upload");
        if (paimon::isRuntimeShuttingDown()) return;

        std::vector<uint8_t> rgbData(static_cast<size_t>(W) * H * 3);
        ImageConverter::rgbaToRgbFast(buf.get(), rgbData.data(), static_cast<size_t>(W) * H);
        auto pair = DominantColors::extract(rgbData.data(), W, H);
        ccColor3B A{pair.first.r, pair.first.g, pair.first.b};
        ccColor3B B{pair.second.r, pair.second.g, pair.second.b};

        std::vector<uint8_t> pngData;
        bool encoded = ImageConverter::rgbaToPngBuffer(
            buf.get(), static_cast<uint32_t>(W), static_cast<uint32_t>(H), pngData);

        Loader::get()->queueInMainThread(
            [levelID, username, A, B, encoded, levelMeta, pngData = std::move(pngData)]() mutable {
                if (paimon::isRuntimeShuttingDown()) return;
                LevelColors::get().set(levelID, A, B);

                if (!encoded) {
                    PaimonNotify::create(Localization::get().getString("capture.save_png_error"), NotificationIcon::Error)->show();
                    return;
                }

                ThumbnailAPI::get().uploadThumbnail(levelID, pngData, username, [levelID, username](bool success, std::string const& msg) {
                    if (success) {
                        bool isPending = (msg.find("pending") != std::string::npos || msg.find("verification") != std::string::npos);
                        if (isPending) {
                            PendingQueue::get().addOrBump(levelID, PendingCategory::Verify, username, {}, false);
                            PaimonNotify::create(Localization::get().getString("capture.suggested"), NotificationIcon::Success)->show();
                        } else {
                            PendingQueue::get().removeForLevel(levelID);
                            PaimonNotify::create(Localization::get().getString("capture.upload_success"), NotificationIcon::Success)->show();
                        }
                    } else {
                        PaimonNotify::create(
                            Localization::get().getString("capture.upload_error") + (msg.empty() ? std::string("") : (" (" + msg + ")")),
                            NotificationIcon::Error
                        )->show();
                    }
                }, levelMeta);
            });
    });
}

static std::atomic<bool> s_hideP1ForCapture{false};
static std::atomic<bool> s_hideP2ForCapture{false};

static void ensurePauseZoomTicker();

class $modify(PaimonCapturePlayLayer, PlayLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("PlayLayer::init", geode::Priority::VeryLate);
        // VeryEarly so we record the death tick before noclip-style mods hook
        // destroyPlayer and short-circuit it; otherwise the capture death guard
        // would never see deaths those mods cancel.
        (void)self.setHookPriorityPre("PlayLayer::destroyPlayer", geode::Priority::VeryEarly);
    }

    struct Fields {
        float m_frameTimer = 0.0f;
        std::chrono::steady_clock::time_point m_forYouSessionStart;
    };

    $override
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        AudioContextCoordinator::get().notifyGameplayStarted();

        s_hideP1ForCapture = false;
        s_hideP2ForCapture = false;
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        PauseZoomManager::get().resetForNewLevel();
        // Clear any death tick carried over from a previous level/attempt.
        paimon::capture::clearDeathTick();

        if (Mod::get()->getSettingValue<bool>("enable-thumbnail-taking")) {
            this->addEventListener(
                KeybindSettingPressedEventV3(Mod::get(), "capture-keybind"),
                [this](Keybind const& keybind, bool down, bool repeat, double timestamp) {
                    if (!down || repeat) return;
                    if (PlayLayer::get() != this) return;
                    if (this->m_isPaused) return;
                    // Race-condition guard: if the user presses Esc + T on the
                    // same frame, GD may have created a PauseLayer before
                    // m_isPaused propagates visibly. Without this check, the
                    // keybind's async callback could show a CapturePreviewPopup
                    // over the PauseLayer, freezing the pause menu behind the
                    // popup (inconsistent state that crashes on GD exit).
                    if (paimon::hasPauseLayerInScene()) return;
                    if (!this->m_level || this->m_level->m_levelID <= 0) return;

                    bool expected = false;
                    if (!gCaptureInProgress.compare_exchange_strong(expected, true)) return;

                    // Also mutual exclusion with path A (PauseLayer button): if a
                    // capture is already in progress from the PauseLayer, don't start another.
                    if (paimon::isCaptureInProgress()) {
                        gCaptureInProgress.store(false);
                        return;
                    }
                    paimon::setCaptureInProgress(true);

                    auto validation = FramebufferCapture::validateCaptureConditions();
                    if (!validation.canCapture) {
                        gCaptureInProgress.store(false);
                        paimon::setCaptureInProgress(false);
                        Notification::create(validation.reason, NotificationIcon::Warning)->show();
                        return;
                    }

                    if (auto* engine = FMODAudioEngine::sharedEngine()) {
                        if (engine->m_backgroundMusicChannel) {
                            engine->m_backgroundMusicChannel->setPaused(true);
                        }
                    }

                    int levelID = this->m_level->m_levelID;
                    geode::WeakRef<PlayLayer> weakRef = this;
                    FramebufferCapture::requestCapture(levelID, [weakRef, levelID](bool success, CCTexture2D* texture, std::shared_ptr<uint8_t> rgbaData, int width, int height) {
                        Ref<CCTexture2D> texRef = texture;
                        Loader::get()->queueInMainThread([weakRef, success, texRef, rgbaData, width, height, levelID]() {
                            if (paimon::isRuntimeShuttingDown()) {
                                gCaptureInProgress.store(false);
                                paimon::setCaptureInProgress(false);
                                if (auto* engine = FMODAudioEngine::sharedEngine()) {
                                    if (engine->m_backgroundMusicChannel) engine->m_backgroundMusicChannel->setPaused(false);
                                }
                                return;
                            }
                            CCTexture2D* texture = texRef.data();
                            // Local helper to clear all state and restore music.
                            // Used on ALL early-exit paths to avoid leaking flags.
                            auto cleanup = []() {
                                gCaptureInProgress.store(false);
                                paimon::setCaptureInProgress(false);
                                if (auto* engine = FMODAudioEngine::sharedEngine()) {
                                    if (engine->m_backgroundMusicChannel) engine->m_backgroundMusicChannel->setPaused(false);
                                }
                            };
                            auto locked = weakRef.lock();
                            if (!locked) {
                                cleanup();
                                return;
                            }
                            auto* self = static_cast<PaimonCapturePlayLayer*>(locked.data());
                            if (!self->getParent()) {
                                cleanup();
                                return;
                            }

                            if (!success || !texture || !rgbaData) {
                                cleanup();
                                PaimonNotify::create(
                                    Localization::get().getString("pause.capture_error").c_str(),
                                    NotificationIcon::Error
                                )->show();
                                return;
                            }

                            // Race-condition guard 2: if Esc was pressed during
                            // the ~50ms between requestCapture and the async
                            // callback, a PauseLayer is now on screen. Showing the
                            // CapturePreviewPopup over it would freeze the pause
                            // menu behind it. Abort cleanly: the user can use the
                            // PauseLayer's capture button (path A), which
                            // coordinates with the pause menu.
                            if (paimon::hasPauseLayerInScene() || self->m_isPaused) {
                                log::warn("[CaptureKeybind] PauseLayer aparecido durante captura, abortando para evitar UI inconsistente");
                                cleanup();
                                return;
                            }

                            // From here, clear only the local gCaptureInProgress
                            // flag; the global flag is cleared in the popup
                            // callback or on cancellation.
                            paimon::setCaptureInProgress(false);

                            bool pausedByPopup = false;
                            if (!self->m_isPaused) { self->pauseGame(true); pausedByPopup = true; }

                            auto* popup = CapturePreviewPopup::create(
                                texture, levelID, rgbaData, width, height,
                                [levelID, pausedByPopup](bool okSave, int levelIDAccepted, std::shared_ptr<uint8_t> buf, int W, int H, std::string mode, std::string replaceId){
                                    gCaptureInProgress.store(false);
                                    if (pausedByPopup) {
                                        // Resume PlayLayer from the main thread for thread-safety
                                        geode::Loader::get()->queueInMainThread([levelID]() {
                                            if (paimon::isRuntimeShuttingDown()) return;
                                            auto* pl = PlayLayer::get();
                                            if (pl && pl->m_isPaused) {
                                                bool hasPause = false;
                                                if (auto* dir = CCDirector::get()) {
                                                    if (auto* sc = dir->getRunningScene()) {
                                                        CCArrayExt<CCNode*> children(sc->getChildren());
                                                        for (auto child : children) { 
                                                            if (typeinfo_cast<PauseLayer*>(child)) { hasPause = true; break; } 
                                                        }
                                                    }
                                                }
                                                if (!hasPause) {
                                                    if (auto* d = CCDirector::get()) {
                                                        if (d->getScheduler() && d->getActionManager()) {
                                                            d->getScheduler()->resumeTarget(pl);
                                                            d->getActionManager()->resumeTarget(pl);
                                                            pl->m_isPaused = false;
                                                            PauseZoomManager::get().onResume();
                                                        }
                                                    }
                                                }
                                            }
                                        });
                                    }
                                    if (okSave && levelIDAccepted > 0 && buf) {
                                        if (W <= 0 || H <= 0) return;
                                        uploadCapturedThumbnail(levelIDAccepted, buf, W, H);
                                    }
                                },
                                [weakRef](bool hideP1, bool hideP2, CapturePreviewPopup* popup) {
                                    s_hideP1ForCapture = hideP1; s_hideP2ForCapture = hideP2;
                                    if (popup) popup->setVisible(false);
                                    gCaptureInProgress.store(false);
                                    // popup may be destroyed before the queued lambda runs (the
                                    // user can dismiss it between frames). Capture by WeakRef so
                                    // we never dereference a dangling pointer.
                                    WeakRef<CapturePreviewPopup> weakPopup = popup;
                                    Loader::get()->queueInMainThread([weakRef, weakPopup]() {
                                        if (paimon::isRuntimeShuttingDown()) return;
                                        auto locked = weakRef.lock(); if (!locked) return;
                                        auto* self = static_cast<PaimonCapturePlayLayer*>(locked.data());
                                        if (!self || !self->getParent()) return;
                                        auto popupLocked = weakPopup.lock();
                                        auto* popupPtr = popupLocked
                                            ? static_cast<CapturePreviewPopup*>(popupLocked.data())
                                            : nullptr;
                                        self->captureScreenshot(popupPtr);
                                    });
                                },
                                s_hideP1ForCapture, s_hideP2ForCapture
                            );
                            if (popup) { popup->setPausedMusic(true); popup->show(); }
                            else { gCaptureInProgress.store(false); }
                        });
                    });
                }
            );
        }

        this->addEventListener(
            KeybindSettingPressedEventV3(Mod::get(), "zoom-in-keybind"),
            [](Keybind const&, bool down, bool repeat, double) {
                if (!down || repeat) return;
                PauseZoomManager::get().zoomInStep();
            }
        );

        this->addEventListener(
            KeybindSettingPressedEventV3(Mod::get(), "zoom-out-keybind"),
            [](Keybind const&, bool down, bool repeat, double) {
                if (!down || repeat) return;
                PauseZoomManager::get().zoomOutStep();
            }
        );

        this->addEventListener(
            KeybindSettingPressedEventV3(Mod::get(), "zoom-reset-keybind"),
            [](Keybind const&, bool down, bool repeat, double) {
                if (!down || repeat) return;
                PauseZoomManager::get().reset();
            }
        );

        this->addEventListener(
            KeybindSettingPressedEventV3(Mod::get(), "zoom-toggle-menu-keybind"),
            [](Keybind const&, bool down, bool repeat, double) {
                if (!down || repeat) return;
                PauseZoomManager::get().togglePauseMenu();
            }
        );

        m_fields->m_forYouSessionStart = std::chrono::steady_clock::now();
        paimon::foryou::ForYouTracker::get().onLevelEnter(this->m_level);

        if (paimon::compat::ModCompat::isLevelTagsLoaded() && this->m_level && this->m_level->m_levelID > 0) {
            int levelID = this->m_level->m_levelID;
            paimon::foryou::LevelTagsIntegration::get().fetchTagsForLevel(levelID,
                [levelID](std::vector<std::string> tags) {
                    if (!tags.empty()) paimon::foryou::ForYouTracker::get().onLevelTagsFetched(levelID, tags);
                });
        }

        return true;
    }
    
    $override
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        // Record the progress tick of real deaths (ignore anticheat-spike
        // pseudo-deaths) so validateCaptureConditions() can reject captures
        // taken on or right after the death frame. VeryEarly hook priority
        // ensures this runs before noclip mods that cancel the death.
        if (object != this->m_anticheatSpike) {
            paimon::capture::recordDeathTick(this->m_gameState.m_currentProgress);
        } else {
            paimon::capture::clearDeathTick();
        }
        PlayLayer::destroyPlayer(player, object);
    }

    $override
    void onQuit() {
        PauseZoomManager::get().onResume();
        FramebufferCapture::cancelPending();
        CaptureLayerEditorPopup::discardTrackedLayers();
        CaptureAssetBrowserPopup::discardTrackedAssets();
        gCaptureInProgress.store(false);

        paimon::foryou::ForYouTracker::get().onLevelExit(this->m_level);

        {
            auto& sm = paimon::menuloop::MenuLoopManager::get();
            const bool randomize = Mod::get()->getSettingValue<bool>("menuLoopRandomizeOnLevelExit");
            const bool restore = Mod::get()->getSettingValue<bool>("menuLoopRestoreOnLevelExit");
            if (randomize) { sm.setShouldRestoreMenuLoopPoint(false); paimon::menuloop::MenuLoopControl::shuffleSong(); }
            else if (restore) { sm.setShouldRestoreMenuLoopPoint(true); }
        }

        PlayLayer::onQuit();
    }

    void captureScreenshot(CapturePreviewPopup* existingPopup = nullptr) {
        if (gCaptureInProgress.load()) return;
        if (!this->m_level || this->m_level->m_levelID <= 0) return;

        auto validation = FramebufferCapture::validateCaptureConditions();
        if (!validation.canCapture) {
            PaimonNotify::create(validation.reason.c_str(), NotificationIcon::Warning)->show();
            if (existingPopup) existingPopup->setVisible(true);
            return;
        }

        gCaptureInProgress.store(true);
        paimon::setCaptureInProgress(true);

        int const levelID = this->m_level->m_levelID;
        bool const hideP1 = s_hideP1ForCapture.load();
        bool const hideP2 = s_hideP2ForCapture.load();
        WeakRef<PaimonCapturePlayLayer> self = this;
        WeakRef<CapturePreviewPopup> weakPopup = existingPopup;

        FramebufferCapture::requestCapture(levelID,
            [self, weakPopup, levelID, hideP1, hideP2](bool success, CCTexture2D* texture,
                std::shared_ptr<uint8_t> rgbaData, int width, int height) {
                Ref<CCTexture2D> texRef = texture;
                Loader::get()->queueInMainThread([self, weakPopup, success, texRef, rgbaData, width, height, levelID, hideP1, hideP2]() {
                    auto cleanup = []() {
                        gCaptureInProgress.store(false);
                        paimon::setCaptureInProgress(false);
                    };

                    if (paimon::isRuntimeShuttingDown()) {
                        cleanup();
                        return;
                    }

                    auto layer = self.lock();
                    if (!layer || !layer->getParent()) {
                        cleanup();
                        return;
                    }

                    auto popupLocked = weakPopup.lock();
                    auto* popupPtr = popupLocked
                        ? static_cast<CapturePreviewPopup*>(popupLocked.data())
                        : nullptr;

                    cleanup();

                    if (!success || !texRef.data() || !rgbaData) {
                        PaimonNotify::create(
                            Localization::get().getString("pause.capture_error").c_str(),
                            NotificationIcon::Error
                        )->show();
                        if (popupPtr) popupPtr->setVisible(true);
                        return;
                    }

                    if (popupPtr) {
                        popupPtr->updateContent(texRef.data(), rgbaData, width, height);
                        popupPtr->setVisible(true);
                        return;
                    }

                    // (5) Same guard as the primary keybind path: if Esc brought
                    // up a PauseLayer while the recapture was in flight, never
                    // open a fresh CapturePreviewPopup on top of it (freezes the
                    // pause menu behind it). Abort cleanly instead.
                    if (paimon::hasPauseLayerInScene() || layer->m_isPaused) {
                        log::warn("[CaptureKeybind] PauseLayer presente durante recaptura; "
                                  "abortando para no montar el preview sobre la pausa");
                        return;
                    }

                    bool pausedByPopup = false;
                    if (!layer->m_isPaused) {
                        layer->pauseGame(true);
                        pausedByPopup = true;
                    }

                    auto* popup = CapturePreviewPopup::create(
                        texRef.data(), levelID, rgbaData, width, height,
                        [levelID, pausedByPopup](bool okSave, int levelIDAccepted, std::shared_ptr<uint8_t> buf, int W, int H, std::string mode, std::string replaceId) {
                            gCaptureInProgress.store(false);
                            if (pausedByPopup) {
                                Loader::get()->queueInMainThread([]() {
                                    if (paimon::isRuntimeShuttingDown()) return;
                                    auto* pl = PlayLayer::get();
                                    if (pl && pl->m_isPaused) {
                                        bool hasPause = false;
                                        if (auto* sc = CCDirector::get()->getRunningScene()) {
                                            for (auto child : CCArrayExt<CCNode*>(sc->getChildren())) {
                                                if (typeinfo_cast<PauseLayer*>(child)) { hasPause = true; break; }
                                            }
                                        }
                                        if (!hasPause) {
                                            if (auto* d = CCDirector::get(); d && d->getScheduler() && d->getActionManager()) {
                                                d->getScheduler()->resumeTarget(pl);
                                                d->getActionManager()->resumeTarget(pl);
                                                pl->m_isPaused = false;
                                                PauseZoomManager::get().onResume();
                                            }
                                        }
                                    }
                                });
                            }
                            if (okSave && levelIDAccepted > 0 && buf) {
                                uploadCapturedThumbnail(levelIDAccepted, buf, W, H);
                            }
                        },
                        [self](bool hideP1, bool hideP2, CapturePreviewPopup* popup) {
                            s_hideP1ForCapture.store(hideP1);
                            s_hideP2ForCapture.store(hideP2);
                            if (popup) popup->setVisible(false);
                            gCaptureInProgress.store(false);
                            WeakRef<CapturePreviewPopup> popupWeak = popup;
                            Loader::get()->queueInMainThread([self, popupWeak]() {
                                if (paimon::isRuntimeShuttingDown()) return;
                                auto layer = self.lock();
                                if (!layer) return;
                                auto popupLocked = popupWeak.lock();
                                if (!popupLocked) return;
                                layer->captureScreenshot(static_cast<CapturePreviewPopup*>(popupLocked.data()));
                            });
                        },
                        hideP1, hideP2
                    );

                    if (popup) popup->show();
                });
            },
            nullptr,
            hideP1,
            hideP2
        );
    }

    $override
    void pauseGame(bool value) {
        log::debug("[PauseZoom] pauseGame({}) called", value);
        ensurePauseZoomTicker();
        if (value) {
            PauseZoomManager::get().onPause();
        } else {
            PauseZoomManager::get().onResume();
        }
        PlayLayer::pauseGame(value);
    }

    $override
    void startGame() {
        PauseZoomManager::get().onResume();
        PlayLayer::startGame();
    }
};

class PauseZoomTickerNode : public CCNode {
public:
    static PauseZoomTickerNode* create() {
        auto ret = new PauseZoomTickerNode();
        if (ret->init()) { ret->autorelease(); return ret; }
        delete ret; return nullptr;
    }
    bool init() override {
        if (!CCNode::init()) return false;
        this->setID("paimon-pause-zoom-ticker"_spr);
        return true;
    }
    void update(float dt) override {
        if (!PlayLayer::get()) return;
        PauseZoomManager::get().update(dt);
    }
};

static Ref<PauseZoomTickerNode> s_pauseZoomTicker = nullptr;

static void ensurePauseZoomTicker() {
    if (s_pauseZoomTicker) return;
    auto* director = CCDirector::get();
    if (!director) return;
    auto* scheduler = director->getScheduler();
    if (!scheduler) return;
    s_pauseZoomTicker = PauseZoomTickerNode::create();
    if (!s_pauseZoomTicker) return;
    scheduler->scheduleUpdateForTarget(s_pauseZoomTicker.data(), 0, false);
}

static void shutdownPauseZoomTicker() {
    if (!s_pauseZoomTicker) return;
    if (auto* director = CCDirector::get()) {
        if (auto* scheduler = director->getScheduler()) {
            scheduler->unscheduleUpdateForTarget(s_pauseZoomTicker.data());
        }
    }
    (void)s_pauseZoomTicker.take();
}

$on_game(Exiting) {
    shutdownPauseZoomTicker();
}

// CCNode::visit hook to skip rendering the PauseLayer during zoom.
// Hooked globally on CCNode (not as a PauseLayer override) because visit() isn't
// in PauseLayer's modify-binding, so $override void visit() there would hook
// nothing. Cheap: an atomic-bool early-out plus a pointer comparison (~5ns/visit).
// Robust where setVisible(false) failed: GD/another mod restores m_bVisible every
// frame, but skipping visit() avoids drawing the node and its children regardless.
class $modify(PaimonPauseZoomVisitFilter, CCNode) {
    static void onModify(auto& self) {
        // Priority Late: run after other mods that may legitimately hook
        // CCNode::visit (devtools, debug overlays). If something wants to see the
        // PauseLayer during zoom, it calls the original before our filter, which is correct.
        (void)self.setHookPriorityPre("cocos2d::CCNode::visit", geode::Priority::Late);

        // CCNode::visit fires for every node every frame (easily 10k+ calls/frame
        // in a busy level), so even this cheap filter pays trampoline overhead on
        // all of them. Keep the hook dormant and let setPauseZoomHidden() enable
        // it only while the PauseLayer is hidden by pause-zoom. If getHook fails,
        // the hook auto-enables as before (correctness over speed).
        if (auto hook = self.getHook("cocos2d::CCNode::visit")) {
            hook.unwrap()->setAutoEnable(false);
            paimon::setPauseZoomVisitHook(hook.unwrap());
        }
    }

    void visit() {
        // Fast path: only matters when the zoom-hidden flag is set. Most game
        // frames aren't in pause-zoom, so this is the hot branch.
        if (paimon::isPauseZoomHidden()) {
            // Direct cast to CCNode* for comparison (PauseLayer extends CCNode
            // via CCBlockLayer/CCLayerColor/CCLayer/CCLayerRGBA).
            auto* activePause = static_cast<CCNode*>(paimon::getActivePauseLayer());
            if (activePause && this == activePause) {
                // Skip rendering the PauseLayer and all its descendants.
                return;
            }
        }
        CCNode::visit();
    }
};

namespace paimon::pausezoom {
    void dispatchScroll(float y, float x) {
        PauseZoomManager::get().onScroll(y, x);
    }
}
