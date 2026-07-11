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
#include "../core/RuntimeLifecycle.hpp"
#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/LevelMetadata.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/thumbnails/services/ThumbnailCache.hpp"
#include "../features/thumbnails/services/ThumbnailTransportClient.hpp"
#include "../features/audio/services/AudioContextCoordinator.hpp"
#include "../features/dynamic-songs/services/DynamicSongManager.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../utils/VideoThumbnailSprite.hpp"
#include "../features/profiles/ui/RatePopup.hpp"

#include "../utils/Localization.hpp"
#include "../utils/ImageConverter.hpp"
#include "../utils/HttpClient.hpp"
#include "../utils/BetaUploadWarning.hpp"
#include "../features/foryou/services/ForYouTracker.hpp"

#include "../features/main-menu-layout/ui/MainMenuLayoutEditor.hpp"
#include "../features/main-menu-layout/services/MainMenuLayoutManager.hpp"
#include "../features/moderation/ui/SetDailyWeeklyPopup.hpp"
#include "../framework/state/SessionState.hpp"

#include "../utils/Shaders.hpp"
#include "../utils/GLSLLoader.hpp"
#include "../blur/BlurSystem.hpp"
#include "../utils/MainThreadDelay.hpp"
#include "../features/audio/services/PaimonAudio.hpp"
#include "../framework/EventBus.hpp"
#include "../framework/ModEvents.hpp"
#include "LevelInfoOverlayPause.hpp"

using namespace geode::prelude;

namespace {
// Active LevelInfoLayer for overlay pause (InfoLayer comments, etc.).
// WeakRef so a destroyed layer never keeps a dangling pause target.
geode::WeakRef<LevelInfoLayer> s_activeLevelInfoForOverlay;
int s_levelInfoOverlayPauseDepth = 0;
}
using namespace Shaders;

#include "../features/thumbnails/ui/LocalThumbnailViewPopup.hpp"
#include "../features/thumbnails/ui/ThumbnailSettingsPopup.hpp"
#include "../features/backgrounds/services/LevelInfoBgHelpers.hpp"
#include "../features/thumbnails/services/LevelInfoThumbnailBg.hpp"
#include "../framework/HookConventions.hpp"
using namespace paimon::levelinfo;

// Applies a single-pass blur shader to a background sprite, supporting both
// AnimatedGIFSprite (via m_intensity/m_screenSize/m_texSize fields) and a normal
// CCSprite (via u_intensity + u_texSize/u_screenSize fallback uniforms). Shared
// by the GIF branch of the "blur" and "paimonblur" styles (identical except the
// source shader).
namespace {
void applyGifBlurShader(CCSprite* sprite, CCGLProgram* shader, int intensity, cocos2d::CCSize win) {
    if (!sprite || !shader) return;
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
        GLint szLoc = shader->getUniformLocationForName("u_texSize");
        if (szLoc == -1) szLoc = shader->getUniformLocationForName("u_screenSize");
        if (szLoc != -1) shader->setUniformLocationWith2f(szLoc, win.width, win.height);
    }
}
} // namespace

// (Z-orders + makeLevelInfoBlurCacheKey moved to
//  features/backgrounds/services/LevelInfoBgHelpers.hpp)

class $modify(PaimonLevelInfoLayer, LevelInfoLayer) {
    CCMenu* findLeftSideMenu() {
        if (auto byId = typeinfo_cast<CCMenu*>(this->getChildByID("left-side-menu"))) {
            return byId;
        }
        // Robust fallback: find a CCMenu in the left half containing at least one
        // known LevelInfoLayer button. Without that check, other mods (BetterInfo,
        // etc.) could leave their own menus there and we'd claim them by mistake.
        static char const* const kKnownButtonIDs[] = {
            "like-button", "info-button", "rate-button",
            "high-object-button", "leaderboard-button", nullptr
        };
        if (auto children = this->getChildren()) {
            CCMenu* candidate = nullptr;
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                auto* menu = typeinfo_cast<CCMenu*>(child);
                if (!menu) continue;
                if (menu->getPositionX() >= this->getContentSize().width * 0.5f) continue;

                bool hasKnown = false;
                for (auto const* const* p = kKnownButtonIDs; *p != nullptr; ++p) {
                    if (menu->getChildByID(*p)) { hasKnown = true; break; }
                }
                if (hasKnown) {
                    return menu;
                }
                if (!candidate) candidate = menu;
            }
            // Only return a candidate without known buttons if it has several
            // children (heuristic: GD's menu usually has the most buttons).
            if (candidate && candidate->getChildrenCount() >= 3) {
                return candidate;
            }
        }
        return nullptr;
    }

    void applyLayoutsToEditableMenus() {
        paimon::menu_layout::MainMenuLayoutManager::get().captureDefaultsAndApply(this);
    }

    void createUtilityButtons(CCMenu* leftMenu) {
        if (!leftMenu) return;

        if (!m_fields->m_editModeBtn) {
            CCSprite* editIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn_001.png");
            if (!editIcon) editIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn02_001.png");
            if (!editIcon) editIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plusBtn_001.png");

            if (editIcon) {
                editIcon->setScale(0.8f);
                auto editSprite = CircleButtonSprite::create(
                    editIcon,
                    CircleBaseColor::Green,
                    CircleBaseSize::Medium
                );
                if (editSprite) {
                    auto btn = CCMenuItemSpriteExtra::create(
                        editSprite,
                        this,
                        menu_selector(PaimonLevelInfoLayer::onToggleEditMode)
                    );
                    if (btn) {
                        btn->setID("levelinfo-layout-editor-button"_spr);
                        PaimonButtonHighlighter::registerButton(btn);
                        leftMenu->addChild(btn);
                        m_fields->m_editModeBtn = btn;
                    }
                }
            }
        }

        if (!m_fields->m_uploadLocalBtn) {
            CCSprite* uploadIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_downloadBtn_001.png");
            if (!uploadIcon) uploadIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_downloadsIcon_001.png");
            if (!uploadIcon) uploadIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plusBtn_001.png");

            if (uploadIcon) {
                uploadIcon->setRotation(180.f);
                uploadIcon->setScale(0.8f);
                auto uploadSprite = CircleButtonSprite::create(
                    uploadIcon,
                    CircleBaseColor::Green,
                    CircleBaseSize::Medium
                );
                if (uploadSprite) {
                    auto btn = CCMenuItemSpriteExtra::create(
                        uploadSprite,
                        this,
                        menu_selector(PaimonLevelInfoLayer::onUploadLocalThumbnail)
                    );
                    if (btn) {
                        btn->setID("levelinfo-upload-local-button"_spr);
                        PaimonButtonHighlighter::registerButton(btn);
                        leftMenu->addChild(btn);
                        m_fields->m_uploadLocalBtn = btn;
                    }
                }
            }
        }

        leftMenu->updateLayout();
    }

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "LevelInfoLayer::init");
    }

    struct Fields {
        Ref<CCMenuItemSpriteExtra> m_thumbnailButton = nullptr;
        Ref<CCNode> m_pixelBg = nullptr;
        Ref<CCLayerColor> m_pixelOverlay = nullptr;
        std::vector<Ref<CCSprite>> m_extraBgSprites;
        struct ExtraUniformCache { GLint time = -2, cursor = -2, click = -2; };
        std::vector<ExtraUniformCache> m_extraUniformsCache;
        float m_shaderTime = 0.0f;
        bool m_animatedShader = false;
        bool m_fromThumbsList = false;
        bool m_fromReportSection = false;
        bool m_fromVerificationQueue = false;
        bool m_fromLeaderboards = false;
        LeaderboardType m_leaderboardType = LeaderboardType::Default;
        // Daily/weekly ID captured before the forced re-download, to restore it
        // if the download (normal-level endpoint) clears it.
        int m_forcedDailyID = 0;
        LeaderboardStat m_leaderboardStat = LeaderboardStat::Stars;
        Ref<CCMenuItemSpriteExtra> m_acceptThumbBtn = nullptr;
        Ref<CCMenuItemSpriteExtra> m_editModeBtn = nullptr;
        Ref<CCMenuItemSpriteExtra> m_uploadLocalBtn = nullptr;
        Ref<CCMenu> m_extraMenu = nullptr;
        bool m_thumbnailRequested = false; // avoids duplicate loads
        int m_loadedInvalidationVersion = 0; // invalidation version to detect changes
        
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
        int m_bgGuardTicks = 0; // background-visibility guard ticks
        int m_lazyLoadIndex = 1; // next thumbnail to load in the background
        int m_fallbackOrigin = -1; // index where loadThumbnail's fallback chain started (-1 = none); avoids an infinite async loop if all fail
        bool m_lazyLoadScheduled = false;
        int m_invalidationListenerId = 0;
        bool m_audioDeactivated = false;
        Ref<VideoThumbnailSprite> m_videoSprite = nullptr;
        // Favorite buttons
        Ref<CCMenuItemSpriteExtra> m_favCreatorBtn = nullptr;
        Ref<CCMenuItemSpriteExtra> m_favLevelBtn = nullptr;
        int m_lastDarkness = -1; // cached darkness setting to skip redundant reads
        Ref<CCLayerColor> m_extraDarknessLayer = nullptr; // separate +0.1 extra darkness
        enum class BgNavDir : uint8_t { None, Left, Right };
        BgNavDir m_bgNavDirection = BgNavDir::Right; // direction for bg transition
        // PaimonAudio reactive darkness
        bool m_paimonAudioActive = false;
        int m_paimonAudioBaseDarkness = 0;
        // init->onEnter load state: avoids races and double-load
        enum class InitLoadState : uint8_t { Idle, Pending, Applying };
        InitLoadState m_initLoadState = InitLoadState::Idle;
        // Cached settings — leidos una vez en init(), refrescados en onSettingsChanged
        std::string m_cachedBgStyle;
        int m_cachedEffectIntensity = 5;
        std::string m_cachedExtraStyles;
        bool m_cachedAutoCycle = false;
        int m_loadedSettingsVersion = 0; // invalidated by listenForSettingChanges
        // m_activeBlurJob removed — replaced by BlurSystem (synchronous FBO-based Dual Kawase)
        // Dynamic Shaders — cursor/touch tracking
        bool m_dynamicShaders = false;
        float m_cursorX = 0.5f; // normalized 0..1 (smoothed/delayed)
        float m_cursorY = 0.5f; // normalized 0..1 (smoothed/delayed)
        float m_targetCursorX = 0.5f; // raw target position
        float m_targetCursorY = 0.5f; // raw target position
        float m_dynamicShadersDelay = 0.0f; // 0..2 seconds
        bool m_touchActive = false; // guards against multiple touches on mobile
        float m_clickState = 0.0f; // animated: lerps toward m_targetClickState
        float m_targetClickState = 0.0f; // 1.0 when pressed, 0.0 when released
        // Perf: cached uniform locations for updateShaderTime (avoid string lookups per frame)
        CCGLProgram* m_cachedMainShader = nullptr;
        GLint m_mainLocTime = -2;
        GLint m_mainLocCursor = -2;
        GLint m_mainLocClick = -2;
        // Snapshot while a comments/info overlay is open (see LevelInfoOverlayPause)
        bool m_overlayPaused = false;
        bool m_overlayHadGallery = false;
        bool m_overlayHadShader = false;
        bool m_overlayHadAudio = false;
        bool m_overlayHadCursor = false;
        bool m_overlayHadVideo = false;
    };

    void pauseHeavyWorkForOverlay() {
        if (m_fields->m_overlayPaused) return;
        m_fields->m_overlayPaused = true;

        // Snapshot desired work from fields (don't rely on isScheduled).
        m_fields->m_overlayHadGallery =
            m_fields->m_cachedAutoCycle && m_fields->m_cycling &&
            m_fields->m_thumbnails.size() > 1;
        m_fields->m_overlayHadShader = m_fields->m_animatedShader;
        m_fields->m_overlayHadAudio = m_fields->m_paimonAudioActive;
        m_fields->m_overlayHadCursor = m_fields->m_dynamicShaders;

        this->unschedule(schedule_selector(PaimonLevelInfoLayer::updateGallery));
        this->unschedule(schedule_selector(PaimonLevelInfoLayer::updateShaderTime));
        this->unschedule(schedule_selector(PaimonLevelInfoLayer::updatePaimonAudio));
        this->unschedule(schedule_selector(PaimonLevelInfoLayer::updateCursorFromMouse));
        this->unschedule(schedule_selector(PaimonLevelInfoLayer::loadNextThumbnailInBackground));
        m_fields->m_lazyLoadScheduled = false;

        // Freeze video decode under the popup (keep last frame visible).
        m_fields->m_overlayHadVideo = false;
        if (m_fields->m_videoSprite && m_fields->m_videoSprite->isPlaying()) {
            m_fields->m_overlayHadVideo = true;
            m_fields->m_videoSprite->pause();
        }

        // Freeze animated GIF backgrounds under the popup.
        if (auto* gif = typeinfo_cast<AnimatedGIFSprite*>(m_fields->m_pixelBg.data())) {
            gif->pause();
        }
        for (auto& s : m_fields->m_extraBgSprites) {
            if (auto* gif = typeinfo_cast<AnimatedGIFSprite*>(s.data())) {
                gif->pause();
            }
        }
    }

    void resumeHeavyWorkForOverlay() {
        if (!m_fields->m_overlayPaused) return;
        m_fields->m_overlayPaused = false;

        if (m_fields->m_overlayHadGallery && m_fields->m_thumbnails.size() > 1 &&
            m_fields->m_cachedAutoCycle && m_fields->m_cycling) {
            this->schedule(schedule_selector(PaimonLevelInfoLayer::updateGallery), 3.0f);
        }
        if (m_fields->m_overlayHadShader && m_fields->m_animatedShader) {
            this->schedule(schedule_selector(PaimonLevelInfoLayer::updateShaderTime));
        }
        if (m_fields->m_overlayHadAudio && m_fields->m_paimonAudioActive) {
            this->schedule(schedule_selector(PaimonLevelInfoLayer::updatePaimonAudio));
        }
        if (m_fields->m_overlayHadCursor && m_fields->m_dynamicShaders) {
            this->schedule(schedule_selector(PaimonLevelInfoLayer::updateCursorFromMouse));
        }
        if (m_fields->m_overlayHadVideo && m_fields->m_videoSprite) {
            m_fields->m_videoSprite->play();
        }
        if (auto* gif = typeinfo_cast<AnimatedGIFSprite*>(m_fields->m_pixelBg.data())) {
            gif->play();
        }
        for (auto& s : m_fields->m_extraBgSprites) {
            if (auto* gif = typeinfo_cast<AnimatedGIFSprite*>(s.data())) {
                gif->play();
            }
        }
    }

    int readDarknessSetting() const {
        return std::clamp(
            static_cast<int>(Mod::get()->getSavedValue<int>("levelinfo-bg-darkness", 27)),
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
    
    // Self-healing for an invisible background. The bg is installed at opacity(0)
    // and faded in; if that fade is interrupted (re-entry of
    // applyThumbnailBackground, the next apply's stopAllActions, a scene change
    // mid-transition, or token churn), the node stays in the tree at opacity 0 —
    // an "invisible background". This guard checks the sprite a few frames later
    // and, if it's still stuck near 0 with no action running, forces full opacity.
    void scheduleBackgroundVisibilityGuard() {
        // Cancel any previous guard and reschedule (each install resets it).
        this->unschedule(schedule_selector(PaimonLevelInfoLayer::backgroundVisibilityGuardTick));
        m_fields->m_bgGuardTicks = 0;
        // 0.5s after install: ample time for the fade (0.3-0.5s) to progress.
        // Retries a few times in case the first tick lands during the scene transition.
        this->schedule(schedule_selector(PaimonLevelInfoLayer::backgroundVisibilityGuardTick), 0.5f);
    }

    void backgroundVisibilityGuardTick(float) {
        auto* bg = m_fields->m_pixelBg.data();
        if (!bg || !bg->getParent()) {
            this->unschedule(schedule_selector(PaimonLevelInfoLayer::backgroundVisibilityGuardTick));
            return;
        }

        // If an action is running (fade/scale/move from the transition), the bg
        // is still animating to its final state; don't touch it.
        if (bg->numberOfRunningActions() > 0) {
            if (++m_fields->m_bgGuardTicks >= 4) {
                this->unschedule(schedule_selector(PaimonLevelInfoLayer::backgroundVisibilityGuardTick));
            }
            return;
        }

        // No actions running: the current state is final. If it's invisible
        // (opacity ~0 or not visible), it's an aborted fade; restore it.
        auto* rgba = typeinfo_cast<CCRGBAProtocol*>(bg);
        bool invisible = (!bg->isVisible()) || (rgba && rgba->getOpacity() < 8);
        bool collapsedScale = (bg->getScaleX() < 0.02f) || (bg->getScaleY() < 0.02f);

        if (invisible || collapsedScale) {
            log::warn("[LevelInfoLayer] background invisible tras transicion (opacity/scale ~0) — forzando visible");
            bg->setVisible(true);
            if (rgba) rgba->setOpacity(255);
            if (collapsedScale && bg->getContentSize().width > 0 && bg->getContentSize().height > 0) {
                auto win = CCDirector::get()->getWinSize();
                float cs = std::max(win.width / bg->getContentSize().width,
                                    win.height / bg->getContentSize().height);
                bg->setScale(cs);
                bg->setPosition({win.width / 2.0f, win.height / 2.0f});
            }
        }

        // Final state resolved (visible or corrected): stop watching.
        this->unschedule(schedule_selector(PaimonLevelInfoLayer::backgroundVisibilityGuardTick));
    }

    void applyThumbnailBackground(CCTexture2D* tex, int32_t levelID) {
        if (!tex) return;

        // Save the current texture so InfoLayer can read it when opened
        paimon::ThumbnailBackgroundChangedEvent::s_lastLevelID = levelID;
        paimon::ThumbnailBackgroundChangedEvent::setLastTexture(tex);

        // Notify CustomSongWidget and InfoLayer to sync backgrounds
        auto subCount = paimon::EventBus::get().subscriberCount<paimon::ThumbnailBackgroundChangedEvent>();
        log::info("[LevelInfoLayer] publishing ThumbnailBackgroundChangedEvent levelID={} tex={} subscribers={}", levelID, (void*)tex, subCount);
        paimon::EventBus::get().publish(paimon::ThumbnailBackgroundChangedEvent{levelID, tex});

        // BlurSystem is synchronous — no progressive job to cancel

        // Initial load (init->requestLoad/refreshGalleryData) resolved
        m_fields->m_initLoadState = Fields::InitLoadState::Applying;
        m_fields->m_fallbackOrigin = -1; // success: the fallback chain finished cleanly

        log::info("[LevelInfoLayer] Aplicando fondo del thumbnail");
        
        // reset previous shader animation
        m_fields->m_animatedShader = false;
        m_fields->m_shaderTime = 0.0f;
        
        // Clear previous extra-effect sprites
        for (auto& s : m_fields->m_extraBgSprites) {
            if (s) s->removeFromParent();
        }
        m_fields->m_extraBgSprites.clear();
        m_fields->m_extraUniformsCache.clear();
        
        // Style and intensity (cached)
        auto bgStyle = m_fields->m_cachedBgStyle;
        int intensity = m_fields->m_cachedEffectIntensity;
        auto win = CCDirector::get()->getWinSize();

        // Style -> shader/flags mapping table
        struct ShaderEntry {
            char const* name; char const* key; char const* glslFile;
            bool boosted; bool screenSize; bool time;
            bool dynamicOnly; // solo disponible en modo dynamic shaders
        };
        static ShaderEntry const kShaderTable[] = {
            {"grayscale",       "grayscale"_spr,       "grayscale.glsl",       false, false, false, false},
            {"sepia",           "sepia"_spr,           "sepia.glsl",           false, false, false, false},
            {"vignette",        "vignette"_spr,        "vignette.glsl",        false, false, false, false},
            {"scanlines",       "scanlines"_spr,       "scanlines.glsl",       false, true,  false, false},
            {"bloom",           "bloom"_spr,           "bloom.glsl",            true, true,  false, false},
            {"chromatic",       "chromatic-v2"_spr,    "chromatic.glsl",        true, false, true,  false},
            {"radial-blur",     "radial-blur-v2"_spr,  "radial_blur.glsl",     true, false, true,  false},
            {"glitch",          "glitch-v2"_spr,       "glitch.glsl",           true, false, true,  false},
            {"posterize",       "posterize"_spr,       "posterize.glsl",       false, false, false, false},
            {"rain",            "rain"_spr,            "rain.glsl",             true, false, true,  false},
            {"matrix",          "matrix"_spr,          "matrix.glsl",           true, false, true,  false},
            {"neon-pulse",      "neon-pulse"_spr,      "neon_pulse.glsl",       true, false, true,  false},
            {"wave-distortion", "wave-distortion"_spr, "wave_distortion.glsl",  true, false, true,  false},
            {"crt",             "crt"_spr,             "crt.glsl",              true, false, true,  false},
            // Dynamic-only shaders (only available with Dynamic Shaders ON)
            {"shockwave",       "shockwave-dyn"_spr,   "shockwave_dynamic.glsl",       true, false, true, true},
            {"vortex",          "vortex-dyn"_spr,      "vortex_dynamic.glsl",          true, false, true, true},
            {"magnetic",        "magnetic-dyn"_spr,    "magnetic_dynamic.glsl",         true, false, true, true},
            {"spotlight",       "spotlight-dyn"_spr,   "spotlight_dynamic.glsl",        true, false, true, true},
            {"ripple",          "ripple-dyn"_spr,      "ripple_dynamic.glsl",           true, false, true, true},
            {"plasma-cursor",   "plasma-cursor-dyn"_spr, "plasma_cursor_dynamic.glsl",  true, false, true, true},
            {"freeze",          "freeze-dyn"_spr,      "freeze_dynamic.glsl",           true, false, true, true},
            {"pixelate-cursor", "pixelate-cursor-dyn"_spr, "pixelate_cursor_dynamic.glsl", true, false, true, true},
            {"kaleidoscope",    "kaleidoscope-dyn"_spr,    "kaleidoscope_dynamic.glsl",    true, false, true, true},
            {"sonar",           "sonar-dyn"_spr,           "sonar_dynamic.glsl",           true, false, true, true},
            {"electric-arc",    "electric-arc-dyn"_spr,    "electric_arc_dynamic.glsl",    true, false, true, true},
            {"prism-split",     "prism-split-dyn"_spr,     "prism_split_dynamic.glsl",     true, false, true, true},
            {"gravity-well",    "gravity-well-dyn"_spr,    "gravity_well_dynamic.glsl",    true, false, true, true},
            {"shatter",         "shatter-dyn"_spr,         "shatter_dynamic.glsl",         true, false, true, true},
            {"heat-haze",       "heat-haze-dyn"_spr,       "heat_haze_dynamic.glsl",       true, false, true, true},
            {"liquify",         "liquify-dyn"_spr,         "liquify_dynamic.glsl",         true, false, true, true},
            {"ink-spread",      "ink-spread-dyn"_spr,      "ink_spread_dynamic.glsl",      true, false, true, true},
            {"hologram",        "hologram-dyn"_spr,        "hologram_dynamic.glsl",        true, false, true, true},
            {"time-warp",       "time-warp-dyn"_spr,       "time_warp_dynamic.glsl",       true, false, true, true},
            {"underwater",      "underwater-dyn"_spr,      "underwater_dynamic.glsl",      true, false, true, true},
            {"neon-trail",      "neon-trail-dyn"_spr,      "neon_trail_dynamic.glsl",      true, false, true, true},
        };

        // Look up shader by name. Capture `intensity` and `this` BY VALUE: this
        // lambda is copied into applyEffects/installBackgroundSprite and invoked
        // from the async GIF-decode callback, which runs after this function has
        // returned. A `[&]` capture would leave a dangling reference to the stack
        // local `intensity` (use-after-scope) feeding garbage into the shader uniform.
        auto lookupShader = [this, intensity](std::string const& style) -> std::tuple<CCGLProgram*, float, bool, bool> {
            // Dynamic shaders: if enabled and the shader supports cursor, use the _dynamic variant
            bool useDynamic = m_fields->m_dynamicShaders;
            for (auto& e : kShaderTable) {
                if (style == e.name) {
                    // Skip dynamic-only shaders when dynamic mode is off
                    if (e.dynamicOnly && !useDynamic) {
                        return {nullptr, 0.f, false, false};
                    }
                    float v = e.boosted ? (intensity / 10.0f) * 2.25f : intensity / 10.0f;
                    // Dynamic-only shaders: load their file directly (already _dynamic.glsl)
                    if (e.dynamicOnly) {
                        return {paimon::shaders::loadShader(e.key, "cell_vertex.glsl", e.glslFile, nullptr, nullptr), v, e.screenSize, e.time};
                    }
                    // Only animated shaders (time=true) have a dynamic variant
                    if (useDynamic && e.time) {
                        // Build the dynamic filename: "chromatic.glsl" -> "chromatic_dynamic.glsl"
                        std::string dynFile = std::string(e.glslFile);
                        auto dotPos = dynFile.rfind('.');
                        if (dotPos != std::string::npos) {
                            dynFile.insert(dotPos, "_dynamic");
                        }
                        std::string dynKey = std::string(e.key) + "-dyn";
                        auto* shader = paimon::shaders::loadShader(dynKey.c_str(), "cell_vertex.glsl", dynFile.c_str(), nullptr, nullptr);
                        if (shader) {
                            return {shader, v, e.screenSize, e.time};
                        }
                    }
                    return {paimon::shaders::loadShader(e.key, "cell_vertex.glsl", e.glslFile, nullptr, nullptr), v, e.screenSize, e.time};
                }
            }
            return {nullptr, 0.f, false, false};
        };

        // Effects lambda
        auto applyEffects = [this, bgStyle, intensity, win, tex, lookupShader](CCSprite*& sprite, bool isGIF) {
            if (!sprite) return;

            // "cover" scale + centering, reused by the initial sprite and the
            // regenerated sprites of the non-GIF blur/paimonblur branches.
            auto fitCover = [win](CCSprite* s) {
                if (!s) return;
                float cs = std::max(win.width / s->getContentSize().width,
                                    win.height / s->getContentSize().height);
                s->setScale(cs);
                s->setAnchorPoint({0.5f, 0.5f});
                s->setPosition({win.width / 2.0f, win.height / 2.0f});
            };

            // Initial scale and position
            fitCover(sprite);

            if (bgStyle == "normal") {
                ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
                sprite->getTexture()->setTexParameters(&params);
            }
            else if (bgStyle == "pixel") {
                if (isGIF) {
                     auto shader = paimon::shaders::loadShader("pixelate"_spr, "cell_vertex.glsl", "pixelate.glsl", nullptr, nullptr);
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
                    auto renderTex = CCRenderTexture::create(
                        renderWidth, renderHeight,
                        kCCTexture2DPixelFormat_RGBA8888,
                        GL_DEPTH24_STENCIL8);
                    if (renderTex) {
                        float renderScale = std::min(
                            static_cast<float>(renderWidth) / tex->getContentSize().width,
                            static_cast<float>(renderHeight) / tex->getContentSize().height);
                        sprite->setScale(renderScale);
                        sprite->setPosition({renderWidth / 2.0f, renderHeight / 2.0f});
                        renderTex->beginWithClear(0.f, 0.f, 0.f, 0.f, 0.f, 0);
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
                     applyGifBlurShader(sprite, Shaders::getBlurSinglePassShader(), intensity, win);
                } else {
                    sprite = BlurSystem::getInstance()->createBlurredSprite(tex, win, static_cast<float>(intensity));
                    fitCover(sprite);
                }
            }
            else if (bgStyle == "paimonblur") {
                if (isGIF) {
                    applyGifBlurShader(sprite, Shaders::getPaimonBlurShader(), intensity, win);
                } else {
                    sprite = BlurSystem::getInstance()->createPaimonBlurSprite(tex, win, static_cast<float>(intensity));
                    fitCover(sprite);
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
                            // Dynamic shaders: set initial cursor position and click state
                            if (m_fields->m_dynamicShaders) {
                                GLint cursorLoc = shader->getUniformLocationForName("u_cursor");
                                if (cursorLoc >= 0) {
                                    shader->setUniformLocationWith2f(cursorLoc, m_fields->m_cursorX, m_fields->m_cursorY);
                                }
                                GLint clickLoc = shader->getUniformLocationForName("u_click");
                                if (clickLoc >= 0) {
                                    shader->setUniformLocationWith1f(clickLoc, 0.0f);
                                }
                            }
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
            this->unschedule(schedule_selector(PaimonLevelInfoLayer::updateCursorFromMouse));

            if (m_fields->m_animatedShader && m_fields->m_pixelBg) {
                this->schedule(schedule_selector(PaimonLevelInfoLayer::updateShaderTime));
            }

            // Dynamic shaders: schedule mouse tracking on desktop
#ifdef GEODE_IS_WINDOWS
            if (m_fields->m_dynamicShaders && m_fields->m_animatedShader && m_fields->m_pixelBg) {
                this->schedule(schedule_selector(PaimonLevelInfoLayer::updateCursorFromMouse));
            }
#endif

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

            // Hide GD's native background now that we have a real thumbnail
            if (auto vanillaBg = this->getChildByID("background")) {
                vanillaBg->setVisible(false);
            }
            // Hide brown decorative objects (corners / top strip)
            for (char const* artId : {
                "bottom-left-art", "bottom-right-art",
                "top-left-art", "top-right-art"
            }) {
                if (auto* art = this->getChildByID(artId)) art->setVisible(false);
            }

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

            // Stop the previous sprite's actions to avoid fade conflicts
            if (oldBg) oldBg->stopAllActions();

            // Stop the overlay's actions so a previous animation doesn't leave an
            // inconsistent opacity during the transition
            if (m_fields->m_pixelOverlay) m_fields->m_pixelOverlay->stopAllActions();

            preparedSprite->setID("paimon-levelinfo-pixel-bg"_spr);
            this->addChild(preparedSprite, kBackgroundZOrder);
            m_fields->m_pixelBg = preparedSprite;

            // Create/update the extra darkness layer (+0.1) as a node separate from the bg
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

            // Ensure the darkness overlay exists and sits above the background
            if (m_fields->m_pixelOverlay && m_fields->m_pixelOverlay->getParent()) {
                m_fields->m_pixelOverlay->setZOrder(kOverlayZOrder);
            }

            this->applyDarknessSetting(this->getAppliedDarknessSetting(), true);

            // Apply configurable background transition
            {
                std::string bgStyle = Mod::get()->getSavedValue<std::string>("levelinfo-bg-transition", "crossfade");
                float dur = Mod::get()->getSavedValue<float>("levelinfo-bg-transition-duration", 0.5f);
                auto win = CCDirector::get()->getWinSize();
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
                            CCRemoveSelf::create(),
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
                            CCRemoveSelf::create(),
                            nullptr));
                        preparedSprite->setPosition({targetPos.x + screenW, targetPos.y});
                        preparedSprite->setOpacity(255);
                        preparedSprite->runAction(CCEaseElasticOut::create(
                            CCMoveTo::create(dur * 1.2f, targetPos), 0.3f));

                    } else if (bgStyle == "slide-left") {
                        oldPtr->runAction(CCSequence::create(
                            CCEaseIn::create(CCMoveTo::create(dur, {targetPos.x - screenW, targetPos.y}), 2.0f),
                            CCRemoveSelf::create(),
                            nullptr));
                        preparedSprite->setPosition({targetPos.x + screenW, targetPos.y});
                        preparedSprite->setOpacity(255);
                        preparedSprite->runAction(CCEaseOut::create(CCMoveTo::create(dur, targetPos), 2.5f));

                    } else if (bgStyle == "slide-right") {
                        oldPtr->runAction(CCSequence::create(
                            CCEaseIn::create(CCMoveTo::create(dur, {targetPos.x + screenW, targetPos.y}), 2.0f),
                            CCRemoveSelf::create(),
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
                            CCRemoveSelf::create(),
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
                            CCRemoveSelf::create(),
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
                            CCRemoveSelf::create(),
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
                            CCRemoveSelf::create(),
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
                            CCRemoveSelf::create(),
                            nullptr));

                    } else if (bgStyle == "wave-slide") {
                        bool slideFromRight = goRight || (!goLeft);
                        float startX = slideFromRight ? (targetPos.x + screenW) : (targetPos.x - screenW);
                        float startRot = slideFromRight ? 12.f : -12.f;
                        
                        preparedSprite->setPosition({startX, targetPos.y});
                        preparedSprite->setScaleX(sx * 0.8f);
                        preparedSprite->setScaleY(sy * 0.8f);
                        preparedSprite->setRotation(startRot);
                        preparedSprite->setOpacity(0);
                        
                        preparedSprite->runAction(CCSpawn::create(
                            CCEaseElasticOut::create(CCMoveTo::create(dur * 1.2f, targetPos), 0.45f),
                            CCEaseBackOut::create(CCRotateTo::create(dur * 1.0f, 0.f)),
                            CCEaseOut::create(CCScaleTo::create(dur * 0.8f, sx, sy), 2.0f),
                            CCFadeTo::create(dur * 0.6f, 255),
                            nullptr
                        ));
                        
                        float oldTargetX = slideFromRight ? (targetPos.x - screenW) : (targetPos.x + screenW);
                        float oldTargetRot = slideFromRight ? -12.f : 12.f;
                        oldPtr->runAction(CCSequence::create(
                            CCSpawn::create(
                                CCEaseIn::create(CCMoveTo::create(dur * 0.8f, {oldTargetX, targetPos.y}), 2.0f),
                                CCEaseIn::create(CCRotateTo::create(dur * 0.8f, oldTargetRot), 2.0f),
                                CCEaseIn::create(CCScaleTo::create(dur * 0.8f, sx * 0.8f, sy * 0.8f), 2.0f),
                                CCFadeTo::create(dur * 0.6f, 0),
                                nullptr
                            ),
                            CCRemoveSelf::create(),
                            nullptr
                        ));

                    } else if (bgStyle == "card-flip") {
                        preparedSprite->setScaleX(0.01f);
                        preparedSprite->setOpacity(0);
                        
                        preparedSprite->runAction(CCSequence::create(
                            CCDelayTime::create(dur * 0.35f),
                            CCSpawn::create(
                                CCEaseElasticOut::create(CCScaleTo::create(dur * 0.85f, sx, sy), 0.5f),
                                CCFadeTo::create(dur * 0.4f, 255),
                                nullptr
                            ),
                            nullptr
                        ));
                        
                        oldPtr->runAction(CCSequence::create(
                            CCSpawn::create(
                                CCEaseIn::create(CCScaleTo::create(dur * 0.45f, 0.01f, sy), 2.0f),
                                CCFadeTo::create(dur * 0.4f, 0),
                                nullptr
                            ),
                            CCRemoveSelf::create(),
                            nullptr
                        ));

                    } else if (bgStyle == "spin-zoom") {
                        bool slideFromRight = goRight || (!goLeft);
                        preparedSprite->setScaleX(0.01f);
                        preparedSprite->setScaleY(0.01f);
                        preparedSprite->setRotation(slideFromRight ? -180.f : 180.f);
                        preparedSprite->setOpacity(0);
                        
                        preparedSprite->runAction(CCSpawn::create(
                            CCEaseElasticOut::create(CCScaleTo::create(dur * 1.2f, sx, sy), 0.4f),
                            CCEaseElasticOut::create(CCRotateTo::create(dur * 1.2f, 0.f), 0.4f),
                            CCFadeTo::create(dur * 0.6f, 255),
                            nullptr
                        ));
                        
                        oldPtr->runAction(CCSequence::create(
                            CCSpawn::create(
                                CCEaseIn::create(CCScaleTo::create(dur * 0.7f, 0.01f, 0.01f), 2.0f),
                                CCEaseIn::create(CCRotateBy::create(dur * 0.7f, slideFromRight ? 180.f : -180.f), 2.0f),
                                CCFadeTo::create(dur * 0.6f, 0),
                                nullptr
                            ),
                            CCRemoveSelf::create(),
                            nullptr
                        ));

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
                            CCRemoveSelf::create(),
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
                            CCRemoveSelf::create(),
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

            // Safety net: if the fade/transition is interrupted and leaves the bg
            // at opacity 0, this guard forces it visible a few frames later.
            this->scheduleBackgroundVisibilityGuard();
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
            // For blur/paimonblur, use ASYNC blur. Synchronous blur (4-16 FBO
            // passes in one frame) caused a noticeable freeze when opening a level
            // with a large thumbnail. Now the blur is spread over 2-3 frames on
            // desktop (no freeze), and an LRU cache hit shows it the same frame for free.
            if (!hasGifBackground && (bgStyle == "blur" || bgStyle == "paimonblur")) {
                auto win = CCDirector::get()->getWinSize();
                Ref<CCTexture2D> texRef = tex;
                Ref<LevelInfoLayer> selfRef = this;
                Ref<CCSprite> finalSpriteRef = finalSprite; // avoid autorelease
                int blurToken = m_fields->m_bgRequestToken;
                bool usePaimon = (bgStyle == "paimonblur");
                int levelIDForBlur = m_level ? m_level->m_levelID.value() : 0;
                auto blurCacheKey = makeLevelInfoBlurCacheKey(
                    levelIDForBlur,
                    m_fields->m_currentThumbnailIndex,
                    bgStyle,
                    intensity,
                    win
                );

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
                        installBackgroundSprite(blurredSprite, false, true); // skipEffects=true, already blurred
                    } else if (finalSpriteRef.data()) {
                        // Fallback without blur
                        installBackgroundSprite(finalSpriteRef.data(), false);
                    }
                };

                if (usePaimon) {
                    BlurSystem::getInstance()->buildPaimonBlurPriority(
                        tex, win, static_cast<float>(intensity), blurCacheKey, onBlurReady);
                } else {
                    BlurSystem::getInstance()->buildGaussianBlurPriority(
                        tex, win, static_cast<float>(intensity), blurCacheKey, onBlurReady);
                }
            } else {
                installBackgroundSprite(finalSprite, false);
            }
        }

        // Multi-effect: extra layers
        std::string extraStylesRaw = m_fields->m_cachedExtraStyles;
        if (!extraStylesRaw.empty() && tex) {
            // Parse comma-separated, max 4 extra
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
                extraSpr->setOpacity(180); // semi-transparent overlay

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

        // Darkness overlay is managed in init()
        
        log::info("[LevelInfoLayer] Fondo aplicado exitosamente (estilo: {}, intensidad: {})", bgStyle, intensity);
    }
    
    $override
    void onEnterTransitionDidFinish() {
        LevelInfoLayer::onEnterTransitionDidFinish();

        // Reset audio deactivation flag
        m_fields->m_audioDeactivated = false;

        // Hide vanilla background if a thumbnail exists
        if (m_fields->m_pixelBg) {
            if (auto vanillaBg = this->getChildByID("background")) vanillaBg->setVisible(false);
            for (char const* artId : {
                "bottom-left-art", "bottom-right-art",
                "top-left-art", "top-right-art"
            }) {
                if (auto* art = this->getChildByID(artId)) art->setVisible(false);
            }

            // The bg fade-in may have started during init() (before onEnter) and
            // been paused/lost by the scene transition, leaving the bg present but
            // invisible. Re-validate visibility now that the transition finished.
            this->scheduleBackgroundVisibilityGuard();
        }

        // Detect a thumbnail change from PauseLayer
        if (m_level && m_fields->m_thumbnailRequested) {
            int32_t levelID = m_level->m_levelID.value();
            int currentVersion = ThumbnailLoader::get().getInvalidationVersion(levelID);
            if (currentVersion != m_fields->m_loadedInvalidationVersion) {
                log::info("[LevelInfoLayer] onEnterTransitionDidFinish: thumbnail invalidated levelID={} ver {} -> {}", levelID, m_fields->m_loadedInvalidationVersion, currentVersion);
                m_fields->m_loadedInvalidationVersion = currentVersion;
                refreshGalleryData(levelID, true);
            } else if (!m_fields->m_pixelBg && m_fields->m_initLoadState == Fields::InitLoadState::Idle) {
                // Only re-fetch if there are no pending requests
                refreshGalleryData(levelID, true);
            }
        }

        // Recreate the darkness overlay if it was destroyed
        if (!m_fields->m_pixelOverlay || !m_fields->m_pixelOverlay->getParent()) {
            auto win = cocos2d::CCDirector::get()->getWinSize();
            auto overlay = cocos2d::CCLayerColor::create({0, 0, 0, 0});
            overlay->setAnchorPoint({0, 0});
            overlay->setPosition({0, 0});
            overlay->setContentSize(win);
            overlay->setID("paimon-levelinfo-pixel-overlay"_spr);
            this->addChild(overlay, kOverlayZOrder);
            m_fields->m_pixelOverlay = overlay;
        }

        // Re-read on enter to fix drift
        m_fields->m_lastDarkness = -1;
        this->unschedule(schedule_selector(PaimonLevelInfoLayer::refreshDarknessOverlay));
        refreshDarknessOverlay(0.f);
        this->schedule(schedule_selector(PaimonLevelInfoLayer::refreshDarknessOverlay), 0.5f);

        // Activate dynamic song
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
            // Read the override (savedValue) first; it has no one-of validation
            m_fields->m_cachedBgStyle = Mod::get()->getSavedValue<std::string>("levelinfo-background-style-override", "");
            if (m_fields->m_cachedBgStyle.empty()) {
                m_fields->m_cachedBgStyle = Mod::get()->getSettingValue<std::string>("levelinfo-background-style");
            }
            m_fields->m_cachedEffectIntensity = std::clamp(
                static_cast<int>(Mod::get()->getSavedValue<int>("levelinfo-effect-intensity", 4)), 1, 10);
            m_fields->m_cachedExtraStyles = Mod::get()->getSavedValue<std::string>("levelinfo-extra-styles", "");
            m_fields->m_cachedAutoCycle = Mod::get()->getSavedValue<bool>("levelcell-gallery-autocycle", true);
            m_fields->m_dynamicShaders = Mod::get()->getSavedValue<bool>("levelinfo-dynamic-shaders", false);
            m_fields->m_dynamicShadersDelay = Mod::get()->getSavedValue<float>("levelinfo-dynamic-shaders-delay", 0.0f);
        }
        applyDarknessSetting(readDarknessSetting());
    }

    void updateShaderTime(float dt) {
        if (!m_fields->m_animatedShader) return;
        m_fields->m_shaderTime += dt;

        // Dynamic shaders: smooth cursor interpolation (delay)
        if (m_fields->m_dynamicShaders) {
            float delay = m_fields->m_dynamicShadersDelay;
            if (delay <= 0.001f) {
                // No delay: instant tracking
                m_fields->m_cursorX = m_fields->m_targetCursorX;
                m_fields->m_cursorY = m_fields->m_targetCursorY;
            } else {
                // Exponential lerp: factor = 1 - e^(-dt/delay). Higher delay = slower tracking.
                float speed = dt / delay;
                float t = std::min(speed * 3.0f, 1.0f); // cap to avoid overshoot
                m_fields->m_cursorX += (m_fields->m_targetCursorX - m_fields->m_cursorX) * t;
                m_fields->m_cursorY += (m_fields->m_targetCursorY - m_fields->m_cursorY) * t;
            }

            // Animated click with asymmetric lerp: expansion (press) a bit slower
            // than contraction (release) for a "charge" feel and a soft "snap"
            // back. Frame-rate-independent exponential lerp: t = 1 - exp(-dt / tau).
            float tauPress   = 0.18f; // seconds to reach ~63% on press
            float tauRelease = 0.12f; // faster on release
            float diff = m_fields->m_targetClickState - m_fields->m_clickState;
            float tau = (diff > 0.0f) ? tauPress : tauRelease;
            float ct = 1.0f - std::exp(-dt / std::max(tau, 0.001f));
            m_fields->m_clickState += diff * ct;
            // Final snap if very close, to avoid numeric jitter
            if (std::fabs(diff) < 0.001f) {
                m_fields->m_clickState = m_fields->m_targetClickState;
            }
        }

        // Update u_time and u_cursor on the main sprite
        if (m_fields->m_pixelBg) {
            auto sprite = typeinfo_cast<CCSprite*>(static_cast<CCNode*>(m_fields->m_pixelBg));
            if (sprite) {
                if (auto gif = typeinfo_cast<AnimatedGIFSprite*>(sprite)) {
                    gif->m_time = m_fields->m_shaderTime;
                } else {
                    auto shader = sprite->getShaderProgram();
                    if (shader) {
                        shader->use();
                        // Perf: cache uniform locations per shader program
                        if (shader != m_fields->m_cachedMainShader) {
                            m_fields->m_cachedMainShader = shader;
                            m_fields->m_mainLocTime = shader->getUniformLocationForName("u_time");
                            m_fields->m_mainLocCursor = shader->getUniformLocationForName("u_cursor");
                            m_fields->m_mainLocClick = shader->getUniformLocationForName("u_click");
                        }
                        if (m_fields->m_mainLocTime >= 0) {
                            shader->setUniformLocationWith1f(m_fields->m_mainLocTime, m_fields->m_shaderTime);
                        }
                        if (m_fields->m_dynamicShaders) {
                            if (m_fields->m_mainLocCursor >= 0) {
                                shader->setUniformLocationWith2f(m_fields->m_mainLocCursor, m_fields->m_cursorX, 1.0f - m_fields->m_cursorY);
                            }
                            if (m_fields->m_mainLocClick >= 0) {
                                shader->setUniformLocationWith1f(m_fields->m_mainLocClick, m_fields->m_clickState);
                            }
                        }
                    }
                }
            }
        }

        // Update u_time and u_cursor on extra sprites (cached uniform locations)
        for (size_t idx = 0; idx < m_fields->m_extraBgSprites.size(); ++idx) {
            auto& extra = m_fields->m_extraBgSprites[idx];
            if (!extra) continue;
            auto shader = extra->getShaderProgram();
            if (!shader) continue;
            shader->use();

            // Lazy-init cached uniform locations per extra sprite
            if (idx >= m_fields->m_extraUniformsCache.size()) {
                m_fields->m_extraUniformsCache.resize(idx + 1, {-2, -2, -2});
            }
            auto& cached = m_fields->m_extraUniformsCache[idx];
            if (cached.time == -2) cached.time = shader->getUniformLocationForName("u_time");
            if (cached.time >= 0) {
                shader->setUniformLocationWith1f(cached.time, m_fields->m_shaderTime);
            }
            if (m_fields->m_dynamicShaders) {
                if (cached.cursor == -2) cached.cursor = shader->getUniformLocationForName("u_cursor");
                if (cached.cursor >= 0) {
                    shader->setUniformLocationWith2f(cached.cursor, m_fields->m_cursorX, 1.0f - m_fields->m_cursorY);
                }
                if (cached.click == -2) cached.click = shader->getUniformLocationForName("u_click");
                if (cached.click >= 0) {
                    shader->setUniformLocationWith1f(cached.click, m_fields->m_clickState);
                }
            }
        }
    }

    void updatePaimonAudio(float dt) {
        if (!m_fields->m_paimonAudioActive) return;

        PaimonAudio::get().update(dt);

        auto& pa = PaimonAudio::get();
        int baseDark = m_fields->m_paimonAudioBaseDarkness;
        float baseAlpha = (baseDark / 50.0f) * 255.0f;

        // Modulate darkness with audio
        float mod = 1.0f + pa.bass() * 0.45f - pa.beatPulse() * 0.7f + pa.energy() * 0.15f;
        mod = std::clamp(mod, 0.08f, 1.8f);
        GLubyte dynAlpha = static_cast<GLubyte>(std::clamp(baseAlpha * mod, 0.f, 255.f));

        auto* overlay = m_fields->m_pixelOverlay.data();
        if (overlay && overlay->getParent()) {
            overlay->setOpacity(dynAlpha);
        }
    }

    // Dynamic Shaders: cursor/touch tracking. Desktop: updateCursorFromMouse
    // captures the mouse position each frame. Mobile: ccTouchBegan/Moved captures
    // the last tap (multitouch guard).
    bool ccTouchBegan(CCTouch* touch, CCEvent* event) {
        if (!m_fields->m_dynamicShaders) return false;
        if (m_fields->m_touchActive) return false; // multitouch guard
        m_fields->m_touchActive = true;
        m_fields->m_targetClickState = 1.0f;

        auto win = CCDirector::get()->getWinSize();
        auto loc = touch->getLocation();
        m_fields->m_targetCursorX = std::clamp(loc.x / win.width, 0.0f, 1.0f);
        m_fields->m_targetCursorY = std::clamp(loc.y / win.height, 0.0f, 1.0f);
        return false; // don't consume; let buttons and menus receive the touch
    }

    void ccTouchMoved(CCTouch* touch, CCEvent* event) {
        if (!m_fields->m_dynamicShaders) return;
        auto win = CCDirector::get()->getWinSize();
        auto loc = touch->getLocation();
        m_fields->m_targetCursorX = std::clamp(loc.x / win.width, 0.0f, 1.0f);
        m_fields->m_targetCursorY = std::clamp(loc.y / win.height, 0.0f, 1.0f);
    }

    void ccTouchEnded(CCTouch* touch, CCEvent* event) {
        if (!m_fields->m_dynamicShaders) return;
        m_fields->m_touchActive = false;
        m_fields->m_targetClickState = 0.0f;
    }

    void ccTouchCancelled(CCTouch* touch, CCEvent* event) {
        if (!m_fields->m_dynamicShaders) return;
        m_fields->m_touchActive = false;
        m_fields->m_targetClickState = 0.0f;
    }

    // Desktop: update the cursor with the mouse position each frame
    void updateCursorFromMouse(float dt) {
#ifdef GEODE_IS_WINDOWS
        auto win = CCDirector::get()->getWinSize();
        auto mousePos = geode::cocos::getMousePos();
        // getMousePos() returns GL coordinates (0,0 = bottom-left)
        m_fields->m_targetCursorX = std::clamp(mousePos.x / win.width, 0.0f, 1.0f);
        m_fields->m_targetCursorY = std::clamp(mousePos.y / win.height, 0.0f, 1.0f);
        // Detect mouse click (left button)
        m_fields->m_targetClickState = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 1.0f : 0.0f;
#endif
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

        auto win = CCDirector::get()->getWinSize();
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
            auto* self = static_cast<PaimonLevelInfoLayer*>(selfRef.data());
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

        if (auto active = s_activeLevelInfoForOverlay.lock();
            active.data() == static_cast<LevelInfoLayer*>(this)) {
            s_activeLevelInfoForOverlay = nullptr;
            s_levelInfoOverlayPauseDepth = 0;
        }
        m_fields->m_overlayPaused = false;

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
        this->unschedule(schedule_selector(PaimonLevelInfoLayer::updateCursorFromMouse));
        this->unschedule(schedule_selector(PaimonLevelInfoLayer::refreshDarknessOverlay));
        this->unschedule(schedule_selector(PaimonLevelInfoLayer::forcePlayDynamic));
        this->unschedule(schedule_selector(PaimonLevelInfoLayer::backgroundVisibilityGuardTick));

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
        m_fields->m_lazyLoadScheduled = false;
        m_fields->m_lazyLoadIndex = 0;
        this->unschedule(schedule_selector(PaimonLevelInfoLayer::loadNextThumbnailInBackground));

        // BlurSystem is synchronous — no progressive job to cancel

        stopVideoBackgroundSprite();

        // Remove paimon visual nodes from the tree to avoid accumulation on
        // return (onEnter recreates them from scratch).
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

        // Stop the bg's actions (crossfade) before removing it
        if (m_fields->m_pixelBg && m_fields->m_pixelBg->getParent()) {
            m_fields->m_pixelBg->stopAllActions();
            m_fields->m_pixelBg->removeFromParent();
        }
        m_fields->m_pixelBg = nullptr;

        // Restore the native background so something shows on return
        if (auto vanillaBg = this->getChildByID("background")) {
            vanillaBg->setVisible(true);
        }

        LevelInfoLayer::onExit();
    }

    // Create the set daily/weekly button if it doesn't exist yet
    void addSetDailyWeeklyButton() {
        // avoid duplicates
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
        PaimonButtonHighlighter::registerButton(btn);

        auto leftMenu = findLeftSideMenu();
        if (leftMenu) {
            leftMenu->addChild(btn);
            leftMenu->updateLayout();
            applyLayoutsToEditableMenus();
        }
    }

    void onSetDailyWeekly(CCObject* sender) {
        if (!m_level || m_level->m_levelID.value() <= 0) return;
        SetDailyWeeklyPopup::create(m_level->m_levelID.value())->show();
    }

    // The forced re-download for partial daily/weekly data uses the normal-level
    // endpoint, which returns the level WITHOUT its daily/weekly ID. GD replaces
    // m_level with that object (m_dailyID=0), so completing the level registers it
    // as a normal level instead of a daily. Restore the ID captured in init so
    // completion counts for the daily.
    $override
    void levelDownloadFinished(GJGameLevel* level) {
        LevelInfoLayer::levelDownloadFinished(level);
        if (m_fields->m_forcedDailyID > 0 && m_level && m_level->m_dailyID.value() <= 0) {
            log::info("[LevelInfoLayer] restaurando dailyID={} tras re-download", m_fields->m_forcedDailyID);
            m_level->m_dailyID = m_fields->m_forcedDailyID;
        }
    }

    $override
    bool init(GJGameLevel* level, bool challenge) {
        log::info("[LevelInfoLayer] init: levelID={} challenge={}", level ? level->m_levelID.value() : 0, challenge);

        if (!LevelInfoLayer::init(level, challenge)) return false;

        s_activeLevelInfoForOverlay = this;

        // came from leaderboards?
        if (auto scene = CCDirector::get()->getRunningScene()) {
            if (scene->getChildByType<LeaderboardsLayer>(0)) {
                m_fields->m_fromLeaderboards = true;
                m_fields->m_leaderboardType = LeaderboardType::Default;
            }
        }

        // Daily/Weekly fix: when arriving from DailyLevelPage (challenge==true or
        // level->m_dailyID > 0), the GJGameLevel often has only a subset of fields
        // (name, likes, creator) because the server returns partial data for the
        // daily/weekly endpoint. Vanilla relies on LevelInfoLayer's auto-download,
        // but mod timing (our hook runs AfterPost of node-ids plus other
        // LevelInfoLayer mods) sometimes means the download doesn't fire or the
        // levelDownloadFinished callback is lost, leaving the UI showing only
        // name+likes. Fix: force a re-download when partial data is detected; the
        // server returns the same levelID and repopulates all fields.
        // downloadLevel() is idempotent.
        if (level) {
            bool isDailyOrWeekly = (level->m_dailyID.value() > 0) || challenge;
            bool hasPartialData = level->m_levelDesc.empty()
                               || level->m_levelString.empty()
                               || level->m_creatorName.empty();
            if (isDailyOrWeekly && hasPartialData) {
                log::info("[LevelInfoLayer] daily/weekly partial data detected (levelID={}), forcing re-download", level->m_levelID.value());
                // The re-download uses the NORMAL-level endpoint, which doesn't
                // return the daily/weekly ID. Without this, levelDownloadFinished
                // replaces m_level with the normal version (m_dailyID=0) and
                // completing the daily marks the normal level instead. Save the ID
                // to restore it in levelDownloadFinished.
                m_fields->m_forcedDailyID = level->m_dailyID.value();
                // Ref<> keeps the layer alive during the server callback.
                // queueInMainThread ensures it runs outside the init stack (avoids
                // reentrancy with other LevelInfoLayer hooks).
                Ref<LevelInfoLayer> safeRef = this;
                Loader::get()->queueInMainThread([safeRef]() {
                    if (paimon::isRuntimeShuttingDown()) return;
                    if (!safeRef || !safeRef->getParent()) return;
                    // downloadLevel() uses m_level and m_challenge internally and
                    // re-calls setupLevelInfo on levelDownloadFinished.
                    safeRef->downloadLevel();
                });
            }
        }

        LayerBackgroundManager::get().applyVanillaBackgroundTintFix(this);

        // The native background is hidden in installBackgroundSprite when the
        // thumbnail is ready, so levels without a thumbnail keep it.

        // Darkness overlay: a single persistent sprite
        {
            // Defensive: remove a residual overlay before creating a new one
            if (auto oldOverlay = this->getChildByID("paimon-levelinfo-pixel-overlay"_spr)) {
                oldOverlay->removeFromParent();
            }

            auto win = CCDirector::get()->getWinSize();

            // Create with alpha=0; refreshDarknessOverlay applies the correct
            // value on the first tick. Avoids the bug where the first setting read
            // returns a wrong value and leaves the screen nearly black until the
            // user touches the slider.
            auto overlay = CCLayerColor::create({0, 0, 0, 0});
            overlay->setAnchorPoint({0, 0});
            overlay->setPosition({0, 0});
            overlay->setContentSize(win);
            overlay->setID("paimon-levelinfo-pixel-overlay"_spr);
            this->addChild(overlay, kOverlayZOrder);
            m_fields->m_pixelOverlay = overlay;
            // m_lastDarkness stays -1 so refreshDarknessOverlay always applies the
            // correct value on the first tick

            // Apply the correct value immediately
            refreshDarknessOverlay(0.f);

            // refresh opacity every 0.5s; cheaply detects setting changes
            this->schedule(schedule_selector(PaimonLevelInfoLayer::refreshDarknessOverlay), 0.5f);
        }

        if (!level || level->m_levelID <= 0) {
                log::debug("[LevelInfoLayer] Level ID invalid, skipping thumbnail button");
                return true;
            }

            // Cache settings once to avoid repeated lookups
            m_fields->m_cachedBgStyle = Mod::get()->getSavedValue<std::string>("levelinfo-background-style-override", "");
            if (m_fields->m_cachedBgStyle.empty()) {
                m_fields->m_cachedBgStyle = Mod::get()->getSettingValue<std::string>("levelinfo-background-style");
            }
            m_fields->m_cachedEffectIntensity = std::clamp(
                static_cast<int>(Mod::get()->getSavedValue<int>("levelinfo-effect-intensity", 4)), 1, 10);
            m_fields->m_cachedExtraStyles = Mod::get()->getSavedValue<std::string>("levelinfo-extra-styles", "");
            m_fields->m_cachedAutoCycle = Mod::get()->getSavedValue<bool>("levelcell-gallery-autocycle", true);
            m_fields->m_dynamicShaders = Mod::get()->getSavedValue<bool>("levelinfo-dynamic-shaders", false);
            m_fields->m_dynamicShadersDelay = Mod::get()->getSavedValue<float>("levelinfo-dynamic-shaders-delay", 0.0f);

            // Enable touch for dynamic shaders (cursor tracking)
            if (m_fields->m_dynamicShaders) {
                this->setTouchEnabled(true);
                this->setTouchMode(kCCTouchesOneByOne);
                this->setTouchPriority(-1); // high priority to capture position without blocking
            }

            // Activate dynamic song during the entry transition. scheduleOnce
            // works from init() because GD already added the node to the tree.
            this->scheduleOnce(schedule_selector(PaimonLevelInfoLayer::forcePlayDynamic), 0.0f);

            // consume the "opened from thumbnail list" flag
            bool fromThumbs = paimon::SessionState::consumeFlag(paimon::SessionState::get().verification.openFromThumbs);
            m_fields->m_fromThumbsList = fromThumbs;

            // opened from report?
            bool fromReport = paimon::SessionState::consumeFlag(paimon::SessionState::get().verification.openFromReport);
            m_fields->m_fromReportSection = fromReport;
            
            // came from the verify queue?
            bool fromVerificationQueue = false;
            int verificationQueueCategory = -1;
            int verificationQueueLevelID = paimon::SessionState::get().verification.queueLevelID;

            if (verificationQueueLevelID == level->m_levelID.value()) {
                fromVerificationQueue = true;
                verificationQueueCategory = paimon::SessionState::get().verification.queueCategory;
                m_fields->m_fromVerificationQueue = true;

                // don't clear; persist into PlayLayer
            }

            // pixel thumb background
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
                            // Don't return here; continue setting up buttons, gallery, etc.
                        }
                    }
                }

                // Don't trigger requestLoad here: refreshGalleryData (below)
                // already loads the main thumbnail with refreshBackground=true.
                // Launching both made them compete for m_bgRequestToken, so the
                // second invalidated the first and the background never applied.
            }

            // left menu
            auto leftMenu = findLeftSideMenu();
            if (!leftMenu) {
                log::warn("Left side menu not found");
                return true;
            }

            // main menu ref for the button-edit overlay
            m_fields->m_extraMenu = static_cast<CCMenu*>(leftMenu);
            
            // button icon sprite (with fallbacks)
            CCSprite* iconSprite = paimon::SpriteHelper::safeCreate("paim_BotonMostrarThumbnails.png"_spr);
            if (!iconSprite) iconSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png");
            if (!iconSprite) iconSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plusBtn_001.png");
            if (!iconSprite) return true;

            // rotate 90
            iconSprite->setRotation(-90.0f);
            // shrink the icon 20%
            iconSprite->setScale(0.8f);

            // Green CircleButtonSprite
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
            
            if (!button) {
                log::error("Failed to create menu button");
                return true;
            }
            
            button->setID("thumbnail-view-button"_spr);
            PaimonButtonHighlighter::registerButton(button);
            m_fields->m_thumbnailButton = button;

            // gallery thumbs (versioned URLs via the list endpoint)
            // If a local video is already applied, don't overwrite the bg with a static image
            m_fields->m_initLoadState = Fields::InitLoadState::Pending;
            bool skipBgRefresh = m_fields->m_videoSprite != nullptr;

            // Instant cache check: try to display the thumbnail immediately
            // from RAM cache while the gallery list API call is in flight.
            // This makes the background appear almost instantly on revisit.
            if (!skipBgRefresh) {
                auto& cache = paimon::cache::ThumbnailCache::get();
                int currentLevelID = level->m_levelID.value();
                auto levelRamTex = cache.getFromRam(currentLevelID, false);
                if (!levelRamTex.has_value()) {
                    levelRamTex = cache.getFromRam(currentLevelID, true);
                }
                if (levelRamTex.has_value() && levelRamTex.value()) {
                    log::info("[LevelInfoLayer] init: instant level RAM cache hit for levelID={}", currentLevelID);
                    this->applyThumbnailBackground(levelRamTex.value(), currentLevelID);
                }

                std::string mainUrl = ThumbnailAPI::get().getThumbnailURL(currentLevelID);
                auto ramTex = cache.getUrlFromRam(mainUrl);
                if (!m_fields->m_pixelBg && ramTex.has_value() && ramTex.value()) {
                    log::info("[LevelInfoLayer] init: instant RAM cache hit for main thumbnail levelID={}", currentLevelID);
                    this->applyThumbnailBackground(ramTex.value(), currentLevelID);
                    // Background is already applied — refreshGalleryData will still
                    // call loadThumbnail(0) but it'll be a cache hit (fast re-apply).
                } else if (!m_fields->m_pixelBg) {
                    Ref<LevelInfoLayer> safeRef = this;
                    Ref<CCNode> layerAnchor = static_cast<CCNode*>(safeRef.data());
                    paimon::thumbnails::levelinfo::requestHeroBackground(
                        currentLevelID,
                        layerAnchor,
                        [safeRef]() {
                            auto* self = static_cast<PaimonLevelInfoLayer*>(safeRef.data());
                            if (!self) return true;
                            if (auto* fields = self->m_fields.self()) {
                                return fields->m_pixelBg != nullptr;
                            }
                            return false;
                        },
                        [safeRef]() {
                            auto* self = static_cast<PaimonLevelInfoLayer*>(safeRef.data());
                            return self && self->m_level ? self->m_level->m_levelID.value() : 0;
                        },
                        [safeRef, currentLevelID](CCTexture2D* tex) {
                            auto* self = static_cast<PaimonLevelInfoLayer*>(safeRef.data());
                            if (!self || !self->getParent() || !self->m_level) return;
                            if (self->m_level->m_levelID.value() != currentLevelID) return;
                            log::info(
                                "[LevelInfoLayer] init: background applied from cache/load levelID={}",
                                currentLevelID
                            );
                            self->applyThumbnailBackground(tex, currentLevelID);
                        }
                    );
                }
            }

            this->refreshGalleryData(level->m_levelID.value(), !skipBgRefresh);

            // add first for default layout
            leftMenu->addChild(button);
            leftMenu->updateLayout();

            // admin? -> daily/weekly button
            // Local check first: if the mod code is saved and the user is marked
            // admin, show the button immediately without waiting on the server.
            {
                bool localAdmin = Mod::get()->getSavedValue<bool>("is-verified-admin", false);
                bool hasModCode = !HttpClient::get().getModCode().empty();

                if (localAdmin && hasModCode) {
                    this->addSetDailyWeeklyButton();
                }
            }
            // Server check (refreshes cache and may add/remove the button)
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

            // verify queue -> save category
            if (fromVerificationQueue && verificationQueueLevelID == level->m_levelID.value()) {
                log::info("Nivel abierto desde verificacion (categoria: {}) - boton listo para usar", verificationQueueCategory);
                // category for the thumbnail view popup
                paimon::SessionState::get().verification.verificationCategory = verificationQueueCategory;
            }

            // apply layouts to all tracked menus so custom IDs stay consistent
            applyLayoutsToEditableMenus();

            // Favorite buttons removed

        return true;
    }

    void onThumbnailButton(CCObject*) {
        if (!m_level) {
            log::error("Level is null");
            return;
        }

        // avoid opening multiple popups on rapid taps
        if (auto* scene = CCDirector::get()->getRunningScene()) {
            for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
                if (geode::cast::typeinfo_cast<LocalThumbnailViewPopup*>(child)) {
                    return;
                }
            }
        }

        int32_t levelID = m_level->m_levelID.value();
        log::info("Opening thumbnail view for level ID: {}", levelID);

        // use the moderator-verification utility
        bool canAccept = false; // no server functionality
        // popup context flag
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
        paimon::menu_layout::MainMenuLayoutManager::get().captureDefaultsAndApply(this);
        paimon::menu_layout::MainMenuLayoutEditor::open(this);
    }

    void onUploadLocalThumbnail(CCObject*) {
        log::info("[LevelInfoLayer] Upload local thumbnail button clicked");
        
        if (!m_level) {
            PaimonNotify::create(Localization::get().getString("level.error_prefix") + "nivel no encontrado", NotificationIcon::Error)->show();
            return;
        }
        
        // level ptr before async
        auto* level = m_level;
        int32_t levelID = level->m_levelID.value();
        
        // local thumb exists?
        if (!LocalThumbs::get().has(levelID)) {
            PaimonNotify::create(Localization::get().getString("level.no_local_thumb").c_str(), NotificationIcon::Error)->show();
            return;
        }
        
        // get the username
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

        // Collect the full level metadata (GJGameLevel bindings) to attach to the upload.
        std::string levelMeta = paimon::collectLevelMetadata(m_level);

        paimon::showBetaUploadWarningIfNeeded([self, levelID, pngData = std::move(pngData), username, levelMeta]() mutable {
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
            }, levelMeta);
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

        // Check whether we return to LevelSelectLayer
        bool returnsToLevelSelect = false;
        auto scene = CCDirector::get()->getRunningScene();
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
            // clear the flags
            paimon::SessionState::get().verification.openFromQueue = false;
            paimon::SessionState::get().verification.queueLevelID  = -1;
            paimon::SessionState::get().verification.queueCategory  = -1;
            
            // reopen popup
            paimon::SessionState::get().verification.reopenQueue = true;
            
            // back to MenuLayer
            TransitionManager::get().replaceScene(MenuLayer::scene(false));
            return;
        }

        // opened from leaderboards?
        if (m_fields->m_fromLeaderboards) {
            auto lbScene = LeaderboardsLayer::scene(m_fields->m_leaderboardType, m_fields->m_leaderboardStat);
            TransitionManager::get().replaceScene(lbScene);
            return;
        }

        // no daily anim
        
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

        auto win = CCDirector::get()->getWinSize();
        float arrowY = 210.f;

        // left arrow (prev)
        auto prevSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_03_001.png");
        if (prevSpr) {
            prevSpr->setScale(0.65f);
            auto prevBtn = CCMenuItemSpriteExtra::create(
                prevSpr, this, menu_selector(PaimonLevelInfoLayer::onPrevBtn)
            );
            prevBtn->setID("gallery-prev-btn"_spr);
            PaimonButtonHighlighter::registerButton(prevBtn);
            prevBtn->setPosition({win.width / 2.f - 62.f, arrowY});
            prevBtn->setOpacity(180);
            menu->addChild(prevBtn);
            m_fields->m_prevBtn = prevBtn;
        }

        // right arrow (next)
        auto nextSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_03_001.png");
        if (nextSpr) {
            nextSpr->setFlipX(true);
            nextSpr->setScale(0.65f);
            auto nextBtn = CCMenuItemSpriteExtra::create(
                nextSpr, this, menu_selector(PaimonLevelInfoLayer::onNextBtn)
            );
            nextBtn->setID("gallery-next-btn"_spr);
            PaimonButtonHighlighter::registerButton(nextBtn);
            nextBtn->setPosition({win.width / 2.f + 62.f, arrowY});
            nextBtn->setOpacity(180);
            menu->addChild(nextBtn);
            m_fields->m_nextBtn = nextBtn;
        }

        this->addChild(menu, 100);
        applyLayoutsToEditableMenus();

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
            // Don't start gallery cycle under an open comments overlay.
            if (!self->m_fields->m_overlayPaused &&
                self->m_fields->m_thumbnails.size() > 1 && autoCycleEnabled) {
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
        // RatePopup handles the rating logic.
        if (m_fields->m_currentThumbnailIndex < 0 || m_fields->m_currentThumbnailIndex >= m_fields->m_thumbnails.size()) return;
        
        auto& thumb = m_fields->m_thumbnails[m_fields->m_currentThumbnailIndex];
        if (!m_level) return;
        RatePopup::create(m_level->m_levelID.value(), thumb.id)->show();
    }

    // Favorite Creator / Level

    void setupFavoriteButtons() {
        if (!m_level || m_level->m_levelID <= 0) return;

        if (auto old = this->getChildByID("fav-menu"_spr)) {
            old->removeFromParent();
        }
        m_fields->m_favCreatorBtn = nullptr;
        m_fields->m_favLevelBtn = nullptr;

        auto win = CCDirector::get()->getWinSize();

        // reuse gallery-menu or create a dedicated fav-menu
        auto favMenu = CCMenu::create();
        favMenu->setID("fav-menu"_spr);
        favMenu->setPosition({0, 0});

        int creatorID = m_level->m_accountID;
        int levelID = m_level->m_levelID.value();
        auto& tracker = paimon::foryou::ForYouTracker::get();

        // Heart button (Favorite Creator)
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
                PaimonButtonHighlighter::registerButton(btn);
                btn->setPosition({win.width - 30.f, win.height - 60.f});
                favMenu->addChild(btn);
                m_fields->m_favCreatorBtn = btn;
            }
        }

        // Star button (Favorite Level)
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
                PaimonButtonHighlighter::registerButton(btn);
                btn->setPosition({win.width - 30.f, win.height - 90.f});
                favMenu->addChild(btn);
                m_fields->m_favLevelBtn = btn;
            }
        }

        this->addChild(favMenu, 101);
        applyLayoutsToEditableMenus();
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
        if (m_fields->m_overlayPaused) return;
        if (!m_fields->m_cycling || m_fields->m_thumbnails.size() <= 1) return;
        m_fields->m_bgNavDirection = Fields::BgNavDir::Right;
        m_fields->m_currentThumbnailIndex = (m_fields->m_currentThumbnailIndex + 1) % static_cast<int>(m_fields->m_thumbnails.size());
        this->loadThumbnail(m_fields->m_currentThumbnailIndex);
    }
    
    void onPrevBtn(CCObject*) {
        log::info("[LevelInfoLayer] onPrevBtn: currentIndex={}", m_fields->m_currentThumbnailIndex);
        if (m_fields->m_thumbnails.empty()) return;
        m_fields->m_cycling = false; // stop auto-cycling on interaction
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

        // Stop the previous video if any
        stopVideoBackgroundSprite();

        // If the thumbnail is video, download and play it with VideoThumbnailSprite
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
                    self->fallbackToNextThumbnail(index);
                    return;
                }

                self->m_fields->m_fallbackOrigin = -1; // success: ends the fallback chain
                self->queueVideoBackgroundSprite(videoSprite, levelID, requestToken);
                log::info("[LevelInfoLayer] loadThumbnail: waiting for first visible video frame for index={}", index);
            });
            return;
        }

        // URL version with _pv=<thumbId> for CDN invalidation
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
                // Start lazy-loading the remaining thumbnails (one by one in the background)
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
                    log::warn("[LevelInfoLayer] loadThumbnail callback: index={} FAILED, trying next", index);
                    self->fallbackToNextThumbnail(index);
                }
            }
        }, ThumbnailLoader::PriorityHero);
    }

    // Advance to the next thumbnail after a load failure, stopping once the whole
    // gallery has been tried. Without this, if ALL thumbnails fail (server down,
    // broken URLs, all uncreatable videos), the async fallback chain bounced
    // between indices forever, emitting endless requests and leaving
    // m_initLoadState stuck in Pending.
    void fallbackToNextThumbnail(int index) {
        int sz = static_cast<int>(m_fields->m_thumbnails.size());
        if (sz > 1) {
            if (m_fields->m_fallbackOrigin < 0) m_fields->m_fallbackOrigin = index;
            int next = (index + 1) % sz;
            if (next != m_fields->m_fallbackOrigin) {
                loadThumbnail(next);
                return;
            }
        }
        // 1-thumbnail gallery, or the whole gallery was tried without success: stop.
        m_fields->m_fallbackOrigin = -1;
        m_fields->m_initLoadState = Fields::InitLoadState::Idle;
    }

    // Load the remaining thumbnails in the background, one by one
    void loadNextThumbnailInBackground(float /*dt*/) {
        auto total = static_cast<int>(m_fields->m_thumbnails.size());
        if (total <= 1) return;
        if (m_fields->m_lazyLoadScheduled) return;

        // Skip videos (not preloaded via URL) until the next image thumbnail.
        // Previously it returned on the first video, breaking the chain and
        // leaving image thumbnails after the video unloaded.
        while (m_fields->m_lazyLoadIndex < total &&
               m_fields->m_thumbnails[m_fields->m_lazyLoadIndex].isVideo()) {
            m_fields->m_lazyLoadIndex++;
        }
        if (m_fields->m_lazyLoadIndex >= total) return;

        int index = m_fields->m_lazyLoadIndex;
        auto& thumb = m_fields->m_thumbnails[index];

        m_fields->m_lazyLoadScheduled = true;
        std::string purl = thumb.url;
        if (!thumb.id.empty()) {
            auto sep = (purl.find('?') == std::string::npos) ? "?" : "&";
            purl += fmt::format("{}_pv={}", sep, thumb.id);
        }

        int const galleryToken = m_fields->m_galleryToken;
        WeakRef<LevelInfoLayer> weakSelf = this;
        ThumbnailLoader::get().requestUrlLoad(purl, [weakSelf, index, galleryToken](CCTexture2D* tex, bool success) {
            auto selfRef = weakSelf.lock();
            if (!selfRef) return;
            auto* self = static_cast<PaimonLevelInfoLayer*>(selfRef.data());
            if (!self->getParent()) return;
            if (self->m_fields->m_galleryToken != galleryToken) return;
            self->m_fields->m_lazyLoadScheduled = false;
            if (success) {
                log::info("[LevelInfoLayer] lazyLoad: index={} loaded", index);
            }
            self->m_fields->m_lazyLoadIndex++;
            if (self->m_fields->m_lazyLoadIndex < static_cast<int>(self->m_fields->m_thumbnails.size())) {
                self->scheduleOnce(schedule_selector(PaimonLevelInfoLayer::loadNextThumbnailInBackground), 0.1f);
            }
        }, 0); // Low priority for background loading
    }
};

namespace paimon {

void pauseLevelInfoHeavyWorkForOverlay() {
    ++s_levelInfoOverlayPauseDepth;
    if (s_levelInfoOverlayPauseDepth != 1) return;
    auto ref = s_activeLevelInfoForOverlay.lock();
    auto* layer = ref.data();
    if (!layer) return;
    static_cast<PaimonLevelInfoLayer*>(layer)->pauseHeavyWorkForOverlay();
}

void resumeLevelInfoHeavyWorkForOverlay() {
    if (s_levelInfoOverlayPauseDepth <= 0) return;
    --s_levelInfoOverlayPauseDepth;
    if (s_levelInfoOverlayPauseDepth != 0) return;
    auto ref = s_activeLevelInfoForOverlay.lock();
    auto* layer = ref.data();
    if (!layer) return;
    static_cast<PaimonLevelInfoLayer*>(layer)->resumeHeavyWorkForOverlay();
}

} // namespace paimon

// onSettings implementation (needs PaimonLevelInfoLayer already defined)
void LocalThumbnailViewPopup::onSettings(CCObject*) {
    auto popup = ThumbnailSettingsPopup::create();
    if (!popup) return;

    // Ref<> keeps the texture alive while the callback exists (shared copy, not a raw ptr)
    geode::Ref<CCTexture2D> texRef = m_thumbnailTexture;
    int32_t levelID = m_levelID;

    popup->setOnSettingsChanged([texRef, levelID]() {
        log::info("[ThumbnailViewPopup] Settings changed, refrescando fondo");
        auto scene = CCDirector::get()->getRunningScene();
        if (!scene) return;

        auto layer = scene->getChildByType<LevelInfoLayer>(0);
        if (!layer) return;

        // remove old backgrounds by node ID (safe)
        if (auto old = layer->getChildByID("paimon-levelinfo-pixel-bg"_spr)) {
            old->removeFromParent();
        }
        // darkness overlay stays persistent; refreshDarknessOverlay updates it

        auto paimon = static_cast<PaimonLevelInfoLayer*>(layer);

        // clear extra sprites that applyThumbnailBackground couldn't reach
        for (auto& s : paimon->m_fields->m_extraBgSprites) {
            if (s && s->getParent()) s->removeFromParent();
        }
        paimon->m_fields->m_extraBgSprites.clear();

        // reset the internal ref to avoid a double-remove inside applyThumbnailBackground
        paimon->m_fields->m_pixelBg = nullptr;

        // force an immediate opacity refresh for instant feedback
        paimon->m_fields->m_lastDarkness = -1;
        paimon->refreshDarknessOverlay(0.0f);

        // re-apply with the new settings
        if (texRef) {
            // Refresh cached settings before re-applying
            paimon->m_fields->m_cachedBgStyle = Mod::get()->getSavedValue<std::string>("levelinfo-background-style-override", "");
            if (paimon->m_fields->m_cachedBgStyle.empty()) {
                paimon->m_fields->m_cachedBgStyle = Mod::get()->getSettingValue<std::string>("levelinfo-background-style");
            }
            paimon->m_fields->m_cachedEffectIntensity = std::clamp(
                static_cast<int>(Mod::get()->getSavedValue<int>("levelinfo-effect-intensity", 4)), 1, 10);
            paimon->m_fields->m_cachedExtraStyles = Mod::get()->getSavedValue<std::string>("levelinfo-extra-styles", "");
            paimon->m_fields->m_cachedAutoCycle = Mod::get()->getSavedValue<bool>("levelcell-gallery-autocycle", true);
            paimon->m_fields->m_dynamicShaders = Mod::get()->getSavedValue<bool>("levelinfo-dynamic-shaders", false);
            paimon->m_fields->m_dynamicShadersDelay = Mod::get()->getSavedValue<float>("levelinfo-dynamic-shaders-delay", 0.0f);
            paimon->applyThumbnailBackground(texRef, levelID);
        }
    });
    popup->show();
}
