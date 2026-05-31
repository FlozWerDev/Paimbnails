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
#include "../utils/ImageConverter.hpp"
#include "../utils/PaimonLoadingOverlay.hpp"
#include "../utils/FileDialog.hpp"
#include "../features/moderation/services/ModeratorUtils.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include <Geode/binding/LoadingCircle.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include "../utils/PaimonNotification.hpp"

#include "../utils/ActivePauseLayer.hpp"

#include "../utils/SpriteHelper.hpp"

using namespace geode::prelude;

namespace {
// #region agent log
// PERF: cuerpo gateado tras PAIMON_DEBUG_AGENT347 (igual que en PlayLayer.cpp).
// Por defecto no-op para evitar abrir/cerrar el log file en cada captura.
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
        paimon::setActivePauseLayer(this);

        // Aseguramos que la flag de zoom-hidden esta limpia al crear un
        // nuevo PauseLayer. Sin esto, si una sesion anterior dejo la flag
        // en true (ej. crash, scene change abrupto), el nuevo PauseLayer
        // nunca se renderizaria.
        paimon::setPauseZoomHidden(false);

        log::info("[PauseLayer] customSetup");

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
            // Fallback: buscar un CCMenu en el lado correcto, pero
            // validando que contenga botones conocidos del PauseLayer.
            // Sin esa validacion, otros mods con menus en la misma zona
            // (BetterEdit, Globed, etc.) podrian ser elegidos por
            // accidente.
            auto winSize = CCDirector::get()->getWinSize();
            // IDs de botones que esperamos en cada lado del PauseLayer.
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

                // Score por presencia de botones conocidos. Un menu
                // con 2+ botones conocidos casi seguro es el correcto
                // y nos protegemos de menus de otros mods que sólo
                // contengan sus propios botones.
                float score = 0.f;
                for (auto const* const* p = knownIDs; *p != nullptr; ++p) {
                    if (menu->getChildByID(*p)) score += 10.f;
                }
                // Tie-break por cantidad de hijos (los menus de GD
                // suelen tener mas botones que los de mods).
                score += static_cast<float>(menu->getChildrenCount()) * 0.1f;

                if (!best || score > bestScore) {
                    best = menu;
                    bestScore = score;
                }
            }
            // Si el "mejor" menu encontrado no contiene NINGUN boton
            // conocido, mejor no devolver nada para evitar añadir
            // nuestro boton a un menu de otro mod.
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

        // Idempotencia: customSetup puede invocarse varias veces (otros mods
        // re-construyen el menu). Si el boton ya existe, no duplicamos.
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
            rightMenu->addChild(btn);
            rightMenu->updateLayout();

            // anade boton para elegir archivo (icono de carpeta)
            // Idempotencia: el guard de capture-button arriba ya filtra reentry
            // pero por defensa anidada chequeamos tambien aqui.
            if (rightMenu->getChildByID("thumbnail-select-button"_spr)) {
                // ya añadido en una invocacion previa
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

    // ── NOTA: el override de visit() NO se hace aqui ───────────────────
    //
    // PauseLayer no expone visit() en su modify binding (solo lo expone
    // CCNode), por lo que `$override void visit()` dentro de este $modify
    // no hookea nada — el primer intento fallo silenciosamente.
    //
    // El skip-de-render durante zoom esta implementado via $modify(CCNode)
    // en PlayLayer.cpp (PaimonPauseZoomVisitFilter), que hookea CCNode::visit
    // y filtra por puntero al activePauseLayer cuando paimon::isPauseZoomHidden
    // esta en true. El hook se aplica a TODOS los nodos pero el check es
    // O(1) (comparacion de punteros), asi que el costo es despreciable.

    void onScreenshot(CCObject*) {
        log::info("[PauseLayer] Capture button pressed; hiding pause menu");
        if (m_fields->m_captureInProgress) {
            log::warn("[PauseLayer] Capture already in progress, ignoring duplicate request");
            return;
        }

        // Mutua exclusión con el camino B (capture-keybind del PlayLayer):
        // si una captura por keybind está en curso, no iniciamos otra desde
        // el botón del PauseLayer. Sin esta guarda, ambos caminos podrían
        // pisarse el `s_request` de FramebufferCapture y dejar uno de los
        // dos callbacks huérfano, con flags `gCaptureInProgress`/
        // `paimon::isCaptureInProgress` colgados como true para siempre.
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

        // Oculta menu de pausa temporalmente
        bool const visBefore = this->isVisible();
        this->setVisible(false);
        // Setea flag global para que el PauseZoomManager::update() ticker NO
        // restaure la visibilidad del PauseLayer durante el periodo entre
        // setVisible(false) y la captura efectiva en swapBuffers (~0.05s).
        // Sin esto, si el zoom esta en 1.0 y autoShow esta on, el ticker
        // restaurara visibilidad y el PauseLayer aparecera en la captura.
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

        // Muestra circulo de carga
        showLoadingOverlay();
        // Guard rail: restaura UI si el callback no vuelve
        this->scheduleOnce(schedule_selector(PaimonPauseLayer::captureSafetyRestore), 8.0f);

        // Programa captura y restaura menu
        auto scheduler = CCDirector::get()->getScheduler();
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
        auto scheduler = CCDirector::get()->getScheduler();
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
        // Si ya no estamos en la escena no tocamos nada — un onExit() posterior
        // o el destructor se encargara de limpiar.
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

        // Guarda esencial: si este PauseLayer ya no esta en la escena
        // (ej: usuario hizo restart mientras esperaba los 0.05s),
        // no tocamos nada.
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

            // Validaciones pre-captura (High Graphics, LDM, muerte)
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

            // Oculta overlay para captura limpia
            auto scene = CCDirector::get()->getRunningScene();
            if (scene) {
                auto overlay = scene->getChildByID("paimon-loading-overlay"_spr);
                if (overlay) overlay->setVisible(false);
            }

            // Muestra overlay en siguiente frame
            CCDirector::get()->getScheduler()->scheduleSelector(
                schedule_selector(PaimonPauseLayer::reShowOverlay),
                this, 0.0f, 0, 0.0f, false
            );

            // WeakRef en vez de Ref: si el PauseLayer se destruye (ej.
            // restart/quit mientras la captura esta en proceso) no queremos
            // revivirlo ni ejecutar callbacks sobre memoria huérfana.
            geode::WeakRef<PauseLayer> weakRef = this;

            // Usa FramebufferCapture
            FramebufferCapture::requestCapture(levelID, [weakRef, levelID](bool success, CCTexture2D* texture, std::shared_ptr<uint8_t> rgbData, int width, int height) {
                // Retiene textura con Ref<> para que sobreviva hasta que
                // el lambda encolado se ejecute en el siguiente frame.
                Ref<CCTexture2D> texRef = texture;
                Loader::get()->queueInMainThread([weakRef, success, texRef, rgbData, width, height, levelID]() {
                    CCTexture2D* texture = texRef.data();
                    auto locked = weakRef.lock();
                    if (!locked) {
                        log::debug("[PauseLayer] Capture callback skipped: PauseLayer was destroyed");
                        // Aun asi limpiamos el flag global porque la captura ya termino
                        paimon::setCaptureInProgress(false);
                        return;
                    }
                    auto* self = static_cast<PaimonPauseLayer*>(locked.data());
                    // Doble check: aunque el objeto exista, puede haber perdido
                    // su parent si un onExit() ya corrió.
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
                    self->setVisible(true);
                    log::info("[PauseLayer] Pause menu restored after capture");
                });
            });

    }

    $override
    void onExit() {
        // Red de seguridad: cubre las rutas que NO pasan por onResume()
        // (Esc/keyBackClicked, quit, restart, scene transitions). Sin esto,
        // el ticker del PauseZoomManager podria seguir tocando este nodo
        // mientras se destruye.
        paimon::notifyPauseClosing();
        paimon::clearActivePauseLayer(this);
        // Limpia el flag global de captura por si quedo seteado (ej. si el
        // usuario hizo quit/restart mientras una captura estaba pendiente).
        paimon::setCaptureInProgress(false);
        // Limpia el flag de zoom-hidden tambien — sin esto, si la pausa se
        // cierra abruptamente con zoom activo, la flag queda en true y el
        // siguiente PauseLayer no se renderiza nunca.
        paimon::setPauseZoomHidden(false);
        m_fields->m_captureInProgress = false;
        m_fields->m_fileDialogOpen = false;

        // Cancela cualquier captura pendiente para que callbacks async no
        // toquen este PauseLayer despues de que pierda su parent.
        FramebufferCapture::cancelPending();

        // Limpia TODOS los selectors programados en este PauseLayer para
        // evitar use-after-free si el scheduler los ejecuta despues de
        // que onExit() termine pero antes de que el destructor corra.
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

            // Validar magic bytes de MP4 (ftyp al offset 4)
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
                    texture->release();
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
                    texture->release();
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
                    // texture se pasa al popup; el popup hace retain
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

    void onResume(CCObject* sender) {
        // Red de seguridad: si PlayLayer::get() es nulo, llamar a PauseLayer::onResume
        // causará un crash por acceso a memoria (EXCEPTION_ACCESS_VIOLATION) en PlayLayer::resume
        // ya que intenta acceder a campos de un PlayLayer inexistente.
        if (!PlayLayer::get()) {
            log::warn("[PauseLayer] onResume called but PlayLayer::get() is null. Preventing crash.");
            // No hacemos removeFromParentAndCleanup aqui — si el PauseLayer esta siendo
            // destruido por una transicion de escena, forzar el cleanup puede causar
            // double-free. Es mas seguro dejar que el sistema maneje la destruccion.
            return;
        }

        // Notifica al PauseZoomManager INMEDIATAMENTE cuando el usuario
        // presiona Resume, no al final de la animacion de cierre. Sin esto,
        // el ticker del manager sigue ejecutandose mientras el PauseLayer
        // anima su salida y llama setPauseMenuVisible(true)/showLayer() cada
        // frame, lo que reinicia la animacion de entrada y produce el
        // sintoma de "menu pegado" al despausar rapido. Si la transicion
        // queda en estado inconsistente, el destructor del PauseLayer ejecuta
        // acciones encoladas sobre memoria liberada y el juego crashea.
        paimon::notifyPauseClosing();
        PauseLayer::onResume(sender);
    }

    void onRestart(CCObject* sender) {
        if (!PlayLayer::get()) {
            log::warn("[PauseLayer] onRestart called but PlayLayer::get() is null. Preventing crash.");
            return;
        }
        // Notifica al PauseZoomManager para limpiar estado de zoom y evitar
        // que el siguiente nivel entre con el PauseLayer invisible.
        paimon::notifyPauseClosing();
        PauseLayer::onRestart(sender);
    }

    void onRestartFull(CCObject* sender) {
        if (!PlayLayer::get()) {
            log::warn("[PauseLayer] onRestartFull called but PlayLayer::get() is null. Preventing crash.");
            return;
        }
        // Notifica al PauseZoomManager para limpiar estado de zoom.
        paimon::notifyPauseClosing();
        PauseLayer::onRestartFull(sender);
    }

    void onNormalMode(CCObject* sender) {
        if (!PlayLayer::get()) {
            log::warn("[PauseLayer] onNormalMode called but PlayLayer::get() is null. Preventing crash.");
            return;
        }
        // Notifica al PauseZoomManager para limpiar estado de zoom.
        paimon::notifyPauseClosing();
        PauseLayer::onNormalMode(sender);
    }

    void onPracticeMode(CCObject* sender) {
        if (!PlayLayer::get()) {
            log::warn("[PauseLayer] onPracticeMode called but PlayLayer::get() is null. Preventing crash.");
            return;
        }
        // Notifica al PauseZoomManager para limpiar estado de zoom.
        paimon::notifyPauseClosing();
        PauseLayer::onPracticeMode(sender);
    }
};
