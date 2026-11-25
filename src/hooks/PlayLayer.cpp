#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCNode.hpp>
#include <Geode/modify/CCCircleWave.hpp>
#include <Geode/modify/CCLayerGradient.hpp>
#include <Geode/modify/CCParticleBatchNode.hpp>
#include <Geode/modify/CCParticleSystem.hpp>
#include <Geode/modify/CCParticleSystemQuad.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/ShaderLayer.hpp>
#include <Geode/binding/GhostTrailEffect.hpp>
#include <Geode/binding/HardStreak.hpp>
#include "../utils/PaimonNotification.hpp"
#include "../utils/Debug.hpp"
#include "../core/modules/ModuleRegistry.hpp"
#include <Geode/loader/SettingV3.hpp>
#include <Geode/utils/Keyboard.hpp>
#include "../features/capture/ui/CapturePreviewPopup.hpp"
#include "../features/capture/ui/CaptureMenuPopup.hpp"
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
#include "../features/foryou/services/TasteProfile.hpp"
#include "../features/foryou/services/LevelTagsClient.hpp"
#include "../features/transitions/services/LevelEntryEffects.hpp"
#include "../features/gameplay-performance/GameplayPerformance.hpp"
#include "../features/dynamic-volume/services/DynamicVolumeManager.hpp"
#include "../framework/compat/ModCompat.hpp"
#include "../features/smooth-scroll/services/SmoothScrollController.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include "../utils/ActivePauseLayer.hpp"
#include "../utils/SceneIdentity.hpp"
#include "../utils/ExtendedKeybind.hpp"
#include "../utils/ThreadTracker.hpp"
#include <algorithm>
#include <cstring>
#include <memory>
#include <chrono>
#include <cstdint>
#include <initializer_list>

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#endif

#include "../features/dynamic-songs/services/DynamicSongManager.hpp"
#include "../features/menu-loop/services/MenuLoopManager.hpp"
#include "../features/menu-loop/services/MenuLoopControl.hpp"
#include "../features/menu-music/services/MenuMusicPlayer.hpp"
#include "../features/menu-music/services/SpotifyService.hpp"

using namespace geode::prelude;

namespace {
    // keybind-capture flow guard (keypress until popup close), distinct from
    // iscaptureinprogress() (gpu flight only); both are checked on acquire
    std::atomic_bool s_captureFlowActive{false};
    std::atomic<std::uint64_t> s_captureFlowGeneration{0};
    constexpr float kPauseZoomStep = 0.18f;
    constexpr float kPauseZoomMin = 1.0f;
    constexpr float kPauseZoomMax = 4.0f;

    void hideNodes(std::initializer_list<CCNode*> nodes) {
        for (auto* node : nodes) {
            if (node) node->setVisible(false);
        }
    }

    void disablePlayerEffects(PlayerObject* player) {
        if (!player) return;

        player->m_playEffects = false;
        player->m_maybeReducedEffects = true;
        player->m_hasGroundParticles = false;
        player->m_hasShipParticles = false;

        if (player->m_ghostTrail) player->m_ghostTrail->stopTrail();
        if (player->m_waveTrail) player->m_waveTrail->stopStroke();

        hideNodes({
            player->m_ghostTrail,
            player->m_regularTrail,
            player->m_shipStreak,
            player->m_waveTrail,
            player->m_playerGroundParticles,
            player->m_trailingParticles,
            player->m_shipClickParticles,
            player->m_vehicleGroundParticles,
            player->m_ufoClickParticles,
            player->m_robotBurstParticles,
            player->m_dashParticles,
            player->m_swingBurstParticles1,
            player->m_swingBurstParticles2,
            player->m_landParticles0,
            player->m_landParticles1,
        });
    }

    void applyConfiguredVisualCuts(PlayLayer* playLayer) {
        if (!playLayer) return;

        if (paimon::gameplayperf::isOptionActive(
                paimon::gameplayperf::kBackgroundModuleId)) {
            hideNodes({playLayer->m_background, playLayer->m_middleground});
        }
        if (paimon::gameplayperf::isOptionActive(
                paimon::gameplayperf::kGroundModuleId)) {
            hideNodes({playLayer->m_groundLayer, playLayer->m_groundLayer2});
        }
        if (paimon::gameplayperf::isOptionActive(
                paimon::gameplayperf::kLevelEffectsModuleId)) {
            hideNodes({
                playLayer->m_flashNode,
                playLayer->m_glowLayerT4,
                playLayer->m_glowLayerT3,
                playLayer->m_glowLayerT2,
                playLayer->m_glowLayerT1,
                playLayer->m_glowLayerB1,
                playLayer->m_glowLayerB2,
                playLayer->m_glowLayerB3,
                playLayer->m_glowLayerB4,
                playLayer->m_glowLayerB5,
            });
        }
        if (paimon::gameplayperf::isOptionActive(
                paimon::gameplayperf::kParticlesModuleId)) {
            hideNodes({
                playLayer->m_particleLayerT4,
                playLayer->m_particleLayerT3,
                playLayer->m_particleLayerT2,
                playLayer->m_particleLayerT1,
                playLayer->m_particleLayerB1,
                playLayer->m_particleLayerB2,
                playLayer->m_particleLayerB3,
                playLayer->m_particleLayerB4,
                playLayer->m_particleLayerB5,
                playLayer->m_particleBlendingLayerT5,
                playLayer->m_particleBlendingLayerT4,
                playLayer->m_particleBlendingLayerT3,
                playLayer->m_particleBlendingLayerT2,
                playLayer->m_particleBlendingLayerT1,
                playLayer->m_particleBlendingLayerB1,
                playLayer->m_particleBlendingLayerB2,
                playLayer->m_particleBlendingLayerB3,
                playLayer->m_particleBlendingLayerB4,
                playLayer->m_particleBlendingLayerB5,
            });
        }
        if (paimon::gameplayperf::isOptionActive(
                paimon::gameplayperf::kDecorationModuleId) && playLayer->m_objects) {
            for (auto* object : CCArrayExt<GameObject*>(playLayer->m_objects)) {
                if (object && object->m_isDecoration) object->setVisible(false);
            }
        }
    }

    // E2: the pause-zoom tick and scroll handlers read these on every frame/
    // event. a frame-gated cache serves every entry point from one refresh
    // per frame; values always converge on the next tick, so a settings edit
    // can lag by at most one frame instead of going stale.
    struct PauseZoomSettings {
        float sensitivity = 1.0f;
        bool autoHideMenu = true;
        bool autoShowMenu = true;
        bool altDisablesScroll = true;
    };

    float clampZoomValue(float value, float minValue, float maxValue);
    CCSize getGameplayScreenSize();
    void resetPlayLayerZoom(CCNode* playLayer);
    void clampPlayLayerZoomPosition(CCNode* playLayer);

    class PauseZoomManager {
    public:
        static PauseZoomManager& get() {
            static PauseZoomManager instance;
            return instance;
        }

        void onPause() {
            if (m_isPaused) return;
            if (!paimon::modules::isEnabled("paimbnails.pausezoom.gameplay")) return;
            m_playLayer = paimon::NodeIdentity(PlayLayer::get());
            m_isPaused = true;
            m_isPanning = false;
            m_menuForcedHidden = false;
            m_pauseLayerMissingFrames = 0;
            m_lastMousePos = cocos::getMousePos();
            m_deltaMousePos = ccp(0.f, 0.f);
        }

        void onResume() {
            if (m_isPaused) {
                if (auto* playLayer = PlayLayer::get(); m_playLayer.matches(playLayer)) {
                    resetPlayLayerZoom(playLayer);
                }
            }
            paimon::setPauseZoomHidden(false);
            m_isPaused = false;
            m_isPanning = false;
            m_menuForcedHidden = false;
            m_pauseLayerMissingFrames = 0;
            m_playLayer = {};
        }

        void resetForNewLevel() {
            m_playLayer = {};
            m_isPaused = false;
            m_isPanning = false;
            m_menuForcedHidden = false;
            m_pauseLayerMissingFrames = 0;
            // do not carry a hidden-pause flag into the next level.
            paimon::setPauseZoomHidden(false);
        }

        PauseZoomSettings const& pauseZoomSettings() {
            if (!m_settingsValid || m_settingsFrame != m_frame) {
                m_settingsFrame = m_frame;
                m_settingsValid = true;
                m_settingsCache.sensitivity = static_cast<float>(Mod::get()->getSavedValue<double>("zoom-sensitivity", 1.0));
                m_settingsCache.autoHideMenu = Mod::get()->getSavedValue<bool>("zoom-auto-hide-menu", true);
                m_settingsCache.autoShowMenu = Mod::get()->getSavedValue<bool>("zoom-auto-show-menu", true);
                m_settingsCache.altDisablesScroll = Mod::get()->getSavedValue<bool>("zoom-alt-disables-scroll", true);
            }
            return m_settingsCache;
        }

        float getPauseZoomSensitivity() { return pauseZoomSettings().sensitivity; }
        bool pauseZoomAutoHideMenu() { return pauseZoomSettings().autoHideMenu; }
        bool pauseZoomAutoShowMenu() { return pauseZoomSettings().autoShowMenu; }
        bool pauseZoomAltDisablesScroll() { return pauseZoomSettings().altDisablesScroll; }

        void update(float) {
            ++m_frame;
            if (!paimon::modules::isEnabled("paimbnails.pausezoom.gameplay")) {
                this->onResume();
                return;
            }
            auto* playLayer = PlayLayer::get();
            if (!playLayer || !playLayer->m_isPaused) {
                this->onResume();
                return;
            }
            auto* pauseLayer = getPauseLayer();

            if (pauseLayer && !m_isPaused) {
                this->onPause();
            }
            if (!pauseLayer) {
                if (m_isPaused && ++m_pauseLayerMissingFrames > 120) this->onResume();
                return;
            }
            m_pauseLayerMissingFrames = 0;

            if (!m_isPaused) return;

            if (hasBlockingPopup()) {
                m_isPanning = false;
                m_lastMousePos = cocos::getMousePos();
                return;
            }

            auto mousePos = cocos::getMousePos();
            m_deltaMousePos = ccp(mousePos.x - m_lastMousePos.x, mousePos.y - m_lastMousePos.y);
            m_lastMousePos = mousePos;
#ifdef GEODE_IS_WINDOWS
            m_isPanning = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
#else
            // no getasynckeystate off windows; keybind tracker keeps os-resynced state.
            m_isPanning = paimon::keybinds::isMouseButtonHeld(paimon::keybinds::MouseButton::Middle);
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
                    hidePauseMenu();
                } else if (autoShow && scale <= 1.01f && !pauseLayer->isVisible() && m_menuForcedHidden) {
                    restorePauseMenuVisible();
                }
            }
        }

        void onScroll(float y, float x) {
            if (!m_isPaused) return;

            auto* playLayer = PlayLayer::get();
            auto* pauseLayer = getPauseLayer();
            if (!playLayer || !pauseLayer) return;

            if (hasBlockingPopup()) return;

            if (pauseZoomAltDisablesScroll()) {
                if (auto* kb = CCKeyboardDispatcher::get(); kb && kb->getAltKeyPressed()) return;
            }

            float zoomDelta = getPauseZoomSensitivity() * 0.1f;
            auto& smooth = paimon::smoothscroll::SmoothScrollController::get();
            if (smooth.isReplaying() ||
                paimon::compat::ModCompat::isPrevterSmoothScrollLoaded()) {
                zoomAtMouse(smooth.filteredZoomSteps(y, x) * zoomDelta);
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
            zoomAtMouse(kPauseZoomStep);
            if (pauseZoomAutoHideMenu()) {
                if (auto* playLayer = PlayLayer::get(); playLayer && playLayer->getScale() > 1.01f) {
                    hidePauseMenu();
                }
            }
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
            if (!m_isPaused || hasBlockingPopup()) return;
            if (auto* playLayer = PlayLayer::get()) {
                resetPlayLayerZoom(playLayer);
            }
            restorePauseMenuVisible();
        }

        bool isPausedAndZoomable() {
            auto* pl = PlayLayer::get();
            if (!pl || !pl->m_isPaused || !getPauseLayer()) return false;
            return !hasBlockingPopup();
        }

        void pinchZoomAt(float scaleRatio, CCPoint focusPoint) {
            if (!isPausedAndZoomable()) return;
            auto* playLayer = PlayLayer::get();
            if (!playLayer) return;
            auto contentSize = playLayer->getContentSize();
            if (contentSize.width <= 0.f || contentSize.height <= 0.f) return;

            float oldScale = std::max(playLayer->getScale(), 0.001f);
            float newScale = clampZoomValue(oldScale * scaleRatio, kPauseZoomMin, kPauseZoomMax);
            if (std::fabs(newScale - oldScale) < 1e-4f) return;

            CCPoint anchorPoint{
                focusPoint.x - contentSize.width * 0.5f,
                focusPoint.y - contentSize.height * 0.5f
            };
            CCPoint deltaFromAnchor = playLayer->getPosition() - anchorPoint;
            playLayer->setPosition(anchorPoint);
            playLayer->setScale(newScale);
            playLayer->setPosition(anchorPoint + deltaFromAnchor * (newScale / oldScale));
            clampPlayLayerZoomPosition(playLayer);

            if (newScale > 1.01f && pauseZoomAutoHideMenu()) {
                hidePauseMenu();
            } else if (newScale <= 1.01f && pauseZoomAutoShowMenu() && m_menuForcedHidden) {
                restorePauseMenuVisible();
            }
        }

        void panByDelta(CCPoint deltaPoints) {
            if (!isPausedAndZoomable()) return;
            auto* playLayer = PlayLayer::get();
            if (!playLayer || playLayer->getScale() <= 1.01f) return;
            playLayer->setPosition(playLayer->getPosition() + deltaPoints);
            clampPlayLayerZoomPosition(playLayer);
        }

    private:
        paimon::NodeIdentity<PlayLayer> m_playLayer;
        bool m_isPaused = false;
        bool m_isPanning = false;
        bool m_menuForcedHidden = false;
        int m_pauseLayerMissingFrames = 0;
        CCPoint m_lastMousePos = ccp(0.f, 0.f);
        CCPoint m_deltaMousePos = ccp(0.f, 0.f);
        std::uint64_t m_frame = 0;
        PauseZoomSettings m_settingsCache;
        std::uint64_t m_settingsFrame = 0;
        bool m_settingsValid = false;

        PauseLayer* getPauseLayer() {
            auto* playLayer = PlayLayer::get();
            if (!playLayer || !playLayer->m_isPaused) return nullptr;
            return paimon::findOpenPauseLayerInScene();
        }

        bool hasBlockingPopup() {
            if (!paimon::modules::isEnabled("paimbnails.pausezoom.gameplay") ||
                paimon::isCaptureInProgress()) return true;
            auto* scene = CCDirector::get() ? CCDirector::get()->getRunningScene() : nullptr;
            if (!scene) return false;
            auto* pauseLayer = getPauseLayer();

            for (auto* obj : CCArrayExt<CCObject*>(scene->getChildren())) {
                auto* node = typeinfo_cast<CCNode*>(obj);
                if (!node || !node->isVisible()) continue;
                if (node == pauseLayer) continue;

                if (typeinfo_cast<FLAlertLayer*>(node) || typeinfo_cast<CCBlockLayer*>(node) ||
                    typeinfo_cast<DialogLayer*>(node)) {
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
            if (pauseLayer) {
                if (pauseLayer->isVisible()) pauseLayer->setVisible(false);
                // the visit filter keeps this hidden because gd may restore visibility
                paimon::setPauseZoomHidden(true);
                pauseLayer->setTouchEnabled(false);
                m_menuForcedHidden = true;
            }
        }

        void restorePauseMenuVisible() {
            if (paimon::isCaptureInProgress()) return;
            auto* pauseLayer = getPauseLayer();
            if (pauseLayer) {
                if (pauseLayer->getParent() && !pauseLayer->isVisible()) pauseLayer->setVisible(true);
                pauseLayer->setTouchEnabled(true);
            }
            paimon::setPauseZoomHidden(false);
            m_menuForcedHidden = false;
        }

        void zoomAtMouse(float delta) {
            auto* playLayer = PlayLayer::get();
            if (!playLayer || !playLayer->m_isPaused || !getPauseLayer()) return;

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
        if (!director) return {0.0f, 0.0f};
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

}

namespace paimon {
    void notifyPauseClosing() {
        PauseZoomManager::get().onResume();
    }
}

class $modify(PaimonPerformanceBaseGameLayer, GJBaseGameLayer) {
    void updateGradientLayers() {
        if (paimon::gameplayperf::isOptionActive(
                paimon::gameplayperf::kGradientsModuleId)) return;
        GJBaseGameLayer::updateGradientLayers();
    }

    void updateShaderLayer(float dt) {
        if (paimon::gameplayperf::isOptionActive(
                paimon::gameplayperf::kShadersModuleId)) return;
        GJBaseGameLayer::updateShaderLayer(dt);
    }
};

class $modify(PaimonPerformanceGradientLayer, CCLayerGradient) {
    void visit() {
        if (paimon::gameplayperf::isOptionActive(
                paimon::gameplayperf::kGradientsModuleId)) return;
        CCLayerGradient::visit();
    }
};

class $modify(PaimonPerformanceCircleWave, CCCircleWave) {
    void draw() {
        if (paimon::gameplayperf::isOptionActive(
                paimon::gameplayperf::kLevelEffectsModuleId)) return;
        CCCircleWave::draw();
    }
};

class $modify(PaimonPerformanceGameObject, GameObject) {
    void setVisible(bool visible) {
        if (paimon::gameplayperf::isOptionActive(
                paimon::gameplayperf::kDecorationModuleId) && m_isDecoration) {
            GameObject::setVisible(false);
            return;
        }
        GameObject::setVisible(visible);
    }
};

class $modify(PaimonPerformanceShaderLayer, ShaderLayer) {
    void update(float dt) {
        if (paimon::gameplayperf::isOptionActive(
                paimon::gameplayperf::kShadersModuleId)) return;
        ShaderLayer::update(dt);
    }

    void visit() {
        if (paimon::gameplayperf::isOptionActive(
                paimon::gameplayperf::kShadersModuleId) && m_state.m_usesShaders) {
            this->resetAllShaders();
        }
        ShaderLayer::visit();
    }
};

class $modify(PaimonPerformanceParticleSystem, CCParticleSystem) {
    void update(float dt) {
        if (paimon::gameplayperf::isOptionActive(
                paimon::gameplayperf::kParticlesModuleId) &&
            (this->isActive() || this->getParticleCount() != 0)) {
            this->resetSystem();
            this->stopSystem();
        }
        CCParticleSystem::update(dt);
    }
};

class $modify(PaimonPerformanceParticleSystemQuad, CCParticleSystemQuad) {
    void draw() {
        if (paimon::gameplayperf::isOptionActive(
                paimon::gameplayperf::kParticlesModuleId)) return;
        CCParticleSystemQuad::draw();
    }
};

// no ios address for ccparticlebatchnode::draw; quad/system hooks cover batched particles.
#ifndef GEODE_IS_IOS
class $modify(PaimonPerformanceParticleBatchNode, CCParticleBatchNode) {
    void draw() {
        if (paimon::gameplayperf::isOptionActive(
                paimon::gameplayperf::kParticlesModuleId)) return;
        CCParticleBatchNode::draw();
    }
};
#endif

    // process and upload captures off-thread; encoding and color extraction are expensive
static void uploadCapturedThumbnail(int levelID, std::shared_ptr<uint8_t> const& buf, int W, int H) {
    std::string username;
    int accountID = 0;
    if (auto* gm = GameManager::sharedState()) {
        username = gm->m_playerName;
        if (auto* am = GJAccountManager::get()) accountID = am->m_accountID;
    }
    if (username.empty()) username = "unknown";
    if (accountID <= 0) {
        PaimonNotify::show(Localization::get().getString("level.account_required"), NotificationIcon::Error);
        return;
    }

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
                    PaimonNotify::show(Localization::get().getString("capture.save_png_error"), NotificationIcon::Error);
                    return;
                }

                ThumbnailAPI::get().uploadThumbnail(levelID, pngData, username, [levelID, username](bool success, std::string const& msg) {
                    if (success) {
                        bool isPending = (msg.find("pending") != std::string::npos || msg.find("verification") != std::string::npos);
                        if (isPending) {
                            PendingQueue::get().addOrBump(levelID, PendingCategory::Verify, username, {}, false);
                            PaimonNotify::show(Localization::get().getString("capture.suggested"), NotificationIcon::Success);
                        } else {
                            PendingQueue::get().removeForLevel(levelID);
                            PaimonNotify::show(Localization::get().getString("capture.upload_success"), NotificationIcon::Success);
                        }
                    } else {
                        PaimonNotify::show(
                            Localization::get().getString("capture.upload_error") + (msg.empty() ? std::string("") : (" (" + msg + ")")),
                            NotificationIcon::Error
                        );
                    }
                }, levelMeta);
            });
    });
}

static std::atomic<bool> s_hideP1ForCapture{false};
static std::atomic<bool> s_hideP2ForCapture{false};

// the preview popup pauses gameplay; resume only when no pauselayer took over
static void resumePlayLayerAfterPopup(WeakRef<PlayLayer> const& owner) {
    if (paimon::isRuntimeShuttingDown()) return;
    auto play = owner.lock();
    auto* director = CCDirector::get();
    if (!play || play.data() != PlayLayer::get() || !play->getParent() || !play->m_isPaused
        || !director || director->getNextScene() || paimon::hasPauseLayerInScene()) return;
    // use the bound native resume so audio and other mods receive the lifecycle too.
    play->resume();
}

static void ensurePauseZoomTicker();

class $modify(PaimonCapturePlayLayer, PlayLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("PlayLayer::init", geode::Priority::VeryLate);
    // record deaths before noclip hooks can cancel destroyplayer
        (void)self.setHookPriorityPre("PlayLayer::destroyPlayer", geode::Priority::VeryEarly);
    }

    struct Fields {
        std::uint64_t m_captureFlowGeneration = 0;
        float m_frameTimer = 0.0f;
        std::chrono::steady_clock::time_point m_forYouSessionStart;
        bool m_performanceApplied = false;
        bool m_previousPerformanceMode = false;
        bool m_previousAddGlow = false;
    };

    $override
    bool init(GJGameLevel* level, bool dontCreateObjects) {
        if (auto* gameManager = GameManager::get();
            paimon::gameplayperf::isEnabled() && gameManager) {
            m_fields->m_previousPerformanceMode = gameManager->m_performanceMode;
            m_fields->m_previousAddGlow = gameManager->m_addGlow;
            m_fields->m_performanceApplied = true;
            if (paimon::gameplayperf::isOptionEnabled(
                    paimon::gameplayperf::kNativeModeModuleId)) {
                gameManager->m_performanceMode = true;
            }
            if (paimon::gameplayperf::isOptionEnabled(
                    paimon::gameplayperf::kGlowModuleId)) {
                gameManager->m_addGlow = false;
            }
        }

        AudioContextCoordinator::get().notifyGameplayStarted(level);
        paimon::menumusic::SpotifyService::get().onEnterLevel();

        s_hideP1ForCapture = false;
        s_hideP2ForCapture = false;
        if (!PlayLayer::init(level, dontCreateObjects)) {
            this->restorePerformanceMode();
            return false;
        }
        paimon::gameplayperf::setActive(m_fields->m_performanceApplied);
        if (m_fields->m_performanceApplied) {
            if (paimon::gameplayperf::isOptionActive(
                    paimon::gameplayperf::kBackgroundEffectsModuleId)) {
                this->toggleBGEffectVisibility(false);
            }
            if (paimon::gameplayperf::isOptionActive(
                    paimon::gameplayperf::kGameplayEffectsModuleId)) {
                this->m_disableGravityEffect = true;
                this->m_glitterEnabled = false;
            }
            if (paimon::gameplayperf::isOptionActive(
                    paimon::gameplayperf::kPlayerEffectsModuleId)) {
                disablePlayerEffects(this->m_player1);
                disablePlayerEffects(this->m_player2);
            }
            applyConfiguredVisualCuts(this);
            paimon::dynvol::DynamicVolumeManager::get().setPerformancePaused(
                paimon::gameplayperf::isOptionActive(
                    paimon::gameplayperf::kDynamicVolumeModuleId)
            );
        }
        PauseZoomManager::get().resetForNewLevel();
        paimon::capture::clearDeathTick();

        if (Mod::get()->getSettingValue<bool>("enable-thumbnail-taking")) {
            this->addEventListener(
                KeybindSettingPressedEventV3(Mod::get(), "capture-keybind"),
                [this](Keybind const&, bool down, bool repeat, double) {
                    if (!down || repeat) return;
                    if (PlayLayer::get() != this) return;
                    if (this->m_isPaused) return;
    // don't open capture over a pauselayer created in the same frame
                    if (paimon::hasPauseLayerInScene()) return;
                    if (!this->m_level || this->m_level->m_levelID <= 0) return;

                    bool expected = false;
                    if (!s_captureFlowActive.compare_exchange_strong(expected, true)) return;
                    auto const generation = ++s_captureFlowGeneration;
                    m_fields->m_captureFlowGeneration = generation;

    // button and keybind capture share one guard
                    if (paimon::isCaptureInProgress()) {
                        s_captureFlowActive.store(false);
                        return;
                    }
                    paimon::setCaptureInProgress(true);

                    auto validation = FramebufferCapture::validateCaptureConditions();
                    if (!validation.canCapture) {
                        s_captureFlowActive.store(false);
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
                    WeakRef<PlayLayer> weakRef = this;
                    FramebufferCapture::requestCapture(levelID, [weakRef, levelID, generation](bool success, CCTexture2D* texture, std::shared_ptr<uint8_t> rgbaData, int width, int height) {
                        Ref<CCTexture2D> texRef = texture;
                        Loader::get()->queueInMainThread([weakRef, generation, success, texRef, rgbaData, width, height, levelID]() {
                            if (generation != s_captureFlowGeneration.load()) return;
                            // restore capture state on every early exit
                            auto cleanup = [weakRef]() {
                                s_captureFlowActive.store(false);
                                paimon::setCaptureInProgress(false);
                                auto play = weakRef.lock();
                                if (play && play.data() == PlayLayer::get() && !play->m_isPaused) {
                                    if (auto* engine = FMODAudioEngine::sharedEngine()) {
                                        if (engine->m_backgroundMusicChannel) engine->m_backgroundMusicChannel->setPaused(false);
                                    }
                                }
                            };
                            if (paimon::isRuntimeShuttingDown()) {
                                cleanup();
                                return;
                            }
                            CCTexture2D* texture = texRef.data();
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
                                PaimonNotify::show(
                                    Localization::get().getString("pause.capture_error").c_str(),
                                    NotificationIcon::Error
                                );
                                return;
                            }

                            // abort if esc opened pauselayer while the request was in flight
                            if (paimon::hasPauseLayerInScene() || self->m_isPaused) {
                                cleanup();
                                return;
                            }

                            // the popup owns the capture flag from this point
                            paimon::setCaptureInProgress(false);

                            bool pausedByPopup = false;
                            if (!self->m_isPaused) { self->pauseGame(true); pausedByPopup = true; }

                            auto* popup = CapturePreviewPopup::create(
                                texture, levelID, rgbaData, width, height,
                                [pausedByPopup, weakRef, generation](bool okSave, int levelIDAccepted, std::shared_ptr<uint8_t> buf, int W, int H, std::string, std::string){
                                    bool const current = generation == s_captureFlowGeneration.load();
                                    if (current) s_captureFlowActive.store(false);
                                    if (pausedByPopup && current) {
                                        geode::Loader::get()->queueInMainThread([weakRef]() {
                                            resumePlayLayerAfterPopup(weakRef);
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
                                    s_captureFlowActive.store(false);
    // the popup may close before the queued callback; keep only a weakref
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
                            else {
                                cleanup();
                                if (pausedByPopup) resumePlayLayerAfterPopup(weakRef);
                            }
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
        paimon::foryou::TasteProfile::get().onLevelEnter(this->m_level);

        if (paimon::foryou::LevelTagsClient::isAvailable() && this->m_level && this->m_level->m_levelID > 0) {
            int levelID = this->m_level->m_levelID;
            paimon::foryou::LevelTagsClient::get().fetchTags({levelID},
                [levelID](paimon::foryou::LevelTagMap tags) {
                    auto it = tags.find(levelID);
                    if (it == tags.end() || it->second.empty()) return;
                    paimon::foryou::TasteProfile::get().onTagsResolved(levelID, it->second);
                });
        }

        return true;
    }
    
    $override
    void destroyPlayer(PlayerObject* player, GameObject* object) {
    // reject captures taken on the death frame before noclip cancellation
        if (object != this->m_anticheatSpike) {
            paimon::capture::recordDeathTick(this->m_gameState.m_currentProgress);
        } else {
            paimon::capture::clearDeathTick();
        }
        PlayLayer::destroyPlayer(player, object);
    }

    $override
    void onQuit() {
        cancelKeybindCaptureFlow();
        bool const skipExitTransition = m_fields->m_performanceApplied;
        this->restorePerformanceMode();
        PauseZoomManager::get().onResume();
        FramebufferCapture::cancelPending();
        CaptureLayerEditorPopup::discardTrackedLayers();
        CaptureAssetBrowserPopup::discardTrackedAssets();
        s_captureFlowActive.store(false);

        paimon::foryou::TasteProfile::get().onLevelExit(this->m_level);

        {
            auto& sm = paimon::menuloop::MenuLoopManager::get();
            const bool randomize = Mod::get()->getSettingValue<bool>("menuLoopRandomizeOnLevelExit");
            const bool restore = Mod::get()->getSettingValue<bool>("menuLoopRestoreOnLevelExit");
            if (randomize) {
                sm.setShouldRestoreMenuLoopPoint(false);
                auto& player = paimon::menumusic::MenuMusicPlayer::get();
                if (player.isManagingPlayback()) player.playNext();
                else paimon::menuloop::MenuLoopControl::shuffleSong();
            }
            else if (restore) { sm.setShouldRestoreMenuLoopPoint(true); }
        }

        if (skipExitTransition) {
            PlayLayer::onQuit();
        } else {
            paimon::transitions::beginLevelExitTransition(this);
            PlayLayer::onQuit();
            paimon::transitions::endLevelExitTransition();
        }
    }

    $override
    void onExit() {
        cancelKeybindCaptureFlow();
        this->restorePerformanceMode();
        PlayLayer::onExit();
    }

    void cancelKeybindCaptureFlow() {
        if (!m_fields->m_captureFlowGeneration ||
            m_fields->m_captureFlowGeneration != s_captureFlowGeneration.load()) return;
        ++s_captureFlowGeneration;
        m_fields->m_captureFlowGeneration = 0;
        if (s_captureFlowActive.exchange(false)) {
            FramebufferCapture::cancelPending();
            paimon::setCaptureInProgress(false);
        }
    }

    void restorePerformanceMode() {
        paimon::gameplayperf::setActive(false);
        paimon::dynvol::DynamicVolumeManager::get().setPerformancePaused(false);
        if (!m_fields->m_performanceApplied) return;

        if (auto* gameManager = GameManager::get()) {
            gameManager->m_performanceMode = m_fields->m_previousPerformanceMode;
            gameManager->m_addGlow = m_fields->m_previousAddGlow;
        }
        m_fields->m_performanceApplied = false;
    }

    void captureScreenshot(CapturePreviewPopup* existingPopup = nullptr) {
        if (s_captureFlowActive.load() || paimon::isCaptureInProgress() || PlayLayer::get() != this) return;
        if (!this->m_level || this->m_level->m_levelID <= 0) return;

        auto validation = FramebufferCapture::validateCaptureConditions();
        if (!validation.canCapture) {
            PaimonNotify::show(validation.reason.c_str(), NotificationIcon::Warning);
            if (existingPopup) existingPopup->setVisible(true);
            return;
        }

        s_captureFlowActive.store(true);
        auto const generation = ++s_captureFlowGeneration;
        m_fields->m_captureFlowGeneration = generation;
        paimon::setCaptureInProgress(true);

        int const levelID = this->m_level->m_levelID;
        bool const hideP1 = s_hideP1ForCapture.load();
        bool const hideP2 = s_hideP2ForCapture.load();
        WeakRef<PaimonCapturePlayLayer> self = this;
        WeakRef<CapturePreviewPopup> weakPopup = existingPopup;

        FramebufferCapture::requestCapture(levelID,
            [self, weakPopup, levelID, hideP1, hideP2, generation, hadPopup = existingPopup != nullptr](bool success, CCTexture2D* texture,
                std::shared_ptr<uint8_t> rgbaData, int width, int height) {
                Ref<CCTexture2D> texRef = texture;
                Loader::get()->queueInMainThread([self, weakPopup, generation, hadPopup, success, texRef, rgbaData, width, height, levelID, hideP1, hideP2]() {
                    if (generation != s_captureFlowGeneration.load()) return;
                    auto cleanup = []() {
                        s_captureFlowActive.store(false);
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
                    if (hadPopup && (!popupPtr || !popupPtr->getParent() || !popupPtr->isRunning())) return;

                    if (!success || !texRef.data() || !rgbaData) {
                        PaimonNotify::show(
                            Localization::get().getString("pause.capture_error").c_str(),
                            NotificationIcon::Error
                        );
                        if (popupPtr) popupPtr->setVisible(true);
                        return;
                    }

                    if (popupPtr) {
                        popupPtr->updateContent(texRef.data(), rgbaData, width, height);
                        popupPtr->setVisible(true);
                        return;
                    }

                    // don't open recapture over a newly created pauselayer
                    if (paimon::hasPauseLayerInScene() || layer->m_isPaused) return;

                    bool pausedByPopup = false;
                    if (!layer->m_isPaused) {
                        layer->pauseGame(true);
                        pausedByPopup = true;
                    }

                    auto* popup = CapturePreviewPopup::create(
                        texRef.data(), levelID, rgbaData, width, height,
                        [pausedByPopup, self, generation](bool okSave, int levelIDAccepted, std::shared_ptr<uint8_t> buf, int W, int H, std::string, std::string) {
                            bool const current = generation == s_captureFlowGeneration.load();
                            if (current) s_captureFlowActive.store(false);
                            if (pausedByPopup && current) {
                                Loader::get()->queueInMainThread([self]() {
                                    resumePlayLayerAfterPopup(WeakRef<PlayLayer>(self.lock().data()));
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
                            s_captureFlowActive.store(false);
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
                    else if (pausedByPopup) {
                        resumePlayLayerAfterPopup(WeakRef<PlayLayer>(layer.data()));
                    }
                });
            },
            nullptr,
            hideP1,
            hideP2
        );
    }

    $override
    void pauseGame(bool unfocused) {
        ensurePauseZoomTicker();
        PlayLayer::pauseGame(unfocused);
        if (m_isPaused) {
            PauseZoomManager::get().onPause();
        }
    }

    $override
    void resume() {
        PlayLayer::resume();
        paimon::synchronizePauseLayers(this);
        if (this == PlayLayer::get() && !m_isPaused) PauseZoomManager::get().onResume();
    }

    $override
    void resumeAndRestart(bool fromStart) {
        PlayLayer::resumeAndRestart(fromStart);
        paimon::synchronizePauseLayers(this);
        if (this == PlayLayer::get() && !m_isPaused) PauseZoomManager::get().onResume();
    }

    $override
    void startGame() {
        PlayLayer::startGame();
        paimon::synchronizePauseLayers(this);
        if (this == PlayLayer::get() && !m_isPaused) PauseZoomManager::get().onResume();
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
        if (paimon::gameplayperf::isOptionActive(
                paimon::gameplayperf::kModVisualsModuleId)) return;
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

    // filter pauselayer in ccnode::visit; the atomic flag survives gd visibility restores
class $modify(PaimonPauseZoomVisitFilter, CCNode) {
    static void onModify(auto& self) {
        // run late so other visit hooks see the original first
        (void)self.setHookPriorityPre("cocos2d::CCNode::visit", geode::Priority::Late);

        // keep this global hook dormant outside pause-zoom; visit runs for every node
        if (auto hook = self.getHook("cocos2d::CCNode::visit")) {
            hook.unwrap()->setAutoEnable(false);
            paimon::setPauseZoomVisitHook(hook.unwrap());
        }
    }

    void visit() {
        if (paimon::isPauseZoomHidden()) {
            auto* activePause = static_cast<CCNode*>(paimon::getActivePauseLayer());
            if (activePause && this == activePause) {
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
    bool isPausedAndZoomable() {
        return PauseZoomManager::get().isPausedAndZoomable();
    }
    void zoomInStep() {
        PauseZoomManager::get().zoomInStep();
    }
    void zoomOutStep() {
        PauseZoomManager::get().zoomOutStep();
    }
    void resetZoom() {
        PauseZoomManager::get().reset();
    }
    void toggleMenu() {
        PauseZoomManager::get().togglePauseMenu();
    }
    void pinchZoomAt(float scaleRatio, cocos2d::CCPoint focusPoint) {
        PauseZoomManager::get().pinchZoomAt(scaleRatio, focusPoint);
    }
    void panByDelta(cocos2d::CCPoint deltaPoints) {
        PauseZoomManager::get().panByDelta(deltaPoints);
    }
}

namespace paimon::capture {
    void triggerCaptureFromGesture() {
        auto* pl = PlayLayer::get();
        if (!pl) {
            CaptureMenuPopup::toggle();
            return;
        }
        auto* mod = Mod::get();
        if (!mod || !mod->getSettingValue<bool>("enable-thumbnail-taking")) return;
        Keybind synthetic;
        KeybindSettingPressedEventV3(std::string(mod->getID()), "capture-keybind").send(
            synthetic, true, false, 0.0
        );
    }
}
