#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCMouseDispatcher.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/modify/CCNode.hpp>
#include <Geode/ui/Notification.hpp>
#include "../utils/PaimonNotification.hpp"
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/HardStreak.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/utils/Keyboard.hpp>
#include "../features/capture/ui/CapturePreviewPopup.hpp"
#include "../features/capture/ui/CaptureLayerEditorPopup.hpp"
#include "../features/capture/ui/CaptureAssetBrowserPopup.hpp"
#include "../features/capture/services/FramebufferCapture.hpp"
#include "../utils/RenderTexture.hpp"
#include "../utils/PlayerToggleHelper.hpp"
#include "../utils/Localization.hpp"
#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../features/moderation/services/PendingQueue.hpp"
#include "../utils/ImageConverter.hpp"
#include "../features/moderation/services/ModeratorUtils.hpp"
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/UILayer.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/cocos/robtop/keyboard_dispatcher/CCKeyboardDispatcher.h>

#include "../utils/DominantColors.hpp"
#include "../features/thumbnails/services/LevelColors.hpp"
#include "../features/audio/services/AudioContextCoordinator.hpp"
#include "../features/foryou/services/ForYouTracker.hpp"
#include "../features/foryou/services/LevelTagsIntegration.hpp"
#include "../framework/compat/ModCompat.hpp"
#include "../utils/ActivePauseLayer.hpp"
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
#include <Geode/binding/FMODAudioEngine.hpp>

using namespace geode::prelude;

namespace {
    // #region agent log
    // PERF: el cuerpo abria/escribia/cerraba debug-347aef.log cada vez (50-200us
    // en Windows). Como onScroll lo invoca por cada evento de scroll durante
    // pause-zoom, eso agregaba 1-10ms de jitter por frame. Ahora es no-op.
    // Para reactivar la instrumentacion, define PAIMON_DEBUG_AGENT347 en CMake.
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

    // BUG FIX: el PauseLayer puede no existir todavia en el arbol de la escena
    // durante los primeros frames despues de pausar (la animacion de entrada de
    // GD tarda ~0.3s). Antes, update() llamaba this->onResume() cuando no
    // encontraba el PauseLayer, lo que seteaba m_isPaused = false PERMANENTEMENTE.
    //
    // Eso hacia que zoomInStep/zoomOutStep y onScroll retornaran inmediatamente
    // (guard !m_isPaused en linea 94), rompiendo el zoom completamente.
    //
    // Solucion: guard m_pauseLayerMissingFrames cuenta cuantos frames consecutivos
    // el PauseLayer no se encuentra. Si no aparece en 2 segundos (~120 frames a
    // 60fps), ahi si asumimos que la pausa fue cancelada y reseteamos. En el
    // 99.9% de casos, el PauseLayer aparece en el frame 2-3 y el zoom funciona.

    class PauseZoomManager {
    public:
        static PauseZoomManager& get() {
            static PauseZoomManager instance;
            return instance;
        }

        void onPause() {
            if (m_isPaused) return;
            m_isPaused = true;
            log::info("[PauseZoom] onPause() called — m_isPaused=true");
            m_isPanning = false;
            m_menuForcedHidden = false;
            m_pauseLayerMissingFrames = 0;
            m_lastMousePos = cocos::getMousePos();
            m_deltaMousePos = ccp(0.f, 0.f);
        }

        void onResume() {
            if (!m_isPaused) return;
            log::info("[PauseZoom] onResume() called — m_isPaused=false (was paused)");
            if (auto* playLayer = PlayLayer::get()) {
                resetPlayLayerZoom(playLayer);
            }
            restorePauseMenuVisible();
            // Garantia adicional: si restorePauseMenuVisible no encontro
            // pauseLayer (ya destruido por scene change), aun asi tenemos
            // que limpiar la flag global para no leakear estado a la
            // siguiente sesion.
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
            // Reseteo defensivo: si un nivel anterior dejo la flag en true
            // (crash, salida abrupta), el siguiente nivel no quedaria con
            // PauseLayer invisible.
            paimon::setPauseZoomHidden(false);
        }

        void update(float dt) {
            // Detecta pausa por presencia del PauseLayer en la escena
            // (no depende de pauseGame() porque mods como compact-pause-menu
            //  reemplazan la logica de pausa y nunca llaman pauseGame(true)).
            auto* playLayer = PlayLayer::get();
            auto* pauseLayer = getPauseLayer();
            bool pauseLayerPresent = (playLayer && pauseLayer);

            if (pauseLayerPresent && !m_isPaused) {
                this->onPause();
            }
            if (!pauseLayerPresent && m_isPaused) {
                // Si el PauseLayer desaparecio por mas de 2 segundos,
                // asumimos que la pausa fue cancelada.
                m_pauseLayerMissingFrames++;
                if (m_pauseLayerMissingFrames > 120) {
                    log::info("[PauseZoom] update auto-resume: pauseLayer missing for {} frames", m_pauseLayerMissingFrames);
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

            auto mousePos = cocos::getMousePos();
            m_deltaMousePos = ccp(mousePos.x - m_lastMousePos.x, mousePos.y - m_lastMousePos.y);
            m_lastMousePos = mousePos;

#ifdef GEODE_IS_WINDOWS
            m_isPanning = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
#else
            m_isPanning = false;
#endif

            if (m_isPanning && playLayer->getScale() > 1.0f) {
                playLayer->setPosition(playLayer->getPosition() + m_deltaMousePos);
                clampPlayLayerZoomPosition(playLayer);
            }

            // ── Mantener visibilidad del PauseLayer coherente con el zoom ──
            //
            // Antes, hidePauseMenu()/restorePauseMenuVisible() solo se llamaban
            // reactivamente desde onScroll/zoomInStep/zoomOutStep/togglePauseMenu.
            // Si el PauseLayer aparecia DESPUES del zoom (por animacion de
            // entrada de GD ~0.3s) o si otro mod restauraba su visibilidad, el
            // menu quedaba visible aunque el zoom siguiera activo.
            //
            // Ahora, cada frame nos aseguramos de que la visibilidad sea
            // coherente con el estado del zoom:
            //   • zoom > 1.01 + autoHide → PauseLayer invisible
            //   • zoom <= 1.01 + autoShow + previamente forzado oculto → visible
            //
            // EXCEPCION: Si hay una captura en curso (paimon::isCaptureInProgress)
            // NO tocamos la visibilidad — el sistema de captura ya la maneja.
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
                log::info("[PauseZoom] onScroll blocked: !m_isPaused");
                return;
            }

            auto* playLayer = PlayLayer::get();
            auto* pauseLayer = getPauseLayer();
            auto* activePause = paimon::getActivePauseLayer();
            if (!playLayer || !pauseLayer) {
                // #region agent log
                {
                    std::ostringstream d;
                    d << "{\"playLayer\":" << (playLayer ? "true" : "false")
                      << ",\"pauseLayer\":" << (pauseLayer ? "true" : "false")
                      << ",\"activePause\":" << (activePause ? "true" : "false") << "}";
                    agentLog347("PlayLayer.cpp:onScroll", "blocked_missing_layer", "B", d.str());
                }
                // #endregion
                log::info("[PauseZoom] onScroll blocked: playLayer={} pauseLayer={}", (void*)playLayer, (void*)pauseLayer);
                return;  // No auto-resume on scroll — espera al ticker
            }

            if (hasBlockingPopup()) {
                // #region agent log
                agentLog347("PlayLayer.cpp:onScroll", "blocked_popup", "E", "{}");
                // #endregion
                log::info("[PauseZoom] onScroll blocked: hasBlockingPopup=true");
                return;
            }

            if (pauseZoomAltDisablesScroll()) {
                if (auto* kb = CCKeyboardDispatcher::get(); kb && kb->getAltKeyPressed()) {
                    log::info("[PauseZoom] onScroll blocked: Alt key held (alt-disables-scroll)");
                    return;
                }
            }

            float zoomDelta = getPauseZoomSensitivity() * 0.1f;
            if (Loader::get()->isModLoaded("prevter.smooth-scroll")) {
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
            if (auto* playLayer = PlayLayer::get()) {
                auto* pauseLayer = getPauseLayer();
                std::ostringstream d;
                d << "{\"scaleBefore\":" << scaleBefore << ",\"scaleAfter\":" << playLayer->getScale()
                  << ",\"pauseVisible\":" << (pauseLayer && pauseLayer->isVisible() ? "true" : "false") << "}";
                agentLog347("PlayLayer.cpp:zoomInStep", "keybind_zoom", "A", d.str());
            }
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

        PauseLayer* getPauseLayer() const {
            auto* scene = CCDirector::get() ? CCDirector::get()->getRunningScene() : nullptr;
            if (!scene) return nullptr;

            if (auto* byID = typeinfo_cast<PauseLayer*>(scene->getChildByID("PauseLayer"))) {
                return byID;
            }

            auto* children = scene->getChildren();
            if (!children) return nullptr;

            for (auto* obj : CCArrayExt<CCObject*>(children)) {
                if (auto* pauseLayer = typeinfo_cast<PauseLayer*>(obj)) {
                    return pauseLayer;
                }
            }

            return nullptr;
        }

        bool hasBlockingPopup() const {
            auto* scene = CCDirector::get() ? CCDirector::get()->getRunningScene() : nullptr;
            auto* pauseLayer = getPauseLayer();
            if (!scene) return false;

            for (auto* obj : CCArrayExt<CCObject*>(scene->getChildren())) {
                auto* node = typeinfo_cast<CCNode*>(obj);
                if (!node || !node->isVisible()) continue;
                if (node == pauseLayer) continue;

                if (typeinfo_cast<FLAlertLayer*>(node)) {
                    log::info("[PauseZoom] hasBlockingPopup: FLAlertLayer id='{}' cls='{}'", node->getID(), typeid(*node).name());
                    return true;
                }

                std::string id = geode::utils::string::toLower(node->getID());

                if ((!id.empty() && (id.find("popup") != std::string::npos || id.find("alert") != std::string::npos))) {
                    log::info("[PauseZoom] hasBlockingPopup: id pattern match id='{}' cls='{}'", node->getID(), typeid(*node).name());
                    return true;
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
                // Setea la flag GLOBAL de zoom-hidden. PauseLayer::visit()
                // (override en PauseLayer.cpp) consulta esta flag y retorna
                // early — esto es necesario porque setVisible(false) por si
                // solo no es persistente: GD/CCBlockLayer/otro mod restaura
                // m_bVisible cada frame (verificado en debug-347aef.log).
                paimon::setPauseZoomHidden(true);
                // Tambien deshabilitamos touch para que clicks sobre la zona
                // donde estaria el menu no activen botones invisibles.
                pauseLayer->setTouchEnabled(false);
                m_menuForcedHidden = true;
            }
            // #region agent log
            {
                std::ostringstream d;
                d << "{\"visBefore\":" << (visBefore ? "true" : "false")
                  << ",\"visAfter\":" << (pauseLayer && pauseLayer->isVisible() ? "true" : "false")
                  << ",\"pausePtr\":" << reinterpret_cast<uintptr_t>(pauseLayer)
                  << ",\"activePtr\":" << reinterpret_cast<uintptr_t>(activePause)
                  << ",\"ptrMatch\":" << (pauseLayer == activePause ? "true" : "false") << "}";
                agentLog347("PlayLayer.cpp:hidePauseMenu", "hide_called", "B", d.str());
            }
            // #endregion
        }

        void restorePauseMenuVisible() {
            auto* pauseLayer = getPauseLayer();
            bool visBefore = pauseLayer ? pauseLayer->isVisible() : false;
            if (pauseLayer) {
                if (pauseLayer->getParent() && !pauseLayer->isVisible()) {
                    pauseLayer->setVisible(true);
                }
                // Re-habilita touch (lo deshabilitamos en hidePauseMenu).
                pauseLayer->setTouchEnabled(true);
            }
            // Limpia la flag de zoom-hidden — visit() volvera a renderizar
            // el PauseLayer normalmente.
            paimon::setPauseZoomHidden(false);
            m_menuForcedHidden = false;
            // #region agent log
            {
                std::ostringstream d;
                d << "{\"visBefore\":" << (visBefore ? "true" : "false")
                  << ",\"visAfter\":" << (pauseLayer && pauseLayer->isVisible() ? "true" : "false") << "}";
                agentLog347("PlayLayer.cpp:restorePauseMenuVisible", "restore_called", "D", d.str());
            }
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

        std::string id = node->getID();
        auto idL = geode::utils::string::toLower(id);
        if (!idL.empty()) {
            static std::vector<std::string> patterns = {
                "ui", "uilayer", "pause", "menu", "dialog", "popup", "editor",
                "notification", "btn", "button", "overlay", "checkpoint",
                "fps", "debug", "attempt", "percent", "progress", "bar",
                "score", "practice", "hitbox", "trajectory", "status"
            };
            for (auto const& p : patterns) {
                if (idL.find(p) != std::string::npos) return true;
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
        log::info("[PauseZoom] notifyPauseClosing() → onResume()");
        PauseZoomManager::get().onResume();
    }
}

static std::atomic<bool> s_hideP1ForCapture{false};
static std::atomic<bool> s_hideP2ForCapture{false};

static void ensurePauseZoomTicker();

class $modify(PaimonCapturePlayLayer, PlayLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("PlayLayer::init", geode::Priority::VeryLate);
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

        if (Mod::get()->getSettingValue<bool>("enable-thumbnail-taking")) {
            this->addEventListener(
                KeybindSettingPressedEventV3(Mod::get(), "capture-keybind"),
                [this](Keybind const& keybind, bool down, bool repeat, double timestamp) {
                    if (!down || repeat) return;
                    if (PlayLayer::get() != this) return;
                    if (this->m_isPaused) return;
                    // Race-condition guard: si el usuario presiona Esc + T en
                    // el mismo frame, GD puede haber creado un PauseLayer
                    // antes de que m_isPaused se propague visiblemente. Sin
                    // esta verificación, el callback async del keybind puede
                    // mostrar un CapturePreviewPopup encima del PauseLayer
                    // dejando el menú de pausa congelado detrás del popup
                    // (estado inconsistente que crashea al cerrar GD).
                    if (paimon::hasPauseLayerInScene()) return;
                    if (!this->m_level || this->m_level->m_levelID <= 0) return;

                    bool expected = false;
                    if (!gCaptureInProgress.compare_exchange_strong(expected, true)) return;

                    // También exclusión mutua con el camino A (botón del
                    // PauseLayer): si una captura ya está en curso desde el
                    // PauseLayer, no iniciamos otra.
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
                            CCTexture2D* texture = texRef.data();
                            // Helper local para limpiar todo el estado y restaurar
                            // música. Usamos esto en TODAS las rutas de salida
                            // temprana para evitar leaks de flags.
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
                                return;
                            }

                            // Race-condition guard 2: si Esc se presionó durante
                            // los ~50ms entre requestCapture y el callback async,
                            // ahora hay un PauseLayer en escena. Mostrar el
                            // CapturePreviewPopup encima dejaría el menú de pausa
                            // congelado detrás. Abortamos limpiamente: el usuario
                            // puede usar el botón de captura del PauseLayer
                            // (camino A) que sí coordina con el menú de pausa.
                            if (paimon::hasPauseLayerInScene() || self->m_isPaused) {
                                log::warn("[CaptureKeybind] PauseLayer aparecido durante captura, abortando para evitar UI inconsistente");
                                cleanup();
                                return;
                            }

                            // A partir de aquí limpiamos solo el flag local
                            // gCaptureInProgress; el flag global se limpia en
                            // el callback del popup o en cancelación.
                            paimon::setCaptureInProgress(false);

                            bool pausedByPopup = false;
                            if (!self->m_isPaused) { self->pauseGame(true); pausedByPopup = true; }

                            auto* popup = CapturePreviewPopup::create(
                                texture, levelID, rgbaData, width, height,
                                [levelID, pausedByPopup](bool okSave, int levelIDAccepted, std::shared_ptr<uint8_t> buf, int W, int H, std::string mode, std::string replaceId){
                                    gCaptureInProgress.store(false);
                                    if (pausedByPopup) {
                                        // Resume PlayLayer desde el main thread para thread-safety
                                        geode::Loader::get()->queueInMainThread([levelID]() {
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
                                        std::vector<uint8_t> rgbData(static_cast<size_t>(W) * static_cast<size_t>(H) * 3);
                                        const uint8_t* src = buf.get();
                                        for(size_t i=0; i < static_cast<size_t>(W)*H; ++i) {
                                            rgbData[i*3+0] = src[i*4+0];
                                            rgbData[i*3+1] = src[i*4+1];
                                            rgbData[i*3+2] = src[i*4+2];
                                        }
                                        auto pair = DominantColors::extract(rgbData.data(), W, H);
                                        ccColor3B A{pair.first.r, pair.first.g, pair.first.b};
                                        ccColor3B B{pair.second.r, pair.second.g, pair.second.b};
                                        LevelColors::get().set(levelIDAccepted, A, B);
                                        std::vector<uint8_t> rgbaVec(static_cast<size_t>(W) * static_cast<size_t>(H) * 4);
                                        memcpy(rgbaVec.data(), buf.get(), rgbaVec.size());
                                        std::vector<uint8_t> pngData;
                                        if (!ImageConverter::rgbToPng(rgbaVec, static_cast<uint32_t>(W), static_cast<uint32_t>(H), pngData)) {
                                            PaimonNotify::create(Localization::get().getString("capture.save_png_error"), NotificationIcon::Error)->show();
                                        } else {
                                            std::string username;
                                            int accountID = 0;
                                            if (auto* gm = GameManager::sharedState()) { username = gm->m_playerName; if (auto* am = GJAccountManager::get()) accountID = am->m_accountID; }
                                            if (username.empty()) username = "unknown";
                                            if (accountID <= 0) { PaimonNotify::create(Localization::get().getString("level.account_required"), NotificationIcon::Error)->show(); return; }
                                            PaimonNotify::show(Localization::get().getString("capture.uploading"), geode::NotificationIcon::Info);
                                            ThumbnailAPI::get().uploadThumbnail(levelIDAccepted, pngData, username, [levelIDAccepted, username](bool success, std::string const& msg){
                                                if (success) {
                                                    bool isPending = (msg.find("pending") != std::string::npos || msg.find("verification") != std::string::npos);
                                                    if (isPending) { PendingQueue::get().addOrBump(levelIDAccepted, PendingCategory::Verify, username, {}, false); PaimonNotify::create(Localization::get().getString("capture.suggested"), NotificationIcon::Success)->show(); }
                                                    else { PendingQueue::get().removeForLevel(levelIDAccepted); PaimonNotify::create(Localization::get().getString("capture.upload_success"), NotificationIcon::Success)->show(); }
                                                } else { PaimonNotify::create(Localization::get().getString("capture.upload_error") + (msg.empty() ? std::string("") : (" (" + msg + ")")), NotificationIcon::Error)->show(); }
                                            });
                                        }
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
    void onQuit() {
        PauseZoomManager::get().onResume();
        FramebufferCapture::cancelPending();
        CaptureLayerEditorPopup::discardTrackedLayers();
        CaptureAssetBrowserPopup::discardTrackedAssets();
        gCaptureInProgress.store(false);

        paimon::foryou::ForYouTracker::get().onLevelExit(this->m_level);

        {
            auto& sm = paimon::menuloop::MenuLoopManager::get();
            const bool randomize = Mod::get()->getSavedValue<bool>("menuLoopRandomizeOnLevelExit", false);
            const bool restore = Mod::get()->getSavedValue<bool>("menuLoopRestoreOnLevelExit", true);
            if (randomize) { sm.setShouldRestoreMenuLoopPoint(false); paimon::menuloop::MenuLoopControl::shuffleSong(); }
            else if (restore) { sm.setShouldRestoreMenuLoopPoint(true); }
        }

        PlayLayer::onQuit();
    }

    void captureScreenshot(CapturePreviewPopup* existingPopup = nullptr) {
        if (gCaptureInProgress.load()) return;
        gCaptureInProgress.store(true);

        auto* director = CCDirector::get();
        if (!director || !this->m_level) { gCaptureInProgress.store(false); return; }

        std::vector<CCNode*> hidden;
        PlayerVisState p1State, p2State;

        if (s_hideP1ForCapture) paimTogglePlayer(this->m_player1, p1State, true);
        if (s_hideP2ForCapture) paimTogglePlayer(this->m_player2, p2State, true);
        
        hideNonGameplayDescendants(this, hidden, true, this);

        if (this->m_uiLayer && this->m_uiLayer->isVisible()) {
            this->m_uiLayer->setVisible(false);
            hidden.push_back(this->m_uiLayer);
        }

        for (auto* obj : CCArrayExt<CCObject*>(this->getChildren())) {
            auto* node = typeinfo_cast<CCNode*>(obj);
            if (!node || node == this->m_uiLayer) continue;
            if (node->isVisible() && isNonGameplayOverlay(node, true)) {
                bool alreadyHidden = false;
                for (auto* h : hidden) { if (h == node) { alreadyHidden = true; break; } }
                if (!alreadyHidden) { node->setVisible(false); hidden.push_back(node); }
            }
        }

        auto hiddenCopy = hidden;
        int levelID = this->m_level->m_levelID;

        auto view = CCEGLView::sharedOpenGLView();
        auto screenSize = view->getFrameSize();
        int screenW = static_cast<int>(screenSize.width);
        int screenH = static_cast<int>(screenSize.height);

        std::string res = geode::Mod::get()->getSettingValue<std::string>("capture-resolution");
        int targetW = 1920;
        if (res == "4k") targetW = 3840; else if (res == "1440p") targetW = 2560;
        double aspect = (screenH > 0) ? static_cast<double>(screenW) / static_cast<double>(screenH) : (16.0 / 9.0);
        int width = targetW;
        int height = std::max(1, static_cast<int>(std::round(width / aspect)));

        std::unique_ptr<uint8_t[]> data;
        bool needsVerticalFlip = true;

        RenderTexture rt(width, height);
        rt.begin();
        this->visit();
        rt.end();
        data = rt.getData();
        needsVerticalFlip = true;

        for (auto* n : hiddenCopy) { if (n) n->setVisible(true); }
        if (s_hideP1ForCapture) paimTogglePlayer(this->m_player1, p1State, false);
        if (s_hideP2ForCapture) paimTogglePlayer(this->m_player2, p2State, false);
        
        if (!data) { gCaptureInProgress.store(false); return; }

        if (needsVerticalFlip) {
            int rowSize = width * 4;
            std::vector<uint8_t> tempRow(rowSize);
            uint8_t* buffer = data.get();
            for (int y = 0; y < height / 2; ++y) {
                uint8_t* topRow = buffer + y * rowSize;
                uint8_t* bottomRow = buffer + (height - y - 1) * rowSize;
                std::memcpy(tempRow.data(), topRow, rowSize);
                std::memcpy(topRow, bottomRow, rowSize);
                std::memcpy(bottomRow, tempRow.data(), rowSize);
            }
        }

        auto* rawTex = new CCTexture2D();
        if (!rawTex->initWithData(data.get(), kCCTexture2DPixelFormat_RGBA8888, width, height, CCSize(static_cast<float>(width), static_cast<float>(height)))) {
            rawTex->release(); gCaptureInProgress.store(false); return;
        }
        rawTex->setAntiAliasTexParameters();
        Ref<CCTexture2D> tex = rawTex;
        rawTex->release();

        std::shared_ptr<uint8_t> rgba(new uint8_t[width * height * 4], std::default_delete<uint8_t[]>());
        memcpy(rgba.get(), data.get(), width * height * 4);
        
        bool pausedByPopup = false;
        if (!this->m_isPaused) { this->pauseGame(true); pausedByPopup = true; }
        
        if (existingPopup) { existingPopup->updateContent(tex, rgba, width, height); existingPopup->setVisible(true); gCaptureInProgress.store(false); return; }

        bool isMod = PaimonUtils::isUserModerator();
        auto* popup = CapturePreviewPopup::create(tex, levelID, rgba, width, height, 
            [levelID, pausedByPopup](bool okSave, int levelIDAccepted, std::shared_ptr<uint8_t> buf, int W, int H, std::string mode, std::string replaceId){
                gCaptureInProgress.store(false);
                if (pausedByPopup) {
                    auto* pl = PlayLayer::get();
                    if (pl && pl->m_isPaused) {
                        bool hasPause = false;
                        if (auto* sc = CCDirector::get()->getRunningScene()) {
                            CCArrayExt<CCNode*> children(sc->getChildren());
                            for (auto child : children) { if (typeinfo_cast<PauseLayer*>(child)) { hasPause = true; break; } }
                        }
                        if (!hasPause) {
                            if (auto* d = CCDirector::get()) {
                                if (d->getScheduler() && d->getActionManager()) { d->getScheduler()->resumeTarget(pl); d->getActionManager()->resumeTarget(pl); pl->m_isPaused = false; PauseZoomManager::get().onResume(); }
                            }
                        }
                    }
                }
                if (okSave && levelIDAccepted > 0 && buf) {
                    std::vector<uint8_t> rgbData(static_cast<size_t>(W) * static_cast<size_t>(H) * 3);
                    const uint8_t* src = buf.get();
                    for(size_t i=0; i < static_cast<size_t>(W)*H; ++i) { rgbData[i*3+0]=src[i*4+0]; rgbData[i*3+1]=src[i*4+1]; rgbData[i*3+2]=src[i*4+2]; }
                    auto pair = DominantColors::extract(rgbData.data(), W, H);
                    ccColor3B A{pair.first.r, pair.first.g, pair.first.b};
                    ccColor3B B{pair.second.r, pair.second.g, pair.second.b};
                    LevelColors::get().set(levelIDAccepted, A, B);
                    std::vector<uint8_t> rgbaData(static_cast<size_t>(W) * static_cast<size_t>(H) * 4);
                    memcpy(rgbaData.data(), buf.get(), rgbaData.size());
                    std::vector<uint8_t> pngData;
                    if (!ImageConverter::rgbToPng(rgbaData, static_cast<uint32_t>(W), static_cast<uint32_t>(H), pngData)) {
                        PaimonNotify::create(Localization::get().getString("capture.save_png_error"), NotificationIcon::Error)->show();
                    } else {
                        std::string username; int accountID = 0;
                        if (auto* gm = GameManager::sharedState()) { username = gm->m_playerName; if (auto* am = GJAccountManager::get()) accountID = am->m_accountID; }
                        if (username.empty()) username = "unknown";
                        if (accountID <= 0) { PaimonNotify::create(Localization::get().getString("level.account_required"), NotificationIcon::Error)->show(); return; }
                        PaimonNotify::show(Localization::get().getString("capture.uploading"), geode::NotificationIcon::Info);
                        ThumbnailAPI::get().uploadThumbnail(levelIDAccepted, pngData, username, [levelIDAccepted, username](bool success, std::string const& msg){
                            if (success) { bool isPending = (msg.find("pending") != std::string::npos || msg.find("verification") != std::string::npos); if (isPending) { PendingQueue::get().addOrBump(levelIDAccepted, PendingCategory::Verify, username, {}, false); PaimonNotify::create(Localization::get().getString("capture.suggested"), NotificationIcon::Success)->show(); } else { PendingQueue::get().removeForLevel(levelIDAccepted); PaimonNotify::create(Localization::get().getString("capture.upload_success"), NotificationIcon::Success)->show(); } }
                            else { PaimonNotify::create(Localization::get().getString("capture.upload_error") + (msg.empty() ? std::string("") : (" (" + msg + ")")), NotificationIcon::Error)->show(); }
                        });
                    }
                }
        },
        [this](bool hideP1, bool hideP2, CapturePreviewPopup* popup) {
            s_hideP1ForCapture = hideP1; s_hideP2ForCapture = hideP2;
            if (popup) popup->setVisible(false);
            gCaptureInProgress.store(false);
            WeakRef<PaimonCapturePlayLayer> self = this;
            WeakRef<CapturePreviewPopup> weakPopup = popup;
            Loader::get()->queueInMainThread([self, weakPopup]() {
                auto layer = self.lock(); if (!layer) return;
                auto popupLocked = weakPopup.lock(); if (!popupLocked) return;
                layer->captureScreenshot(static_cast<CapturePreviewPopup*>(popupLocked.data()));
            });
        }, s_hideP1ForCapture, s_hideP2ForCapture);
        if (popup) { if (existingPopup) {} else { popup->show(); } }
        else { gCaptureInProgress.store(false); }
    }

    $override
    void pauseGame(bool value) {
        log::info("[PauseZoom] pauseGame({}) called", value);
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

// ── CCNode::visit hook para skip de render del PauseLayer durante zoom ──
//
// Por que este hook globalmente sobre CCNode en lugar de un override en
// PaimonPauseLayer:
//   `visit()` no esta en el modify-binding de PauseLayer (solo CCNode lo
//   expone), asi que `$override void visit()` dentro de $modify(*, PauseLayer)
//   no engancha nada — el hook se descarta silenciosamente. Verificado
//   leyendo bindings/Geode/modify/PauseLayer.hpp (no hay GEODE_STATICS_visit).
//
// Por que no se rompe el rendimiento:
//   visit() se llama miles de veces por frame (uno por cada CCNode visible),
//   pero el filtro aqui es:
//     1. early-out con std::atomic<bool>::load (1 instruccion x86 con
//        memory_order_relaxed/acquire — practicamente gratis)
//     2. una comparacion de punteros (1 instruccion)
//   El costo total es ~5 ns por visit, despreciable contra los ~100 us
//   tipicos de un render frame.
//
// Por que es robusto donde setVisible(false) fallaba:
//   El log debug-347aef.log mostro que m_bVisible se restaura a true entre
//   frames, ganando la carrera contra el ticker que setea false. Hookear
//   visit() salta el render a prueba de eso: no importa que m_bVisible sea
//   true, retornamos antes de dibujar el nodo y antes de visitar a sus hijos.
class $modify(PaimonPauseZoomVisitFilter, CCNode) {
    static void onModify(auto& self) {
        // Priority Late: corremos despues de otros mods que pudieran tener
        // hooks legitimos sobre CCNode::visit (devtools, debug overlays).
        // Si quien sea quiere ver el PauseLayer durante zoom, llama al
        // original ANTES que nuestro filtro, lo cual es lo correcto.
        (void)self.setHookPriorityPre("cocos2d::CCNode::visit", geode::Priority::Late);
    }

    void visit() {
        // Fast path: solo nos importa cuando el flag de zoom-hidden esta
        // activo. La mayoria de frames del juego no estan en pausa-zoom,
        // asi que esta es la rama caliente.
        if (paimon::isPauseZoomHidden()) {
            // Cast directo a CCNode* para comparacion (PauseLayer extiende
            // CCNode via CCBlockLayer/CCLayerColor/CCLayer/CCLayerRGBA).
            auto* activePause = static_cast<CCNode*>(paimon::getActivePauseLayer());
            if (activePause && this == activePause) {
                // Skip render del PauseLayer y de todos sus descendientes.
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