#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/LeaderboardsLayer.hpp>
#include "../utils/PaimonButtonHighlighter.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include "../utils/PaimonNotification.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/LevelSelectLayer.hpp>
#include <vector>
#include <cmath>
#include <filesystem>
#include <sstream>

#include "../core/Settings.hpp"
#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/thumbnails/services/ThumbnailCache.hpp"
#include "../features/thumbnails/services/ThumbnailTransportClient.hpp"
#include "../features/audio/services/AudioContextCoordinator.hpp"
#include "../features/dynamic-songs/services/DynamicSongManager.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../utils/VideoThumbnailSprite.hpp"
#include "../features/profiles/ui/RatePopup.hpp"

#include "../utils/Localization.hpp"
#include "../utils/ImageConverter.hpp"
#include "../utils/HttpClient.hpp"
#include "../features/foryou/services/ForYouTracker.hpp"

#include "../layers/ButtonEditOverlay.hpp"
#include "../managers/ButtonLayoutManager.hpp"
#include "../features/moderation/ui/SetDailyWeeklyPopup.hpp"
#include "../framework/state/SessionState.hpp"

#include "../utils/Shaders.hpp"
#include "../blur/BlurSystem.hpp"
#include "../utils/MainThreadDelay.hpp"
#include "../features/audio/services/PaimonAudio.hpp"
#include "../framework/EventBus.hpp"
#include "../framework/ModEvents.hpp"

using namespace geode::prelude;
using namespace Shaders;

#include "../features/thumbnails/ui/LocalThumbnailViewPopup.hpp"
#include "../features/thumbnails/ui/ThumbnailSettingsPopup.hpp"

// Z-order constants for LevelInfoLayer background layering
static constexpr int kBackgroundZOrder = -4;
static constexpr int kExtraDarknessZOrder = -3; // oscuridad extra separada del bg
static constexpr int kEffectsZOrder   = -2;
static constexpr int kOverlayZOrder   = -1;

class $modify(PaimonLevelInfoLayer, LevelInfoLayer) {
    CCMenu* findLeftSideMenu() {
        if (auto byId = typeinfo_cast<CCMenu*>(this->getChildByID("left-side-menu"))) {
            return byId;
        }
        if (auto children = this->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                auto* menu = typeinfo_cast<CCMenu*>(child);
                if (!menu) continue;
                if (menu->getPositionX() < this->getContentSize().width * 0.5f) {
                    return menu;
                }
            }
        }
        return nullptr;
    }

    static void onModify(auto& self) {
        // Dependemos de node-ids para ubicar nodos y fondos con IDs estables.
        (void)self.setHookPriorityAfterPost("LevelInfoLayer::init", "geode.node-ids");
    }

    struct Fields {
        Ref<CCMenuItemSpriteExtra> m_thumbnailButton = nullptr;
        Ref<CCNode> m_pixelBg = nullptr;
        Ref<CCLayerColor> m_pixelOverlay = nullptr;
        std::vector<Ref<CCSprite>> m_extraBgSprites;
        float m_shaderTime = 0.0f;
        bool m_animatedShader = false;
        bool m_fromThumbsList = false;
        bool m_fromReportSection = false;
        bool m_fromVerificationQueue = false;
        bool m_fromLeaderboards = false;
        LeaderboardType m_leaderboardType = LeaderboardType::Default;
        LeaderboardStat m_leaderboardStat = LeaderboardStat::Stars;
        Ref<CCMenuItemSpriteExtra> m_acceptThumbBtn = nullptr;
        Ref<CCMenuItemSpriteExtra> m_editModeBtn = nullptr;
        Ref<CCMenuItemSpriteExtra> m_uploadLocalBtn = nullptr;
        Ref<CCMenu> m_extraMenu = nullptr;
        bool m_thumbnailRequested = false; // evita cargas duplicadas
        int m_loadedInvalidationVersion = 0; // version invalidacion pa detectar cambios
        
        // multi-thumb
        std::vector<ThumbnailAPI::ThumbnailInfo> m_thumbnails;
        int m_currentThumbnailIndex = 0;
        Ref<CCMenuItemSpriteExtra> m_prevBtn = nullptr;
        Ref<CCMenuItemSpriteExtra> m_nextBtn = nullptr;
        Ref<CCMenuItemSpriteExtra> m_rateBtn = nullptr;
        bool m_cycling = true;
        float m_cycleTimer = 0.0f;
        int m_galleryToken = 0;
        int m_bgRequestToken = 0;
        int m_lazyLoadIndex = 1; // siguiente thumbnail a cargar en background
        bool m_lazyLoadScheduled = false;
        int m_invalidationListenerId = 0;
        bool m_audioDeactivated = false;
        Ref<VideoThumbnailSprite> m_videoSprite = nullptr;
        // Favorite buttons
        Ref<CCMenuItemSpriteExtra> m_favCreatorBtn = nullptr;
        Ref<CCMenuItemSpriteExtra> m_favLevelBtn = nullptr;
        int m_lastDarkness = -1; // cached darkness setting to skip redundant reads
        Ref<CCLayerColor> m_extraDarknessLayer = nullptr; // +0.1 oscuridad adicional separada
        enum class BgNavDir : uint8_t { None, Left, Right };
        BgNavDir m_bgNavDirection = BgNavDir::Right; // direction for bg transition
        // PaimonAudio reactive darkness
        bool m_paimonAudioActive = false;
        int m_paimonAudioBaseDarkness = 0;
        // Estado de carga init→onEnter: evita carreras y doble-carga
        enum class InitLoadState : uint8_t { Idle, Pending, Applying };
        InitLoadState m_initLoadState = InitLoadState::Idle;
        // Cached settings — leidos una vez en init(), refrescados en onSettingsChanged
        std::string m_cachedBgStyle;
        int m_cachedEffectIntensity = 5;
        std::string m_cachedExtraStyles;
        bool m_cachedAutoCycle = false;
        int m_loadedSettingsVersion = 0; // invalidacion por listenForSettingChanges
        // m_activeBlurJob removed — replaced by BlurSystem (synchronous FBO-based Dual Kawase)
    };

    int readDarknessSetting() const {
        return std::clamp(
            static_cast<int>(Mod::get()->getSettingValue<int64_t>("levelinfo-bg-darkness")),
            0,
            50
        );
    }

    int getAppliedDarknessSetting() {
        if (m_fields->m_lastDarkness >= 0) {
            return m_fields->m_lastDarkness;
        }
        return readDarknessSetting();
    }

    GLubyte overlayAlphaForDarkness(int darknessVal) const {
        return static_cast<GLubyte>((std::clamp(darknessVal, 0, 50) / 50.0f) * 255.0f);
    }

    GLubyte extraDarknessAlphaForDarkness(int darknessVal) const {
        return static_cast<GLubyte>(std::round((std::clamp(darknessVal, 0, 50) / 50.0f) * 26.0f));
    }

    void applyDarknessSetting(int darknessVal, bool force = false) {
        darknessVal = std::clamp(darknessVal, 0, 50);
        if (!force && darknessVal == m_fields->m_lastDarkness) return;

        m_fields->m_lastDarkness = darknessVal;
        m_fields->m_paimonAudioBaseDarkness = darknessVal;

        auto* overlay = m_fields->m_pixelOverlay.data();
        if (overlay && overlay->getParent()) {
            overlay->setOpacity(overlayAlphaForDarkness(darknessVal));
        }

        auto* extraDarkness = m_fields->m_extraDarknessLayer.data();
        if (extraDarkness && extraDarkness->getParent()) {
            extraDarkness->setOpacity(extraDarknessAlphaForDarkness(darknessVal));
        }
    }
    
    void applyThumbnailBackground(CCTexture2D* tex, int32_t levelID) {
        if (!tex) return;

        // Guardar textura actual para que InfoLayer pueda consultarla al abrirse
        paimon::ThumbnailBackgroundChangedEvent::s_lastLevelID = levelID;
        paimon::ThumbnailBackgroundChangedEvent::s_lastTexture = tex;

        // Notificar a CustomSongWidget e InfoLayer para sincronizar fondos
        auto subCount = paimon::EventBus::get().subscriberCount<paimon::ThumbnailBackgroundChangedEvent>();
        log::info("[LevelInfoLayer] publishing ThumbnailBackgroundChangedEvent levelID={} tex={} subscribers={}", levelID, (void*)tex, subCount);
        paimon::EventBus::get().publish(paimon::ThumbnailBackgroundChangedEvent{levelID, tex});

        // BlurSystem is synchronous — no progressive job to cancel

        // La carga inicial (init→requestLoad/refreshGalleryData) ya resolvio
        m_fields->m_initLoadState = Fields::InitLoadState::Applying;

        log::info("[LevelInfoLayer] Aplicando fondo del thumbnail");
        
        // reset animacion de shader previo
        m_fields->m_animatedShader = false;
        m_fields->m_shaderTime = 0.0f;
        
        // Limpia sprites de efectos extra anteriores
        for (auto& s : m_fields->m_extraBgSprites) {
            if (s) s->removeFromParent();
        }
        m_fields->m_extraBgSprites.clear();
        
        // Estilo e intensidad (cached)
        auto bgStyle = m_fields->m_cachedBgStyle;
        int intensity = m_fields->m_cachedEffectIntensity;
        auto win = CCDirector::sharedDirector()->getWinSize();

        // Tabla de mapeo estilo -> shader/flags
        struct ShaderEntry {
            char const* name; char const* key; char const* frag;
            bool boosted; bool screenSize; bool time;
        };
        static ShaderEntry const kShaderTable[] = {
            {"grayscale",       "grayscale"_spr,       fragmentShaderGrayscale,       false, false, false},
            {"sepia",           "sepia"_spr,           fragmentShaderSepia,           false, false, false},
            {"vignette",        "vignette"_spr,        fragmentShaderVignette,        false, false, false},
            {"scanlines",       "scanlines"_spr,       fragmentShaderScanlines,       false, true,  false},
            {"bloom",           "bloom"_spr,           fragmentShaderBloom,            true, true,  false},
            {"chromatic",       "chromatic-v2"_spr,    fragmentShaderChromatic,        true, false, true},
            {"radial-blur",     "radial-blur-v2"_spr,  fragmentShaderRadialBlur,       true, false, true},
            {"glitch",          "glitch-v2"_spr,       fragmentShaderGlitch,           true, false, true},
            {"posterize",       "posterize"_spr,       fragmentShaderPosterize,       false, false, false},
            {"rain",            "rain"_spr,            fragmentShaderRain,             true, false, true},
            {"matrix",          "matrix"_spr,          fragmentShaderMatrix,           true, false, true},
            {"neon-pulse",      "neon-pulse"_spr,      fragmentShaderNeonPulse,        true, false, true},
            {"wave-distortion", "wave-distortion"_spr, fragmentShaderWaveDistortion,   true, false, true},
            {"crt",             "crt"_spr,             fragmentShaderCRT,              true, false, true},
        };

        // Busca shader por nombre
        auto lookupShader = [&](std::string const& style) -> std::tuple<CCGLProgram*, float, bool, bool> {
            for (auto& e : kShaderTable) {
                if (style == e.name) {
                    float v = e.boosted ? (intensity / 10.0f) * 2.25f : intensity / 10.0f;
                    return {getOrCreateShader(e.key, vertexShaderCell, e.frag), v, e.screenSize, e.time};
                }
            }
            return {nullptr, 0.f, false, false};
        };

        // Lambda de efectos
        auto applyEffects = [this, bgStyle, intensity, win, tex, lookupShader](CCSprite*& sprite, bool isGIF) {
            if (!sprite) return;

            // Escala y posicion inicial
            float scaleX = win.width / sprite->getContentSize().width;
            float scaleY = win.height / sprite->getContentSize().height;
            float scale = std::max(scaleX, scaleY);
            sprite->setScale(scale);
            sprite->setPosition({win.width / 2.0f, win.height / 2.0f});
            sprite->setAnchorPoint({0.5f, 0.5f});

            if (bgStyle == "normal") {
                ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
                sprite->getTexture()->setTexParameters(&params);
            }
            else if (bgStyle == "pixel") {
                if (isGIF) {
                     auto shader = getOrCreateShader("pixelate"_spr, vertexShaderCell, fragmentShaderPixelate);
                     if (shader) {
                         sprite->setShaderProgram(shader);
                         shader->use();
                         shader->setUniformsForBuiltins();
                         float intensityVal = (intensity - 1) / 9.0f;
                         if (auto ags = typeinfo_cast<AnimatedGIFSprite*>(sprite)) {
                             ags->m_intensity = intensityVal;
                             ags->m_screenSize = win;
                             if (auto* animTex = ags->getTexture()) {
                                 ags->m_texSize = animTex->getContentSizeInPixels();
                             }
                         } else {
                             shader->setUniformLocationWith1f(shader->getUniformLocationForName("u_intensity"), intensityVal);
                             shader->setUniformLocationWith2f(shader->getUniformLocationForName("u_screenSize"), win.width, win.height);
                         }
                     }
                } else {
                    float t = (intensity - 1) / 9.0f;
                    float pixelFactor = 0.5f - (t * 0.47f);
                    int renderWidth = std::max(32, static_cast<int>(win.width * pixelFactor));
                    int renderHeight = std::max(32, static_cast<int>(win.height * pixelFactor));
                    auto renderTex = CCRenderTexture::create(renderWidth, renderHeight);
                    if (renderTex) {
                        float renderScale = std::min(
                            static_cast<float>(renderWidth) / tex->getContentSize().width,
                            static_cast<float>(renderHeight) / tex->getContentSize().height);
                        sprite->setScale(renderScale);
                        sprite->setPosition({renderWidth / 2.0f, renderHeight / 2.0f});
                        renderTex->begin();
                        glClearColor(0, 0, 0, 0);
                        glClear(GL_COLOR_BUFFER_BIT);
                        sprite->visit();
                        renderTex->end();
                        auto pixelTexture = renderTex->getSprite()->getTexture();
                        sprite = CCSprite::createWithTexture(pixelTexture);
                        if (sprite) {
                            float finalScale = std::max(win.width / renderWidth, win.height / renderHeight);
                            sprite->setScale(finalScale);
                            sprite->setFlipY(true);
                            sprite->setAnchorPoint({0.5f, 0.5f});
                            sprite->setPosition({win.width / 2.0f, win.height / 2.0f});
                            ccTexParams params{GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
                            pixelTexture->setTexParameters(&params);
                        }
                    }
                }
            }
            else if (bgStyle == "blur") {
                if (isGIF) {
                     auto shader = Shaders::getBlurSinglePassShader();
                     if (shader) {
                         sprite->setShaderProgram(shader);
                         shader->use();
                         shader->setUniformsForBuiltins();
                         float intensityVal = (intensity - 1) / 9.0f;
                         if (auto ags = typeinfo_cast<AnimatedGIFSprite*>(sprite)) {
                             ags->m_intensity = intensityVal;
                             ags->m_screenSize = win;
                             if (auto* animTex = ags->getTexture()) {
                                 ags->m_texSize = animTex->getContentSizeInPixels();
                             }
                         } else {
                             shader->setUniformLocationWith1f(shader->getUniformLocationForName("u_intensity"), intensityVal);
                             // Try u_texSize first (.glsl BlurSystem shader), then u_screenSize (inline fallback)
                             GLint szLoc = shader->getUniformLocationForName("u_texSize");
                             if (szLoc == -1) szLoc = shader->getUniformLocationForName("u_screenSize");
                             if (szLoc != -1) shader->setUniformLocationWith2f(szLoc, win.width, win.height);
                         }
                     }
                } else {
                    sprite = BlurSystem::getInstance()->createBlurredSprite(tex, win, static_cast<float>(intensity));
                    if (sprite) {
                        float cs = std::max(win.width / sprite->getContentSize().width, win.height / sprite->getContentSize().height);
                        sprite->setScale(cs);
                        sprite->setAnchorPoint({0.5f, 0.5f});
                        sprite->setPosition({win.width / 2.0f, win.height / 2.0f});
                    }
                }
            }
            else if (bgStyle == "paimonblur") {
                if (isGIF) {
                    auto shader = Shaders::getPaimonBlurShader();
                    if (shader) {
                        sprite->setShaderProgram(shader);
                        shader->use();
                        shader->setUniformsForBuiltins();
                        float intensityVal = (intensity - 1) / 9.0f;
                        if (auto ags = typeinfo_cast<AnimatedGIFSprite*>(sprite)) {
                            ags->m_intensity = intensityVal;
                            ags->m_screenSize = win;
                            if (auto* animTex = ags->getTexture()) {
                                ags->m_texSize = animTex->getContentSizeInPixels();
                            }
                        } else {
                            shader->setUniformLocationWith1f(shader->getUniformLocationForName("u_intensity"), intensityVal);
                            // Try u_texSize first (.glsl BlurSystem shader), then u_screenSize (inline fallback)
                            GLint szLoc = shader->getUniformLocationForName("u_texSize");
                            if (szLoc == -1) szLoc = shader->getUniformLocationForName("u_screenSize");
                            if (szLoc != -1) shader->setUniformLocationWith2f(szLoc, win.width, win.height);
                        }
                    }
                } else {
                    sprite = BlurSystem::getInstance()->createPaimonBlurSprite(tex, win, static_cast<float>(intensity));
                    if (sprite) {
                        float cs = std::max(win.width / sprite->getContentSize().width, win.height / sprite->getContentSize().height);
                        sprite->setScale(cs);
                        sprite->setAnchorPoint({0.5f, 0.5f});
                        sprite->setPosition({win.width / 2.0f, win.height / 2.0f});
                    }
                }
            }
            else {
                auto [shader, val, useScreenSize, needsTime] = lookupShader(bgStyle);

                if (shader) {
                    sprite->setShaderProgram(shader);
                    shader->use();
                    shader->setUniformsForBuiltins();
                    if (auto ags = typeinfo_cast<AnimatedGIFSprite*>(sprite)) {
                        ags->m_intensity = val;
                        ags->m_screenSize = win;
                    } else {
                        shader->setUniformLocationWith1f(shader->getUniformLocationForName("u_intensity"), val);
                    }
                    if (useScreenSize) {
                        if (auto ags = typeinfo_cast<AnimatedGIFSprite*>(sprite)) {
                            ags->m_screenSize = win;
                        } else {
                            shader->setUniformLocationWith2f(shader->getUniformLocationForName("u_screenSize"), win.width, win.height);
                        }
                    }
                    if (needsTime) {
                        if (auto ags = typeinfo_cast<AnimatedGIFSprite*>(sprite)) {
                            ags->m_time = 0.0f;
                        } else {
                            shader->setUniformLocationWith1f(shader->getUniformLocationForName("u_time"), 0.0f);
                        }
                        m_fields->m_animatedShader = true;
                        m_fields->m_shaderTime = 0.0f;
                    }
                }
            }
        };

        auto refreshShaderSchedule = [this, bgStyle]() {
            this->unschedule(schedule_selector(PaimonLevelInfoLayer::updateShaderTime));
            this->unschedule(schedule_selector(PaimonLevelInfoLayer::updatePaimonAudio));

            if (m_fields->m_animatedShader && m_fields->m_pixelBg) {
                this->schedule(schedule_selector(PaimonLevelInfoLayer::updateShaderTime));
            }

            // PaimonAudio: activate audio-reactive darkness when paimonblur is active
            if (bgStyle == "paimonblur" && m_fields->m_pixelBg) {
                PaimonAudio::get().activate();
                m_fields->m_paimonAudioActive = true;
                m_fields->m_paimonAudioBaseDarkness = this->getAppliedDarknessSetting();
                this->schedule(schedule_selector(PaimonLevelInfoLayer::updatePaimonAudio));
            } else if (m_fields->m_paimonAudioActive) {
                PaimonAudio::get().deactivate();
                m_fields->m_paimonAudioActive = false;
            }
        };

        auto installBackgroundSprite = [this, applyEffects, refreshShaderSchedule, win, bgStyle](CCSprite* sprite, bool isGIF, bool skipEffects = false) {
            if (!sprite) return;

            // Ocultar fondo nativo de GD ahora que tenemos thumbnail real
            if (auto vanillaBg = this->getChildByID("background")) {
                vanillaBg->setVisible(false);
            }
            // Ocultar objetos decorativos marrones (side-art corners) instantaneamente
            if (auto blArt = this->getChildByID("bottom-left-art")) blArt->setVisible(false);
            if (auto brArt = this->getChildByID("bottom-right-art")) brArt->setVisible(false);

            m_fields->m_animatedShader = false;
            m_fields->m_shaderTime = 0.0f;

            CCSprite* preparedSprite = sprite;
            if (!skipEffects) {
                applyEffects(preparedSprite, isGIF);
            }
            if (!preparedSprite) return;

            Ref<CCNode> oldBg = m_fields->m_pixelBg;
            if (!oldBg) {
                oldBg = this->getChildByID("paimon-levelinfo-pixel-bg"_spr);
            }

            // Parar acciones del sprite anterior para evitar conflictos de fade
            if (oldBg) oldBg->stopAllActions();

            // Parar acciones del overlay para evitar que una animacion previa
            // deje una opacidad inconsistente durante la transicion
            if (m_fields->m_pixelOverlay) m_fields->m_pixelOverlay->stopAllActions();

            preparedSprite->setID("paimon-levelinfo-pixel-bg"_spr);
            this->addChild(preparedSprite, kBackgroundZOrder);
            m_fields->m_pixelBg = preparedSprite;

            // Crear/actualizar capa de oscuridad extra (+0.1) como nodo separado del bg
            {
                if (m_fields->m_extraDarknessLayer && m_fields->m_extraDarknessLayer->getParent()) {
                    m_fields->m_extraDarknessLayer->removeFromParent();
                }
                auto extraAlpha = this->extraDarknessAlphaForDarkness(this->getAppliedDarknessSetting());
                auto extraDark = CCLayerColor::create({0, 0, 0, extraAlpha});
                extraDark->setContentSize(win);
                extraDark->setPosition({0, 0});
                extraDark->setID("paimon-levelinfo-extra-darkness"_spr);
                this->addChild(extraDark, kExtraDarknessZOrder);
                m_fields->m_extraDarknessLayer = extraDark;
            }

            // Asegurar que el overlay de oscuridad existe y esta por encima del fondo
            if (m_fields->m_pixelOverlay && m_fields->m_pixelOverlay->getParent()) {
                m_fields->m_pixelOverlay->setZOrder(kOverlayZOrder);
            }

            this->applyDarknessSetting(this->getAppliedDarknessSetting(), true);

            // Apply configurable background transition
            {
                std::string bgStyle = Mod::get()->getSettingValue<std::string>("levelinfo-bg-transition");
                float dur = Mod::get()->getSettingValue<float>("levelinfo-bg-transition-duration");
                auto win = CCDirector::sharedDirector()->getWinSize();
                float screenW = win.width;
                bool goLeft = (m_fields->m_bgNavDirection == Fields::BgNavDir::Left);
                bool goRight = (m_fields->m_bgNavDirection == Fields::BgNavDir::Right);
                CCPoint targetPos = preparedSprite->getPosition();
                float sx = preparedSprite->getScaleX();
                float sy = preparedSprite->getScaleY();

                if (oldBg && oldBg->getParent()) {
                    auto* oldPtr = oldBg.data();

                    if (bgStyle == "directional-elastic") {
                        float oldTargetX = goLeft ? (targetPos.x + screenW) : (targetPos.x - screenW);
                        oldPtr->runAction(CCSequence::create(
                            CCSpawn::create(
                                CCEaseBackIn::create(CCMoveTo::create(dur * 0.6f, {oldTargetX, targetPos.y})),
                                CCFadeTo::create(dur * 0.5f, 0),
                                nullptr),
                            CCCallFunc::create(oldPtr, callfunc_selector(CCNode::removeFromParent)),
                            nullptr));
                        float startX = goLeft ? (targetPos.x - screenW) : (targetPos.x + screenW);
                        preparedSprite->setPosition({startX, targetPos.y});
                        preparedSprite->setOpacity(255);
                        preparedSprite->runAction(CCEaseElasticOut::create(
                            CCMoveTo::create(dur * 1.1f, targetPos), 0.35f));

                    } else if (bgStyle == "elastic-slide") {
                        oldPtr->runAction(CCSequence::create(
                            CCSpawn::create(
                                CCEaseBackIn::create(CCMoveTo::create(dur * 0.7f, {targetPos.x - screenW, targetPos.y})),
                                CCFadeTo::create(dur * 0.6f, 0),
                                nullptr),
                            CCCallFunc::create(oldPtr, callfunc_selector(CCNode::removeFromParent)),
                            nullptr));
                        preparedSprite->setPosition({targetPos.x + screenW, targetPos.y});
                        preparedSprite->setOpacity(255);
                        preparedSprite->runAction(CCEaseElasticOut::create(
                            CCMoveTo::create(dur * 1.2f, targetPos), 0.3f));

                    } else if (bgStyle == "slide-left") {
                        oldPtr->runAction(CCSequence::create(
                            CCEaseIn::create(CCMoveTo::create(dur, {targetPos.x - screenW, targetPos.y}), 2.0f),
                            CCCallFunc::create(oldPtr, callfunc_selector(CCNode::removeFromParent)),
                            nullptr));
                        preparedSprite->setPosition({targetPos.x + screenW, targetPos.y});
                        preparedSprite->setOpacity(255);
                        preparedSprite->runAction(CCEaseOut::create(CCMoveTo::create(dur, targetPos), 2.5f));

                    } else if (bgStyle == "slide-right") {
                        oldPtr->runAction(CCSequence::create(
                            CCEaseIn::create(CCMoveTo::create(dur, {targetPos.x + screenW, targetPos.y}), 2.0f),
                            CCCallFunc::create(oldPtr, callfunc_selector(CCNode::removeFromParent)),
                            nullptr));
                        preparedSprite->setPosition({targetPos.x - screenW, targetPos.y});
                        preparedSprite->setOpacity(255);
                        preparedSprite->runAction(CCEaseOut::create(CCMoveTo::create(dur, targetPos), 2.5f));

                    } else if (bgStyle == "zoom-in") {
                        preparedSprite->setScaleX(sx * 1.3f);
                        preparedSprite->setScaleY(sy * 1.3f);
                        preparedSprite->setOpacity(0);
                        preparedSprite->runAction(CCSpawn::create(
                            CCEaseOut::create(CCScaleTo::create(dur, sx, sy), 2.5f),
                            CCFadeTo::create(dur * 0.6f, 255),
                            nullptr));
                        oldPtr->runAction(CCSequence::create(
                            CCFadeTo::create(dur * 0.5f, 0),
                            CCCallFunc::create(oldPtr, callfunc_selector(CCNode::removeFromParent)),
                            nullptr));

                    } else if (bgStyle == "zoom-out") {
                        preparedSprite->setScaleX(sx * 0.01f);
                        preparedSprite->setScaleY(sy * 0.01f);
                        preparedSprite->setOpacity(0);
                        preparedSprite->runAction(CCSpawn::create(
                            CCEaseOut::create(CCScaleTo::create(dur, sx, sy), 2.5f),
                            CCFadeTo::create(dur * 0.6f, 255),
                            nullptr));
                        oldPtr->runAction(CCSequence::create(
                            CCFadeTo::create(dur * 0.5f, 0),
                            CCCallFunc::create(oldPtr, callfunc_selector(CCNode::removeFromParent)),
                            nullptr));

                    } else if (bgStyle == "bounce") {
                        float startX = goRight ? (targetPos.x + screenW) : (targetPos.x - screenW);
                        preparedSprite->setPosition({startX, targetPos.y});
                        preparedSprite->setOpacity(255);
                        preparedSprite->runAction(CCEaseBounceOut::create(
                            CCMoveTo::create(dur * 1.1f, targetPos)));
                        float exitX = goRight ? (targetPos.x - screenW) : (targetPos.x + screenW);
                        oldPtr->runAction(CCSequence::create(
                            CCEaseIn::create(CCMoveTo::create(dur * 0.6f, {exitX, targetPos.y}), 2.0f),
                            CCCallFunc::create(oldPtr, callfunc_selector(CCNode::removeFromParent)),
                            nullptr));

                    } else if (bgStyle == "flip-horizontal") {
                        preparedSprite->setScaleX(0.01f);
                        preparedSprite->setOpacity(0);
                        preparedSprite->runAction(CCSequence::create(
                            CCEaseOut::create(CCScaleTo::create(dur * 0.5f, sx, sy), 2.5f),
                            nullptr));
                        preparedSprite->runAction(CCFadeTo::create(dur * 0.4f, 255));
                        oldPtr->runAction(CCSequence::create(
                            CCEaseIn::create(CCScaleTo::create(dur * 0.4f, 0.01f, sy), 2.0f),
                            CCFadeTo::create(dur * 0.3f, 0),
                            CCCallFunc::create(oldPtr, callfunc_selector(CCNode::removeFromParent)),
                            nullptr));

                    } else if (bgStyle == "flip-vertical") {
                        preparedSprite->setScaleY(0.01f);
                        preparedSprite->setOpacity(0);
                        preparedSprite->runAction(CCSequence::create(
                            CCEaseOut::create(CCScaleTo::create(dur * 0.5f, sx, sy), 2.5f),
                            nullptr));
                        preparedSprite->runAction(CCFadeTo::create(dur * 0.4f, 255));
                        oldPtr->runAction(CCSequence::create(
                            CCEaseIn::create(CCScaleTo::create(dur * 0.4f, sx, 0.01f), 2.0f),
                            CCFadeTo::create(dur * 0.3f, 0),
                            CCCallFunc::create(oldPtr, callfunc_selector(CCNode::removeFromParent)),
                            nullptr));

                    } else if (bgStyle == "dissolve") {
                        preparedSprite->setScaleX(sx * 0.8f);
                        preparedSprite->setScaleY(sy * 0.8f);
                        preparedSprite->setOpacity(0);
                        preparedSprite->runAction(CCSpawn::create(
                            CCEaseOut::create(CCScaleTo::create(dur, sx, sy), 2.0f),
                            CCFadeTo::create(dur * 0.7f, 255),
                            nullptr));
                        oldPtr->runAction(CCSequence::create(
                            CCSpawn::create(
                                CCEaseIn::create(CCScaleTo::create(dur * 0.7f, sx * 1.15f, sy * 1.15f), 2.0f),
                                CCFadeTo::create(dur * 0.6f, 0),
                                nullptr),
                            CCCallFunc::create(oldPtr, callfunc_selector(CCNode::removeFromParent)),
                            nullptr));

                    } else {
                        // Default: crossfade
                        float targetScale = preparedSprite->getScale();
                        preparedSprite->setOpacity(0);
                        preparedSprite->setScale(targetScale * 1.03f);
                        preparedSprite->runAction(CCSpawn::create(
                            CCFadeTo::create(dur, 255),
                            CCEaseOut::create(CCScaleTo::create(dur, targetScale), 2.0f),
                            nullptr));
                        oldPtr->runAction(CCSequence::create(
                            CCFadeTo::create(dur * 0.85f, 0),
                            CCCallFunc::create(oldPtr, callfunc_selector(CCNode::removeFromParent)),
                            nullptr));
                    }
                } else {
                    if (oldBg) {
                        oldBg->removeFromParent();
                    }
                    // First load — smooth fade in
                    preparedSprite->setOpacity(0);
                    preparedSprite->runAction(CCFadeIn::create(0.3f));
                }

                m_fields->m_bgNavDirection = Fields::BgNavDir::Right; // reset to default
            }

            m_fields->m_initLoadState = Fields::InitLoadState::Idle;
            refreshShaderSchedule();
        };

        bool hasGifBackground = ThumbnailLoader::get().hasGIFData(levelID);
        std::string gifPath = hasGifBackground
            ? geode::utils::string::pathToString(ThumbnailLoader::get().getCachePath(levelID, true))
            : std::string();

        if (hasGifBackground) {
            if (AnimatedGIFSprite::isCached(gifPath)) {
                if (auto cachedGif = AnimatedGIFSprite::createFromCache(gifPath)) {
                    cachedGif->play();
                    installBackgroundSprite(cachedGif, true);
                } else if (auto fallbackSprite = CCSprite::createWithTexture(tex)) {
                    installBackgroundSprite(fallbackSprite, false);
                }
            } else {
                Ref<LevelInfoLayer> self = this;
                int gifToken = m_fields->m_bgRequestToken;
                AnimatedGIFSprite::createAsync(gifPath, [self, applyEffects, installBackgroundSprite, tex, gifToken](AnimatedGIFSprite* anim) {
                    auto* layer = static_cast<PaimonLevelInfoLayer*>(self.data());
                    if (!layer || !layer->getParent()) {
                        return;
                    }
                    // Token mismatch = thumbnail changed while GIF was loading
                    if (layer->m_fields->m_bgRequestToken != gifToken) return;

                    if (anim) {
                        anim->play();
                        installBackgroundSprite(anim, true);
                    } else if (auto fallbackSprite = CCSprite::createWithTexture(tex)) {
                        installBackgroundSprite(fallbackSprite, false);
                    }
                });
            }
        } else if (auto finalSprite = CCSprite::createWithTexture(tex)) {
            // Para blur/paimonblur, usar blur ASYNC (ProgressiveBlurJob).
            // Antes: blur sincrono (4-16 pasadas FBO en un frame) = freeze perceptible
            // al abrir un nivel con thumbnail grande. Ahora: el blur se reparte en
            // 2-3 frames en desktop, el usuario no ve freeze y ademas si la key
            // cae en cache LRU el blur aparece en el mismo frame sin costo.
            if (!hasGifBackground && (bgStyle == "blur" || bgStyle == "paimonblur")) {
                auto win = CCDirector::sharedDirector()->getWinSize();
                Ref<CCTexture2D> texRef = tex;
                Ref<LevelInfoLayer> selfRef = this;
                Ref<CCSprite> finalSpriteRef = finalSprite; // Evita autorelease
                int blurToken = m_fields->m_bgRequestToken;
                bool usePaimon = (bgStyle == "paimonblur");

                auto onBlurReady = [selfRef, installBackgroundSprite, finalSpriteRef, win, blurToken, texRef](CCSprite* blurredSprite) {
                    auto* layer = static_cast<PaimonLevelInfoLayer*>(selfRef.data());
                    if (!layer || !layer->getParent()) return;
                    if (layer->m_fields->m_bgRequestToken != blurToken) return;

                    if (blurredSprite) {
                        float cs = std::max(win.width / blurredSprite->getContentSize().width,
                                            win.height / blurredSprite->getContentSize().height);
                        blurredSprite->setScale(cs);
                        blurredSprite->setAnchorPoint({0.5f, 0.5f});
                        blurredSprite->setPosition({win.width / 2.0f, win.height / 2.0f});
                        installBackgroundSprite(blurredSprite, false, true); // skipEffects=true, ya tiene blur
                    } else if (finalSpriteRef.data()) {
                        // Fallback sin blur
                        installBackgroundSprite(finalSpriteRef.data(), false);
                    }
                };

                if (usePaimon) {
                    BlurSystem::getInstance()->buildPaimonBlurAsync(
                        tex, win, static_cast<float>(intensity), onBlurReady);
                } else {
                    BlurSystem::getInstance()->buildGaussianBlurAsync(
                        tex, win, static_cast<float>(intensity), onBlurReady);
                }
            } else {
                installBackgroundSprite(finalSprite, false);
            }
        }

        // Multi-efecto: capas extra
        std::string extraStylesRaw = m_fields->m_cachedExtraStyles;
        if (!extraStylesRaw.empty() && tex) {
            // Parsea comma-separated, max 4 extra
            std::vector<std::string> extraStyles;
            {
                std::stringstream ss(extraStylesRaw);
                std::string token;
                while (std::getline(ss, token, ',') && extraStyles.size() < 4) {
                    // Trim whitespace
                    size_t start = token.find_first_not_of(" \t");
                    size_t end = token.find_last_not_of(" \t");
                    if (start != std::string::npos) {
                        extraStyles.push_back(token.substr(start, end - start + 1));
                    }
                }
            }

            for (auto& es : extraStyles) {
                if (es.empty() || es == "normal" || es == bgStyle) continue;

                auto [eshader, eval, eScreenSize, eNeedsTime] = lookupShader(es);
                if (!eshader) continue;

                auto extraSpr = CCSprite::createWithTexture(tex);
                if (!extraSpr) continue;

                float sx = win.width / extraSpr->getContentSize().width;
                float sy = win.height / extraSpr->getContentSize().height;
                extraSpr->setScale(std::max(sx, sy));
                extraSpr->setPosition({win.width / 2.0f, win.height / 2.0f});
                extraSpr->setAnchorPoint({0.5f, 0.5f});
                extraSpr->setOpacity(180); // Overlay semi-transparente

                extraSpr->setShaderProgram(eshader);
                eshader->use();
                eshader->setUniformsForBuiltins();
                eshader->setUniformLocationWith1f(eshader->getUniformLocationForName("u_intensity"), eval);
                if (eScreenSize) {
                    eshader->setUniformLocationWith2f(eshader->getUniformLocationForName("u_screenSize"), win.width, win.height);
                }
                if (eNeedsTime) {
                    eshader->setUniformLocationWith1f(eshader->getUniformLocationForName("u_time"), 0.0f);
                    m_fields->m_animatedShader = true;
                }

                this->addChild(extraSpr, kEffectsZOrder);
                m_fields->m_extraBgSprites.push_back(extraSpr);
            }
        }

        // Overlay de oscuridad se gestiona en init()
        
        log::info("[LevelInfoLayer] Fondo aplicado exitosamente (estilo: {}, intensidad: {})", bgStyle, intensity);
    }
    
    $override
    void onEnterTransitionDidFinish() {
        LevelInfoLayer::onEnterTransitionDidFinish();

        // Reset audio deactivation flag
        m_fields->m_audioDeactivated = false;

        // Oculta fondo vanilla si ya hay thumbnail
        if (m_fields->m_pixelBg) {
            if (auto vanillaBg = this->getChildByID("background")) vanillaBg->setVisible(false);
            if (auto blArt = this->getChildByID("bottom-left-art")) blArt->setVisible(false);
            if (auto brArt = this->getChildByID("bottom-right-art")) brArt->setVisible(false);
        }

        // Detecta cambio de miniatura desde PauseLayer
        if (m_level && m_fields->m_thumbnailRequested) {
            int32_t levelID = m_level->m_levelID.value();
            int currentVersion = ThumbnailLoader::get().getInvalidationVersion(levelID);
            if (currentVersion != m_fields->m_loadedInvalidationVersion) {
                log::info("[LevelInfoLayer] onEnterTransitionDidFinish: thumbnail invalidated levelID={} ver {} -> {}", levelID, m_fields->m_loadedInvalidationVersion, currentVersion);
                m_fields->m_loadedInvalidationVersion = currentVersion;
                refreshGalleryData(levelID, true);
            } else if (!m_fields->m_pixelBg && m_fields->m_initLoadState == Fields::InitLoadState::Idle) {
                // Solo re-fetch si no hay requests pendientes
                refreshGalleryData(levelID, true);
            }
        }

        // Recrea overlay de oscuridad si fue destruido
        if (!m_fields->m_pixelOverlay || !m_fields->m_pixelOverlay->getParent()) {
            auto win = cocos2d::CCDirector::sharedDirector()->getWinSize();
            auto overlay = cocos2d::CCLayerColor::create({0, 0, 0, 0});
            overlay->setAnchorPoint({0, 0});
            overlay->setPosition({0, 0});
            overlay->setContentSize(win);
            overlay->setID("paimon-levelinfo-pixel-overlay"_spr);
            this->addChild(overlay, kOverlayZOrder);
            m_fields->m_pixelOverlay = overlay;
        }

        // Releer al entrar para corregir desfase
        m_fields->m_lastDarkness = -1;
        this->unschedule(schedule_selector(PaimonLevelInfoLayer::refreshDarknessOverlay));
        refreshDarknessOverlay(0.f);
        this->schedule(schedule_selector(PaimonLevelInfoLayer::refreshDarknessOverlay), 0.5f);

        // Activa dynamic song
        this->unschedule(schedule_selector(PaimonLevelInfoLayer::forcePlayDynamic));
        this->scheduleOnce(schedule_selector(PaimonLevelInfoLayer::forcePlayDynamic), 0.0f);
    }

    void forcePlayDynamic(float /*dt*/) {
        if (m_fields->m_audioDeactivated || !this->getParent() || !m_level) return;
        AudioContextCoordinator::get().activateLevelInfo(m_level, true);
    }

    void refreshDarknessOverlay(float /*dt*/) {
        // Re-cache settings if listenForSettingChanges detected a change
        uint64_t currentVersion = paimon::settings::internal::g_settingsVersion.load(std::memory_order_relaxed);
        if (m_fields->m_loadedSettingsVersion < static_cast<int>(currentVersion)) {
            m_fields->m_loadedSettingsVersion = static_cast<int>(currentVersion);
            m_fields->m_cachedBgStyle = Mod::get()->getSettingValue<std::string>("levelinfo-background-style");
            m_fields->m_cachedEffectIntensity = std::clamp(
                static_cast<int>(Mod::get()->getSettingValue<int64_t>("levelinfo-effect-intensity")), 1, 10);
            m_fields->m_cachedExtraStyles = Mod::get()->getSettingValue<std::string>("levelinfo-extra-styles");
            m_fields->m_cachedAutoCycle = Mod::get()->getSettingValue<bool>("levelcell-gallery-autocycle");
        }
        applyDarknessSetting(readDarknessSetting());
    }

    void updateShaderTime(float dt) {
        if (!m_fields->m_animatedShader) return;
        m_fields->m_shaderTime += dt;

        // Actualiza u_time en sprite principal
        if (m_fields->m_pixelBg) {
            auto sprite = typeinfo_cast<CCSprite*>(static_cast<CCNode*>(m_fields->m_pixelBg));
            if (sprite) {
                if (auto gif = typeinfo_cast<AnimatedGIFSprite*>(sprite)) {
                    gif->m_time = m_fields->m_shaderTime;
                } else {
                    auto shader = sprite->getShaderProgram();
                    if (shader) {
                        shader->use();
                        GLint loc = shader->getUniformLocationForName("u_time");
                        if (loc >= 0) {
                            shader->setUniformLocationWith1f(loc, m_fields->m_shaderTime);
                        }
                    }
                }
            }
        }

        // Actualiza u_time en sprites extra
        for (auto& extra : m_fields->m_extraBgSprites) {
            if (!extra) continue;
            auto shader = extra->getShaderProgram();
            if (!shader) continue;
            shader->use();
            GLint loc = shader->getUniformLocationForName("u_time");
            if (loc >= 0) {
                shader->setUniformLocationWith1f(loc, m_fields->m_shaderTime);
            }
        }
    }

    void updatePaimonAudio(float dt) {
        if (!m_fields->m_paimonAudioActive) return;

        PaimonAudio::get().update(dt);

        auto& pa = PaimonAudio::get();
        int baseDark = m_fields->m_paimonAudioBaseDarkness;
        float baseAlpha = (baseDark / 50.0f) * 255.0f;

        // Modula oscuridad con audio
        float mod = 1.0f + pa.bass() * 0.45f - pa.beatPulse() * 0.7f + pa.energy() * 0.15f;
        mod = std::clamp(mod, 0.08f, 1.8f);
        GLubyte dynAlpha = static_cast<GLubyte>(std::clamp(baseAlpha * mod, 0.f, 255.f));

        auto* overlay = m_fields->m_pixelOverlay.data();
        if (overlay && overlay->getParent()) {
            overlay->setOpacity(dynAlpha);
        }
    }

    void stopVideoBackgroundSprite() {
        if (!m_fields->m_videoSprite) {
            return;
        }

        m_fields->m_videoSprite->setOnFirstVisibleFrame({});
        m_fields->m_videoSprite->stop();
        if (m_fields->m_pixelBg.data() == m_fields->m_videoSprite.data()) {
            m_fields->m_pixelBg = nullptr;
        }
        if (m_fields->m_videoSprite->getParent()) {
            m_fields->m_videoSprite->removeFromParent();
        }
        m_fields->m_videoSprite = nullptr;
    }

    void activateVideoBackgroundSprite(VideoThumbnailSprite* videoSprite, int32_t levelID, int requestToken = -1) {
        if (!videoSprite || !this->getParent()) {
            return;
        }

        if (requestToken >= 0 && m_fields->m_bgRequestToken != requestToken) {
            if (m_fields->m_videoSprite.data() == videoSprite) {
                stopVideoBackgroundSprite();
            } else if (videoSprite->getParent()) {
                videoSprite->removeFromParent();
            }
            return;
        }

        if (m_fields->m_videoSprite.data() != videoSprite) {
            return;
        }

        auto* tex = videoSprite->getTexture();
        if (!videoSprite->hasVisibleFrame() || !tex) {
            return;
        }

        this->applyThumbnailBackground(tex, levelID);

        if (auto oldBg = this->getChildByID("paimon-levelinfo-pixel-bg"_spr)) {
            oldBg->removeFromParent();
        }

        if (!videoSprite->getParent()) {
            this->addChild(videoSprite, kBackgroundZOrder);
        } else {
            this->reorderChild(videoSprite, kBackgroundZOrder);
        }

        videoSprite->stopAllActions();
        videoSprite->setVisible(true);
        videoSprite->setOpacity(0);
        videoSprite->runAction(CCFadeTo::create(0.15f, 255));
        m_fields->m_pixelBg = videoSprite;
    }

    void queueVideoBackgroundSprite(VideoThumbnailSprite* videoSprite, int32_t levelID, int requestToken = -1) {
        if (!videoSprite) {
            return;
        }

        auto win = CCDirector::sharedDirector()->getWinSize();
        auto videoSize = videoSprite->getVideoSize();
        float safeWidth = std::max(1.f, videoSize.width);
        float safeHeight = std::max(1.f, videoSize.height);
        float scaleX = win.width / safeWidth;
        float scaleY = win.height / safeHeight;
        float scale = std::max(scaleX, scaleY);

        videoSprite->setScale(scale);
        videoSprite->setPosition({win.width / 2.0f, win.height / 2.0f});
        videoSprite->setAnchorPoint({0.5f, 0.5f});
        videoSprite->setID("levelinfo-video-driver"_spr);
        videoSprite->setVolume(0.0f);
        videoSprite->setLoop(true);
        videoSprite->setVisible(false);
        videoSprite->setOpacity(255);

        this->addChild(videoSprite, kBackgroundZOrder);
        m_fields->m_videoSprite = videoSprite;

        WeakRef<PaimonLevelInfoLayer> safeRef = this;
        videoSprite->setOnFirstVisibleFrame([safeRef, levelID, requestToken](VideoThumbnailSprite* readySprite) {
            auto selfRef = safeRef.lock();
            auto* self = static_cast<PaimonLevelInfoLayer*>(selfRef);
            if (!self) {
                return;
            }

            self->activateVideoBackgroundSprite(readySprite, levelID, requestToken);
        });
        videoSprite->play();
    }

    $override
    void onExit() {
        log::info("[LevelInfoLayer] onExit: levelID={}", m_level ? m_level->m_levelID.value() : 0);

        // Deactivate dynamic song context if this layer is being permanently removed.
        // onBack() already handles normal navigation; this catches scene replacements
        // and forced navigations that bypass onBack().
        if (!m_fields->m_audioDeactivated) {
            m_fields->m_audioDeactivated = true;
            AudioContextCoordinator::get().deactivateLevelInfo(false);
        }

        this->unschedule(schedule_selector(PaimonLevelInfoLayer::updateGallery));
        this->unschedule(schedule_selector(PaimonLevelInfoLayer::updateShaderTime));
        this->unschedule(schedule_selector(PaimonLevelInfoLayer::updatePaimonAudio));
        this->unschedule(schedule_selector(PaimonLevelInfoLayer::refreshDarknessOverlay));
        this->unschedule(schedule_selector(PaimonLevelInfoLayer::forcePlayDynamic));

        // PaimonAudio cleanup
        if (m_fields->m_paimonAudioActive) {
            PaimonAudio::get().deactivate();
            m_fields->m_paimonAudioActive = false;
        }
        if (m_fields->m_invalidationListenerId != 0) {
            ThumbnailLoader::get().removeInvalidationListener(m_fields->m_invalidationListenerId);
            m_fields->m_invalidationListenerId = 0;
        }
        m_fields->m_galleryToken++;
        m_fields->m_bgRequestToken++;

        // BlurSystem is synchronous — no progressive job to cancel

        stopVideoBackgroundSprite();

        // Remover nodos visuales de paimon del arbol para evitar acumulacion
        // al volver (onEnter los recrea desde cero).
        for (auto& s : m_fields->m_extraBgSprites) {
            if (s && s->getParent()) s->removeFromParent();
        }
        m_fields->m_extraBgSprites.clear();

        if (m_fields->m_pixelOverlay && m_fields->m_pixelOverlay->getParent()) {
            m_fields->m_pixelOverlay->removeFromParent();
        }
        m_fields->m_pixelOverlay = nullptr;

        if (m_fields->m_extraDarknessLayer && m_fields->m_extraDarknessLayer->getParent()) {
            m_fields->m_extraDarknessLayer->removeFromParent();
        }
        m_fields->m_extraDarknessLayer = nullptr;

        // Parar acciones del bg (crossfade) antes de removerlo
        if (m_fields->m_pixelBg && m_fields->m_pixelBg->getParent()) {
            m_fields->m_pixelBg->stopAllActions();
            m_fields->m_pixelBg->removeFromParent();
        }
        m_fields->m_pixelBg = nullptr;

        // Restaurar fondo nativo para que se vea algo al volver
        if (auto vanillaBg = this->getChildByID("background")) {
            vanillaBg->setVisible(true);
        }

        LevelInfoLayer::onExit();
    }

    // Crea el boton de set daily/weekly si aun no existe
    void addSetDailyWeeklyButton() {
        // evitar duplicados
        if (this->getChildByIDRecursive("set-daily-weekly-button"_spr)) return;

        CCSprite* iconSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_timeIcon_001.png");
        if (!iconSpr) {
            iconSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starBtn_001.png");
        }
        if (!iconSpr) return;

        iconSpr->setScale(0.8f);

        auto btnSprite = CircleButtonSprite::create(
            iconSpr,
            CircleBaseColor::Green,
            CircleBaseSize::Medium
        );
        if (!btnSprite) return;

        auto btn = CCMenuItemSpriteExtra::create(
            btnSprite,
            this,
            menu_selector(PaimonLevelInfoLayer::onSetDailyWeekly)
        );
        btn->setID("set-daily-weekly-button"_spr);

        auto leftMenu = findLeftSideMenu();
        if (leftMenu) {
            leftMenu->addChild(btn);
            leftMenu->updateLayout();
            ButtonLayoutManager::get().applyLayoutToMenu("LevelInfoLayer", leftMenu);
        }
    }

    void onSetDailyWeekly(CCObject* sender) {
        if (!m_level || m_level->m_levelID.value() <= 0) return;
        SetDailyWeeklyPopup::create(m_level->m_levelID.value())->show();
    }

    $override
    bool init(GJGameLevel* level, bool challenge) {
        log::info("[LevelInfoLayer] init: levelID={} challenge={}", level ? level->m_levelID.value() : 0, challenge);
        // vinimos de leaderboards?
        if (auto scene = CCDirector::sharedDirector()->getRunningScene()) {
            if (scene->getChildByType<LeaderboardsLayer>(0)) {
                m_fields->m_fromLeaderboards = true;
                m_fields->m_leaderboardType = LeaderboardType::Default;
            }
        }

        if (!LevelInfoLayer::init(level, challenge)) return false;

        // El fondo nativo se oculta en installBackgroundSprite cuando el
        // thumbnail esta listo, asi niveles sin thumbnail conservan el fondo.

        // ── Overlay de oscuridad: un solo sprite persistente ──
        {
            // Defensive: eliminar overlay residual antes de crear uno nuevo
            if (auto oldOverlay = this->getChildByID("paimon-levelinfo-pixel-overlay"_spr)) {
                oldOverlay->removeFromParent();
            }

            auto win = CCDirector::sharedDirector()->getWinSize();

            // Crear con alpha=0 — refreshDarknessOverlay aplica el valor
            // correcto en el primer tick. Esto evita el bug donde la primera
            // lectura del setting devuelve un valor incorrecto y deja la
            // pantalla casi negra hasta que el usuario toca el slider.
            auto overlay = CCLayerColor::create({0, 0, 0, 0});
            overlay->setAnchorPoint({0, 0});
            overlay->setPosition({0, 0});
            overlay->setContentSize(win);
            overlay->setID("paimon-levelinfo-pixel-overlay"_spr);
            this->addChild(overlay, kOverlayZOrder);
            m_fields->m_pixelOverlay = overlay;
            // m_lastDarkness queda en -1 para que refreshDarknessOverlay
            // siempre aplique el valor correcto en el primer tick

            // Aplicar valor correcto inmediatamente
            refreshDarknessOverlay(0.f);

            // refresca opacidad cada 0.5s — detecta cambios de setting sin gasto
            this->schedule(schedule_selector(PaimonLevelInfoLayer::refreshDarknessOverlay), 0.5f);
        }

        if (!level || level->m_levelID <= 0) {
                log::debug("[LevelInfoLayer] Level ID invalid, skipping thumbnail button");
                return true;
            }

            // Cachear settings una sola vez para evitar lookups repetidos
            m_fields->m_cachedBgStyle = Mod::get()->getSettingValue<std::string>("levelinfo-background-style");
            m_fields->m_cachedEffectIntensity = std::clamp(
                static_cast<int>(Mod::get()->getSettingValue<int64_t>("levelinfo-effect-intensity")), 1, 10);
            m_fields->m_cachedExtraStyles = Mod::get()->getSettingValue<std::string>("levelinfo-extra-styles");
            m_fields->m_cachedAutoCycle = Mod::get()->getSettingValue<bool>("levelcell-gallery-autocycle");

            // Activar dynamic song durante la transicion de entrada.
            // scheduleOnce funciona desde init() porque GD ya añadio el nodo al arbol.
            this->scheduleOnce(schedule_selector(PaimonLevelInfoLayer::forcePlayDynamic), 0.0f);

            // consumir el flag "abierto desde lista de miniaturas"
            bool fromThumbs = paimon::SessionState::consumeFlag(paimon::SessionState::get().verification.openFromThumbs);
            m_fields->m_fromThumbsList = fromThumbs;

            // abierto desde report?
            bool fromReport = paimon::SessionState::consumeFlag(paimon::SessionState::get().verification.openFromReport);
            m_fields->m_fromReportSection = fromReport;
            
            // vinimos de cola verify?
            bool fromVerificationQueue = false;
            int verificationQueueCategory = -1;
            int verificationQueueLevelID = paimon::SessionState::get().verification.queueLevelID;

            if (verificationQueueLevelID == level->m_levelID.value()) {
                fromVerificationQueue = true;
                verificationQueueCategory = paimon::SessionState::get().verification.queueCategory;
                m_fields->m_fromVerificationQueue = true;

                // no limpiar, persistir en playlayer
            }

            // fondo pixel thumb
            bool isMainLevel = level->m_levelType == GJLevelType::Main;
            if (m_fields->m_invalidationListenerId == 0) {
                WeakRef<PaimonLevelInfoLayer> safeRef = this;
                m_fields->m_invalidationListenerId = ThumbnailLoader::get().addInvalidationListener([safeRef](int invalidLevelID) {
                    auto selfRef = safeRef.lock();
                    if (!selfRef) return;
                    auto* self = static_cast<PaimonLevelInfoLayer*>(selfRef.data());
                    if (!self || !self->getParent() || !self->m_level) return;
                    if (self->m_level->m_levelID.value() != invalidLevelID) return;
                    self->m_fields->m_loadedInvalidationVersion = ThumbnailLoader::get().getInvalidationVersion(invalidLevelID);
                    self->refreshGalleryData(invalidLevelID, true);
                });
            }
            if (!isMainLevel && !m_fields->m_thumbnailRequested) {
                m_fields->m_thumbnailRequested = true;
                int32_t levelID = level->m_levelID.value();

                auto localThumbPath = LocalThumbs::get().findAnyThumbnail(levelID);
                if (localThumbPath) {
                    auto lowerPath = geode::utils::string::toLower(*localThumbPath);
                    if (lowerPath.ends_with(".mp4")) {
                        log::info("[LevelInfoLayer] init: found local MP4 for levelID={}", levelID);
                        auto* videoSprite = VideoThumbnailSprite::create(*localThumbPath);
                        if (videoSprite) {
                            this->queueVideoBackgroundSprite(videoSprite, levelID);
                            // No retornar aqui — continuar el setup de botones, galeria, etc.
                        }
                    }
                }

                // No disparamos requestLoad aqui: refreshGalleryData (mas abajo)
                // ya carga el thumbnail principal con refreshBackground=true.
                // Antes se lanzaban ambos y competian por m_bgRequestToken,
                // causando que el segundo invalidara al primero y el fondo
                // nunca se aplicara.
            }

            // load layouts botones
            ButtonLayoutManager::get().load();
            
            // menu izq
            auto leftMenu = findLeftSideMenu();
            if (!leftMenu) {
                log::warn("Left side menu not found");
                return true;
            }

            // ref menu pa buttoneditoverlay
            m_fields->m_extraMenu = static_cast<CCMenu*>(leftMenu);
            
            // sprite icono btn (con fallbacks)
            CCSprite* iconSprite = paimon::SpriteHelper::safeCreate("paim_BotonMostrarThumbnails.png"_spr);
            if (!iconSprite) iconSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png");
            if (!iconSprite) iconSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plusBtn_001.png");
            if (!iconSprite) return true;

            // rotar 90
            iconSprite->setRotation(-90.0f);
            // reducir el icono un 20%
            iconSprite->setScale(0.8f);

            // CircleButtonSprite verde
            auto btnSprite = CircleButtonSprite::create(
                iconSprite,
                CircleBaseColor::Green,
                CircleBaseSize::Medium
            );

            if (!btnSprite) {
                log::error("Failed to create CircleButtonSprite");
                return true;
            }
            
            auto button = CCMenuItemSpriteExtra::create(
                btnSprite,
                this,
                menu_selector(PaimonLevelInfoLayer::onThumbnailButton)
            );
            PaimonButtonHighlighter::registerButton(button);
            
            if (!button) {
                log::error("Failed to create menu button");
                return true;
            }
            
            button->setID("thumbnail-view-button"_spr);
            m_fields->m_thumbnailButton = button;

            // thumbs galeria (URLs versionadas via list endpoint)
            // Si ya hay un video local aplicado, no pisar el fondo con una imagen estatica
            m_fields->m_initLoadState = Fields::InitLoadState::Pending;
            bool skipBgRefresh = m_fields->m_videoSprite != nullptr;

            // Instant cache check: try to display the thumbnail immediately
            // from RAM cache while the gallery list API call is in flight.
            // This makes the background appear almost instantly on revisit.
            if (!skipBgRefresh) {
                auto& cache = paimon::cache::ThumbnailCache::get();
                std::string mainUrl = ThumbnailAPI::get().getThumbnailURL(level->m_levelID.value());
                auto ramTex = cache.getUrlFromRam(mainUrl);
                if (ramTex.has_value() && ramTex.value()) {
                    log::info("[LevelInfoLayer] init: instant RAM cache hit for main thumbnail levelID={}", level->m_levelID.value());
                    this->applyThumbnailBackground(ramTex.value(), level->m_levelID.value());
                    // Background is already applied — refreshGalleryData will still
                    // call loadThumbnail(0) but it'll be a cache hit (fast re-apply).
                } else {
                    // Paralelizar: disparar descarga del thumbnail base antes de que
                    // getThumbnails responda. Si la galeria tarda, el fondo aparece
                    // tan pronto como el download termine.
                    int currentLevelID = level->m_levelID.value();
                    Ref<LevelInfoLayer> safeRef = this;
                    ThumbnailLoader::get().requestUrlLoad(mainUrl, [safeRef, currentLevelID](CCTexture2D* tex, bool success) {
                        auto* self = static_cast<PaimonLevelInfoLayer*>(safeRef.data());
                        if (!self || !self->getParent() || !self->m_level) return;
                        if (self->m_level->m_levelID.value() != currentLevelID) return;
                        if (self->m_fields->m_pixelBg) return; // ya se aplico fondo
                        if (!success || !tex) return;
                        log::info("[LevelInfoLayer] init: background applied from parallel URL load levelID={}", currentLevelID);
                        self->applyThumbnailBackground(tex, currentLevelID);
                    }, ThumbnailLoader::PriorityHero);
                }
            }

            this->refreshGalleryData(level->m_levelID.value(), !skipBgRefresh);

            // add primero pa layout default
            leftMenu->addChild(button);
            leftMenu->updateLayout();

            ButtonLayout defaultLayout;
            defaultLayout.position = button->getPosition();
            defaultLayout.scale = button->getScale();
            defaultLayout.opacity = 1.0f;
            ButtonLayoutManager::get().setDefaultLayoutIfAbsent("LevelInfoLayer", "thumbnail-view-button", defaultLayout);

            // load layout guardado
            auto savedLayout = ButtonLayoutManager::get().getLayout("LevelInfoLayer", "thumbnail-view-button");
            if (savedLayout) {
                button->setPosition(savedLayout->position);
                button->setScale(savedLayout->scale);
                button->setOpacity(static_cast<GLubyte>(savedLayout->opacity * 255));
            }

            // admin? -> btn daily/weekly
            // Verificacion local primero: si el mod code esta guardado y el usuario
            // esta marcado como admin, mostrar el boton inmediatamente sin esperar al server.
            {
                bool localAdmin = Mod::get()->getSavedValue<bool>("is-verified-admin", false);
                bool hasModCode = !HttpClient::get().getModCode().empty();

                if (localAdmin && hasModCode) {
                    this->addSetDailyWeeklyButton();
                }
            }
            // Verificacion con servidor (refresca cache y puede agregar/quitar el boton)
            if (auto gm = GameManager::get()) {
                auto username = gm->m_playerName;
                auto accountID = 0;
                if (auto am = GJAccountManager::get()) accountID = am->m_accountID;
                
                Ref<LevelInfoLayer> selfMod = this;
                ThumbnailAPI::get().checkModeratorAccount(username, accountID, [selfMod](bool isMod, bool isAdmin) {
                    auto* self = static_cast<PaimonLevelInfoLayer*>(selfMod.data());
                    if (!self || !self->getParent()) return;
                    if (isAdmin) {
                        self->addSetDailyWeeklyButton();
                    }
                });
            }

            log::info("Thumbnail button added successfully");

            // cola verify -> guardar categoria
            if (fromVerificationQueue && verificationQueueLevelID == level->m_levelID.value()) {
                log::info("Nivel abierto desde verificacion (categoria: {}) - boton listo para usar", verificationQueueCategory);
                // categoria pa thumbnailviewpopup
                paimon::SessionState::get().verification.verificationCategory = verificationQueueCategory;
            }

            // apply layouts to left menu to restore any vanilla button customizations
            ButtonLayoutManager::get().applyLayoutToMenu("LevelInfoLayer", leftMenu);

        return true;
    }

    void onThumbnailButton(CCObject*) {
        if (!m_level) {
            log::error("Level is null");
            return;
        }

        // evita abrir multiples popups si se aprieta rapido
        if (auto* scene = CCDirector::sharedDirector()->getRunningScene()) {
            for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
                if (geode::cast::typeinfo_cast<LocalThumbnailViewPopup*>(child)) {
                    return;
                }
            }
        }

        int32_t levelID = m_level->m_levelID.value();
        log::info("Opening thumbnail view for level ID: {}", levelID);

        // usar utilidad moderatorverification
        bool canAccept = false; // sin funcionalidad de server
        // contexto popup flag
        paimon::SessionState::get().verification.fromReportPopup = m_fields->m_fromReportSection;
        auto popup = LocalThumbnailViewPopup::create(levelID, canAccept);
        if (popup) {
            popup->show();
        } else {
            log::error("Failed to create thumbnail view popup");
            PaimonNotify::create("Error al abrir miniatura", NotificationIcon::Error)->show();
        }
    }

    void onToggleEditMode(CCObject*) {
        if (!m_fields->m_extraMenu) return;

        // overlay edicion
        auto overlay = ButtonEditOverlay::create("LevelInfoLayer", m_fields->m_extraMenu);
        if (auto scene = CCDirector::sharedDirector()->getRunningScene()) {
            scene->addChild(overlay, 1000);
        }
    }

    void onUploadLocalThumbnail(CCObject*) {
        log::info("[LevelInfoLayer] Upload local thumbnail button clicked");
        
        if (!m_level) {
            PaimonNotify::create(Localization::get().getString("level.error_prefix") + "nivel no encontrado", NotificationIcon::Error)->show();
            return;
        }
        
        // ptr nivel antes async
        auto* level = m_level;
        int32_t levelID = level->m_levelID.value();
        
        // existe thumb local?
        if (!LocalThumbs::get().has(levelID)) {
            PaimonNotify::create(Localization::get().getString("level.no_local_thumb").c_str(), NotificationIcon::Error)->show();
            return;
        }
        
        // obtener nombre de usuario
        std::string username;
        int accountID = 0;
        auto* gm = GameManager::get();
        if (gm) {
            username = gm->m_playerName;
            if (auto* am = GJAccountManager::get()) accountID = am->m_accountID;
        } else {
            log::warn("[LevelInfoLayer] GameManager::get() es null");
            username = "Unknown";
        }
        if (accountID <= 0) {
            PaimonNotify::create(Localization::get().getString("level.account_required").c_str(), NotificationIcon::Error)->show();
            return;
        }
        
        // load local -> png
        auto pathOpt = LocalThumbs::get().getThumbPath(levelID);
        if (!pathOpt) {
            PaimonNotify::create("No se pudo encontrar la miniatura", NotificationIcon::Error)->show();
            return;
        }
        
        std::vector<uint8_t> pngData;
        if (!ImageConverter::loadRgbFileToPng(*pathOpt, pngData)) {
            PaimonNotify::create(Localization::get().getString("level.png_error").c_str(), NotificationIcon::Error)->show();
            return;
        }
        
        WeakRef<PaimonLevelInfoLayer> self = this;

        // Single upload — server handles mod check + exists check + routing (live vs pending)
        PaimonNotify::show(Localization::get().getString("capture.uploading").c_str(), geode::NotificationIcon::Info);
        ThumbnailAPI::get().uploadThumbnail(levelID, pngData, username, [self, levelID, username](bool success, std::string const& msg) {
            auto layer = self.lock();
            if (!layer) return;

            if (success) {
                bool isPending = (msg.find("pending") != std::string::npos || msg.find("verification") != std::string::npos);
                if (isPending) {
                    PendingQueue::get().addOrBump(levelID, PendingCategory::Verify, username, {}, false);
                    PaimonNotify::create(Localization::get().getString("capture.suggested").c_str(), NotificationIcon::Success)->show();
                } else {
                    PendingQueue::get().removeForLevel(levelID);
                    PaimonNotify::create(Localization::get().getString("capture.upload_success").c_str(), NotificationIcon::Success)->show();
                    ThumbnailLoader::get().invalidateLevel(levelID);
                    layer->refreshGalleryData(levelID, true);
                }
            } else {
                PaimonNotify::create(Localization::get().getString("capture.upload_error") + msg, NotificationIcon::Error)->show();
            }
        });
    }

    $override
    void onPlay(CCObject* sender) {
        log::info("[LevelInfoLayer] onPlay: levelID={}", m_level ? m_level->m_levelID.value() : 0);
        AudioContextCoordinator::get().beginGameplayTransition();
        LevelInfoLayer::onPlay(sender);
    }

    $override
    void onBack(CCObject* sender) {
        log::info("[LevelInfoLayer] onBack: levelID={} fromVerify={} fromLeaderboards={}", m_level ? m_level->m_levelID.value() : 0, m_fields->m_fromVerificationQueue, m_fields->m_fromLeaderboards);

        // Verificar si volvemos a LevelSelectLayer
        bool returnsToLevelSelect = false;
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (scene) {
            for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
                if (typeinfo_cast<LevelSelectLayer*>(child)) {
                    returnsToLevelSelect = true;
                    break;
                }
            }
        }

        m_fields->m_audioDeactivated = true;
        AudioContextCoordinator::get().deactivateLevelInfo(returnsToLevelSelect);

        if (m_fields->m_fromVerificationQueue) {
            // limpiar los flags
            paimon::SessionState::get().verification.openFromQueue = false;
            paimon::SessionState::get().verification.queueLevelID  = -1;
            paimon::SessionState::get().verification.queueCategory  = -1;
            
            // reabrir popup
            paimon::SessionState::get().verification.reopenQueue = true;
            
            // volver a menulayer
            TransitionManager::get().replaceScene(MenuLayer::scene(false));
            return;
        }

        // abrio desde leaderboards?
        if (m_fields->m_fromLeaderboards) {
            auto lbScene = LeaderboardsLayer::scene(m_fields->m_leaderboardType, m_fields->m_leaderboardStat);
            TransitionManager::get().replaceScene(lbScene);
            return;
        }

        // sin anim daily
        
        LevelInfoLayer::onBack(sender);
    }

    void setupGallery() {
        if (auto old = this->getChildByID("gallery-menu"_spr)) {
            old->removeFromParent();
        }
        m_fields->m_prevBtn = nullptr;
        m_fields->m_nextBtn = nullptr;

        auto menu = CCMenu::create();
        menu->setID("gallery-menu"_spr);
        menu->setPosition({0, 0});

        auto win = CCDirector::sharedDirector()->getWinSize();
        float arrowY = 210.f;

        // flecha izquierda (prev)
        auto prevSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_03_001.png");
        if (prevSpr) {
            prevSpr->setScale(0.65f);
            auto prevBtn = CCMenuItemSpriteExtra::create(
                prevSpr, this, menu_selector(PaimonLevelInfoLayer::onPrevBtn)
            );
            prevBtn->setPosition({win.width / 2.f - 62.f, arrowY});
            prevBtn->setOpacity(180);
            menu->addChild(prevBtn);
            m_fields->m_prevBtn = prevBtn;
        }

        // flecha derecha (next)
        auto nextSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_03_001.png");
        if (nextSpr) {
            nextSpr->setFlipX(true);
            nextSpr->setScale(0.65f);
            auto nextBtn = CCMenuItemSpriteExtra::create(
                nextSpr, this, menu_selector(PaimonLevelInfoLayer::onNextBtn)
            );
            nextBtn->setPosition({win.width / 2.f + 62.f, arrowY});
            nextBtn->setOpacity(180);
            menu->addChild(nextBtn);
            m_fields->m_nextBtn = nextBtn;
        }

        this->addChild(menu, 100);

        bool showNav = m_fields->m_thumbnails.size() > 1;
        if (m_fields->m_prevBtn) m_fields->m_prevBtn->setVisible(showNav);
        if (m_fields->m_nextBtn) m_fields->m_nextBtn->setVisible(showNav);
    }

    void refreshGalleryData(int32_t levelID, bool refreshBackground) {
        int token = ++m_fields->m_galleryToken;
        log::info("[LevelInfoLayer] refreshGalleryData: levelID={} refreshBg={} token={}", levelID, refreshBackground, token);
        Ref<LevelInfoLayer> safeRef = this;
        ThumbnailAPI::get().getThumbnails(levelID, [safeRef, levelID, token, refreshBackground](bool success, std::vector<ThumbnailAPI::ThumbnailInfo> const& thumbs) {
            auto* self = static_cast<PaimonLevelInfoLayer*>(safeRef.data());
            if (!self || !self->m_level || self->m_level->m_levelID.value() != levelID) return;
            if (self->m_fields->m_galleryToken != token) return;

            self->m_fields->m_thumbnails.clear();
            if (success) self->m_fields->m_thumbnails = thumbs;
            // Reset lazy load state for new gallery
            self->m_fields->m_lazyLoadIndex = 1;
            self->m_fields->m_lazyLoadScheduled = false;
            log::info("[LevelInfoLayer] refreshGalleryData callback: levelID={} success={} thumbCount={}", levelID, success, thumbs.size());
            if (self->m_fields->m_thumbnails.empty()) {
                ThumbnailAPI::ThumbnailInfo mainThumb;
                mainThumb.id = "0";
                mainThumb.url = ThumbnailAPI::get().getThumbnailURL(levelID);
                self->m_fields->m_thumbnails.push_back(mainThumb);
            }

            self->m_fields->m_currentThumbnailIndex = 0;
            self->m_fields->m_cycleTimer = 0.f;
            self->setupGallery();

            bool autoCycleEnabled = self->m_fields->m_cachedAutoCycle;
            self->unschedule(schedule_selector(PaimonLevelInfoLayer::updateGallery));
            if (self->m_fields->m_thumbnails.size() > 1 && autoCycleEnabled) {
                self->schedule(schedule_selector(PaimonLevelInfoLayer::updateGallery), 3.0f);
            }

            if (refreshBackground) {
                self->loadThumbnail(0);
            } else {
                self->m_fields->m_initLoadState = Fields::InitLoadState::Idle;
            }
        });
    }
    
    void onRateBtn(CCObject* sender) {
        // abrir ratepopup con estrella pre-seleccionada? o solo abrirlo.
        // el usuario podria querer ratear directamente.
        // ratepopup maneja logica
        if (m_fields->m_currentThumbnailIndex < 0 || m_fields->m_currentThumbnailIndex >= m_fields->m_thumbnails.size()) return;
        
        auto& thumb = m_fields->m_thumbnails[m_fields->m_currentThumbnailIndex];
        if (!m_level) return;
        RatePopup::create(m_level->m_levelID.value(), thumb.id)->show();
    }

    // ── Favorite Creator / Level ────────────────────────────

    void setupFavoriteButtons() {
        if (!m_level || m_level->m_levelID <= 0) return;

        auto win = CCDirector::sharedDirector()->getWinSize();

        // reuse gallery-menu or create a dedicated fav-menu
        auto favMenu = CCMenu::create();
        favMenu->setID("fav-menu"_spr);
        favMenu->setPosition({0, 0});

        int creatorID = m_level->m_accountID;
        int levelID = m_level->m_levelID.value();
        auto& tracker = paimon::foryou::ForYouTracker::get();

        // ── Heart button (Favorite Creator) ──
        {
            bool isFav = tracker.isCreatorFavorited(creatorID);
            auto heartSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_heart_01.png");
            if (!heartSpr) heartSpr = paimon::SpriteHelper::safeCreateWithFrameName("gj_heartOn_001.png");
            if (heartSpr) {
                heartSpr->setScale(0.55f);
                if (!isFav) heartSpr->setOpacity(120);
                auto btn = CCMenuItemSpriteExtra::create(
                    heartSpr, this, menu_selector(PaimonLevelInfoLayer::onFavCreator)
                );
                btn->setID("fav-creator-btn"_spr);
                btn->setPosition({win.width - 30.f, win.height - 60.f});
                favMenu->addChild(btn);
                m_fields->m_favCreatorBtn = btn;
            }
        }

        // ── Star button (Favorite Level) ──
        {
            bool isFav = tracker.isLevelFavorited(levelID);
            auto starSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png");
            if (!starSpr) starSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_bigStar_001.png");
            if (starSpr) {
                starSpr->setScale(0.55f);
                if (!isFav) starSpr->setOpacity(120);
                auto btn = CCMenuItemSpriteExtra::create(
                    starSpr, this, menu_selector(PaimonLevelInfoLayer::onFavLevel)
                );
                btn->setID("fav-level-btn"_spr);
                btn->setPosition({win.width - 30.f, win.height - 90.f});
                favMenu->addChild(btn);
                m_fields->m_favLevelBtn = btn;
            }
        }

        this->addChild(favMenu, 101);
    }

    void updateFavoriteButtonStates() {
        if (!m_level) return;
        auto& tracker = paimon::foryou::ForYouTracker::get();

        if (m_fields->m_favCreatorBtn) {
            bool isFav = tracker.isCreatorFavorited(m_level->m_accountID);
            if (auto spr = typeinfo_cast<CCSprite*>(m_fields->m_favCreatorBtn->getNormalImage())) {
                spr->setOpacity(isFav ? 255 : 120);
            }
        }
        if (m_fields->m_favLevelBtn) {
            bool isFav = tracker.isLevelFavorited(m_level->m_levelID.value());
            if (auto spr = typeinfo_cast<CCSprite*>(m_fields->m_favLevelBtn->getNormalImage())) {
                spr->setOpacity(isFav ? 255 : 120);
            }
        }
    }

    void onFavCreator(CCObject*) {
        if (!m_level || m_level->m_accountID <= 0) return;
        auto& tracker = paimon::foryou::ForYouTracker::get();
        int creatorID = m_level->m_accountID;

        if (tracker.isCreatorFavorited(creatorID)) {
            tracker.onUnfavoriteCreator(creatorID);
            PaimonNotify::create(
                Localization::get().getString("foryou.fav_creator_removed").c_str(),
                NotificationIcon::Info
            )->show();
        } else {
            tracker.onFavoriteCreator(creatorID);
            PaimonNotify::create(
                Localization::get().getString("foryou.fav_creator_added").c_str(),
                NotificationIcon::Success
            )->show();
        }
        tracker.save();
        updateFavoriteButtonStates();
    }

    void onFavLevel(CCObject*) {
        if (!m_level || m_level->m_levelID <= 0) return;
        auto& tracker = paimon::foryou::ForYouTracker::get();
        int levelID = m_level->m_levelID.value();

        if (tracker.isLevelFavorited(levelID)) {
            tracker.onUnfavoriteLevel(levelID);
            PaimonNotify::create(
                Localization::get().getString("foryou.fav_level_removed").c_str(),
                NotificationIcon::Info
            )->show();
        } else {
            tracker.onFavoriteLevel(levelID);
            PaimonNotify::create(
                Localization::get().getString("foryou.fav_level_added").c_str(),
                NotificationIcon::Success
            )->show();
        }
        tracker.save();
        updateFavoriteButtonStates();
    }
    
    void updateGallery(float dt) {
        if (!m_fields->m_cycling || m_fields->m_thumbnails.size() <= 1) return;
        m_fields->m_bgNavDirection = Fields::BgNavDir::Right;
        m_fields->m_currentThumbnailIndex = (m_fields->m_currentThumbnailIndex + 1) % static_cast<int>(m_fields->m_thumbnails.size());
        this->loadThumbnail(m_fields->m_currentThumbnailIndex);
    }
    
    void onPrevBtn(CCObject*) {
        log::info("[LevelInfoLayer] onPrevBtn: currentIndex={}", m_fields->m_currentThumbnailIndex);
        if (m_fields->m_thumbnails.empty()) return;
        m_fields->m_cycling = false; // detener auto-ciclado al interactuar
        m_fields->m_bgNavDirection = Fields::BgNavDir::Left;
        m_fields->m_currentThumbnailIndex--;
        if (m_fields->m_currentThumbnailIndex < 0) m_fields->m_currentThumbnailIndex = static_cast<int>(m_fields->m_thumbnails.size()) - 1;
        this->loadThumbnail(m_fields->m_currentThumbnailIndex);
    }
    
    void onNextBtn(CCObject*) {
        log::info("[LevelInfoLayer] onNextBtn: currentIndex={}", m_fields->m_currentThumbnailIndex);
        if (m_fields->m_thumbnails.empty()) return;
        m_fields->m_cycling = false;
        m_fields->m_bgNavDirection = Fields::BgNavDir::Right;
        m_fields->m_currentThumbnailIndex = (m_fields->m_currentThumbnailIndex + 1) % static_cast<int>(m_fields->m_thumbnails.size());
        this->loadThumbnail(m_fields->m_currentThumbnailIndex);
    }
    
    void loadThumbnail(int index) {
        if (index < 0 || index >= m_fields->m_thumbnails.size()) return;
        
        auto& thumb = m_fields->m_thumbnails[index];
        int requestToken = ++m_fields->m_bgRequestToken;
        log::info("[LevelInfoLayer] loadThumbnail: index={}/{} thumbId={} token={}", index, m_fields->m_thumbnails.size(), thumb.id, requestToken);

        // Detener video anterior si existe
        stopVideoBackgroundSprite();

        // Si el thumbnail es video, descargar y reproducir con VideoThumbnailSprite
        if (thumb.isVideo() && !thumb.url.empty()) {
            log::info("[LevelInfoLayer] loadThumbnail: video detected for index={}", index);
            int32_t levelID = m_level ? m_level->m_levelID.value() : 0;
            std::string cacheKey = fmt::format("levelinfo_video_{}_{}", levelID, index);
            Ref<LevelInfoLayer> safeRef = this;
            VideoThumbnailSprite::createAsync(thumb.url, cacheKey, [safeRef, index, requestToken, levelID](VideoThumbnailSprite* videoSprite) {
                auto* self = static_cast<PaimonLevelInfoLayer*>(safeRef.data());
                if (!self) return;
                if (self->m_fields->m_bgRequestToken != requestToken) return;

                if (!videoSprite) {
                    log::warn("[LevelInfoLayer] loadThumbnail: video creation failed for index={}, falling back", index);
                    // Fallback al proximo thumbnail
                    if (self->m_fields->m_thumbnails.size() > 1) {
                        int next = (index + 1) % static_cast<int>(self->m_fields->m_thumbnails.size());
                        if (next != index) self->loadThumbnail(next);
                    } else {
                        self->m_fields->m_initLoadState = Fields::InitLoadState::Idle;
                    }
                    return;
                }

                self->queueVideoBackgroundSprite(videoSprite, levelID, requestToken);
                log::info("[LevelInfoLayer] loadThumbnail: waiting for first visible video frame for index={}", index);
            });
            return;
        }

        // version URL con _pv=<thumbId> para invalidacion de CDN
        std::string url = thumb.url;
        if (!thumb.id.empty()) {
            auto sep = (url.find('?') == std::string::npos) ? "?" : "&";
            url += fmt::format("{}_pv={}", sep, thumb.id);
        }
        Ref<LevelInfoLayer> safeRef = this;
        ThumbnailLoader::get().requestUrlLoad(url, [safeRef, index, requestToken](CCTexture2D* tex, bool success) {
            auto* self = static_cast<PaimonLevelInfoLayer*>(safeRef.data());
            if (!self) return;
            if (self->m_fields->m_bgRequestToken != requestToken) return;
            if (success && tex) {
                log::info("[LevelInfoLayer] loadThumbnail callback: index={} OK", index);
                int32_t levelID = self->m_level ? self->m_level->m_levelID.value() : 0;
                self->applyThumbnailBackground(tex, levelID);
                // Iniciar carga lazy de thumbnails restantes (uno por uno en background)
                if (index == 0 && self->m_fields->m_thumbnails.size() > 1) {
                    self->m_fields->m_lazyLoadIndex = 1;
                    self->loadNextThumbnailInBackground(0.0f);
                }
            } else {
                // URL download failed — try local cache fallback before giving up
                int32_t fallbackLevelID = self->m_level ? self->m_level->m_levelID.value() : 0;
                bool fallbackApplied = false;

                // 1. Try LocalThumbs (on-disk thumbnails)
                if (fallbackLevelID > 0) {
                    auto localPath = LocalThumbs::get().findAnyThumbnail(fallbackLevelID);
                    if (localPath) {
                        auto lowerPath = geode::utils::string::toLower(*localPath);
                        if (!lowerPath.ends_with(".mp4")) {
                            auto localTex = LocalThumbs::get().loadTexture(fallbackLevelID);
                            if (localTex) {
                                log::info("[LevelInfoLayer] loadThumbnail: local cache fallback hit for levelID={}", fallbackLevelID);
                                self->applyThumbnailBackground(localTex, fallbackLevelID);
                                fallbackApplied = true;
                            }
                        }
                    }
                }

                // 2. Try ThumbnailLoader RAM cache (level-based key)
                if (!fallbackApplied && fallbackLevelID > 0) {
                    auto& cache = paimon::cache::ThumbnailCache::get();
                    auto ramTex = cache.getFromRam(fallbackLevelID, false);
                    if (!ramTex.has_value()) ramTex = cache.getFromRam(fallbackLevelID, true);
                    if (ramTex.has_value() && ramTex.value()) {
                        log::info("[LevelInfoLayer] loadThumbnail: RAM cache fallback hit for levelID={}", fallbackLevelID);
                        self->applyThumbnailBackground(ramTex.value(), fallbackLevelID);
                        fallbackApplied = true;
                    }
                }

                if (!fallbackApplied) {
                    if (self->m_fields->m_thumbnails.size() > 1) {
                        log::warn("[LevelInfoLayer] loadThumbnail callback: index={} FAILED, trying next", index);
                        int next = (index + 1) % static_cast<int>(self->m_fields->m_thumbnails.size());
                        if (next != index) self->loadThumbnail(next);
                    } else {
                        self->m_fields->m_initLoadState = Fields::InitLoadState::Idle;
                    }
                }
            }
        });
    }

    // Carga thumbnails restantes en background, uno por uno
    void loadNextThumbnailInBackground(float /*dt*/) {
        auto total = static_cast<int>(m_fields->m_thumbnails.size());
        if (total <= 1) return;
        if (m_fields->m_lazyLoadIndex >= total) return;
        if (m_fields->m_lazyLoadScheduled) return;

        int index = m_fields->m_lazyLoadIndex;
        auto& thumb = m_fields->m_thumbnails[index];
        if (thumb.isVideo()) {
            // Skip videos in lazy load, move to next
            m_fields->m_lazyLoadIndex++;
            return;
        }

        m_fields->m_lazyLoadScheduled = true;
        std::string purl = thumb.url;
        if (!thumb.id.empty()) {
            auto sep = (purl.find('?') == std::string::npos) ? "?" : "&";
            purl += fmt::format("{}_pv={}", sep, thumb.id);
        }

        Ref<LevelInfoLayer> safeRef = this;
        ThumbnailLoader::get().requestUrlLoad(purl, [safeRef, index](CCTexture2D* tex, bool success) {
            auto* self = static_cast<PaimonLevelInfoLayer*>(safeRef.data());
            if (!self) return;
            self->m_fields->m_lazyLoadScheduled = false;
            if (success) {
                log::info("[LevelInfoLayer] lazyLoad: index={} loaded", index);
            }
            // Always move to next, regardless of success/failure
            self->m_fields->m_lazyLoadIndex++;
            // Schedule next load with small delay to avoid flooding
            if (self->m_fields->m_lazyLoadIndex < static_cast<int>(self->m_fields->m_thumbnails.size())) {
                self->scheduleOnce(schedule_selector(PaimonLevelInfoLayer::loadNextThumbnailInBackground), 0.1f);
            }
        }, 0); // Low priority for background loading
    }
};

// implementacion de onSettings (necesita PaimonLevelInfoLayer ya definido)
void LocalThumbnailViewPopup::onSettings(CCObject*) {
    auto popup = ThumbnailSettingsPopup::create();
    if (!popup) return;

    // Ref<> mantiene la textura viva mientras el callback exista (copia shared, no raw ptr)
    geode::Ref<CCTexture2D> texRef = m_thumbnailTexture;
    int32_t levelID = m_levelID;

    popup->setOnSettingsChanged([texRef, levelID]() {
        log::info("[ThumbnailViewPopup] Settings changed, refrescando fondo");
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;

        auto layer = scene->getChildByType<LevelInfoLayer>(0);
        if (!layer) return;

        // quitar fondos viejos por node ID (seguro)
        if (auto old = layer->getChildByID("paimon-levelinfo-pixel-bg"_spr)) {
            old->removeFromParent();
        }
        // overlay de oscuridad se mantiene persistente — refreshDarknessOverlay lo actualiza

        auto paimon = static_cast<PaimonLevelInfoLayer*>(layer);

        // limpiar extra sprites que applyThumbnailBackground no podria alcanzar
        for (auto& s : paimon->m_fields->m_extraBgSprites) {
            if (s && s->getParent()) s->removeFromParent();
        }
        paimon->m_fields->m_extraBgSprites.clear();

        // resetear la ref interna para evitar double-remove dentro de applyThumbnailBackground
        paimon->m_fields->m_pixelBg = nullptr;

        // forzar refresh inmediato de opacidad para feedback instantaneo
        paimon->m_fields->m_lastDarkness = -1;
        paimon->refreshDarknessOverlay(0.0f);

        // re-aplicar con las nuevas settings
        if (texRef) {
            // Refrescar cached settings antes de re-aplicar
            paimon->m_fields->m_cachedBgStyle = Mod::get()->getSettingValue<std::string>("levelinfo-background-style");
            paimon->m_fields->m_cachedEffectIntensity = std::clamp(
                static_cast<int>(Mod::get()->getSettingValue<int64_t>("levelinfo-effect-intensity")), 1, 10);
            paimon->m_fields->m_cachedExtraStyles = Mod::get()->getSettingValue<std::string>("levelinfo-extra-styles");
            paimon->m_fields->m_cachedAutoCycle = Mod::get()->getSettingValue<bool>("levelcell-gallery-autocycle");
            paimon->applyThumbnailBackground(texRef, levelID);
        }
    });
    popup->show();
}
