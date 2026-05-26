#include <Geode/modify/LevelSelectLayer.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/BoomScrollLayer.hpp>
#include <Geode/binding/GJGroundLayer.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/audio/services/AudioContextCoordinator.hpp"
#include "../features/dynamic-songs/services/DynamicSongManager.hpp"
#include "../features/profile-music/services/ProfileMusicManager.hpp"
#include "../features/menu-loop/services/MenuLoopManager.hpp"
#include "../features/menu-loop/services/MenuLoopControl.hpp"
#include "../features/menu-loop/ui/NowPlayingCard.hpp"
#include "../utils/AudioInterop.hpp"
#include "../utils/Shaders.hpp"
#include "../blur/BlurSystem.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../framework/EventBus.hpp"
#include "../framework/ModEvents.hpp"
#include "../framework/compat/SceneLocators.hpp"
#include <functional>

using namespace geode::prelude;
using namespace Shaders;

namespace {
    inline void restoreMenuLoopPositionIfNeeded() {
        auto& sm = paimon::menuloop::MenuLoopManager::get();
        if (sm.isOriginalMenuLoop() || sm.isOverride()) return;

        auto* colon = sm.getColonMenuLoopStartTime();
        if ((colon && colon->getSettingValue<bool>("enable")) || !sm.getShouldRestoreMenuLoopPoint()) {
            sm.setPauseSongPositionTracking(false);
            return;
        }

        auto* fmod = FMODAudioEngine::get();
        if (fmod && !sm.getPauseSongPositionTracking()) {
            auto oldTrack = fmod->getActiveMusic(0);
            if (oldTrack == sm.getCurrentSong()) {
                sm.setPauseSongPositionTracking(false);
                return;
            }
        }

        sm.restoreLastMenuLoopPosition();
        sm.setShouldRestoreMenuLoopPoint(false);
        sm.setPauseSongPositionTracking(false);
    }
}

// Evita que GameManager pise canciones dinamicas o de perfil.
// Usamos Priority::Late en vez de Last para no bloquear a otros mods que
// quieran observar/decidir DESPUES de nosotros (con Priority::VeryLate o
// Last). Esto mantiene la coordinacion cooperativa sin pisar a otros.
class $modify(PaimonGameManager, GameManager) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("GameManager::fadeInMenuMusic", geode::Priority::Late);
    }

    $override
    void fadeInMenuMusic() {
        bool passthrough = Mod::get()->getSavedValue<bool>("music-hook-passthrough", false);
        if (passthrough) {
            GameManager::fadeInMenuMusic();
            restoreMenuLoopPositionIfNeeded();
            return;
        }

        auto* dsm = DynamicSongManager::get();

        // Notificar a otros mods que estamos pisando audio para que puedan
        // reaccionar (ej: un mod de musica externo puede pausarse).
        auto notifyBlocked = [](char const* reason) {
            paimon::EventBus::get().publish(paimon::AudioOwnerChangedEvent{
                "menu", reason, 0
            });
        };

        if (paimon::isDynamicSongInteropActive() && dsm->isInValidLayer()) {
            notifyBlocked("paimon-dynamic");
            return;
        }
        if (dsm->hasSuspendedPlayback()) {
            notifyBlocked("paimon-dynamic-suspended");
            return;
        }
        if (paimon::isDynamicSongInteropActive() && !dsm->isActive()) paimon::setDynamicSongInteropActive(false);
        if (paimon::isProfileMusicInteropActive()) {
            notifyBlocked("paimon-profile-music");
            return;
        }
        if (paimon::isVideoAudioInteropActive()) {
            notifyBlocked("paimon-video-audio");
            return;
        }
        GameManager::fadeInMenuMusic();
        restoreMenuLoopPositionIfNeeded();
    }
};

// Evita que GD reinicie musica en transiciones.
// TambiÃ©n maneja transiciones de mÃºsica (guardar/restaurar posiciÃ³n).
// Priority::Late (no Last) — ver comentario arriba.
class $modify(PaimonFMODAudioEngine, FMODAudioEngine) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("FMODAudioEngine::playMusic", geode::Priority::Late);
    }

    $override
    void update(float dt) {
        FMODAudioEngine::update(dt);

        // Perf: Menu loop position tracking and constant shuffle are already
        // handled by PaimonMenuLoopFMODHook.
        auto* engine = FMODAudioEngine::sharedEngine();
        if (!engine) return;
        auto* ch = engine->getActiveMusicChannel(0);
        if (!ch) return;
        // â”€â”€ Menu Loop position tracking + constant shuffle â”€â”€
        auto& sm = paimon::menuloop::MenuLoopManager::get();
        if (!GJBaseGameLayer::get() && !sm.isOriginalMenuLoop() && !paimon::menuloop::isVanillaMenuLoopDisabled()) {
            unsigned int position = 0;
            if (ch->getPosition(&position, FMOD_TIMEUNIT_MS) == FMOD_OK) {
                if (!sm.getPauseSongPositionTracking()) {
                    sm.setLastMenuLoopPosition(position);
                }
            }

            // Constant shuffle: switch song when near end
            if (sm.getConstantShuffleMode() && !sm.isOverride() && sm.getSongsSize() >= 2) {
                bool isPlaying = true;
                FMOD::Sound* sound = nullptr;
                unsigned int length = 0;
                ch->getCurrentSound(&sound);
                ch->isPlaying(&isPlaying);
                if (sound && isPlaying) {
                    sound->getLength(&length, FMOD_TIMEUNIT_MS);
                    if (length > 0 && (length - 100) < position) {
                        paimon::menuloop::MenuLoopControl::constantShuffleModeNewSong();
                        if (auto* menuLayer = GameManager::get()->m_menuLayer) {
                            paimon::menuloop::NowPlayingCard::showForCurrentSong(menuLayer);
                        }
                    }
                }
            }
        }
    }

    $override
    void stopAllMusic(bool p0) {
        // Pausar tracking de posiciÃ³n del MenuLoopManager
        if (!GJBaseGameLayer::get()) {
            paimon::menuloop::MenuLoopManager::get().setPauseSongPositionTracking(true);
        }
        FMODAudioEngine::stopAllMusic(p0);
    }

    $override
    void playMusic(gd::string path, bool shouldLoop, float fadeInTime, int channel) {
        bool passthrough = Mod::get()->getSavedValue<bool>("music-hook-passthrough", false);
        if (passthrough) {
            FMODAudioEngine::playMusic(path, shouldLoop, fadeInTime, channel);
            return;
        }
        auto requestedPath = static_cast<std::string>(path);
        std::string menuTrack = GameManager::get() ? std::string(GameManager::get()->getMenuMusicFile()) : std::string();
        bool isMenuTrack = !menuTrack.empty() && requestedPath == menuTrack;

        if (!DynamicSongManager::s_selfPlayMusic) {
            auto* dsm = DynamicSongManager::get();

            if (paimon::isVideoAudioInteropActive() && isMenuTrack) {
                return;
            }

            if (paimon::isProfileMusicInteropActive()) {
                if (isMenuTrack) {
                    return;
                }

                ProfileMusicManager::get().forceStop();
                FMODAudioEngine::playMusic(path, shouldLoop, fadeInTime, channel);
                return;
            }

            if (paimon::isDynamicSongInteropActive() && dsm->isInValidLayer()) {
                return;
            }
            if (dsm->hasSuspendedPlayback() && isMenuTrack) {
                return;
            }
        }
        FMODAudioEngine::playMusic(path, shouldLoop, fadeInTime, channel);
    }
};

class $modify(PaimonLevelSelectLayer, LevelSelectLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("LevelSelectLayer::init", geode::Priority::Late);
    }

    struct Fields {
        Ref<CCSprite> m_bgSprite = nullptr;
        Ref<CCSprite> m_sharpBgSprite = nullptr;
        Ref<CCNode> m_bottomGradient = nullptr;
        Ref<CCNode> m_pageSliderThumb = nullptr;
        float m_sliderBgWidth = 0.f;
        float m_sliderStartX = 0.f;
        float m_sliderThumbWidth = 0.f;
        int m_currentLevelID = 0;
        float m_pageCheckTimer = 0.f;
        float m_smoothedPeak = 0.f;
        int m_verifyFrameCounter = 0;  // verificar musica cada ~1s
        bool m_meteringEnabled = false;
        bool m_audioCleanedUp = false;
        // Perf: cache dynamic-song setting and throttle metering
        bool m_cachedDynamicSong = false;
        int m_meteringFrameCounter = 0;
    };

    $override
    bool init(int p0) {
        if (!LevelSelectLayer::init(p0)) return false;

        auto win = CCDirector::sharedDirector()->getWinSize();

        // pagina 0 = nivel 1 (Stereo Madness)
        int levelID = p0 + 1;
        m_fields->m_currentLevelID = levelID;
        
        // bg del nivel
        this->updateThumbnailBackground(levelID);
        
        // quitar el fondo que GD pone (respetando nodos de otros mods)
        if (m_backgroundSprite) {
            m_backgroundSprite->setVisible(false);
        }
        if (m_groundLayer) {
            m_groundLayer->setVisible(false);
        }
        // Delegamos a LevelSelectLocator para respetar nodos de otros mods
        // (texture-loader, happy textures, imageplus, etc.).
        paimon::compat::LevelSelectLocator::hideVanillaBackground(this);

        // ocultar los dots del BoomScrollLayer y reemplazar con slider
        if (m_scrollLayer) {
            m_scrollLayer->togglePageIndicators(false);

            // ocultar page-buttons originales (flechas izquierda/derecha)
            // SOLO por ID conocido — antes usabamos heuristica
            // (posY<50 && childCount<=2) que ocultaba menus arbitrarios de
            // otros mods (BetterInfo, More Icons, BackgroundLight, etc.)
            // que sucedian a tener pocos hijos en la zona inferior.
            // node-ids estandariza estos IDs en LevelSelectLayer.
            static constexpr char const* kArrowMenuIDs[] = {
                "page-buttons", "prev-page-menu", "next-page-menu",
                "prev-next-menu", "arrow-menu", " arrows-menu",
                "prev-btn", "next-btn", "prev-arrow", "next-arrow"
            };
            auto isArrowMenuID = [](std::string const& id) {
                for (auto* candidate : kArrowMenuIDs) {
                    if (id == candidate) return true;
                }
                return false;
            };

            for (auto* child : CCArrayExt<CCNode*>(this->getChildren())) {
                if (!child) continue;
                if (isArrowMenuID(child->getID())) {
                    child->setVisible(false);
                }
            }
            // busqueda recursiva por todos los hijos por si el menu esta anidado.
            // Igualmente: solo escondemos lo que tenga un ID reconocible.
            std::function<void(CCNode*)> hideArrowMenus = [&](CCNode* node) {
                if (!node) return;
                for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) {
                    if (!child) continue;
                    if (isArrowMenuID(child->getID())) {
                        child->setVisible(false);
                        continue;
                    }
                    hideArrowMenus(child);
                }
            };
            hideArrowMenus(this);

            // slider horizontal que refleja la posicion de la pagina
            float sliderW = win.width * 0.6f;
            float sliderH = 6.f;
            float sliderLeftX = 115.f;
            float sliderY = 32.f;

            // fondo del slider (anchor left-center)
            auto sliderBg = paimon::SpriteHelper::createRoundedRect(
                sliderW, sliderH, sliderH * 0.5f,
                {0.1f, 0.1f, 0.1f, 0.6f}
            );
            if (sliderBg) {
                sliderBg->setAnchorPoint({0.f, 0.5f});
                sliderBg->setPosition({sliderLeftX, sliderY});
                sliderBg->setZOrder(50);
                this->addChild(sliderBg);
            }

            // thumb del slider (barra activa)
            const int totalPages = 24; // 22 niveles + 2 secciones vacias
            float thumbW = std::max(sliderW / totalPages, 10.f);
            auto thumb = paimon::SpriteHelper::createRoundedRect(
                thumbW, sliderH, sliderH * 0.5f,
                {1.f, 1.f, 1.f, 0.85f}
            );
            if (thumb) {
                thumb->setID("paimon-slider-thumb"_spr);
                thumb->setAnchorPoint({0.5f, 0.5f});
                thumb->setZOrder(51);
                // posicion inicial: centrado en la izquierda del slider
                float thumbStartX = sliderLeftX + thumbW / 2;
                thumb->setPosition({thumbStartX, sliderY});
                this->addChild(thumb);
                m_fields->m_pageSliderThumb = thumb;
                m_fields->m_sliderBgWidth = sliderW;
                m_fields->m_sliderStartX = sliderLeftX + thumbW / 2;
                m_fields->m_sliderThumbWidth = thumbW;
            }
        }

        // gradiente inferior para legibilidad de texto/UI
        {
            float gradH = win.height * 0.2f;
            auto grad = CCLayerGradient::create(
                {0, 0, 0, 0},
                {0, 0, 0, 140}
            );
            if (grad) {
                grad->setContentSize({win.width, gradH});
                grad->setPosition({0, 0});
                grad->setZOrder(-8);
                grad->setOpacity(0);
                this->addChild(grad);
                grad->runAction(CCFadeTo::create(0.8f, 255));
                m_fields->m_bottomGradient = grad;
            }
        }

        // estilo del levels-list: bordes oscuros + 1px mas alto arriba y abajo.
        //
        // Compat: solo tocamos el primer CCScale9Sprite vanilla que matchee
        // la geometria esperada y que NO tenga un ID custom asignado por otro
        // mod. Si otro mod ya asigno un ID al fondo (sea por su cuenta o via
        // node-ids), respetamos su decision y no lo modificamos. Esto evita
        // teñir nodos de mods como BetterInfo o themes alternativos.
        {
            for (auto* child : CCArrayExt<CCNode*>(this->getChildren())) {
                if (!child) continue;
                auto* s9 = typeinfo_cast<cocos2d::extension::CCScale9Sprite*>(child);
                if (!s9) continue;
                // Si tiene un ID, asumimos que es un nodo "marcado" (por
                // node-ids o por otro mod) — no lo retocamos.
                if (!std::string(s9->getID()).empty()) continue;

                auto size = s9->getContentSize();
                auto pos  = s9->getPosition();
                if (size.width <= win.width * 0.5f) continue;
                if (pos.y <= win.height * 0.2f || pos.y >= win.height * 0.8f) continue;

                // 1px mas alto arriba y abajo
                s9->setContentSize({size.width, size.height + 2.f});
                s9->setPosition({pos.x, pos.y - 1.f});
                // bordes oscuros + marcamos para no reentrar en re-init
                s9->setColor({30, 30, 30});
                s9->setOpacity(200);
                s9->setID("paimon-levels-list-bg"_spr);
                break; // solo el primer match
            }
        }
        
        this->scheduleOnce(schedule_selector(PaimonLevelSelectLayer::forcePlayMusic), 0.0f);
        this->schedule(schedule_selector(PaimonLevelSelectLayer::checkPageLoop));

        // Perf: cache dynamic-song setting once at init
        m_fields->m_cachedDynamicSong = Mod::get()->getSettingValue<bool>("dynamic-song");
        
        return true;
    }

    $override
    void onEnter() {
        LevelSelectLayer::onEnter();
        int levelID = this->resolveVisibleLevelID();
        m_fields->m_currentLevelID = levelID;
        if (levelID > 0 && levelID <= 22) {
            this->syncLevelSelectSong(true);
        }
    }

    $override
    void onExit() {
        this->unschedule(schedule_selector(PaimonLevelSelectLayer::checkPageLoop));
        LevelSelectLayer::onExit();
    }

    int resolveVisibleLevelID() {
        if (!m_scrollLayer) return m_fields->m_currentLevelID;

        CCLayer* pagesLayer = m_scrollLayer->m_extendedLayer;
        if (!pagesLayer) return m_fields->m_currentLevelID;

        float width = m_scrollLayer->getContentSize().width;
        if (width <= 0.f) return m_fields->m_currentLevelID;

        int page = static_cast<int>(std::round(-pagesLayer->getPositionX() / width));

        const int totalLevels = 22;
        const int emptySections = 2;
        const int cycleSize = totalLevels + emptySections;

        int cycleIndex = (page % cycleSize + cycleSize) % cycleSize;
        if (cycleIndex < totalLevels) {
            return cycleIndex + 1;
        }

        return -1;
    }

    void syncLevelSelectSong(bool force = false) {
        if (m_fields->m_audioCleanedUp) return;

        auto& coordinator = AudioContextCoordinator::get();
        if (coordinator.isGameplayActive() || coordinator.isProfileOpen()) return;

        int levelID = m_fields->m_currentLevelID;
        if (levelID <= 0 || levelID > 22) return;

        auto* dsm = DynamicSongManager::get();
        bool needsSync = force ||
            coordinator.getDynamicContextLayer() != DynSongLayer::LevelSelect ||
            coordinator.getCurrentLevelSelectID() != levelID ||
            !dsm->isActive() ||
            dsm->getCurrentPlayingLevelID() != levelID ||
            !dsm->verifyPlayback();
        if (!needsSync) return;

        coordinator.activateLevelSelect(levelID, true);
    }

    void forcePlayMusic(float dt) {
         // playSong ya maneja transiciones, no necesito stopSong antes

         int levelID = this->resolveVisibleLevelID();
         if (levelID <= 0) levelID = 1;
         m_fields->m_currentLevelID = levelID;
         
         this->syncLevelSelectSong(true);
    }

    void checkPageLoop(float dt) {
        if (!m_scrollLayer) return;

        int levelID = this->resolveVisibleLevelID();
        
        // cambio de pagina: actualizar cancion y fondo
        if (m_fields->m_currentLevelID != levelID) {
            m_fields->m_currentLevelID = levelID;
            m_fields->m_pageCheckTimer = 0.f;

            // nuevo fondo
            this->updateThumbnailBackground(levelID);

            // nueva cancion dinamica
            if (levelID > 0 && levelID <= 22) {
                this->syncLevelSelectSong(true);
            }
        }

        // actualizar posicion del slider thumb (con soporte de loop)
        if (m_fields->m_pageSliderThumb && m_scrollLayer) {
            const int totalPages = 24;
            float pageWidth = m_scrollLayer->getContentSize().width;
            if (pageWidth > 0.f) {
                float scrollX = -m_scrollLayer->m_extendedLayer->getPositionX();
                int rawPage = static_cast<int>(std::round(scrollX / pageWidth));
                // loop: el scroll es ciclico, el slider debe volver al inicio
                int loopedPage = ((rawPage % totalPages) + totalPages) % totalPages;
                float progress = static_cast<float>(loopedPage) / static_cast<float>(totalPages - 1);
                // thumb centrado: va desde sliderStartX (izquierda + mitad thumb) hasta
                // sliderStartX + sliderBgWidth - thumbWidth (derecha - mitad thumb)
                float maxTravel = m_fields->m_sliderBgWidth - m_fields->m_sliderThumbWidth;
                float thumbX = m_fields->m_sliderStartX + progress * maxTravel;
                m_fields->m_pageSliderThumb->setPosition({thumbX, m_fields->m_pageSliderThumb->getPosition().y});
            }
        }

        m_fields->m_pageCheckTimer += dt;
        if (m_fields->m_pageCheckTimer >= 0.05f) {
            m_fields->m_pageCheckTimer = 0.f;
            this->syncLevelSelectSong();
        }

        // logica del efecto â€œpulsoâ€ con la musica
        if (m_fields->m_bgSprite && m_fields->m_cachedDynamicSong) {
             // Perf: throttle FMOD metering to every 3 frames
             if (++m_fields->m_meteringFrameCounter < 3) return;
             m_fields->m_meteringFrameCounter = 0;
             // master channel group pa leer picos
              auto engine = FMODAudioEngine::sharedEngine();
              if (engine && engine->m_system) {
                 FMOD::ChannelGroup* masterGroup = nullptr;
                 engine->m_system->getMasterChannelGroup(&masterGroup);
                 
                 if (masterGroup) {
                     FMOD::DSP* headDSP = nullptr;
                     masterGroup->getDSP(FMOD_CHANNELCONTROL_DSP_HEAD, &headDSP);
                     
                     if (headDSP) {
                         if (!m_fields->m_meteringEnabled) {
                             headDSP->setMeteringEnabled(false, true);
                             m_fields->m_meteringEnabled = true;
                         }

                         FMOD_DSP_METERING_INFO meteringInfo = {};
                         headDSP->getMeteringInfo(nullptr, &meteringInfo);
                         
                         float peak = 0.f;
                         if (meteringInfo.numchannels > 0) {
                             for (int i=0; i<meteringInfo.numchannels; i++) {
                                 if (meteringInfo.peaklevel[i] > peak) peak = meteringInfo.peaklevel[i];
                             }
                         }
                         
                         // smoothing: ataque rapido, decay lento
                         if (peak > m_fields->m_smoothedPeak) {
                             m_fields->m_smoothedPeak = peak;
                         } else {
                             m_fields->m_smoothedPeak -= dt * 1.5f; // velocidad de â€œdecayâ€
                             if (m_fields->m_smoothedPeak < 0.f) m_fields->m_smoothedPeak = 0.f;
                         }
                         
                         // bajar sensibilidad un 30%
                         float val = m_fields->m_smoothedPeak * 0.7f;

                         // brillo: 80 base â†’ 255 en pico
                         float brightnessVal = 80.f + (val * 175.f);
                         if (brightnessVal > 255.f) brightnessVal = 255.f;
                         GLubyte cVal = static_cast<GLubyte>(brightnessVal);

                         if (m_fields->m_bgSprite) {
                             m_fields->m_bgSprite->setColor({cVal, cVal, cVal});
                         }
                         
                         if (m_fields->m_sharpBgSprite) {
                             GLubyte sharpVal = static_cast<GLubyte>(cVal * 0.9f); 
                             m_fields->m_sharpBgSprite->setColor({sharpVal, sharpVal, sharpVal});
                         }
                     }
                 }
             }
        }
    }
    

    void updateThumbnailBackground(int levelID) {
        // solo niveles 1-22 tienen thumbnail
        bool isMainLevel = (levelID >= 1 && levelID <= 22);

        if (!isMainLevel) {
             // secciones vacias = negro
             this->applyBackground(nullptr, levelID); 
             return;
        }

        // Fast path: si el thumbnail ya esta en RAM (precargado al iniciar
        // el mod), aplicarlo INMEDIATAMENTE en el mismo frame. Sin esto,
        // el callback de requestLoad se encola y tarda 1-2 frames en
        // resolver, causando un flash negro al abrir LevelSelectLayer.
        if (auto* cached = ThumbnailLoader::get().tryGetCachedTexture(levelID, false)) {
            this->applyBackground(cached, levelID);
            return;
        }

        std::string fileName = fmt::format("{}.png", levelID);

        Ref<LevelSelectLayer> self = this;

        ThumbnailLoader::get().requestLoad(levelID, fileName, [self, levelID](CCTexture2D* tex, bool success) {
            // si cambio de pagina mientras cargaba, ignorar
            auto* layer = static_cast<PaimonLevelSelectLayer*>(self.data());
            if (!layer || !layer->getParent()) return;
            if (layer->m_fields->m_currentLevelID == levelID) {
                if (success && tex) {
                    layer->applyBackground(tex, levelID);
                } else {
                    layer->applyBackground(nullptr, levelID);
                }
            }
        }, ThumbnailLoader::PriorityHero, false);
    }
    
    
    void applyBackground(CCTexture2D* tex, int levelID = -1) {
        auto win = CCDirector::sharedDirector()->getWinSize();
        CCSprite* finalSprite = nullptr;
        CCSprite* sharpSprite = nullptr;

        // guardar refs a fondos viejos para crossfade
        Ref<CCSprite> oldBg = m_fields->m_bgSprite;
        Ref<CCSprite> oldSharp = m_fields->m_sharpBgSprite;

        if (tex) {
            sharpSprite = CCSprite::createWithTexture(tex);
            finalSprite = BlurSystem::getInstance()->createPaimonBlurSprite(tex, win, 2.0f);

            if (finalSprite) {
                float scaleX = win.width / finalSprite->getContentSize().width;
                float scaleY = win.height / finalSprite->getContentSize().height;
                float scale = std::max(scaleX, scaleY);

                auto setupBgSprite = [&](CCSprite* spr, int z, GLubyte targetAlpha, ccColor3B tint) {
                    spr->setScale(scale);
                    spr->setPosition(win / 2);
                    spr->setColor(tint);
                    spr->setZOrder(z);
                    spr->setOpacity(0);
                    this->addChild(spr);
                    spr->runAction(CCFadeTo::create(0.4f, targetAlpha));
                    spr->runAction(CCRepeatForever::create(CCSequence::create(
                        CCScaleTo::create(10.0f, scale * 1.3f),
                        CCScaleTo::create(10.0f, scale),
                        nullptr
                    )));
                };

                setupBgSprite(sharpSprite, -11, 255, {80, 80, 80});
                setupBgSprite(finalSprite, -10, 255, {255, 255, 255});
            }
        }

        m_fields->m_bgSprite = finalSprite;
        m_fields->m_sharpBgSprite = sharpSprite;

        // crossfade: desvanecer fondos viejos despues de que los nuevos
        // ya estan en pantalla (sin gap visible)
        if (oldBg) {
            oldBg->stopAllActions();
            oldBg->runAction(CCSequence::create(
                CCFadeOut::create(0.4f),
                CCRemoveSelf::create(),
                nullptr
            ));
        }
        if (oldSharp) {
            oldSharp->stopAllActions();
            oldSharp->runAction(CCSequence::create(
                CCFadeOut::create(0.4f),
                CCRemoveSelf::create(),
                nullptr
            ));
        }
    }
    

    void cleanupDynamicSong() {
        if (m_fields->m_audioCleanedUp) return;
        m_fields->m_audioCleanedUp = true;
        AudioContextCoordinator::get().deactivateLevelSelect(true);
    }

    $override
    void onBack(CCObject* sender) {
        cleanupDynamicSong();
        LevelSelectLayer::onBack(sender);
    }
    
    $override
    void keyBackClicked() {
        cleanupDynamicSong();
        LevelSelectLayer::keyBackClicked();
    }
};
