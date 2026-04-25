#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
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

#include "../utils/DominantColors.hpp"
#include "../features/thumbnails/services/LevelColors.hpp"
#include "../features/audio/services/AudioContextCoordinator.hpp"
#include "../features/foryou/services/ForYouTracker.hpp"
#include "../features/foryou/services/LevelTagsIntegration.hpp"
#include "../framework/compat/ModCompat.hpp"
#include <cstring>
#include <memory>
#include <chrono>

#include "../features/dynamic-songs/services/DynamicSongManager.hpp"
#include <Geode/binding/FMODAudioEngine.hpp>

using namespace geode::prelude;

namespace {
    std::atomic_bool gCaptureInProgress{false};

    CCNode* findGameplayNode(CCNode* root) {
        if (!root) return nullptr;
        auto* children = root->getChildren();
        if (!children) return nullptr;
        CCObject* obj = nullptr;
        
        // 1. Busca GJBaseGameLayer / GameLayer
        for (auto* node : CCArrayExt<CCNode*>(children)) {
            if (node) {
                if (typeinfo_cast<GJBaseGameLayer*>(node)) {
                    log::debug("[FindGameplay] Found GJBaseGameLayer");
                    return node;
                }
            }
        }

        // 2. Prueba por ID "game-layer"
        for (auto* node : CCArrayExt<CCNode*>(children)) {
            if (node) {
                std::string id = node->getID();
                if (id == "game-layer" || id == "GameLayer") {
                    log::debug("[FindGameplay] Found by ID: {}", id);
                    return node;
                }
            }
        }

        // 3. Recursivo saltando UILayer/PauseLayer
        for (auto* node : CCArrayExt<CCNode*>(children)) {
            if (node) {
                std::string cls = typeid(*node).name();
                std::string id = node->getID();
                
                // Salta contenedores de UI
                if (cls.find("UILayer") != std::string::npos || id == "UILayer") continue;
                if (cls.find("PauseLayer") != std::string::npos) continue;

                if (auto* found = findGameplayNode(node)) return found;
            }
        }
        return nullptr;
    }

    bool buildPathToNode(CCNode* root, CCNode* target, std::vector<CCNode*>& path) {
        if (!root) return false;
        path.push_back(root);
        if (root == target) return true;
        auto* children = root->getChildren();
        if (children) {
            for (auto* obj : CCArrayExt<CCObject*>(children)) {
                if (auto* node = typeinfo_cast<CCNode*>(obj)) {
                    if (buildPathToNode(node, target, path)) return true;
                }
            }
        }
        path.pop_back();
        return false;
    }

    void hideSiblingsOutsidePath(std::vector<CCNode*> const& path, std::vector<CCNode*>& hidden) {
        if (path.size() < 2) return;
        for (size_t i = 0; i + 1 < path.size(); ++i) {
            auto* parent = path[i];
            auto* keepChild = path[i + 1];
            auto* children = parent->getChildren();
            if (!children) continue;
            for (auto* obj : CCArrayExt<CCObject*>(children)) {
                if (auto* node = typeinfo_cast<CCNode*>(obj)) {
                    if (node != keepChild && node->isVisible()) {
                        node->setVisible(false);
                        hidden.push_back(node);
                    }
                }
            }
        }
    }

    bool isNonGameplayOverlay(CCNode* node, bool checkZ) {
        if (!node) return false;
        
        // Player nunca es UI
        if (typeinfo_cast<PlayerObject*>(node)) return false;

        // 1. Check de z-order
        if (checkZ && node->getZOrder() >= 10) return true;

        // 2. Heuristicas por nombre de clase
        std::string cls = typeid(*node).name();
        auto clsL = geode::utils::string::toLower(cls);

        if (clsL.find("uilayer") != std::string::npos ||
            clsL.find("pause") != std::string::npos ||
            clsL.find("menu") != std::string::npos ||
            clsL.find("dialog") != std::string::npos ||
            clsL.find("popup") != std::string::npos ||
            clsL.find("editor") != std::string::npos ||
            clsL.find("notification") != std::string::npos ||
            (clsL.find("label") != std::string::npos && clsL.find("gameobject") == std::string::npos) || // excluyo LabelGameObject
            clsL.find("progress") != std::string::npos ||
            clsL.find("status") != std::string::npos || // overlays tipo Megahack status
            clsL.find("trajectory") != std::string::npos || // cosas de showTrajectory
            clsL.find("hitbox") != std::string::npos) return true;

        // 3. Heuristicas por ID
        std::string id = node->getID();
        auto idL = geode::utils::string::toLower(id);
        if (!idL.empty()) {
            static std::vector<std::string> patterns = {
                "ui", "uilayer", "pause", "menu", "dialog", "popup", "editor", "notification", "btn", "button", "overlay", "checkpoint", "fps", "debug", "attempt", "percent", "progress", "bar", "score", "practice", "hitbox", "trajectory", "status"
            };
            for (auto const& p : patterns) {
                if (idL.find(p) != std::string::npos) return true;
            }
        }
        
        // 4. Tipos explicitos
        if (typeinfo_cast<CCMenu*>(node) != nullptr) return true;
        
        // CCLabelBMFont
        if (auto* label = typeinfo_cast<CCLabelBMFont*>(node)) {
             // Hijo directo con Z alta es UI
             if (checkZ && node->getZOrder() >= 10) return true;
             // si no es hijo directo, dejo que ID/clase decidan
        }

        return false;
    }

    // Oculta UI recursivamente
    void hideNonGameplayDescendants(CCNode* root, std::vector<CCNode*>& hidden, bool checkZ, PlayLayer* pl) {
        if (!root) return;
        auto* children = root->getChildren();
        if (!children) return;

        for (auto* obj : CCArrayExt<CCObject*>(children)) {
            auto* node = typeinfo_cast<CCNode*>(obj);
            if (!node) continue;

            // No toca jugadores
            if (pl) {
                if (node == pl->m_player1 || node == pl->m_player2) continue;
            }

            // Oculta UI y no sigue bajando
            if (node->isVisible() && isNonGameplayOverlay(node, checkZ)) {
                node->setVisible(false);
                hidden.push_back(node);
                log::debug("[Capture] Hide: ID='{}', Class='{}', Z={}", node->getID(), typeid(*node).name(), node->getZOrder());
            }
            // Solo baja si parece contenedor/layer
            else {
                std::string cls = typeid(*node).name();
                 // solo bajo si parece contenedor o layer
                 // no bajo en GameObjects, batch nodes, particle systems, etc
                if (cls.find("CCNode") != std::string::npos || cls.find("Layer") != std::string::npos) {
                     // evito meterme en GameLayer
                     if (cls.find("GameLayer") == std::string::npos) {
                        hideNonGameplayDescendants(node, hidden, false, pl);
                     }
                }
            }
        }
    }
}

static std::atomic<bool> s_hideP1ForCapture{false};
static std::atomic<bool> s_hideP2ForCapture{false};

class $modify(PaimonCapturePlayLayer, PlayLayer) {
    static void onModify(auto& self) {
        // La limpieza de audio/estado debe ocurrir justo antes del init original.
        (void)self.setHookPriorityPre("PlayLayer::init", geode::Priority::Last);
    }

    struct Fields {
        float m_frameTimer = 0.0f;
        std::chrono::steady_clock::time_point m_forYouSessionStart;
    };

    $override
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        // Mata dynamic song al entrar al gameplay
        AudioContextCoordinator::get().notifyGameplayStarted();

        s_hideP1ForCapture = false;
        s_hideP2ForCapture = false;
        log::info("[PaimonCapture] init() llamado para level {}", level ? level->m_levelID : 0);
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        log::info("[PaimonCapture] PlayLayer::init() exitoso");

        // Registra listener LOCAL para keybind de captura
        if (Mod::get()->getSettingValue<bool>("enable-thumbnail-taking")) {
            this->addEventListener(
                KeybindSettingPressedEventV3(Mod::get(), "capture-keybind"),
                [this](Keybind const& keybind, bool down, bool repeat, double timestamp) {
                    // Solo actua al presionar, no al soltar
                    if (!down || repeat) return;

                    // Verifica que este PlayLayer siga activo
                    if (PlayLayer::get() != this) return;

                    // Verifica que no este en pausa
                    if (this->m_isPaused) return;

                    // Verifica que el nivel tenga ID valido
                    if (!this->m_level || this->m_level->m_levelID <= 0) {
                        log::info("[Keybind] Level ID invalido, ignorando captura");
                        return;
                    }

                    // Evita capturas simultaneas
                    bool expected = false;
                    if (!gCaptureInProgress.compare_exchange_strong(expected, true)) return;

                    log::info("[Keybind] Captura activada con tecla: {} (timestamp: {})", keybind.toString(), timestamp);

                    // Pausa musica inmediatamente
                    if (auto* engine = FMODAudioEngine::sharedEngine()) {
                        if (engine->m_backgroundMusicChannel) {
                            engine->m_backgroundMusicChannel->setPaused(true);
                        }
                    }

                    // Difiere captura al siguiente frame via FramebufferCapture
                    int levelID = this->m_level->m_levelID;

                    Ref<PlayLayer> safeRef = this;
                    FramebufferCapture::requestCapture(levelID, [safeRef, levelID](bool success, CCTexture2D* texture, std::shared_ptr<uint8_t> rgbaData, int width, int height) {
                        Loader::get()->queueInMainThread([safeRef, success, texture, rgbaData, width, height, levelID]() {
                            auto* self = static_cast<PaimonCapturePlayLayer*>(safeRef.data());
                            if (!self->getParent()) {
                                gCaptureInProgress.store(false);
                                if (auto* engine = FMODAudioEngine::sharedEngine()) {
                                    if (engine->m_backgroundMusicChannel) {
                                        engine->m_backgroundMusicChannel->setPaused(false);
                                    }
                                }
                                return;
                            }

                            if (!success || !texture || !rgbaData) {
                                log::error("[Keybind] FramebufferCapture fallo");
                                gCaptureInProgress.store(false);
                                // Reanuda musica si la captura fallo
                                if (auto* engine = FMODAudioEngine::sharedEngine()) {
                                    if (engine->m_backgroundMusicChannel) {
                                        engine->m_backgroundMusicChannel->setPaused(false);
                                    }
                                }
                                return;
                            }

                            log::info("[Keybind] Captura exitosa: {}x{}", width, height);

                            // Pausa juego para mostrar popup de preview
                            bool pausedByPopup = false;
                            if (!self->m_isPaused) { self->pauseGame(true); pausedByPopup = true; }

                            auto* popup = CapturePreviewPopup::create(
                                texture,
                                levelID,
                                rgbaData,
                                width,
                                height,
                                [levelID, pausedByPopup](bool okSave, int levelIDAccepted, std::shared_ptr<uint8_t> buf, int W, int H, std::string mode, std::string replaceId){
                                    gCaptureInProgress.store(false);

                                    if (pausedByPopup) {
                                        auto* pl = PlayLayer::get();
                                        if (pl && pl->m_isPaused) {
                                            bool hasPause = false;
                                            if (auto* sc = CCDirector::sharedDirector()->getRunningScene()) {
                                                CCArrayExt<CCNode*> children(sc->getChildren());
                                                for (auto child : children) { 
                                                    if (typeinfo_cast<PauseLayer*>(child)) { 
                                                        hasPause = true; 
                                                        break; 
                                                    } 
                                                }
                                            }
                                            if (!hasPause) {
                                                if (auto* d = CCDirector::sharedDirector()) {
                                                    if (d->getScheduler() && d->getActionManager()) {
                                                        d->getScheduler()->resumeTarget(pl);
                                                        d->getActionManager()->resumeTarget(pl);
                                                        pl->m_isPaused = false;
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    if (okSave && levelIDAccepted > 0 && buf) {
                                        if (W <= 0 || H <= 0) {
                                            log::error("[Keybind] Dimensiones invalidas: {}x{}", W, H);
                                            return;
                                        }
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
                                            if (auto* gm = GameManager::sharedState()) {
                                                username = gm->m_playerName;
                                                if (auto* am = GJAccountManager::get()) {
                                                    accountID = am->m_accountID;
                                                }
                                            }
                                            if (username.empty()) username = "unknown";

                                            if (accountID <= 0) {
                                                PaimonNotify::create(Localization::get().getString("level.account_required"), NotificationIcon::Error)->show();
                                                return;
                                            }

                                            // Single upload: servidor maneja mod check
                                            PaimonNotify::create(Localization::get().getString("capture.uploading"), NotificationIcon::Info)->show();
                                            ThumbnailAPI::get().uploadThumbnail(levelIDAccepted, pngData, username, [levelIDAccepted, username](bool success, std::string const& msg){
                                                if (success) {
                                                    bool isPending = (msg.find("pending") != std::string::npos || msg.find("verification") != std::string::npos);
                                                    if (isPending) {
                                                        PendingQueue::get().addOrBump(levelIDAccepted, PendingCategory::Verify, username, {}, false);
                                                        PaimonNotify::create(Localization::get().getString("capture.suggested"), NotificationIcon::Success)->show();
                                                    } else {
                                                        PendingQueue::get().removeForLevel(levelIDAccepted);
                                                        PaimonNotify::create(Localization::get().getString("capture.upload_success"), NotificationIcon::Success)->show();
                                                    }
                                                } else {
                                                    PaimonNotify::create(Localization::get().getString("capture.upload_error") + (msg.empty() ? std::string("") : (" (" + msg + ")")), NotificationIcon::Error)->show();
                                                }
                                            });
                                        }
                                    }
                                },
                                [safeRef](bool hideP1, bool hideP2, CapturePreviewPopup* popup) {
                                    s_hideP1ForCapture = hideP1;
                                    s_hideP2ForCapture = hideP2;
                                    if (popup) popup->setVisible(false);
                                    gCaptureInProgress.store(false);
                                    Loader::get()->queueInMainThread([safeRef, popup]() {
                                        auto* self = static_cast<PaimonCapturePlayLayer*>(safeRef.data());
                                        if (!self->getParent()) return;
                                        self->captureScreenshot(popup);
                                    });
                                },
                                s_hideP1ForCapture,
                                s_hideP2ForCapture
                            );
                            if (popup) {
                                popup->setPausedMusic(true);
                                popup->show();
                            } else {
                                gCaptureInProgress.store(false);
                            }
                        });
                    });
                }
            );
            log::info("[PaimonCapture] Keybind listener LOCAL registrado para captura");
        }

        // Para Ti: registrar entrada al nivel
        m_fields->m_forYouSessionStart = std::chrono::steady_clock::now();
        paimon::foryou::ForYouTracker::get().onLevelEnter(this->m_level);

        // Fetch tags for this level if Level-Tags is loaded
        if (paimon::compat::ModCompat::isLevelTagsLoaded() && this->m_level && this->m_level->m_levelID > 0) {
            int levelID = this->m_level->m_levelID;
            paimon::foryou::LevelTagsIntegration::get().fetchTagsForLevel(levelID,
                [levelID](std::vector<std::string> tags) {
                    if (!tags.empty()) {
                        paimon::foryou::ForYouTracker::get().onLevelTagsFetched(levelID, tags);
                    }
                });
        }

        log::info("[PaimonCapture] init() completado exitosamente");
        return true;
    }
    
    $override
    void onQuit() {
        // Cancela captura pendiente para evitar leak de Ref<PlayLayer>
        FramebufferCapture::cancelPending();
        CaptureLayerEditorPopup::restoreAllLayers();
        CaptureAssetBrowserPopup::restoreAllAssets();
        gCaptureInProgress.store(false);

        // Para Ti: registrar salida del nivel
        paimon::foryou::ForYouTracker::get().onLevelExit(this->m_level);

        // addEventListener se limpia automaticamente
        PlayLayer::onQuit();
    }

    void triggerRecapture(float dt) {
        this->captureScreenshot();
    }

    void captureScreenshot(CapturePreviewPopup* existingPopup = nullptr) {
        if (gCaptureInProgress.load()) return;
        gCaptureInProgress.store(true);

        auto* director = CCDirector::sharedDirector();
        if (!director || !this->m_level) { gCaptureInProgress.store(false); return; }
        auto* scene = director->getRunningScene();

        log::debug("=== STARTING CAPTURE ===");
        // Log hijos directos del PlayLayer
        {
            auto children = this->getChildren();
            if (children) {
                for (auto* obj : CCArrayExt<CCObject*>(children)) {
                    auto node = typeinfo_cast<CCNode*>(obj);
                    if (node) {
                        std::string cls = typeid(*node).name();
                        std::string id = node->getID();
                        log::debug("PlayLayer Child: Class='{}', ID='{}', Z={}, Vis={}", cls, id, node->getZOrder(), node->isVisible());
                    }
                }
            }
        }

        std::vector<CCNode*> hidden;
        
        PlayerVisState p1State, p2State;

        if (s_hideP1ForCapture) {
            paimTogglePlayer(this->m_player1, p1State, true);
        }
        if (s_hideP2ForCapture) {
            paimTogglePlayer(this->m_player2, p2State, true);
        }
        
        // oculto UI con checkZ activado en la raiz
        log::debug("[Capture] Iniciando limpieza recursiva de UI en PlayLayer");
        hideNonGameplayDescendants(this, hidden, true, this);

        // debug
        if (!hidden.empty()) log::debug("[Capture] Ocultados {} nodos (recursivo)", hidden.size());

        // Oculta m_uiLayer manualmente
        if (this->m_uiLayer && this->m_uiLayer->isVisible()) {
            this->m_uiLayer->setVisible(false);
            hidden.push_back(this->m_uiLayer);
            log::debug("[Capture] Ocultando m_uiLayer explicitamente");
        }

        // Pasada extra por hijos directos para overlays escapados
        for (auto* obj : CCArrayExt<CCObject*>(this->getChildren())) {
            auto* node = typeinfo_cast<CCNode*>(obj);
            if (!node) continue;
            if (node == this->m_uiLayer) continue;

            if (node->isVisible() && isNonGameplayOverlay(node, true)) {
                bool alreadyHidden = false;
                for (auto* h : hidden) {
                    if (h == node) {
                        alreadyHidden = true;
                        break;
                    }
                }

                if (!alreadyHidden) {
                    node->setVisible(false);
                    hidden.push_back(node);
                    log::debug("[Capture] Ocultando nodo UI (Backup Loop): ID='{}', Class='{}', Z={}",
                        node->getID(), typeid(*node).name(), node->getZOrder());
                }
            }
        }

        auto hiddenCopy = hidden;
        int levelID = this->m_level->m_levelID;

        // Captura framebuffer con overlays ocultos
        log::debug("[Capture] Capturando PlayLayer usando RenderTexture");

        // 2. Captura propiamente dicha
        auto view = CCEGLView::sharedOpenGLView();
        auto screenSize = view->getFrameSize();
        int screenW = static_cast<int>(screenSize.width);
        int screenH = static_cast<int>(screenSize.height);

        // Respetar la configuracion de resolucion de captura (igual que FramebufferCapture/CapturePreviewPopup)
        std::string res = geode::Mod::get()->getSettingValue<std::string>("capture-resolution");
        int targetW = 1920;
        if (res == "4k")         targetW = 3840;
        else if (res == "1440p") targetW = 2560;

        double aspect = (screenH > 0) ? static_cast<double>(screenW) / static_cast<double>(screenH) : (16.0 / 9.0);
        int width = targetW;
        int height = std::max(1, static_cast<int>(std::round(width / aspect)));

        std::unique_ptr<uint8_t[]> data;
        bool needsVerticalFlip = true;

        // Pinta en RenderTexture para leer pixeles
        RenderTexture rt(width, height);
        rt.begin();
        this->visit();
        rt.end();
        data = rt.getData();
        
        needsVerticalFlip = true; // Flip necesario por glReadPixels

        // Restaura visibilidad de lo ocultado
        for (auto* n : hiddenCopy) {
            if (n) {
                n->setVisible(true);
            }
        }
        
        // Restaura visibilidad de jugadores
        if (s_hideP1ForCapture) {
            paimTogglePlayer(this->m_player1, p1State, false);
        }
        if (s_hideP2ForCapture) {
            paimTogglePlayer(this->m_player2, p2State, false);
        }
        
        if (!data) { 
            gCaptureInProgress.store(false); 
            return; 
        }

        // flip en Y (para dejar la imagen con origen en la esquina superior)
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

        // Crea CCTexture2D con datos RGBA8888 usando Ref<>
        auto* rawTex = new CCTexture2D();
        if (!rawTex->initWithData(data.get(), kCCTexture2DPixelFormat_RGBA8888,
                               width, height,
                               CCSize(static_cast<float>(width), static_cast<float>(height)))) {
            rawTex->release();
            gCaptureInProgress.store(false);
            return;
        }
        rawTex->setAntiAliasTexParameters();
        // Ref<> gestiona refcount automaticamente
        Ref<CCTexture2D> tex = rawTex;
        rawTex->release(); // Ref<> ya retiene, balanceo el new

        // Copia a shared_ptr para el popup
        std::shared_ptr<uint8_t> rgba(new uint8_t[width * height * 4], std::default_delete<uint8_t[]>());
        memcpy(rgba.get(), data.get(), width * height * 4);
        
        bool pausedByPopup = false;
        if (!this->m_isPaused) { this->pauseGame(true); pausedByPopup = true; }
        
        // Actualiza popup existente
        if (existingPopup) {
            existingPopup->updateContent(tex, rgba, width, height);
            existingPopup->setVisible(true);
            // Ref<> gestiona el refcount automaticamente
            gCaptureInProgress.store(false);
            return;
        }

        bool isMod = PaimonUtils::isUserModerator();

        auto* popup = CapturePreviewPopup::create(
            tex, 
            levelID, 
            rgba, 
            width, 
            height, 
            [levelID, pausedByPopup](bool okSave, int levelIDAccepted, std::shared_ptr<uint8_t> buf, int W, int H, std::string mode, std::string replaceId){
                gCaptureInProgress.store(false);
                if (pausedByPopup) {
                    auto* pl = PlayLayer::get();
                    if (pl && pl->m_isPaused) {
                        bool hasPause = false;
                        if (auto* sc = CCDirector::sharedDirector()->getRunningScene()) {
                            CCArrayExt<CCNode*> children(sc->getChildren());
                            for (auto child : children) { 
                                if (typeinfo_cast<PauseLayer*>(child)) { 
                                    hasPause = true; 
                                    break; 
                                } 
                            }
                        }
                        if (!hasPause) {
                            if (auto* d = CCDirector::sharedDirector()) {
                                if (d->getScheduler() && d->getActionManager()) {
                                    d->getScheduler()->resumeTarget(pl);
                                    d->getActionManager()->resumeTarget(pl);
                                    pl->m_isPaused = false;
                                }
                            }
                        }
                    }
                }

                // si el usuario acepta guardar, proceso la mini
                if (okSave && levelIDAccepted > 0 && buf) {
                    // Podria guardar buffer en disco local

                    // Extrae colores dominantes para gradientes
                    
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

                    // Convierte a PNG
                    
                    std::vector<uint8_t> rgbaData(static_cast<size_t>(W) * static_cast<size_t>(H) * 4);
                    memcpy(rgbaData.data(), buf.get(), rgbaData.size());
                    
                    std::vector<uint8_t> pngData;
                    if (!ImageConverter::rgbToPng(rgbaData, static_cast<uint32_t>(W), static_cast<uint32_t>(H), pngData)) {
                        PaimonNotify::create(Localization::get().getString("capture.save_png_error"), NotificationIcon::Error)->show();
                    } else {
                        // Obtiene username/accountID
                        std::string username;
                        int accountID = 0;
                        if (auto* gm = GameManager::sharedState()) {
                            username = gm->m_playerName;
                            if (auto* am = GJAccountManager::get()) {
                                accountID = am->m_accountID;
                            }
                        }
                        if (username.empty()) username = "unknown";

                        if (accountID <= 0) {
                            PaimonNotify::create(Localization::get().getString("level.account_required"), NotificationIcon::Error)->show();
                            return;
                        }

                        // Single upload: servidor maneja routing
                        PaimonNotify::create(Localization::get().getString("capture.uploading"), NotificationIcon::Info)->show();
                        ThumbnailAPI::get().uploadThumbnail(levelIDAccepted, pngData, username, [levelIDAccepted, username](bool success, std::string const& msg){
                            if (success) {
                                bool isPending = (msg.find("pending") != std::string::npos || msg.find("verification") != std::string::npos);
                                if (isPending) {
                                    PendingQueue::get().addOrBump(levelIDAccepted, PendingCategory::Verify, username, {}, false);
                                    PaimonNotify::create(Localization::get().getString("capture.suggested"), NotificationIcon::Success)->show();
                                } else {
                                    PendingQueue::get().removeForLevel(levelIDAccepted);
                                    PaimonNotify::create(Localization::get().getString("capture.upload_success"), NotificationIcon::Success)->show();
                                }
                            } else {
                                PaimonNotify::create(Localization::get().getString("capture.upload_error") + (msg.empty() ? std::string("") : (" (" + msg + ")")), NotificationIcon::Error)->show();
                            }
                        });
                    }
                }
                // la flag ya se reseteo arriba
        },
        [this](bool hideP1, bool hideP2, CapturePreviewPopup* popup) {
            s_hideP1ForCapture = hideP1;
            s_hideP2ForCapture = hideP2;
            
            // Oculta popup para nueva captura
            if (popup) popup->setVisible(false);

            gCaptureInProgress.store(false);
            WeakRef<PaimonCapturePlayLayer> self = this;
            Loader::get()->queueInMainThread([self, popup]() {
                auto layer = self.lock();
                if (!layer) return;
                layer->captureScreenshot(popup);
            });
        },
        s_hideP1ForCapture,
        s_hideP2ForCapture
        );
        if (popup) { 
            if (existingPopup) {
                // Refresca popup existente
            } else {
                popup->show(); 
            }
            // Ref<> gestiona el refcount de tex automaticamente al salir de scope
        }
        else { 
            gCaptureInProgress.store(false);
        }
    }
};

