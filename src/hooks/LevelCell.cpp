#include <Geode/modify/LevelCell.hpp>
#include <Geode/binding/BoomListView.hpp>
#include <Geode/binding/DailyLevelNode.hpp>
#include <Geode/binding/LevelBrowserLayer.hpp>
#include <Geode/binding/LevelListLayer.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include "../utils/PaimonDrawNode.hpp"
#include <cmath>
#include <algorithm>
#include <string_view>
#include <random>
#include <unordered_map>
#include <chrono>
#include <memory>
#include <utility>
#include <unordered_set>
#include "../core/Settings.hpp"
#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../features/thumbnails/services/LevelColors.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/Constants.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../utils/Shaders.hpp"
#include "../utils/GLSLLoader.hpp"
#include "../blur/BlurSystem.hpp"
#include "../utils/PaimonShaderSprite.hpp"
#include "../utils/RetainedLazyTextureLoad.hpp"
#include "../features/thumbnails/ui/LevelCellSettingsPopup.hpp"
#include "../framework/compat/ModCompat.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../utils/ScissorClipNode.hpp"
#include "../video/VideoPlayer.hpp"
#include "../utils/VideoThumbnailSprite.hpp"
#include "../utils/HttpClient.hpp"
#include "LevelCellContext.hpp"
#include "../features/thumbnails/services/LevelCellThumbHelpers.hpp"
#include "../features/thumbnails/services/LevelCellThumbnailLoad.hpp"
#include "../features/thumbnails/services/ThumbnailTransportClient.hpp"
#include "../features/thumbnails/services/LevelCellGalleryLoad.hpp"
#include "../features/thumbnails/services/LevelCellState.hpp"
#include "../features/thumbnails/services/LevelCellVideoLoad.hpp"
#include "../features/thumbnails/services/LevelCellLoadPipeline.hpp"
#include "../features/thumbnails/services/LevelCellMaintenance.hpp"
#include "../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace Shaders;
using namespace paimon::levelcell;

namespace paimon::hooks {
    bool g_suppressLevelCellEnhancements = false;
    bool g_forceCompactLevelCells = false;
    thread_local bool g_suppressCompactLevelCellsInContext = false;
}

class $modify(PaimonLevelCell, LevelCell) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "LevelCell::loadFromLevel");
        paimon::hooks::afterNodeIdsOrLate(self, "LevelCell::loadCustomLevelCell");
    }

    struct Fields {
        Ref<CCClippingNode> m_clippingNode = nullptr;
        Ref<CCNode> m_separator = nullptr;
        Ref<CCNode> m_gradient = nullptr;
        Ref<CCParticleSystemQuad> m_mythicParticles = nullptr;
        Ref<CCNode> m_darkOverlay = nullptr;
        float m_gradientTime = 0.0f;
        ccColor3B m_gradientColorA = {0, 0, 0};
        ccColor3B m_gradientColorB = {0, 0, 0};
        Ref<CCSprite> m_gradientLayer = nullptr;
        bool m_gradientIsPSG = false; // Perf: cached typeinfo_cast result
        Ref<geode::LoadingSpinner> m_loadingSpinner = nullptr;
        bool m_isBeingDestroyed = false;
        Ref<CCSprite> m_thumbSprite = nullptr;
        CCPoint m_thumbBasePos = {0.f, 0.f};
        CCPoint m_clipBasePos = {0.f, 0.f}; 
        CCPoint m_separatorBasePos = {0.f, 0.f}; 
        float m_thumbBaseScaleX = 1.0f;
        float m_thumbBaseScaleY = 1.0f;
        bool m_thumbnailRequested = false; 
        int m_requestId = 0;
        // Invalidate async callbacks when the cell is recycled.
        std::shared_ptr<std::monostate> m_asyncCancelToken = std::make_shared<std::monostate>();
        int m_lastRequestedLevelID = 0; 
        bool m_thumbnailApplied = false; 
        // Sticky "load failed" flag: set when ThumbnailLoader returns failure for the
        // current levelID, cleared when the level changes / thumbnail is invalidated /
        // settings change. Prevents updateMaintenance() from re-issuing tryLoadThumbnail()
        // every 200ms once the loader's failedCache/notFoundCache is the only thing
        // we'd hit, which produced an infinite "load FAILED" log spam loop.
        bool m_thumbnailFailed = false;
        bool m_wasInCenter = false; 
        float m_centerLerp = 0.0f; 
        Ref<CCMenuItemSpriteExtra> m_viewOverlay = nullptr; 
        
        float m_animTime = 0.0f;
        bool m_hasGif = false;
        // Perf: cached typeinfo results for thumbSprite (avoids 3x RTTI per tick)
        bool m_thumbTypeCached = false;
        bool m_thumbIsPSS = false;
        bool m_thumbIsAGS = false;
        Ref<CCTexture2D> m_staticTexture = nullptr;

        paimon::image::RetainedLazyTextureLoad m_staticThumbLoad;
        bool m_isHovering = false;
        float m_hoverCheckAccumulator = 0.0f;

        std::unique_ptr<paimon::video::VideoPlayer> m_videoPlayer = nullptr;
        Ref<VideoThumbnailSprite> m_videoDriver = nullptr;
        bool m_hasVideo = false;

        bool m_settingsCached = false;
        PaimonAnimType m_cachedAnimType = PaimonAnimType::ZoomSlide;
        float m_cachedAnimSpeed = 1.0f;
        PaimonAnimEffect m_cachedAnimEffect = PaimonAnimEffect::None;
        bool m_cachedHoverEnabled = true;
        bool m_cachedCompactMode = false;
        bool m_compactLayoutApplied = false;
        bool m_cachedTransparentMode = false;
        bool m_cachedEffectOnGradient = false;
        PaimonBgType m_cachedBgType = PaimonBgType::Gradient;
        PaimonGalleryTransition m_cachedGalleryTransition = PaimonGalleryTransition::Crossfade;
        float m_cachedTransitionDuration = 0.6f;
        bool m_cachedShowSeparator = true;
        bool m_cachedShowViewButton = true;
        bool m_cachedAnimatedGradient = true;
        bool m_cachedMythicParticles = true;
        bool m_cachedGalleryAutocycle = true;
        float m_cachedThumbWidthFactor = 0.5f;
        float m_cachedBackgroundBlur = 5.0f;
        float m_cachedBackgroundDarkness = 0.5f;
        bool m_isGalleryTransitioning = false; 
        float m_lastClipHoverOffsetX = 0.0f;
        float m_lastClipHoverPosAdjustment = 0.0f;
        float m_lastClipHoverScaleX = 1.0f;
        float m_lastClipHoverRotation = 0.0f;
        float m_lastSeparatorHoverOffsetX = 0.0f;
        float m_lastSeparatorHoverRotation = 0.0f;
        float m_lastSpriteHoverScale = 1.0f;
        float m_lastSpriteHoverRotation = 0.0f;
        float m_lastSpriteHoverOffsetX = 0.0f;
        float m_lastBoundsHoverScale = 1.0f;
        float m_lastBoundsHoverOffsetX = 0.0f;
        float m_lastBoundsHoverRotation = 0.0f;
        Ref<CCClippingNode> m_boundsClipper = nullptr;
        Ref<CCNode> m_hoverContainer = nullptr; 
        bool m_isCenterAnimScheduled = false; 
        bool m_isGradientAnimScheduled = false;
        bool m_isMaintenanceScheduled = false;

        int m_loadedInvalidationVersion = 0;

        int m_loadedPopupSettingsVersion = 0;

        int m_loadedSettingsVersion = 0;

        int m_cellLevelID = 0;
        bool m_isDailyCell = false;
        bool m_isDailyCellCached = false;
        std::vector<ThumbnailAPI::ThumbnailInfo> m_galleryThumbnails;
        std::unordered_set<std::string> m_galleryPendingUrls;
        int m_galleryIndex = 0;
        float m_galleryTimer = 0.f;
        bool m_galleryRequested = false;
        bool m_galleryPrefetchScheduled = false;
        int m_galleryToken = 0;
        int m_invalidationListenerId = 0;
        float m_updateCheckTimer = 0.f; // Throttle checks update()
        std::chrono::steady_clock::time_point m_galleryTransitionStart{};
        static constexpr float GALLERY_TRANSITION_SAFETY_TIMEOUT = 2.0f;
        int m_galleryConsecutiveMisses = 0;
        ccColor3B m_lastBgColor = {0, 0, 0};
        int m_bgBlurToken = 0;

        // Staged layout distribution fields (360fps optimization)
        bool m_hasDeferredSetup = false;
        bool m_hasDeferredExtras = false;
        int32_t m_deferredLevelID = 0;
        int m_deferredRequestId = 0;
        Ref<CCTexture2D> m_deferredTexture = nullptr;

        // Safety timeout: if the thumbnail doesn't arrive in 1.5s, force a retry
        std::chrono::steady_clock::time_point m_thumbnailRequestTime{};
    };
    
    ~PaimonLevelCell() {
        auto fields = m_fields.self();
        if (fields) {
            fields->m_isBeingDestroyed = true;
        }
    }
    
    static void calculateFullCoverageThumbScale(CCSprite* sprite, float targetWidth, float targetHeight, float& outScale) {
        if (!sprite) return;
        
        const float contentWidth = sprite->getContentSize().width;
        const float contentHeight = sprite->getContentSize().height;
        if (contentWidth <= 0.f || contentHeight <= 0.f || targetWidth <= 0.f || targetHeight <= 0.f) {
            outScale = 1.f;
            return;
        }

        outScale = safeCoverScale(targetWidth, targetHeight, contentWidth, contentHeight, 1.f) * 1.15f;
    }
    
    void showLoadingSpinner() {
        auto fields = m_fields.self();
        
        if (fields->m_loadingSpinner) {
            fields->m_loadingSpinner->removeFromParent();
            fields->m_loadingSpinner = nullptr;
        }
        
        auto spinner = geode::LoadingSpinner::create(10.f);
        
        auto bg = m_backgroundLayer;
        if (bg) {
            auto cs = bg->getContentSize();
            spinner->setPosition({cs.width - 75.f, cs.height / 2.f});
        } else {
            spinner->setPosition({PaimonConstants::LEVELCELL_SPINNER_FALLBACK_X, PaimonConstants::LEVELCELL_SPINNER_FALLBACK_Y});
        }
        
        spinner->setZOrder(999);
        
        spinner->setID("paimon-loading-spinner"_spr);
        
        this->addChild(spinner);
        fields->m_loadingSpinner = spinner;
        
        // Smooth fade-in
        spinner->setOpacity(0);
        spinner->runAction(CCFadeTo::create(0.3f, 180));
    }
    
    void hideLoadingSpinner() {
        auto fields = m_fields.self();
        if (fields->m_loadingSpinner) {
            auto* spinnerNode = static_cast<geode::LoadingSpinner*>(fields->m_loadingSpinner);
            spinnerNode->runAction(CCSequence::create(
                CCFadeOut::create(0.2f),
                CCCallFunc::create(spinnerNode, callfunc_selector(CCNode::removeFromParent)),
                nullptr
            ));
            
            fields->m_loadingSpinner = nullptr;
        }
    }

    void setVideoDriver(VideoThumbnailSprite* videoDriver, float staleDriverRemovalDelay = 0.0f) {
        auto fields = m_fields.self();
        if (!fields) {
            return;
        }

        auto* currentDriver = fields->m_videoDriver.data();
        if (currentDriver && currentDriver != videoDriver) {
            currentDriver->stopAllActions();
            if (staleDriverRemovalDelay > 0.0f && currentDriver->getParent()) {
                currentDriver->runAction(CCSequence::create(
                    CCDelayTime::create(staleDriverRemovalDelay),
                    CCRemoveSelf::create(),
                    nullptr
                ));
            } else if (currentDriver->getParent()) {
                currentDriver->removeFromParent();
            }
        }

        fields->m_videoDriver = videoDriver;
        if (!videoDriver) {
            return;
        }

        videoDriver->stopAllActions();
        videoDriver->setVisible(false);
        videoDriver->setOpacity(255);
        videoDriver->setID("video-driver"_spr);
        if (!videoDriver->getParent()) {
            this->addChild(videoDriver, -1000);
        }
    }

    // Viewport culling: is the cell visible or near the viewport? Off-screen
    // cells skip full thumbnail setup during fast scrolling.
    bool isCellVisibleOrNearby(float marginFactor = 0.5f) {
        if (!this->getParent()) return false;
        
        auto bbox = this->boundingBox();
        CCPoint worldMin = this->getParent()->convertToWorldSpace(ccp(bbox.getMinX(), bbox.getMinY()));
        CCPoint worldMax = this->getParent()->convertToWorldSpace(ccp(bbox.getMaxX(), bbox.getMaxY()));
        
        auto* director = cocos2d::CCDirector::get();
        CCSize visibleSize = director->getWinSize();
        
        // Expand viewport with a preload margin (50% by default)
        float marginX = visibleSize.width * marginFactor;
        float marginY = visibleSize.height * marginFactor;
        
        return !(worldMax.x < -marginX || worldMin.x > visibleSize.width + marginX ||
                 worldMax.y < -marginY || worldMin.y > visibleSize.height + marginY);
    }

    void configureThumbnailLoader() {
        paimon::thumbnails::levelcell::configureLoaderFromSettingsOnce();
    }

    static bool supportsLazyStaticThumbnailPath(std::string const& path) {
        auto lowerPath = geode::utils::string::toLower(path);
        return lowerPath.ends_with(".png") ||
            lowerPath.ends_with(".jpg") ||
            lowerPath.ends_with(".jpeg") ||
            lowerPath.ends_with(".webp") ||
            lowerPath.ends_with(".qoi") ||
            lowerPath.ends_with(".jxl");
    }

    static std::optional<std::string> resolveLazyStaticThumbnailPath(int32_t levelID) {
        auto path = LocalThumbs::get().findAnyThumbnail(levelID);
        if (!path || !supportsLazyStaticThumbnailPath(*path)) {
            return std::nullopt;
        }
        return path;
    }

    bool shouldHandleThumbnailCallback(int32_t levelID, int currentRequestId) {
        auto fields = m_fields.self();
        return fields &&
            !fields->m_isBeingDestroyed &&
            fields->m_requestId == currentRequestId &&
            m_level &&
            m_level->m_levelID == levelID;
    }

    paimon::thumbnails::levelcell::ThumbLoadResetFields thumbResetBundle() {
        auto* fields = m_fields.self();
        return paimon::thumbnails::levelcell::ThumbLoadResetFields{
            fields->m_lastRequestedLevelID,
            fields->m_thumbnailRequested,
            fields->m_thumbnailApplied,
            fields->m_thumbnailFailed,
            fields->m_hasGif,
            fields->m_isDailyCellCached,
            fields->m_loadedInvalidationVersion,
            fields->m_galleryIndex,
            fields->m_galleryTimer,
            fields->m_galleryRequested,
            fields->m_galleryToken,
            fields->m_hasVideo,
            fields->m_asyncCancelToken,
            fields->m_staticTexture,
            fields->m_staticThumbLoad,
            fields->m_galleryThumbnails,
            fields->m_galleryPendingUrls,
            fields->m_videoPlayer,
            fields->m_videoDriver,
        };
    }

    paimon::thumbnails::levelcell::ThumbnailLoadFieldRefs thumbLoadFieldRefs() {
        auto* fields = m_fields.self();
        return paimon::thumbnails::levelcell::ThumbnailLoadFieldRefs{
            fields->m_isBeingDestroyed,
            fields->m_thumbnailRequested,
            fields->m_thumbnailApplied,
            fields->m_thumbnailFailed,
            fields->m_lastRequestedLevelID,
            fields->m_requestId,
            fields->m_loadedInvalidationVersion,
            fields->m_hasGif,
            fields->m_thumbSprite,
            fields->m_staticTexture,
            fields->m_staticThumbLoad,
            fields->m_thumbnailRequestTime,
            thumbResetBundle(),
        };
    }

    void scheduleGalleryAutocycle(float delay = 4.5f) {
        this->unschedule(schedule_selector(PaimonLevelCell::updateGalleryCycle));
        this->schedule(schedule_selector(PaimonLevelCell::updateGalleryCycle), delay);
    }

    void handleRemoteThumbnailInvalidation(int invalidLevelID) {
        if (!m_level || m_level->m_levelID.value() != invalidLevelID) return;
        auto fields = m_fields.self();
        if (!fields) return;
        auto bundle = thumbResetBundle();
        paimon::thumbnails::levelcell::invalidateForRemoteThumbnail(
            bundle,
            ThumbnailLoader::get().getInvalidationVersion(invalidLevelID)
        );
        tryLoadThumbnail();
    }

    paimon::thumbnails::levelcell::LocalVideoHost makeLocalVideoHost() {
        WeakRef<PaimonLevelCell> self = this;
        return {
            .refreshScheduling = [self]() {
                if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                    cell->refreshRuntimeScheduling();
                }
            },
            .applyThumbTexture = [self](CCTexture2D* tex) {
                if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                    cell->addOrUpdateThumb(tex);
                }
            },
            .hideSpinner = [self]() {
                if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                    cell->hideLoadingSpinner();
                }
            },
            .storePlayer = [self](std::unique_ptr<paimon::video::VideoPlayer> player) {
                if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                    if (auto f = cell->m_fields.self()) f->m_videoPlayer = std::move(player);
                }
            },
            .getPlayer = [self]() -> paimon::video::VideoPlayer* {
                if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                    if (auto f = cell->m_fields.self()) return f->m_videoPlayer.get();
                }
                return nullptr;
            },
            .setHasVideo = [self](bool value) {
                if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                    if (auto f = cell->m_fields.self()) f->m_hasVideo = value;
                }
            },
            .setThumbnailApplied = [self](bool value) {
                if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                    if (auto f = cell->m_fields.self()) f->m_thumbnailApplied = value;
                }
            },
        };
    }

    paimon::thumbnails::levelcell::ServerVideoHost makeServerVideoHost() {
        WeakRef<PaimonLevelCell> self = this;
        return {
            .shouldHandle = [self](int32_t levelID, int requestId) {
                if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                    return cell->shouldHandleThumbnailCallback(levelID, requestId);
                }
                return false;
            },
            .showSpinner = [self]() {
                if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                    cell->showLoadingSpinner();
                }
            },
            .hideSpinner = [self]() {
                if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                    cell->hideLoadingSpinner();
                }
            },
            .retryThumbnailLoad = [self]() {
                if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                    if (auto f = cell->m_fields.self()) f->m_thumbnailRequested = false;
                    cell->tryLoadThumbnail();
                }
            },
            .applyThumb = [self](CCTexture2D* tex, VideoThumbnailSprite* driver) {
                if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                    cell->addOrUpdateThumb(tex, driver);
                }
            },
            .attachPendingDriver = [self](VideoThumbnailSprite* sprite) {
                if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                    cell->addChild(sprite, -1000);
                }
            },
            .setHasVideo = [self](bool value) {
                if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                    if (auto f = cell->m_fields.self()) f->m_hasVideo = value;
                }
            },
            .setThumbnailApplied = [self](bool value) {
                if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                    if (auto f = cell->m_fields.self()) f->m_thumbnailApplied = value;
                }
            },
            .setGalleryIndex = [self](int index) {
                if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                    if (auto f = cell->m_fields.self()) f->m_galleryIndex = index;
                }
            },
            .scheduleGalleryCycle = [self]() {
                if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                    cell->scheduleGalleryAutocycle();
                }
            },
            .galleryThumbCount = [self]() -> size_t {
                if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                    if (auto f = cell->m_fields.self()) return f->m_galleryThumbnails.size();
                }
                return 0;
            },
            .galleryAutocycle = [self]() -> bool {
                if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                    if (auto f = cell->m_fields.self()) return f->m_cachedGalleryAutocycle;
                }
                return false;
            },
        };
    }

    void resetThumbnailRequestFlags() {
        auto fields = m_fields.self();
        if (!fields) return;
        fields->m_thumbnailRequested = false;
        fields->m_thumbnailApplied = false;
        fields->m_thumbnailFailed = false;
    }



    void flashThumbnailSprite() {
        auto fields = m_fields.self();
        if (!fields || !fields->m_thumbSprite) {
            return;
        }

        auto flash = CCLayerColor::create({255, 255, 255, 255});
        flash->setContentSize(fields->m_thumbSprite->getContentSize());
        flash->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        fields->m_thumbSprite->addChild(flash, 100);
        flash->runAction(CCSequence::create(CCFadeOut::create(0.5f), CCRemoveSelf::create(), nullptr));
    }

    void applyMainLevelFallbackThumbnail(int32_t levelID) {
        // Only apply the black placeholder to official levels (1-100). For online
        // levels (>100) without an uploaded thumbnail, show nothing and let the
        // cell use its normal gradient background; a scaled 1x1 black placeholder
        // looks worse than nothing.
        if (levelID <= 0 || levelID > 100) {
            return;
        }

        // Shared 1x1 black placeholder, built once and reused across every official
        // level fallback (and every retry). Intentionally leaked (no autorelease, no
        // Ref): the raw retainCount keeps it alive for the process lifetime, which
        // avoids both per-fallback allocation and a static-destruction release
        // touching a dead CCTextureCache at exit. Sprites share it via retain/release.
        static CCTexture2D* s_blackTex = []() -> CCTexture2D* {
            uint8_t blackPixel[4] = {0, 0, 0, 255};
            auto* tex = new CCTexture2D();
            if (tex->initWithData(blackPixel, kCCTexture2DPixelFormat_RGBA8888, 1, 1, CCSize(1, 1))) {
                return tex;
            }
            // initWithData failed: free via release(), not delete (CCObject refcount).
            tex->release();
            return nullptr;
        }();

        if (s_blackTex) {
            this->addOrUpdateThumb(s_blackTex);
        }
    }

    void applyStaticThumbnailTexture(int32_t levelID, int currentRequestId, CCTexture2D* texture, bool enableSpinners) {
        if (!this->shouldHandleThumbnailCallback(levelID, currentRequestId)) {
            return;
        }

        auto fields = m_fields.self();
        fields->m_staticThumbLoad.reset();

        if (enableSpinners) {
            this->hideLoadingSpinner();
        }

        if (fields->m_thumbnailApplied) {
            return;
        }

        if (!texture) {
            // Load failed. m_thumbnailFailed=true stops the maintenance tick's
            // automatic 200ms retry loop (which otherwise spams logs forever);
            // the flag clears on level change, invalidation, or settings change.
            // Official levels (1-100) get a 1x1 black placeholder for consistency.
            fields->m_thumbnailRequested = false;
            fields->m_thumbnailApplied = false;
            fields->m_thumbnailFailed = true;
            this->applyMainLevelFallbackThumbnail(levelID);
            return;
        }

        fields->m_staticTexture = texture;
        fields->m_thumbnailFailed = false;

        // No stagger: show thumbnails immediately
        this->addOrUpdateThumb(texture);

        // addOrUpdateThumb creates the sprite directly; if it didn't (e.g. null
        // background layer), the maintenance tick retries.
        if (fields->m_thumbSprite && fields->m_thumbSprite->getParent()) {
            fields->m_thumbnailApplied = true;
            this->flashThumbnailSprite();
        }
    }

    void ensureMaintenanceTickScheduled() {
        auto fields = m_fields.self();
        if (!fields || fields->m_isBeingDestroyed || fields->m_isMaintenanceScheduled) {
            return;
        }

        this->schedule(schedule_selector(PaimonLevelCell::updateMaintenance), LEVELCELL_MAINTENANCE_INTERVAL);
        fields->m_isMaintenanceScheduled = true;
    }

    void refreshRuntimeScheduling() {
        auto fields = m_fields.self();
        if (!fields || fields->m_isBeingDestroyed) {
            return;
        }

        cacheSettings();

        bool wantsCenterAnimation =
            fields->m_cachedHoverEnabled &&
            fields->m_clippingNode &&
            fields->m_thumbSprite;

        if (wantsCenterAnimation) {
            if (!fields->m_isCenterAnimScheduled) {
                this->schedule(schedule_selector(PaimonLevelCell::updateCenterAnimation), LEVELCELL_VISUAL_TICK_INTERVAL);
                fields->m_isCenterAnimScheduled = true;
            }
        }
        else if (fields->m_isCenterAnimScheduled) {
            this->unschedule(schedule_selector(PaimonLevelCell::updateCenterAnimation));
            fields->m_isCenterAnimScheduled = false;
        }

        bool wantsGradientAnimation =
            fields->m_cachedAnimatedGradient &&
            fields->m_gradientLayer &&
            fields->m_gradientIsPSG;

        if (wantsGradientAnimation) {
            if (!fields->m_isGradientAnimScheduled) {
                this->schedule(schedule_selector(PaimonLevelCell::updateGradientAnim), LEVELCELL_VISUAL_TICK_INTERVAL);
                fields->m_isGradientAnimScheduled = true;
            }
        }
        else if (fields->m_isGradientAnimScheduled) {
            this->unschedule(schedule_selector(PaimonLevelCell::updateGradientAnim));
            fields->m_isGradientAnimScheduled = false;
        }

        bool wantsFrameUpdate =
            fields->m_hasVideo &&
            fields->m_videoPlayer &&
            fields->m_videoPlayer->isPlaying();

        if (wantsFrameUpdate) {
            this->scheduleUpdate();
        }
        else {
            this->unscheduleUpdate();
        }

        ensureMaintenanceTickScheduled();
    }

    void startLazyStaticThumbnailLoad(int32_t levelID, int currentRequestId, bool enableSpinners, CCTexture2D* fallbackTexture) {
        auto fields = m_fields.self();
        if (!fields) {
            return;
        }

        auto path = resolveLazyStaticThumbnailPath(levelID);
        if (!path) {
            this->applyStaticThumbnailTexture(levelID, currentRequestId, fallbackTexture, enableSpinners);
            return;
        }

        WeakRef<PaimonLevelCell> safeRef = this;
        Ref<CCTexture2D> fallbackRef = fallbackTexture;
        fields->m_staticThumbLoad.loadFromFile(std::filesystem::path(*path), [
            safeRef,
            levelID,
            currentRequestId,
            enableSpinners,
            fallbackRef
        ](CCTexture2D* texture, bool success) {
            auto cellRef = safeRef.lock();
            auto* cell = static_cast<PaimonLevelCell*>(cellRef.data());
            if (!cell || !cell->getParent()) {
                return;
            }

            cell->applyStaticThumbnailTexture(
                levelID,
                currentRequestId,
                success ? texture : fallbackRef.data(),
                enableSpinners
            );
        });
    }

    void cleanPaimonNodes(CCNode* bg) {
        auto fields = m_fields.self();
        
        // Safely remove tracked nodes
        auto removeNodeSafe = [](auto& node) {
            if (node) {
                if (node->getParent()) node->removeFromParent();
                node = nullptr; 
            }
        };

        // If no tracked Ref<> existed, there are probably no leftover "paimon"
        // nodes — skip the scan.
        bool anyTrackedNode = fields->m_clippingNode || fields->m_separator ||
            fields->m_gradient || fields->m_hoverContainer || fields->m_boundsClipper ||
            fields->m_mythicParticles || fields->m_darkOverlay || fields->m_gradientLayer ||
            fields->m_thumbSprite;

        removeNodeSafe(fields->m_clippingNode);
        removeNodeSafe(fields->m_separator);
        removeNodeSafe(fields->m_gradient);
        removeNodeSafe(fields->m_hoverContainer);
        removeNodeSafe(fields->m_boundsClipper);
        
        // CCParticleSystemQuad needs manual handling
        if (fields->m_mythicParticles) {
            if (fields->m_mythicParticles->getParent()) fields->m_mythicParticles->removeFromParent();
            fields->m_mythicParticles = nullptr;
        }

        removeNodeSafe(fields->m_darkOverlay);
        
        fields->m_gradientLayer = nullptr;
        fields->m_gradientIsPSG = false;
        fields->m_thumbSprite = nullptr;
        fields->m_thumbTypeCached = false;
        fields->m_staticThumbLoad.reset();
        fields->m_loadingSpinner = nullptr; // normally managed via show/hide; clear here for safety
        fields->m_lastClipHoverOffsetX = 0.0f;
        fields->m_lastClipHoverPosAdjustment = 0.0f;
        fields->m_lastClipHoverScaleX = 1.0f;
        fields->m_lastClipHoverRotation = 0.0f;
        fields->m_lastSeparatorHoverOffsetX = 0.0f;
        fields->m_lastSeparatorHoverRotation = 0.0f;
        fields->m_lastSpriteHoverScale = 1.0f;
        fields->m_lastSpriteHoverRotation = 0.0f;
        fields->m_lastSpriteHoverOffsetX = 0.0f;
        fields->m_lastBoundsHoverScale = 1.0f;
        fields->m_lastBoundsHoverOffsetX = 0.0f;

        // Skip the children scan if there were no prior paimon nodes (~2ms per fresh cell).
        if (!anyTrackedNode) {
            return;
        }

        // Clean up leftovers with "paimon" id
        auto cleanByID = [](CCNode* parent) {
            if (!parent) return;
            auto children = parent->getChildren();
            if (!children) return;
            std::vector<CCNode*> toRemove;
            toRemove.reserve(8);
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (child) {
                    std::string_view id = child->getID();
                    // our IDs start with "paimon"
                    if (id.find("paimon") != std::string_view::npos) {
                        toRemove.push_back(child);
                    }
                }
            }
            for (auto* node : toRemove) node->removeFromParent();
        };
        cleanByID(bg);
        cleanByID(this);
    }

    CCSprite* createThumbnailSprite(CCTexture2D* texture, bool allowLevelGIF = true) {
        if (!texture) return nullptr;
        int32_t levelIDForGIF = m_level ? m_level->m_levelID.value() : 0;
        bool hasLevelGIF = allowLevelGIF && levelIDForGIF > 0 && ThumbnailLoader::get().hasGIFData(levelIDForGIF);
        std::string gifPath = hasLevelGIF
            ? geode::utils::string::pathToString(ThumbnailLoader::get().getCachePath(levelIDForGIF, true))
            : std::string();

        if (hasLevelGIF && AnimatedGIFSprite::isCached(gifPath)) {
            if (auto gifSprite = AnimatedGIFSprite::createFromCache(gifPath)) {
                gifSprite->setID("paimon-thumbnail"_spr);
                gifSprite->play();
                return gifSprite;
            }
        }

        CCSprite* sprite = PaimonShaderSprite::createWithTexture(texture);
        if (!sprite) return nullptr;

        if (hasLevelGIF) {
            // Don't set opacity(0) to avoid blank cells
            
            WeakRef<PaimonLevelCell> safeRef = this;
            int currentRequestId = m_fields->m_requestId;
            AnimatedGIFSprite::createAsync(gifPath, [safeRef, levelIDForGIF, currentRequestId](AnimatedGIFSprite* anim) {
                auto cellRef = safeRef.lock();
                auto* cell = static_cast<PaimonLevelCell*>(cellRef.data());
                if (!cell || !cell->getParent()) return;
                if (!cell->shouldHandleThumbnailCallback(levelIDForGIF, currentRequestId)) return;
                auto fields = cell->m_fields.self();
                if (anim && anim->getFrameCount() >= 2 && fields->m_thumbSprite) {
                    auto old = fields->m_thumbSprite;
                    auto parent = old->getParent();
                    if (parent) {
                        anim->setScaleX(old->getScaleX());
                        anim->setScaleY(old->getScaleY());
                        anim->setPosition(old->getPosition());
                        anim->setAnchorPoint(old->getAnchorPoint());
                        anim->setSkewX(old->getSkewX());
                        anim->setSkewY(old->getSkewY());
                        anim->setZOrder(old->getZOrder());
                        anim->setColor(old->getColor());
                        anim->setOpacity(255);
                        anim->setID("paimon-thumbnail"_spr);
                        anim->play();
                        
                        old->removeFromParent();
                        parent->addChild(anim);
                        fields->m_thumbSprite = anim;
                    }
                }
                // If GIF fails, the static sprite stays visible
            });
        }
        
        if (sprite) {
            sprite->setID("paimon-thumbnail"_spr);
            // Ensure ID for shader detection
            if (auto pss = typeinfo_cast<PaimonShaderSprite*>(sprite)) {
                 pss->setID("paimon-shader-sprite"_spr);
            }
            
            std::string bgType = Mod::get()->getSavedValue<std::string>("levelcell-background-type", "thumbnail");

            // Effects handled in updateCenterAnimation
        }
        return sprite;
    }

    void setupClippingAndSeparator(CCNode* bg, CCSprite* sprite) {
        auto fields = m_fields.self();
        if (!fields) return;
        cacheSettings();
        log::debug("[LevelCell] setupClippingAndSeparator: entering");

        // force full width for Daily cells
        bool isDaily = false;
        if (m_level && m_level->m_dailyID > 0) isDaily = true;

        float coverScale = 1.0f;
        auto clippingNode = createThumbnailClippingNode(bg, sprite, coverScale);
        if (!clippingNode) return;

        auto bgSize = bg->getContentSize();
        auto bgPos = bg->getPosition();

        // Master clipper limiting to the cell's visible area
        auto boundsStencil = paimon::SpriteHelper::createRectStencil(bgSize.width, bgSize.height);
        if (!boundsStencil) return;
        auto boundsClipper = paimon::ScissorClipNode::create(boundsStencil);
        if (!boundsClipper) return;
        boundsClipper->setContentSize(bgSize);
        boundsClipper->setPosition(bgPos);
        boundsClipper->setAnchorPoint({0, 0});
        boundsClipper->setZOrder(-1);
        boundsClipper->setID("paimon-bounds-clipper"_spr);
        this->addChild(boundsClipper);

        // Intermediate container for hover zoom
        auto hoverContainer = CCNode::create();
        hoverContainer->setContentSize(bgSize);
        hoverContainer->setAnchorPoint({0, 0});
        hoverContainer->setPosition({0, 0});
        hoverContainer->setID("paimon-hover-container"_spr);
        boundsClipper->addChild(hoverContainer);

        hoverContainer->addChild(clippingNode);

        fields->m_boundsClipper = boundsClipper;
        fields->m_hoverContainer = hoverContainer;
        fields->m_thumbSprite = sprite;
        fields->m_thumbBasePos = sprite->getPosition();
        fields->m_clipBasePos = clippingNode->getPosition();
        fields->m_thumbBaseScaleX = coverScale;
        fields->m_thumbBaseScaleY = coverScale;
        fields->m_lastClipHoverOffsetX = 0.0f;
        fields->m_lastClipHoverPosAdjustment = 0.0f;
        fields->m_lastClipHoverScaleX = 1.0f;
        fields->m_lastClipHoverRotation = 0.0f;
        fields->m_lastSeparatorHoverOffsetX = 0.0f;
        fields->m_lastSeparatorHoverRotation = 0.0f;
        fields->m_lastSpriteHoverScale = 1.0f;
        fields->m_lastSpriteHoverRotation = 0.0f;
        fields->m_lastSpriteHoverOffsetX = 0.0f;
        log::debug("[LevelCell] setupClippingAndSeparator: coverScale={:.4f} clipPos=({:.1f},{:.1f}) thumbPos=({:.1f},{:.1f})",
            coverScale, clippingNode->getPosition().x, clippingNode->getPosition().y, sprite->getPosition().x, sprite->getPosition().y);
        
        fields->m_clippingNode = clippingNode;

        bool showSeparator = fields->m_cachedShowSeparator;
        const float bgWidth = bgSize.width;
        CCSize scaledSize = clippingNode->getContentSize();

        if (showSeparator && !isDaily) { // No separator for Daily
            float separatorXMul = m_compactView ? 0.75f : 1.0f;
            const float kDiagonalSkew = 35.f;
            const float sepW = 3.f; // separator visual thickness

            auto separator = PaimonDrawNode::create();
            separator->setID("paimon-separator"_spr);

            // Diagonal parallelogram matching the thumbnail cut
            float h = scaledSize.height;
            CCPoint poly[4] = {
                ccp(0.f,           0.f),
                ccp(sepW,          0.f),
                ccp(sepW + kDiagonalSkew, h),
                ccp(kDiagonalSkew, h)
            };
            ccColor4F sepColor = {0.f, 0.f, 0.f, 50.f / 255.f};
            separator->drawPolygon(poly, 4, sepColor, 0.f, sepColor);

            separator->setContentSize({sepW + kDiagonalSkew, h});
            separator->setAnchorPoint({1.f, 0.f});
            separator->setPosition({bgWidth - (20.f * separatorXMul), 0.f});
            separator->setZOrder(-2);

            fields->m_separator = separator;
            fields->m_separatorBasePos = separator->getPosition();
            hoverContainer->addChild(separator);
        }
    }

    void setupThumbnailBackground(CCNode* bg, int levelID, CCTexture2D* texture, bool transparentMode) {
        auto fields = m_fields.self();
        Ref<CCNode> safeBg = bg;
             // Hide bg but keep the node visible for children
             bg->setVisible(true);
             if (auto* bgLayer = typeinfo_cast<CCLayerColor*>(bg)) {
                 // Transparent mode: hide the color quad but keep contentSize
                 if (transparentMode) {
                     auto size = bgLayer->getContentSize();
                     bgLayer->changeWidthAndHeight(0.f, 0.f);
                     bgLayer->setContentSize(size);
                 } else {
                     bgLayer->setOpacity(0);
                 }
             }
             float blurIntensity = static_cast<float>(Mod::get()->getSavedValue<double>("levelcell-background-blur", 3.0));
             bool hasGifBackground = ThumbnailLoader::get().hasGIFData(levelID);
             std::string gifPath = hasGifBackground
                 ? geode::utils::string::pathToString(ThumbnailLoader::get().getCachePath(levelID, true))
                 : std::string();

             auto ensureBgClipper = [safeBg]() -> CCClippingNode* {
                 if (!safeBg || !safeBg->getParent()) return nullptr;
                 if (auto existing = typeinfo_cast<CCClippingNode*>(static_cast<CCNode*>(safeBg->getChildByID("paimon-bg-clipper"_spr)))) {
                     return existing;
                 }

                 auto stencil = paimon::SpriteHelper::createRectStencil(safeBg->getContentWidth(), safeBg->getContentHeight());
                 if (!stencil) return nullptr;
                 auto clipper = paimon::ScissorClipNode::create(stencil);
                 if (!clipper) {
                     return nullptr;
                 }
                 clipper->setContentSize(safeBg->getContentSize());
                 clipper->setPosition({0,0});
                 clipper->setZOrder(10);
                 clipper->setID("paimon-bg-clipper"_spr);
                 safeBg->addChild(clipper);
                 safeBg->reorderChild(clipper, 10);
                 return clipper;
             };

             auto attachOverlay = [safeBg, fields](CCClippingNode* clipper) {
                 if (!safeBg || !safeBg->getParent() || !clipper) return;
                 if (auto oldOverlay = clipper->getChildByID("paimon-level-background-overlay"_spr)) {
                     oldOverlay->removeFromParent();
                 }

                 float darkness = fields->m_cachedBackgroundDarkness;
                 GLubyte opacity = static_cast<GLubyte>(std::clamp(darkness, 0.0f, 1.0f) * 255.0f);
                 auto overlay = paimon::SpriteHelper::createDarkPanel(safeBg->getContentWidth(), safeBg->getContentHeight(), opacity, 0.f);
                 if (!overlay) return;
                 overlay->setPosition({0, 0});
                 overlay->setID("paimon-level-background-overlay"_spr);
                 clipper->addChild(overlay);
                 fields->m_darkOverlay = overlay;
             };

             auto attachBackgroundSprite = [safeBg, fields, ensureBgClipper, attachOverlay](CCSprite* mediaSprite) {
                 if (!safeBg || !safeBg->getParent()) return;
                 auto clipper = ensureBgClipper();
                 if (!clipper) return;

                 if (auto oldMedia = clipper->getChildByID("paimon-level-background"_spr)) {
                     oldMedia->removeFromParent();
                 }

                 if (mediaSprite) {
                     float targetW = safeBg->getContentWidth();
                     float targetH = safeBg->getContentHeight();
                     float scale = safeCoverScale(
                         targetW, targetH,
                         mediaSprite->getContentSize().width, mediaSprite->getContentSize().height,
                         1.0f
                     );
                     mediaSprite->setScale(scale);
                     mediaSprite->setPosition(safeBg->getContentSize() / 2);
                     mediaSprite->setID("paimon-level-background"_spr);
                     clipper->addChild(mediaSprite);
                     fields->m_gradientLayer = mediaSprite;
                 } else {
                     fields->m_gradientLayer = nullptr;
                 }

                 attachOverlay(clipper);
             };

             // Retain the texture for the lambdas' lifetime
             Ref<CCTexture2D> texRef = texture;

             // Placeholder without blur; real blur runs async via dispatchAsyncBlur
             auto createStaticBackground = [texRef]() -> CCSprite* {
                 return PaimonShaderSprite::createWithTexture(texRef.data());
             };

             // Dispatch async blur and swap the placeholder when done; uses the
             // RAM (LRU) cache for instant re-entries.
             int captured_requestId = fields->m_requestId;
             int captured_blurToken = ++fields->m_bgBlurToken;
             WeakRef<PaimonLevelCell> blurSafeRef = this;
              auto dispatchAsyncBlur = [blurSafeRef, levelID, texRef, blurIntensity, captured_requestId, captured_blurToken]() {
                  auto* texPtr = texRef.data();
                  if (!texPtr) return;
                  auto cellRef = blurSafeRef.lock();
                  auto* cell = static_cast<PaimonLevelCell*>(cellRef.data());
                  if (!cell || !cell->getParent()) return; // cell destroyed or off-scene
                  auto bgNode = cell->m_backgroundLayer;
                  if (!bgNode) return;

                 CCSize targetSize = bgNode->getContentSize();
                 targetSize.width = std::max(targetSize.width, 512.f);
                 targetSize.height = std::max(targetSize.height, 256.f);

                 BlurSystem::getInstance()->buildPaimonBlurAsync(
                     texPtr,
                     targetSize,
                     blurIntensity,
                     makeLevelCellBlurCacheKey(levelID, cell->m_fields->m_galleryIndex, blurIntensity, true),
                      [blurSafeRef, levelID, captured_requestId, captured_blurToken](CCSprite* blurred) {
                          if (!blurred) return;
                          auto cellRef2 = blurSafeRef.lock();
                          auto* cell2 = static_cast<PaimonLevelCell*>(cellRef2.data());
                          // Verify the cell is still alive and on-scene before touching nodes
                          if (!cell2 || !cell2->getParent() || !cell2->shouldHandleThumbnailCallback(levelID, captured_requestId)) return;
                          auto fields2 = cell2->m_fields.self();
                          if (!fields2 || fields2->m_isBeingDestroyed) return;
                          if (fields2->m_bgBlurToken != captured_blurToken) return;

                         auto bg2 = cell2->m_backgroundLayer;
                         if (!bg2) return;
                         auto* clipper = typeinfo_cast<CCClippingNode*>(static_cast<CCNode*>(bg2->getChildByID("paimon-bg-clipper"_spr)));
                         if (!clipper) return;

                         // Swap placeholder for the blurred sprite
                         if (auto oldMedia = clipper->getChildByID("paimon-level-background"_spr)) {
                             oldMedia->removeFromParent();
                         }

                         float scale = safeCoverScale(
                             bg2->getContentWidth(), bg2->getContentHeight(),
                             blurred->getContentSize().width, blurred->getContentSize().height,
                             1.0f
                         );
                         blurred->setScale(scale);
                         blurred->setPosition(bg2->getContentSize() / 2);
                         blurred->setID("paimon-level-background"_spr);
                         clipper->addChild(blurred);
                         fields2->m_gradientLayer = blurred;
                     });
             };

             // Helper: attach placeholder + dispatch async blur
             auto attachStaticWithAsyncBlur = [attachBackgroundSprite, createStaticBackground, dispatchAsyncBlur]() {
                 attachBackgroundSprite(createStaticBackground());
                 dispatchAsyncBlur();
             };

             auto configureGifBackground = [blurIntensity](AnimatedGIFSprite* anim) {
                 if (!anim) return;
                 if (auto shader = Shaders::getBlurCellShader()) {
                     anim->setShaderProgram(shader);
                     anim->m_intensity = std::clamp((blurIntensity - 1.0f) / 9.0f, 0.0f, 1.0f);
                     if (auto* animTex = anim->getTexture()) {
                         anim->m_texSize = animTex->getContentSizeInPixels();
                     } else {
                         anim->m_texSize = anim->getContentSize();
                     }
                 }
                 anim->play();
             };

             if (hasGifBackground) {
                 if (AnimatedGIFSprite::isCached(gifPath)) {
                     if (auto anim = AnimatedGIFSprite::createFromCache(gifPath)) {
                         configureGifBackground(anim);
                         attachBackgroundSprite(anim);
                     } else {
                         attachStaticWithAsyncBlur();
                     }
                 } else {
                     attachBackgroundSprite(nullptr);
                     WeakRef<PaimonLevelCell> gradSafeRef = this;
                     AnimatedGIFSprite::createAsync(gifPath, [gradSafeRef, levelID, configureGifBackground, attachBackgroundSprite, attachStaticWithAsyncBlur](AnimatedGIFSprite* anim) {
                         auto selfRef = gradSafeRef.lock();
                         auto* self = static_cast<PaimonLevelCell*>(selfRef.data());
                         if (!self || !self->getParent()) return;
                         if (!self->m_level || self->m_level->m_levelID != levelID) return;

                         if (anim) {
                             configureGifBackground(anim);
                             attachBackgroundSprite(anim);
                         } else {
                             attachStaticWithAsyncBlur();
                         }
                     });
                 }
             } else {
                 attachStaticWithAsyncBlur();
             }

             return;
    }

    void setupColorGradient(CCNode* bg, int levelID) {
        auto fields = m_fields.self();
        // Hide the whole bg for the gradient
        bg->setVisible(false);

        ccColor3B colorA = {0, 0, 0};
        ccColor3B colorB = {255, 0, 0};

        if (auto pair = LevelColors::get().getPair(levelID)) {
            colorA = pair->a;
            colorB = pair->b;
        }

        bool animatedGradient = fields->m_cachedAnimatedGradient;

        auto grad = PaimonShaderGradient::create(
            ccc4(colorA.r, colorA.g, colorA.b, 255),
            ccc4(colorB.r, colorB.g, colorB.b, 255)
        );
        grad->setContentSize({ bg->getContentWidth() + 2.f, bg->getContentHeight() + 1.f });
        grad->setAnchorPoint({0,0});
        grad->setPosition({0.0f, 0.0f});
        
        // Add gradient to 'this' to avoid BatchNode issues
        int bgZ = bg->getZOrder();
        grad->setZOrder(bgZ - 1); // Behind bg
        grad->setID("paimon-level-gradient"_spr);
        
        grad->setPosition(bg->getPosition());
        
        this->addChild(grad);
        
        fields->m_gradient = grad;

        fields->m_gradientLayer = grad;
        fields->m_gradientIsPSG = (typeinfo_cast<PaimonShaderGradient*>(static_cast<CCSprite*>(grad)) != nullptr);
        fields->m_gradientColorA = colorA;
        fields->m_gradientColorB = colorB;

        (void)animatedGradient;
    }

    void setupGradient(CCNode* bg, int levelID, CCTexture2D* texture) {
        auto fields = m_fields.self();
        Ref<CCNode> safeBg = bg; // Retain bg for async callbacks to avoid dangling pointer
        log::debug("[LevelCell] setupGradient: levelID={} hasTexture={}", levelID, texture != nullptr);

        // Clean up previous background nodes
        if (auto children = bg->getChildren()) {
            std::vector<CCNode*> toRemove;
            toRemove.reserve(4);
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (!child) continue;
                std::string_view childID = child->getID();
                if (childID.find("paimon-level-gradient") != std::string_view::npos ||
                    childID.find("paimon-bg-clipper") != std::string_view::npos ||
                    childID == "paimon-level-background") {
                    toRemove.push_back(child);
                }
            }
            for (auto node : toRemove) node->removeFromParent();
        }
        fields->m_gradientLayer = nullptr;

        cacheSettings();
        PaimonBgType bgType = fields->m_cachedBgType;

        // Read transparent mode directly (don't rely only on the cache)
        bool transparentMode = Mod::get()->getSavedValue<bool>("transparent-list-mode", false);

        if (bgType == PaimonBgType::Thumbnail && texture) {
            setupThumbnailBackground(bg, levelID, texture, transparentMode);
            return;
        }

        // Gradient case: in transparent mode, just hide the bg without adding a gradient
        if (transparentMode) {
            bg->setVisible(true);
            if (auto* bgLayer = typeinfo_cast<CCLayerColor*>(bg)) {
                auto size = bgLayer->getContentSize();
                bgLayer->changeWidthAndHeight(0.f, 0.f);
                bgLayer->setContentSize(size);
            }
            return;
        }

        setupColorGradient(bg, levelID);
    }

    void setupMythicParticles(CCNode* bg, int levelID) {
        auto fields = m_fields.self();
        cacheSettings();
        bool enableMythic = fields->m_cachedMythicParticles;

        if (enableMythic && m_level && m_level->m_isEpic >= 3) {
                auto brighten = [](ccColor3B c) {
                    auto clamp = [](int v){ return std::max(0, std::min(255, v)); };
                    int add = 35;
                    return ccColor3B{ (GLubyte)clamp(c.r + add), (GLubyte)clamp(c.g + add), (GLubyte)clamp(c.b + add) };
                };
                ccColor3B ca{220,220,220}, cb{255,255,255};
                if (auto pair2 = LevelColors::get().getPair(levelID)) {
                    ca = brighten(pair2->a);
                    cb = brighten(pair2->b);
                }
                
                auto ps = CCParticleSystemQuad::create();
                if (!ps) return;
                
                ps->setBlendAdditive(false);
                ps->setID("paimon-mythic-particles"_spr);
                ps->setEmitterMode(kCCParticleModeGravity);
                ps->setGravity({0.f, 0.f});
                ps->setAngle(0.f);
                ps->setAngleVar(6.f);
                
                float width = bg->getContentWidth();
                float speed = 160.f;
                float life = (0.70f * width) / speed;
                if (life < 0.4f) life = 0.4f;
                
                ps->setSpeed(speed);
                ps->setSpeedVar(20.f);
                ps->setLife(life);
                ps->setLifeVar(life * 0.15f);
                
                float height = bg->getContentHeight();
                ps->setPosition({0.f, height * 0.5f});
                ps->setPosVar({0.f, height * 0.5f});
                
                ps->setStartSize(3.0f);
                ps->setStartSizeVar(1.2f);
                ps->setEndSize(2.0f);
                ps->setEndSizeVar(1.0f);
                
                ccColor4F startColorA{ ca.r / 255.f, ca.g / 255.f, ca.b / 255.f, 0.80f };
                ccColor4F startColorB{ cb.r / 255.f, cb.g / 255.f, cb.b / 255.f, 0.80f };
                ccColor4F base{
                    (startColorA.r + startColorB.r) * 0.5f,
                    (startColorA.g + startColorB.g) * 0.5f,
                    (startColorA.b + startColorB.b) * 0.5f,
                    0.80f
                };
                ccColor4F var{
                    fabsf(startColorA.r - startColorB.r) * 0.5f,
                    fabsf(startColorA.g - startColorB.g) * 0.5f,
                    fabsf(startColorA.b - startColorB.b) * 0.5f,
                    0.05f
                };
                ps->setStartColor(base);
                ps->setStartColorVar(var);
                ccColor4F end = base; end.a = 0.f;
                ccColor4F endVar = var; endVar.a = 0.05f;
                ps->setEndColor(end);
                ps->setEndColorVar(endVar);
                
                ps->setTotalParticles(120);
                ps->setEmissionRate(120.f / life);
                ps->setDuration(-1.f);
                ps->setPositionType(kCCPositionTypeRelative);
                ps->setAutoRemoveOnFinish(false);
                
                fields->m_mythicParticles = ps;
                
                // Fix: Add particles to 'this' instead of 'bg'
                ps->setPosition(ps->getPosition() + bg->getPosition());
                this->addChild(ps, bg->getZOrder() + 1); // Above bg
                
                ps->resetSystem();
        }
    }

    // Is this menu item the "view" button?
    static bool isViewButtonItem(CCMenuItemSpriteExtra* menuItem, bool checkDailyPos, bool isDaily, CCSize const& cellSize) {        std::string_view id = menuItem->getID();
        if (id == "view-button" || id == "main-button" || id == "paimon-view-button") return true;
        if (id == "paimon-view-button"_spr) return true;

        if (auto ni = menuItem->getNormalImage()) {
            if (auto ch = ni->getChildren()) {
                for (auto* child : CCArrayExt<CCNode*>(ch)) {
                    if (auto lbl = typeinfo_cast<CCLabelBMFont*>(child)) {
                        auto tl = geode::utils::string::toLower(std::string(lbl->getString()));
                        if (tl.find("view") != std::string::npos || tl.find("ver") != std::string::npos ||
                            tl.find("get it") != std::string::npos || tl.find("play") != std::string::npos ||
                            tl.find("safe") != std::string::npos) {
                            return true;
                        }
                    }
                }
            }
        }

        if (checkDailyPos && isDaily) {
            if (menuItem->getPosition().x > cellSize.width * 0.4f) return true;
        }
        return false;
    }

    // Find the "view" button via a single DFS
    void findAndSetupViewButton() {
        auto fields = m_fields.self();
        cacheSettings();
        bool isDaily = isDailyCell();

        bool showButton = fields->m_cachedShowViewButton;
        if (showButton) return;

        // Skip DFS if button already found and still in the tree
        if (fields->m_viewOverlay && fields->m_viewOverlay->getParent()) return;

        auto cellSize = this->getContentSize();
        constexpr float kAreaWidth = 90.f;
        float areaHeight = cellSize.height;

        // DFS over all children
        std::vector<CCNode*> stack;
        stack.reserve(32);        stack.push_back(this);

        while (!stack.empty()) {
            CCNode* cur = stack.back();
            stack.pop_back();
            if (!cur) continue;

            if (auto menuItem = typeinfo_cast<CCMenuItemSpriteExtra*>(cur)) {
                if (isViewButtonItem(menuItem, true, isDaily, cellSize)) {
                    if (std::string_view(menuItem->getID()) == "paimon-view-button" ||
                        std::string_view(menuItem->getID()) == "paimon-view-button"_spr) {
                        menuItem->setID("view-button");
                    }
                    fields->m_viewOverlay = menuItem;
                    menuItem->m_baseScale = menuItem->getScale();
                    menuItem->setVisible(true);
                    menuItem->setEnabled(true);

                    if (!isDaily) {
                        // Create invisible overlay sprites
                        auto makeInvisible = [kAreaWidth, areaHeight]() {
                            auto s = CCSprite::create();
                            s->setContentSize({kAreaWidth, areaHeight});
                            s->setAnchorPoint({0.5f, 0.5f});
                            return s;
                        };

                        if (auto img = menuItem->getNormalImage()) img->setVisible(false);
                        if (auto img = menuItem->getSelectedImage()) img->setVisible(false);
                        if (auto img = menuItem->getDisabledImage()) img->setVisible(false);

                        menuItem->setNormalImage(makeInvisible());
                        menuItem->setSelectedImage(makeInvisible());
                        menuItem->setDisabledImage(makeInvisible());

                        // Position overlay at the thumbnail edge
                        CCPoint centerLocal;
                        if (fields->m_clippingNode) {
                            CCPoint clipPos = fields->m_clippingNode->getPosition();
                            centerLocal = CCPoint(clipPos.x - kAreaWidth / 2.f - 15.f, cellSize.height / 2.f - 1.f);
                        } else {
                            centerLocal = CCPoint(cellSize.width - kAreaWidth / 2.f - 15.f, cellSize.height / 2.f - 1.f);
                        }
                        CCPoint centerWorld = this->convertToWorldSpace(centerLocal);
                        if (auto parentNode = menuItem->getParent()) {
                            parentNode->setVisible(true);
                            menuItem->setPosition(parentNode->convertToNodeSpace(centerWorld));
                        } else {
                            menuItem->setPosition(centerLocal);
                        }
                    }
                    return; // found and configured
                }            }

            if (auto arr = cur->getChildren()) {
                for (auto* child : CCArrayExt<CCNode*>(arr)) {
                    if (child) stack.push_back(child);
                }
            }
        }
    }

    // Gallery slide: the new node enters from offset and the old exits toward
    // -offset with fade. The four directions differ only in the offset vector.
    void applySlideGalleryTransition(CCNode* newNode, CCNode* oldNode, CCSprite* oldSprite,
                                     CCPoint targetPos, float dur, CCPoint offset) {
        newNode->setPosition({targetPos.x + offset.x, targetPos.y + offset.y});
        newNode->runAction(CCEaseOut::create(CCMoveTo::create(dur, targetPos), 2.5f));
        if (oldNode) oldNode->runAction(CCSequence::create(
            CCSpawn::create(
                CCEaseIn::create(CCMoveTo::create(dur, {targetPos.x - offset.x, targetPos.y - offset.y}), 2.0f),
                oldSprite ? static_cast<CCFiniteTimeAction*>(CCFadeTo::create(dur * 0.8f, 0)) : static_cast<CCFiniteTimeAction*>(CCDelayTime::create(dur)),
                nullptr),
            CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
    }

    // Remove oldNode after delay seconds; shared by the simple gallery
    // transitions (crossfade/zoom/slide-cover/fade/swipe).
    static void removeNodeAfterDelay(CCNode* node, float delay) {
        if (!node) return;
        node->runAction(CCSequence::create(
            CCDelayTime::create(delay),
            CCCallFunc::create(node, callfunc_selector(CCNode::removeFromParent)), nullptr));
    }

    // Apply the configured transition, keeping cover-scale inside the clip
    void applyGalleryTransition(CCNode* newNode, CCSprite* newSprite,
                                CCNode* oldNode, CCSprite* oldSprite,
                                PaimonGalleryTransition type, float dur, CCSize clipSize) {
        if (!newNode || !newSprite) {
            log::warn("[LevelCell] applyGalleryTransition: null node/sprite");
            return;
        }
        if (type == PaimonGalleryTransition::Random)
            type = resolveRandomTransition();

        CCPoint targetPos = newNode->getPosition();
        float sx = newNode->getScaleX();
        float sy = newNode->getScaleY();
        log::debug("[LevelCell] applyGalleryTransition: type={} dur={:.2f} targetPos=({:.1f},{:.1f}) sx={:.3f} sy={:.3f} hasOld={}",
            static_cast<int>(type), dur, targetPos.x, targetPos.y, sx, sy, oldNode != nullptr);
        float halfDur = dur * 0.5f;
        float removeDelay = dur + 0.05f;

        switch (type) {

        case PaimonGalleryTransition::Crossfade: {
            newSprite->setOpacity(0);
            newSprite->runAction(CCFadeTo::create(dur, 255));
            removeNodeAfterDelay(oldNode, removeDelay);
        } break;

        case PaimonGalleryTransition::SlideLeft:
            applySlideGalleryTransition(newNode, oldNode, oldSprite, targetPos, dur, {clipSize.width, 0.f});
            break;

        case PaimonGalleryTransition::SlideRight:
            applySlideGalleryTransition(newNode, oldNode, oldSprite, targetPos, dur, {-clipSize.width, 0.f});
            break;

        case PaimonGalleryTransition::SlideUp:
            applySlideGalleryTransition(newNode, oldNode, oldSprite, targetPos, dur, {0.f, -clipSize.height});
            break;

        case PaimonGalleryTransition::SlideDown:
            applySlideGalleryTransition(newNode, oldNode, oldSprite, targetPos, dur, {0.f, clipSize.height});
            break;

        case PaimonGalleryTransition::ZoomIn: {
            newNode->setScaleX(sx * 1.12f);
            newNode->setScaleY(sy * 1.12f);
            newSprite->setOpacity(0);
            newNode->runAction(CCEaseOut::create(CCScaleTo::create(dur, sx, sy), 2.5f));
            newSprite->runAction(CCFadeTo::create(dur * 0.7f, 255));
            removeNodeAfterDelay(oldNode, removeDelay);
        } break;

        case PaimonGalleryTransition::ZoomOut: {
            newNode->setScaleX(sx * 1.6f);
            newNode->setScaleY(sy * 1.6f);
            newSprite->setOpacity(0);
            newNode->runAction(CCEaseOut::create(CCScaleTo::create(dur, sx, sy), 2.5f));
            newSprite->runAction(CCFadeTo::create(dur * 0.7f, 255));
            removeNodeAfterDelay(oldNode, removeDelay);
        } break;

        case PaimonGalleryTransition::FlipHorizontal: {
            newNode->setScaleX(0.01f);
            newSprite->setOpacity(0);
            newNode->runAction(CCSequence::create(
                CCDelayTime::create(halfDur),
                CCEaseOut::create(CCScaleTo::create(halfDur, sx, sy), 2.0f), nullptr));
            newSprite->runAction(CCSequence::create(
                CCDelayTime::create(halfDur),
                CCFadeTo::create(halfDur * 0.5f, 255), nullptr));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseIn::create(CCScaleTo::create(halfDur, 0.01f, sy), 2.0f),
                    oldSprite ? static_cast<CCFiniteTimeAction*>(CCFadeTo::create(halfDur, 0)) : static_cast<CCFiniteTimeAction*>(CCDelayTime::create(halfDur)),
                    nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::FlipVertical: {
            newNode->setScaleY(0.01f);
            newSprite->setOpacity(0);
            newNode->runAction(CCSequence::create(
                CCDelayTime::create(halfDur),
                CCEaseOut::create(CCScaleTo::create(halfDur, sx, sy), 2.0f), nullptr));
            newSprite->runAction(CCSequence::create(
                CCDelayTime::create(halfDur),
                CCFadeTo::create(halfDur * 0.5f, 255), nullptr));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseIn::create(CCScaleTo::create(halfDur, sx, 0.01f), 2.0f),
                    oldSprite ? static_cast<CCFiniteTimeAction*>(CCFadeTo::create(halfDur, 0)) : static_cast<CCFiniteTimeAction*>(CCDelayTime::create(halfDur)),
                    nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::RotateCW: {
            newNode->setRotation(90.0f);
            newSprite->setOpacity(0);
            newNode->runAction(CCEaseOut::create(CCRotateTo::create(dur, 0.0f), 2.5f));
            newSprite->runAction(CCFadeTo::create(dur * 0.6f, 255));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseIn::create(CCRotateTo::create(dur, -90.0f), 2.0f),
                    oldSprite ? static_cast<CCFiniteTimeAction*>(CCFadeTo::create(dur * 0.8f, 0)) : static_cast<CCFiniteTimeAction*>(CCDelayTime::create(dur)),
                    nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::RotateCCW: {
            newNode->setRotation(-90.0f);
            newSprite->setOpacity(0);
            newNode->runAction(CCEaseOut::create(CCRotateTo::create(dur, 0.0f), 2.5f));
            newSprite->runAction(CCFadeTo::create(dur * 0.6f, 255));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseIn::create(CCRotateTo::create(dur, 90.0f), 2.0f),
                    oldSprite ? static_cast<CCFiniteTimeAction*>(CCFadeTo::create(dur * 0.8f, 0)) : static_cast<CCFiniteTimeAction*>(CCDelayTime::create(dur)),
                    nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::Cube: {
            // 3D cube illusion: slide + scaleX perspective
            float offset = clipSize.width * 0.5f;
            newNode->setPosition({targetPos.x + offset, targetPos.y});
            newNode->setScaleX(0.01f);
            newNode->runAction(CCSpawn::create(
                CCEaseOut::create(CCMoveTo::create(dur, targetPos), 2.0f),
                CCEaseOut::create(CCScaleTo::create(dur, sx, sy), 2.0f), nullptr));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseIn::create(CCMoveTo::create(dur, {targetPos.x - offset, targetPos.y}), 2.0f),
                    CCEaseIn::create(CCScaleTo::create(dur, 0.01f, sy), 2.0f), nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::Dissolve: {
            // stepped fade: opacity ramps in discrete steps for a dissolve look
            newSprite->setOpacity(0);
            float step = dur / 5.0f;
            newSprite->runAction(CCSequence::create(
                CCFadeTo::create(step, 50),
                CCFadeTo::create(step, 120),
                CCFadeTo::create(step, 180),
                CCFadeTo::create(step, 220),
                CCFadeTo::create(step, 255), nullptr));
            removeNodeAfterDelay(oldNode, removeDelay);
        } break;

        case PaimonGalleryTransition::Swipe: {
            // new covers old from right -- old stays still
            newNode->setPosition({targetPos.x + clipSize.width, targetPos.y});
            newNode->runAction(CCEaseOut::create(CCMoveTo::create(dur, targetPos), 3.0f));
            removeNodeAfterDelay(oldNode, removeDelay);
        } break;

        case PaimonGalleryTransition::Bounce: {
            newNode->setPosition({targetPos.x + clipSize.width, targetPos.y});
            newNode->runAction(CCEaseBounceOut::create(CCMoveTo::create(dur, targetPos)));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseIn::create(CCMoveTo::create(dur * 0.5f, {targetPos.x - clipSize.width * 0.3f, targetPos.y}), 2.0f),
                    oldSprite ? static_cast<CCFiniteTimeAction*>(CCFadeTo::create(dur * 0.5f, 0)) : static_cast<CCFiniteTimeAction*>(CCDelayTime::create(dur * 0.5f)),
                    nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::ElasticSlide: {
            // New enters from the right with an elastic effect
            newNode->setPosition({targetPos.x + clipSize.width, targetPos.y});
            newNode->runAction(CCEaseElasticOut::create(
                CCMoveTo::create(dur * 1.2f, targetPos), 0.3f));
            // Old: small zoom + slide left with back-ease + fade
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseBackIn::create(
                        CCMoveTo::create(dur * 0.7f, {targetPos.x - clipSize.width, targetPos.y})),
                    CCScaleTo::create(dur * 0.3f, sx * 1.05f, sy * 1.05f),
                    oldSprite ? static_cast<CCFiniteTimeAction*>(CCFadeTo::create(dur * 0.6f, 0))
                              : static_cast<CCFiniteTimeAction*>(CCDelayTime::create(dur * 0.6f)),
                    nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::DirectionalElastic: {
            // Directional elastic: new slides in from nav direction with elastic snap
            // Old slides out opposite direction with back-ease + fade
            // Default direction: right (for auto-cycle)
            float newStartX = targetPos.x + clipSize.width;
            float oldExitX = targetPos.x - clipSize.width;
            newNode->setPosition({newStartX, targetPos.y});
            newNode->runAction(CCEaseElasticOut::create(
                CCMoveTo::create(dur * 1.1f, targetPos), 0.35f));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseBackIn::create(
                        CCMoveTo::create(dur * 0.6f, {oldExitX, targetPos.y})),
                    oldSprite ? static_cast<CCFiniteTimeAction*>(CCFadeTo::create(dur * 0.5f, 0))
                              : static_cast<CCFiniteTimeAction*>(CCDelayTime::create(dur * 0.5f)),
                    nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::Spiral: {
            // New: rotate from 360 + scale from 0 + fade in
            newNode->setScaleX(sx * 0.01f);
            newNode->setScaleY(sy * 0.01f);
            newNode->setRotation(360.0f);
            newSprite->setOpacity(0);
            newNode->runAction(CCSpawn::create(
                CCEaseOut::create(CCScaleTo::create(dur, sx, sy), 2.5f),
                CCEaseOut::create(CCRotateTo::create(dur, 0.0f), 2.5f),
                nullptr));
            newSprite->runAction(CCFadeTo::create(dur * 0.7f, 255));
            // Old: rotate + scale to 0 + fade out
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseIn::create(CCScaleTo::create(dur * 0.8f, 0.01f, 0.01f), 2.0f),
                    CCEaseIn::create(CCRotateTo::create(dur * 0.8f, -360.0f), 2.0f),
                    oldSprite ? static_cast<CCFiniteTimeAction*>(CCFadeTo::create(dur * 0.7f, 0))
                              : static_cast<CCFiniteTimeAction*>(CCDelayTime::create(dur * 0.7f)),
                    nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::Wave: {
            // New: smooth slide from the right
            newNode->setPosition({targetPos.x + clipSize.width, targetPos.y});
            newNode->runAction(CCEaseOut::create(CCMoveTo::create(dur, targetPos), 3.0f));
            newSprite->setOpacity(0);
            newSprite->runAction(CCFadeTo::create(dur * 0.5f, 255));
            // Old: wobble (alternating X/Y scale pulses) + fade out
            if (oldNode) {
                float waveDur = dur * 0.8f;
                float pulse = waveDur / 4.0f;
                oldNode->runAction(CCSequence::create(
                    CCSpawn::create(
                        CCSequence::create(
                            CCScaleTo::create(pulse, sx * 1.08f, sy * 0.94f),
                            CCScaleTo::create(pulse, sx * 0.94f, sy * 1.06f),
                            CCScaleTo::create(pulse, sx * 1.04f, sy * 0.97f),
                            CCScaleTo::create(pulse, sx * 0.5f, sy * 0.5f),
                            nullptr),
                        oldSprite ? static_cast<CCFiniteTimeAction*>(CCFadeTo::create(waveDur, 0))
                                  : static_cast<CCFiniteTimeAction*>(CCDelayTime::create(waveDur)),
                        nullptr),
                    CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
            }
        } break;

        case PaimonGalleryTransition::Pop: {
            // Sequential: old scales to 0 first, then new pops from 0
            newNode->setScaleX(0.01f);
            newNode->setScaleY(0.01f);
            newSprite->setOpacity(0);
            // New appears after the old disappears
            newNode->runAction(CCSequence::create(
                CCDelayTime::create(halfDur),
                CCSpawn::create(
                    CCEaseBackOut::create(CCScaleTo::create(halfDur, sx, sy)),
                    nullptr),
                nullptr));
            newSprite->runAction(CCSequence::create(
                CCDelayTime::create(halfDur),
                CCFadeTo::create(halfDur * 0.3f, 255),
                nullptr));
            // Old scales to 0 with back-ease
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseBackIn::create(CCScaleTo::create(halfDur, 0.01f, 0.01f)),
                    oldSprite ? static_cast<CCFiniteTimeAction*>(CCFadeTo::create(halfDur * 0.8f, 0))
                              : static_cast<CCFiniteTimeAction*>(CCDelayTime::create(halfDur)),
                    nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        default: { // fallback = crossfade
            newSprite->setOpacity(0);
            newSprite->runAction(CCFadeTo::create(dur, 255));
            removeNodeAfterDelay(oldNode, removeDelay);
        } break;
        }
    }

    // Apply the entry transition (first appearance)
    void applyEntryTransition(CCNode* clipNode, CCSprite* sprite, PaimonGalleryTransition type,
                              float dur, CCSize clipSize) {
        log::debug("[LevelCell] applyEntryTransition: type={} dur={:.2f} clipSize=({:.1f},{:.1f})", static_cast<int>(type), dur, clipSize.width, clipSize.height);
        applyGalleryTransition(clipNode, sprite, nullptr, nullptr, type, dur, clipSize);
    }

    // Mark the end of the gallery transition
    void endGalleryTransition(float /*dt*/) {
        auto fields = m_fields.self();
        if (fields && !fields->m_isBeingDestroyed) {
            fields->m_isGalleryTransitioning = false;
        }
    }

    // Enable the transition guard and schedule its end
    void beginGalleryTransitionGuard(float dur) {
        auto fields = m_fields.self();
        if (!fields) return;
        fields->m_isGalleryTransitioning = true;
        fields->m_galleryTransitionStart = std::chrono::steady_clock::now();

        log::debug("[LevelCell] beginGalleryTransitionGuard: dur={:.2f} centerLerp={:.2f}", dur, fields->m_centerLerp);
        // Cancel the previous callback if a transition is still pending
        this->unschedule(schedule_selector(PaimonLevelCell::endGalleryTransition));
        this->scheduleOnce(schedule_selector(PaimonLevelCell::endGalleryTransition), dur + 0.1f);
    }

    void crossfadeToThumb(CCTexture2D* texture, VideoThumbnailSprite* activeVideoDriver = nullptr) {
        if (!texture) {
            log::warn("[LevelCell] crossfadeToThumb: null texture");
            return;
        }
        auto fields = m_fields.self();
        if (!fields || fields->m_isBeingDestroyed) return;

        // Fall back to a full rebuild if clip/sprite is missing
        if (!fields->m_clippingNode || !fields->m_clippingNode->getParent() ||
            !fields->m_thumbSprite || !fields->m_thumbSprite->getParent()) {
            log::debug("[LevelCell] crossfadeToThumb: fallback to full rebuild (missing clip/sprite)");
            addOrUpdateThumb(texture, activeVideoDriver);
            return;
        }
        log::debug("[LevelCell] crossfadeToThumb: starting, oldBaseScale={:.4f}", fields->m_thumbBaseScaleX);

        auto oldClip = fields->m_clippingNode;
        auto oldSprite = fields->m_thumbSprite;

        CCSprite* newSprite = createThumbnailSprite(texture, false);
        if (!newSprite) {
            addOrUpdateThumb(texture, activeVideoDriver);
            return;
        }

        float oldBaseScale = fields->m_thumbBaseScaleX;
        float newBaseScale = oldBaseScale;
        auto bg = m_backgroundLayer;
        auto newClip = createThumbnailClippingNode(bg, newSprite, newBaseScale);
        if (!newClip) {
            log::warn("[LevelCell] crossfadeToThumb: createThumbnailClippingNode failed, fallback");
            addOrUpdateThumb(texture, activeVideoDriver);
            return;
        }
        log::debug("[LevelCell] crossfadeToThumb: newBaseScale={:.4f} clipSize=({:.1f},{:.1f})", newBaseScale, newClip->getContentSize().width, newClip->getContentSize().height);

        newSprite->setAnchorPoint(oldSprite->getAnchorPoint());
        newSprite->setZOrder(oldSprite->getZOrder());
        newSprite->setColor(oldSprite->getColor());
        newSprite->setPosition(newClip->getContentSize() * 0.5f);
        newClip->setPosition(fields->m_clipBasePos);
        newClip->setScale(1.0f);
        newClip->setRotation(0.0f);

        oldClip->setPosition(fields->m_clipBasePos);
        oldClip->setScale(1.0f);
        oldClip->setRotation(0.0f);
        oldSprite->setPosition(fields->m_thumbBasePos);
        oldSprite->setScale(oldBaseScale);
        oldSprite->setRotation(0.0f);
        oldSprite->setOpacity(255);

        // Don't apply the saturation shader here -- updateCenterAnimation handles it

        // read cached transition settings
        cacheSettings();
        auto transType = fields->m_cachedGalleryTransition;
        float dur = fields->m_cachedTransitionDuration;
        CCSize clipSize = newClip->getContentSize();

        setVideoDriver(activeVideoDriver, dur + 0.05f);

        log::debug("[LevelCell] crossfadeToThumb: transType={} dur={:.2f}", static_cast<int>(transType), dur);

        // Update fields BEFORE applying the transition so that:
        // 1) m_clipBasePos stores the correct target position (not the offset
        //    position that applyGalleryTransition may set for slide animations)
        // 2) updateCenterAnimation (which is guarded by m_isGalleryTransitioning)
        //    will have the right base values once the transition ends
        fields->m_clippingNode = newClip;
        fields->m_thumbSprite = newSprite;
        fields->m_thumbBasePos = newSprite->getPosition();
        fields->m_clipBasePos = newClip->getPosition();
        fields->m_thumbBaseScaleX = newBaseScale;
        fields->m_thumbBaseScaleY = newBaseScale;
        fields->m_lastClipHoverOffsetX = 0.0f;
        fields->m_lastClipHoverPosAdjustment = 0.0f;
        fields->m_lastClipHoverScaleX = 1.0f;
        fields->m_lastClipHoverRotation = 0.0f;
        fields->m_lastSpriteHoverScale = 1.0f;
        fields->m_lastSpriteHoverRotation = 0.0f;
        fields->m_lastSpriteHoverOffsetX = 0.0f;

        // Add to hoverContainer if present, else to this as fallback
        if (fields->m_hoverContainer && fields->m_hoverContainer->getParent()) {
            fields->m_hoverContainer->addChild(newClip);
        } else if (fields->m_boundsClipper && fields->m_boundsClipper->getParent()) {
            fields->m_boundsClipper->addChild(newClip);
        } else {
            this->addChild(newClip);
        }

        // Force a hover tick before the transition to avoid a visual jump
        this->updateCenterAnimation(0.f);

        beginGalleryTransitionGuard(dur);
        applyGalleryTransition(newClip, newSprite, oldClip, oldSprite, transType, dur, clipSize);

        // crossfade gradient background if bgType is thumbnail
        bool isThumbnailBg = fields->m_cachedBgType == PaimonBgType::Thumbnail;
        if (isThumbnailBg && m_level && fields->m_gradientLayer &&
            fields->m_gradientLayer->getParent()) {
            auto bg = m_backgroundLayer;
            if (bg) {
                float blurIntensity = fields->m_cachedBackgroundBlur;
                CCSize targetSize = bg->getContentSize();
                targetSize.width = std::max(targetSize.width, 512.f);
                targetSize.height = std::max(targetSize.height, 256.f);

                int32_t levelID = m_level->m_levelID.value();
                int captured_requestId = fields->m_requestId;
                int captured_blurToken = ++fields->m_bgBlurToken;
                WeakRef<PaimonLevelCell> crossSafeRef = this;

                // Blur async + crossfade
                BlurSystem::getInstance()->buildPaimonBlurAsync(
                    texture,
                    targetSize,
                    blurIntensity,
                    makeLevelCellBlurCacheKey(levelID, fields->m_galleryIndex, blurIntensity, true),
                    [crossSafeRef, levelID, captured_requestId, captured_blurToken, dur](CCSprite* newBgSprite) {
                        if (!newBgSprite) return;
                        auto cellRef = crossSafeRef.lock();
                        auto* cell = static_cast<PaimonLevelCell*>(cellRef.data());
                        if (!cell || !cell->shouldHandleThumbnailCallback(levelID, captured_requestId)) return;
                        auto fields2 = cell->m_fields.self();
                        if (!fields2 || fields2->m_isBeingDestroyed) return;
                        if (fields2->m_bgBlurToken != captured_blurToken) return;
                        if (!fields2->m_gradientLayer || !fields2->m_gradientLayer->getParent()) return;

                        auto bg2 = cell->m_backgroundLayer;
                        if (!bg2) return;

                        auto clipper = fields2->m_gradientLayer->getParent();
                        float scale = safeCoverScale(
                            bg2->getContentWidth(), bg2->getContentHeight(),
                            newBgSprite->getContentSize().width, newBgSprite->getContentSize().height,
                            1.0f
                        );
                        newBgSprite->setScale(scale);
                        newBgSprite->setPosition(bg2->getContentSize() / 2);
                        newBgSprite->setID("paimon-level-background"_spr);
                        newBgSprite->setOpacity(0);
                        clipper->addChild(newBgSprite);
                        newBgSprite->runAction(CCFadeTo::create(dur, 255));

                        // Remove the old one when done
                        auto oldGrad = fields2->m_gradientLayer;
                        oldGrad->runAction(CCSequence::create(
                            CCDelayTime::create(dur + 0.05f),
                            CCCallFunc::create(oldGrad, callfunc_selector(CCNode::removeFromParent)),
                            nullptr
                        ));
                        fields2->m_gradientLayer = newBgSprite;
                    });
            }
        }
    }

    bool galleryWindowNeedsPrefetch(int startIndex, int count) {
        auto fields = m_fields.self();
        if (!fields || !m_level) return false;

        int const gallerySize = static_cast<int>(fields->m_galleryThumbnails.size());
        if (gallerySize <= 0) return false;

        int const levelID = m_level->m_levelID.value();
        int const requestCount = std::min(gallerySize, std::max(1, count));
        for (int step = 0; step < requestCount; ++step) {
            int const index = (startIndex + step) % gallerySize;
            auto const& thumb = fields->m_galleryThumbnails[index];
            if (thumb.url.empty()) continue;

            if (thumb.isVideo()) {
                std::string cacheKey = fmt::format("gallery_video_{}_{}", levelID, index);
                if (!VideoThumbnailSprite::isCached(cacheKey)) return true;
                continue;
            }
            if (!ThumbnailLoader::get().isUrlLoaded(thumb.url)) return true;
        }
        return false;
    }

    void scheduleDeferredGalleryPrefetch() {
        auto fields = m_fields.self();
        if (!fields || fields->m_isBeingDestroyed || !m_level) return;
        if (fields->m_galleryThumbnails.size() <= 1) return;
        if (fields->m_galleryPrefetchScheduled) return;
        if (!galleryWindowNeedsPrefetch(fields->m_galleryIndex, LEVELCELL_GALLERY_LOOKAHEAD + 1)) {
            return;
        }

        fields->m_galleryPrefetchScheduled = true;
        int const levelID = m_level->m_levelID.value();
        float const delay = static_cast<float>(levelID % LEVELCELL_GALLERY_REENTER_STAGGER_SLOTS)
            * LEVELCELL_GALLERY_REENTER_STAGGER;
        this->unschedule(schedule_selector(PaimonLevelCell::deferredGalleryPrefetch));
        this->scheduleOnce(schedule_selector(PaimonLevelCell::deferredGalleryPrefetch), delay);
    }

    void deferredGalleryPrefetch(float /*dt*/) {
        auto fields = m_fields.self();
        if (!fields) return;
        fields->m_galleryPrefetchScheduled = false;
        this->unschedule(schedule_selector(PaimonLevelCell::deferredGalleryPrefetch));
        if (fields->m_isBeingDestroyed || !m_level) return;
        if (fields->m_galleryThumbnails.size() <= 1) return;
        requestGalleryWindow(fields->m_galleryIndex);
    }

    void requestGalleryWindow(int startIndex, int count = LEVELCELL_GALLERY_LOOKAHEAD + 1) {
        auto fields = m_fields.self();
        if (!fields || fields->m_isBeingDestroyed || !m_level) return;

        int gallerySize = static_cast<int>(fields->m_galleryThumbnails.size());
        if (gallerySize <= 0) return;

        int requestCount = std::min(gallerySize, std::max(1, count));
        for (int step = 0; step < requestCount; ++step) {
            int index = (startIndex + step) % gallerySize;
            this->requestGalleryThumbnail(index, step == 0);
        }
    }

    void requestGalleryThumbnail(int index, bool allowOverBudget = false) {
        auto fields = m_fields.self();
        if (!fields || fields->m_isBeingDestroyed || !m_level) return;
        if (index < 0 || index >= static_cast<int>(fields->m_galleryThumbnails.size())) return;

        auto const& thumb = fields->m_galleryThumbnails[index];
        if (thumb.url.empty()) return;

        // Download MP4 and create a VideoThumbnailSprite
        if (thumb.isVideo()) {
            const int levelID = m_level->m_levelID.value();
            const int galleryToken = fields->m_galleryToken;
            std::string cacheKey = fmt::format("gallery_video_{}_{}", levelID, index);
            WeakRef<PaimonLevelCell> safeRef = this;
            VideoThumbnailSprite::createAsync(thumb.url, cacheKey, [safeRef, levelID, galleryToken, index](VideoThumbnailSprite* videoSprite) {
                auto cellRef = safeRef.lock();
                auto* cell = static_cast<PaimonLevelCell*>(cellRef.data());
                if (!cell || !cell->m_level || cell->m_level->m_levelID != levelID) return;
                auto fields = cell->m_fields.self();
                if (!fields || fields->m_galleryToken != galleryToken) return;
                if (!videoSprite) return;

                if (fields->m_galleryIndex == index) {
                    // Stop old video player if any
                    if (fields->m_videoPlayer) {
                        fields->m_videoPlayer->forceStop();
                        fields->m_videoPlayer.reset();
                    }
                    videoSprite->setVolume(0.0f);
                    videoSprite->setLoop(true);
                    videoSprite->setVisible(false);
                    videoSprite->setID("video-driver-pending"_spr);
                    cell->addChild(videoSprite, -1000);
                    videoSprite->setOnFirstVisibleFrame([safeRef, levelID, galleryToken, index](VideoThumbnailSprite* readySprite) {
                        auto cellRefInner = safeRef.lock();
                        auto* currentCell = static_cast<PaimonLevelCell*>(cellRefInner.data());
                        if (!currentCell || !currentCell->m_level || currentCell->m_level->m_levelID != levelID) {
                            if (readySprite->getParent()) {
                                readySprite->removeFromParent();
                            }
                            return;
                        }

                        auto currentFields = currentCell->m_fields.self();
                        if (!currentFields || currentFields->m_galleryToken != galleryToken || currentFields->m_galleryIndex != index) {
                            if (readySprite->getParent()) {
                                readySprite->removeFromParent();
                            }
                            return;
                        }

                        if (auto* tex = readySprite->getTexture()) {
                            currentFields->m_hasVideo = true;
                            currentCell->crossfadeToThumb(tex, readySprite);
                        }
                    });
                    videoSprite->play();
                }
            });
            return;
        }

        const int levelID = m_level->m_levelID.value();
        const int galleryToken = fields->m_galleryToken;

        // Check the shared URL cache first
        if (ThumbnailLoader::get().isUrlLoaded(thumb.url)) {
            if (fields->m_galleryIndex == index) {
                // Re-entry: thumbnail already on screen -- skip crossfade and the main-thread queue.
                if (fields->m_thumbSprite && fields->m_thumbSprite->getTexture()) {
                    return;
                }
                log::debug("[LevelCell] requestGalleryThumbnail: shared cache hit index={} url={}", index, thumb.url);
                WeakRef<PaimonLevelCell> safeRef = this;
                ThumbnailLoader::get().requestUrlLoad(thumb.url, [safeRef, levelID, galleryToken, index](CCTexture2D* tex, bool ok) {
                    auto cellRef = safeRef.lock();
                    auto* cell = static_cast<PaimonLevelCell*>(cellRef.data());
                    if (!cell || !cell->m_level || cell->m_level->m_levelID != levelID) return;
                    auto fields = cell->m_fields.self();
                    if (!fields || fields->m_galleryToken != galleryToken) return;
                    if (!ok || !tex || fields->m_galleryIndex != index) return;
                    if (fields->m_thumbSprite && fields->m_thumbSprite->getTexture() == tex) return;
                    cell->crossfadeToThumb(tex);
                });
            }
            return;
        }

        if (!allowOverBudget && fields->m_galleryPendingUrls.size() >= LEVELCELL_GALLERY_MAX_PENDING) {
            log::debug("[LevelCell] requestGalleryThumbnail: pending budget reached index={} pending={}", index, fields->m_galleryPendingUrls.size());
            return;
        }

        if (!fields->m_galleryPendingUrls.insert(thumb.url).second) {
            log::debug("[LevelCell] requestGalleryThumbnail: already pending index={}", index);
            return;
        }

        WeakRef<PaimonLevelCell> safeRef = this;
        ThumbnailLoader::get().requestUrlLoad(thumb.url, [safeRef, levelID, galleryToken, index, url = thumb.url](CCTexture2D* tex, bool success) {
            auto cellRef = safeRef.lock();
            auto* cell = static_cast<PaimonLevelCell*>(cellRef.data());
            if (!cell || !cell->m_level || cell->m_level->m_levelID != levelID) return;

            auto fields = cell->m_fields.self();
            if (!fields) return;
            fields->m_galleryPendingUrls.erase(url);
            if (fields->m_galleryToken != galleryToken) return;
            if (!success || !tex) {
                log::debug("[LevelCell] requestGalleryThumbnail callback: download failed index={} url={}", index, url);
                return;
            }

            if (fields->m_galleryIndex == index) {
                cell->crossfadeToThumb(tex);
            }
        });
    }

    void updateGalleryCycle(float dt) {
        auto fields = m_fields.self();
        if (!fields || fields->m_isBeingDestroyed || !m_level) return;

        // Safety timeout: release the guard if stuck >2s
        if (fields->m_isGalleryTransitioning) {
            auto elapsed = std::chrono::steady_clock::now() - fields->m_galleryTransitionStart;
            if (elapsed > std::chrono::duration<float>(Fields::GALLERY_TRANSITION_SAFETY_TIMEOUT)) {
                log::warn("[LevelCell] updateGalleryCycle: transition guard stuck for >{}s, forcing release",
                    Fields::GALLERY_TRANSITION_SAFETY_TIMEOUT);
                fields->m_isGalleryTransitioning = false;
                this->unschedule(schedule_selector(PaimonLevelCell::endGalleryTransition));
            } else {
                return; // still transitioning normally
            }
        }

        const int gallerySize = static_cast<int>(fields->m_galleryThumbnails.size());
        if (gallerySize < 2) {
            this->unschedule(schedule_selector(PaimonLevelCell::updateGalleryCycle));
            return;
        }

        // scan -> show -> prefetch with a bounded window
        int foundIndex = -1;
        int searchWindow = std::min(gallerySize - 1, LEVELCELL_GALLERY_SEARCH_WINDOW);

        for (int i = 1; i <= searchWindow; ++i) {
            int idx = (fields->m_galleryIndex + i) % gallerySize;
            auto const& thumb = fields->m_galleryThumbnails[idx];
            if (thumb.url.empty()) continue;

            if (thumb.isVideo()) {
                // Videos are cached by VideoThumbnailSprite; check its cache
                int levelID = m_level->m_levelID.value();
                std::string cacheKey = fmt::format("gallery_video_{}_{}", levelID, idx);
                if (VideoThumbnailSprite::isCached(cacheKey)) {
                    foundIndex = idx;
                    break;
                }
                this->requestGalleryThumbnail(idx, i == 1);
                continue;
            }

            if (ThumbnailLoader::get().isUrlLoaded(thumb.url)) {
                foundIndex = idx;
                break;
            }
            this->requestGalleryThumbnail(idx, i == 1);
        }

        if (foundIndex != -1) {
            // Image ready: show it and advance the index
            fields->m_galleryIndex = foundIndex;
            fields->m_galleryConsecutiveMisses = 0;
            this->requestGalleryThumbnail(foundIndex, true);

            if (gallerySize > 1) {
                this->requestGalleryWindow((foundIndex + 1) % gallerySize);
            }

            scheduleGalleryAutocycle();
        } else {
            fields->m_galleryConsecutiveMisses++;

            // after N consecutive misses, stop the cycle to save requests
            if (fields->m_galleryConsecutiveMisses >= LEVELCELL_GALLERY_MAX_MISSES) {
                log::debug("[LevelCell] updateGalleryCycle: {} consecutive misses, stopping gallery cycle",
                    fields->m_galleryConsecutiveMisses);
                this->unschedule(schedule_selector(PaimonLevelCell::updateGalleryCycle));
                return;
            }

            if (gallerySize > 1) {
                this->requestGalleryWindow((fields->m_galleryIndex + 1) % gallerySize);
            }

            // Nothing ready; retry with increasing backoff
            float const delay = LEVELCELL_GALLERY_RETRY_DELAY * fields->m_galleryConsecutiveMisses;
            scheduleGalleryAutocycle(delay);
        }
    }

    void addOrUpdateThumb(CCTexture2D* texture, VideoThumbnailSprite* activeVideoDriver = nullptr) {
        if (!texture) {
            log::warn("[LevelCell] addOrUpdateThumb called with null texture");
            return;
        }
        
        auto fields = m_fields.self();
        if (!fields || fields->m_isBeingDestroyed) {
            log::warn("[LevelCell] Fields null or destroyed in addOrUpdateThumb");
            return;
        }

        auto bg = m_backgroundLayer;
        if (!bg) {
            log::warn("[LevelCell] Background layer null in addOrUpdateThumb");
            return;
        }

        // Fast path: if a thumb is already applied for the same level, just swap
        // the existing sprite's texture and recompute scale -- avoids the full
        // rebuild (~3-5ms per cell).
        int32_t currentLevelID = m_level ? m_level->m_levelID.value() : 0;
        bool canFastSwap =
            !activeVideoDriver &&
            currentLevelID > 0 &&
            fields->m_cellLevelID == currentLevelID &&
            fields->m_thumbSprite &&
            fields->m_thumbSprite->getParent() &&
            fields->m_clippingNode &&
            fields->m_clippingNode->getParent();

        if (canFastSwap) {
            fields->m_thumbSprite->setTexture(texture);
            fields->m_thumbSprite->setTextureRect(CCRect(0, 0, texture->getContentSize().width, texture->getContentSize().height));
            fields->m_staticTexture = texture;

            cacheSettings();
            float widthFactor = fields->m_cachedThumbWidthFactor;
            float bgW = bg->getContentSize().width;
            float bgH = bg->getContentSize().height;
            float scaleX = 1.f, scaleY = 1.f;
            calculateLevelCellThumbScale(fields->m_thumbSprite, bgW, bgH, widthFactor, scaleX, scaleY);
            fields->m_thumbSprite->setScaleX(scaleX);
            fields->m_thumbSprite->setScaleY(scaleY);
            fields->m_thumbBaseScaleX = scaleX;
            fields->m_thumbBaseScaleY = scaleY;

            if (auto pair = LevelColors::get().getPair(currentLevelID)) {
                if (pair->a.r != fields->m_gradientColorA.r ||
                    pair->a.g != fields->m_gradientColorA.g ||
                    pair->a.b != fields->m_gradientColorA.b ||
                    pair->b.r != fields->m_gradientColorB.r ||
                    pair->b.g != fields->m_gradientColorB.g ||
                    pair->b.b != fields->m_gradientColorB.b) {
                    setupGradient(bg, currentLevelID, texture);
                } else if (fields->m_cachedBgType == PaimonBgType::Thumbnail && fields->m_gradientLayer) {
                    setupGradient(bg, currentLevelID, texture);
                }
            }

            flashThumbnailSprite();
            refreshRuntimeScheduling();
            return;
        }

        // Full path: first time or level change -- distribute across frames
        setVideoDriver(activeVideoDriver, 0.0f);

        // Always create the sprite when the texture callback arrives -- no
        // viewport culling or deferral. ThumbnailLoader's cache prevents repeat
        // downloads, and deferring setup caused bugs (texture lost in
        // viewport-pending state during layout rebuilds).

        // Clean up paimon nodes before adding new ones
        cleanPaimonNodes(bg);
        bg->setZOrder(-2);

        // Stage 0 (immediate): create sprite and clipping so the user sees something
        CCSprite* sprite = createThumbnailSprite(texture);
        if (!sprite) {
            log::warn("[LevelCell] Failed to create sprite from texture");
            return;
        }

        setupClippingAndSeparator(bg, sprite);

        // Mark that this cell now shows the current level
        if (m_level) {
            fields->m_cellLevelID = m_level->m_levelID.value();
        }

        // Entry animation
        if (fields->m_clippingNode && fields->m_thumbSprite) {
            cacheSettings();
            auto transType = fields->m_cachedGalleryTransition;
            float dur = fields->m_cachedTransitionDuration;
            CCSize clipSize = fields->m_clippingNode->getContentSize();
            beginGalleryTransitionGuard(dur);
            applyEntryTransition(fields->m_clippingNode, fields->m_thumbSprite, transType, dur, clipSize);
        }

        // Stage 1 (next frame): gradient and effects (async blur)
        if (m_level) {
            int32_t levelID = m_level->m_levelID.value();
            // Dispatch gradient + async blur next frame to avoid piling work on
            // this frame (critical at 360fps).
            WeakRef<PaimonLevelCell> weakSelf = this;
            int reqId = fields->m_requestId;
            this->scheduleOnce(schedule_selector(PaimonLevelCell::deferredSetupGradient), 0.0f);
            fields->m_deferredLevelID = levelID;
            fields->m_deferredTexture = texture;
            fields->m_deferredRequestId = reqId;
            fields->m_hasDeferredSetup = true;
        }

        refreshRuntimeScheduling();
    }

    // Scheduled callback for stage 1 of the distributed layout
    void deferredSetupGradient(float /*dt*/) {
        auto fields = m_fields.self();
        if (!fields || fields->m_isBeingDestroyed || !fields->m_hasDeferredSetup) return;
        
        // Verify the level/request didn't change
        if (!m_level || m_level->m_levelID.value() != fields->m_deferredLevelID) return;
        if (fields->m_requestId != fields->m_deferredRequestId) return;

        auto bg = m_backgroundLayer;
        if (!bg) return;

        int32_t levelID = fields->m_deferredLevelID;
        CCTexture2D* texture = fields->m_deferredTexture.data();
        if (!texture) return;

        // Re-entry with blur already applied: avoid another async GPU job.
        if (fields->m_cellLevelID == levelID && fields->m_gradientLayer &&
            fields->m_gradientLayer->getParent()) {
            fields->m_hasDeferredSetup = false;
            fields->m_deferredTexture = nullptr;
            WeakRef<PaimonLevelCell> weakSelf = this;
            this->scheduleOnce(schedule_selector(PaimonLevelCell::deferredSetupExtras), 0.0f);
            fields->m_hasDeferredExtras = true;
            return;
        }

        setupGradient(bg, levelID, texture);

        // Stage 2 (another frame): particles + view button (lower priority)
        WeakRef<PaimonLevelCell> weakSelf = this;
        this->scheduleOnce(schedule_selector(PaimonLevelCell::deferredSetupExtras), 0.0f);
        fields->m_hasDeferredExtras = true;

        // Update gradient colors
        if (fields->m_gradientLayer) {
            if (!fields->m_gradientLayer->getParent()) {
                fields->m_gradientLayer = nullptr;
            } else {
                if (auto pair = LevelColors::get().getPair(levelID)) {
                    fields->m_gradientColorA = pair->a;
                    fields->m_gradientColorB = pair->b;
                    if (auto grad = typeinfo_cast<PaimonShaderGradient*>(static_cast<CCSprite*>(fields->m_gradientLayer))) {
                        grad->setStartColor(pair->a);
                        grad->setEndColor(pair->b);
                    }
                }
            }
        }
    }

    // Scheduled callback for stage 2 of the distributed layout
    void deferredSetupExtras(float /*dt*/) {
        auto fields = m_fields.self();
        if (!fields || fields->m_isBeingDestroyed || !fields->m_hasDeferredExtras) return;
        if (fields->m_requestId != fields->m_deferredRequestId) return;

        auto bg = m_backgroundLayer;
        if (!bg) return;

        int32_t levelID = fields->m_deferredLevelID;
        setupMythicParticles(bg, levelID);
        findAndSetupViewButton();

        fields->m_hasDeferredSetup = false;
        fields->m_hasDeferredExtras = false;
    }

    bool checkMenuCollision(CCNode* node, CCPoint worldPoint, CCNode* ignoreNode, int depth = 0) {
        if (!node || !node->isVisible()) return false;
        // Perf: limit recursion depth to avoid deep tree walks on complex cells
        if (depth > 4) return false;
        
        // If it's a CCMenu, check its items
        if (auto menu = typeinfo_cast<CCMenu*>(node)) {
            if (!menu->isEnabled()) return false;
            
            auto children = menu->getChildren();
            if (!children) return false;
            
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (!child || !child->isVisible()) continue;

                // Skip the ignored node (m_viewOverlay)
                if (child == ignoreNode) continue;
                
                auto item = typeinfo_cast<CCMenuItem*>(child);
                if (item && item->isEnabled()) {
                    CCPoint local = item->getParent()->convertToNodeSpace(worldPoint);
                    CCRect r = item->boundingBox();
                    if (r.containsPoint(local)) {
                        return true;
                    }
                }
            }
        }
        
        auto children = node->getChildren();
        if (children) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (child && checkMenuCollision(child, worldPoint, ignoreNode, depth + 1)) return true;
            }
        }
        return false;
    }

    bool isTouchOnMenu(CCTouch* touch) {
        auto fields = m_fields.self();
        CCPoint worldPoint = touch->getLocation();
        // Include m_viewOverlay in the collision check so CCMenu handles it
        return checkMenuCollision(this, worldPoint, nullptr);
    }

    void updateMaintenance(float /*dt*/) {
        auto fields = m_fields.self();
        if (!fields || fields->m_isBeingDestroyed) return;

        int32_t const levelID = m_level ? m_level->m_levelID.value() : 0;
        bool const spriteAlive = fields->m_thumbSprite && fields->m_thumbSprite->getParent();
        std::chrono::milliseconds requestAge{};
        if (fields->m_thumbnailRequestTime != std::chrono::steady_clock::time_point{}) {
            requestAge = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - fields->m_thumbnailRequestTime
            );
        }

        auto const action = paimon::thumbnails::levelcell::evaluateMaintenance({
            .isBeingDestroyed = fields->m_isBeingDestroyed,
            .thumbnailRequested = fields->m_thumbnailRequested,
            .thumbnailApplied = fields->m_thumbnailApplied,
            .thumbnailFailed = fields->m_thumbnailFailed,
            .spriteAlive = spriteAlive,
            .hasLevel = m_level != nullptr,
            .levelID = levelID,
            .lastRequestedLevelID = fields->m_lastRequestedLevelID,
            .popupSettingsVersion = LevelCellSettingsPopup::s_settingsVersion,
            .loadedPopupSettingsVersion = fields->m_loadedPopupSettingsVersion,
            .loadedInvalidationVersion = fields->m_loadedInvalidationVersion,
            .currentInvalidationVersion = levelID > 0
                ? ThumbnailLoader::get().getInvalidationVersion(levelID)
                : fields->m_loadedInvalidationVersion,
            .thumbnailRequestAge = requestAge,
        });

        if (action == paimon::thumbnails::levelcell::MaintenanceAction::RetryLoad) {
            if (fields->m_thumbnailApplied && !spriteAlive) {
                log::debug("[LevelCell] maintenance: sprite lost for levelID={}, retrying", levelID);
            } else if (fields->m_thumbnailRequested && !fields->m_thumbnailApplied &&
                requestAge.count() > 1500) {
                log::warn(
                    "[LevelCell] thumbnail timeout for levelID={} after {}ms, retrying",
                    levelID,
                    requestAge.count()
                );
            }
            fields->m_thumbnailRequested = false;
            fields->m_thumbnailApplied = false;
            tryLoadThumbnail();
            return;
        }

        int const popupSettingsVer = LevelCellSettingsPopup::s_settingsVersion;
        if (popupSettingsVer != fields->m_loadedPopupSettingsVersion) {
            fields->m_loadedPopupSettingsVersion = popupSettingsVer;
            fields->m_settingsCached = false;

            bool const newCompact = shouldUseCompactForLevel(m_level);
            if (newCompact != fields->m_cachedCompactMode) {
                m_compactView = newCompact;
                fields->m_cachedCompactMode = newCompact;
                if (m_level) {
                    if (m_cellMode == 0) {
                        loadFromLevel(m_level);
                    } else {
                        loadCustomLevelCell();
                    }
                }

                CCNode* parent = this->getParent();
                for (int depth = 0; parent && depth < 15; ++depth, parent = parent->getParent()) {
                    if (auto* listView = typeinfo_cast<BoomListView*>(parent)) {
                        listView->setupList(0.f);
                        break;
                    }
                }
            }

            if (fields->m_thumbnailApplied && m_level) {
                resetThumbnailRequestFlags();
                tryLoadThumbnail();
                return;
            }
        }

        // Deferred gallery metadata: cells bound while off-screen skip the gallery
        // request in the load pipeline. Fire it once (guarded internally by
        // m_galleryRequested / shouldRequestGalleryMetadata) when the cell scrolls
        // into view so autocycle can start. The cheap bool checks short-circuit
        // before the convertToWorldSpace in isLevelCellLikelyOnScreen.
        if (fields->m_thumbnailApplied && !fields->m_galleryRequested && levelID > 0 &&
            paimon::levelcell::isLevelCellLikelyOnScreen(this)) {
            requestGalleryData(levelID);
        }

        if (fields->m_thumbnailApplied && levelID > 0) {
            applyTransparentMode();
        }

        refreshRuntimeScheduling();
    }

    // Auto-recovery when the cell returns to the tree after a transient onExit
    // (layout rebuild, scroll cycle, layer navigation). Restores the maintenance
    // tick and invalidation listener.
    $override void onEnter() {
        LevelCell::onEnter();

        auto fields = m_fields.self();
        if (!fields) return;

        // Clear the "being destroyed" flag set by onExit; the cell returned to
        // the tree, so it wasn't really destroyed.
        fields->m_isBeingDestroyed = false;

        // Re-arm the maintenance tick onExit unscheduled; the safety net that
        // detects lost sprites and request timeouts.
        ensureMaintenanceTickScheduled();

        // Re-register the invalidation listener if onExit canceled it; otherwise
        // the cell misses uploads/reorders and shows the old thumbnail until the
        // next loadFromLevel.
        ensureInvalidationListener();

        refreshRuntimeScheduling();
    }

    $override void onExit() {
        log::debug("[LevelCell] onExit: levelID={}", m_level ? m_level->m_levelID.value() : 0);
        bool realtimePreviewCell = isInsideRealtimeSearchPreviewContext();

        // Stop animations (avoid heavy work in the destructor)
        this->unscheduleUpdate();
        this->unschedule(schedule_selector(PaimonLevelCell::updateMaintenance));
        this->unschedule(schedule_selector(PaimonLevelCell::updateGalleryCycle));
        this->unschedule(schedule_selector(PaimonLevelCell::updateGradientAnim));
        this->unschedule(schedule_selector(PaimonLevelCell::updateCenterAnimation));
        this->unschedule(schedule_selector(PaimonLevelCell::deferredSetupGradient));
        this->unschedule(schedule_selector(PaimonLevelCell::deferredSetupExtras));
        this->unschedule(schedule_selector(PaimonLevelCell::deferredGalleryPrefetch));
        this->unschedule(schedule_selector(PaimonLevelCell::endGalleryTransition));

        bool spriteStillAttached = false;
        bool thumbnailStillApplied = false;
        if (auto fields = m_fields.self()) {
            fields->m_isCenterAnimScheduled = false;
            fields->m_isGradientAnimScheduled = false;
            fields->m_isMaintenanceScheduled = false;
            fields->m_isBeingDestroyed = true;
            // Perf: only increment requestId if thumbnail is NOT already applied.
            // This prevents invalidating in-flight callbacks during layout rebuilds
            // (which cause onExit→onEnter without the cell actually being recycled).
            // The 10th cell in a list was losing its callback because requestId
            // changed during a transient onExit.
            spriteStillAttached = fields->m_thumbSprite && fields->m_thumbSprite->getParent();
            thumbnailStillApplied = fields->m_thumbnailApplied;
            if (!fields->m_thumbnailApplied || !spriteStillAttached) {
                fields->m_requestId++;
            }
            fields->m_thumbnailRequestTime = std::chrono::steady_clock::time_point{};
            // Stop the video player. Use forceStop() (shutdown-safe: no seek /
            // SetCurrentPosition) since we destroy the player immediately after;
            // this avoids needless work while the cell is being torn down on a
            // page change / scene transition.
            if (fields->m_videoPlayer) {
                fields->m_videoPlayer->forceStop();
                fields->m_videoPlayer.reset();
                fields->m_hasVideo = false;
            }
            if (fields->m_videoDriver) {
                if (fields->m_videoDriver->getParent()) {
                    fields->m_videoDriver->removeFromParent();
                }
                fields->m_videoDriver = nullptr;
            }
            // Only invalidate flags if the sprite isn't still in the tree
            if (!spriteStillAttached) {
                fields->m_thumbnailRequested = false;
                fields->m_thumbnailApplied = false;
            }
            fields->m_staticThumbLoad.reset();
            // Keep gallery data, invalidate in-flight callbacks
            fields->m_galleryToken++;
            fields->m_galleryPrefetchScheduled = false;
            fields->m_asyncCancelToken = std::make_shared<std::monostate>();
            fields->m_galleryPendingUrls.clear();
            if (fields->m_invalidationListenerId != 0) {
                ThumbnailLoader::get().removeInvalidationListener(fields->m_invalidationListenerId);
                fields->m_invalidationListenerId = 0;
            }
        }

        if (realtimePreviewCell && m_backgroundLayer) {
            cleanPaimonNodes(m_backgroundLayer);
        }

        if (m_level && !realtimePreviewCell) {
            bool const keepLoads = spriteStillAttached && thumbnailStillApplied;
            if (!keepLoads) {
                ThumbnailLoader::get().cancelLoad(m_level->m_levelID.value());
                ThumbnailLoader::get().cancelLoad(m_level->m_levelID.value(), true);
            }
        }
        LevelCell::onExit();
    }

    void playTapFlashAnimation() {
        auto fields = m_fields.self();
        if (!fields) return;

        CCSize cellSize = this->getContentSize();
        if (cellSize.width < 4.f || cellSize.height < 4.f) {
            if (auto p = this->getParent()) cellSize = p->getContentSize();
        }
        // Use an available parent for higher stacking
        CCNode* flashParent = this;
        if (m_mainMenu) flashParent = m_mainMenu;
        else if (m_button && m_button->getParent()) flashParent = m_button->getParent();

        auto flash = CCLayerColor::create(ccc4(255,255,255,0));
        if (!flash) return;
        flash->setContentSize(cellSize);
        flash->ignoreAnchorPointForPosition(false);
        flash->setAnchorPoint({0.5f,0.5f});
        
        // Compute position in flashParent space to center correctly
        CCPoint centerLocal = CCPoint(cellSize.width / 2.0f, cellSize.height / 2.0f);
        CCPoint centerWorld = this->convertToWorldSpace(centerLocal);
        CCPoint centerInParent = flashParent->convertToNodeSpace(centerWorld);
        flash->setPosition(centerInParent);

        flash->setZOrder(99999);
        flash->setID("paimon-tap-flash"_spr);
        flashParent->addChild(flash);
        flashParent->reorderChild(flash, 99999);

        ccBlendFunc blend { GL_SRC_ALPHA, GL_ONE };
        flash->setBlendFunc(blend);

        auto fadeIn  = CCFadeTo::create(0.05f, 230);
        auto hold    = CCDelayTime::create(0.02f);
        auto fadeOut = CCFadeTo::create(0.30f, 0);
        auto remove  = CCCallFunc::create(flash, callfunc_selector(CCNode::removeFromParent));
        auto scaleUp = CCScaleTo::create(0.07f, 1.05f);
        auto scaleDown = CCScaleTo::create(0.25f, 1.0f);
        auto pulse = CCSequence::create(scaleUp, scaleDown, nullptr);

        auto easeIn = CCEaseOut::create(static_cast<CCActionInterval*>(fadeIn->copy()->autorelease()), 2.6f);
        auto easeOut = CCEaseIn::create(static_cast<CCActionInterval*>(fadeOut->copy()->autorelease()), 1.4f);
        auto flashSeq = CCSequence::create(easeIn, hold, easeOut, remove, nullptr);
        flash->runAction(flashSeq);
        flash->runAction(pulse);

        if (m_backgroundLayer) {
            auto originalColor = m_backgroundLayer->getColor();
            m_backgroundLayer->setColor({255,255,255});
            auto delayBG = CCDelayTime::create(0.03f);
            auto tintBack = CCTintTo::create(0.22f, originalColor.r, originalColor.g, originalColor.b);
            m_backgroundLayer->runAction(CCSequence::create(delayBG, tintBack, nullptr));
        }

        if (fields->m_thumbSprite) {
            auto ts = fields->m_thumbSprite;
            ts->setOpacity(255);
            auto thumbPulseUp = CCScaleTo::create(0.07f, ts->getScale() * 1.02f);
            auto thumbPulseDown = CCScaleTo::create(0.22f, ts->getScale());
            ts->runAction(CCSequence::create(thumbPulseUp, thumbPulseDown, nullptr));
        }
    }

    // Click flash
    $override void onClick(CCObject* sender) {
        // In excluded contexts (My Levels, LevelListLayer), skip the mod's
        // visual effects and let GD handle the vanilla click.
        if (isInCompactExcludedContext()) {
            LevelCell::onClick(sender);
            return;
        }
        playTapFlashAnimation();
        LevelCell::onClick(sender);
    }

    // Lighten a color
    static inline ccColor3B brightenColor(ccColor3B const& c, int add) {        auto clamp = [](int v){ return std::max(0, std::min(255, v)); };
        return ccColor3B{
            (GLubyte)clamp(c.r + add),
            (GLubyte)clamp(c.g + add),
            (GLubyte)clamp(c.b + add)
        };
    }

    void updateGradientAnim(float dt) {
        {
            auto fields = m_fields.self();
            if (!fields || fields->m_isBeingDestroyed || !fields->m_gradientLayer) return;
            
            auto grad = typeinfo_cast<PaimonShaderGradient*>(static_cast<CCSprite*>(fields->m_gradientLayer));
            if (!grad) return;

            fields->m_gradientTime += dt;
            float t = (sinf(fields->m_gradientTime * 1.2f) + 1.0f) / 2.0f;
            
            // Base gradient colors (animated wave)
            ccColor3B left = {
                (GLubyte)((1-t)*fields->m_gradientColorA.r + t*fields->m_gradientColorB.r),
                (GLubyte)((1-t)*fields->m_gradientColorA.g + t*fields->m_gradientColorB.g),
                (GLubyte)((1-t)*fields->m_gradientColorA.b + t*fields->m_gradientColorB.b)
            };
            ccColor3B right = {
                (GLubyte)((1-t)*fields->m_gradientColorB.r + t*fields->m_gradientColorA.r),
                (GLubyte)((1-t)*fields->m_gradientColorB.g + t*fields->m_gradientColorA.g),
                (GLubyte)((1-t)*fields->m_gradientColorB.b + t*fields->m_gradientColorA.b)
            };
            
            // Apply brightness based on centerLerp
            auto clamp = [](int v) { return std::max(0, std::min(255, v)); };
            int brightAmount = static_cast<int>(60.0f * fields->m_centerLerp);
            
            left.r = (GLubyte)clamp(left.r + brightAmount);
            left.g = (GLubyte)clamp(left.g + brightAmount);
            left.b = (GLubyte)clamp(left.b + brightAmount);
            
            right.r = (GLubyte)clamp(right.r + brightAmount);
            right.g = (GLubyte)clamp(right.g + brightAmount);
            right.b = (GLubyte)clamp(right.b + brightAmount);
            
            grad->setStartColor(left);
            grad->setEndColor(right);
        }
    }

    void cacheSettings() {
        auto fields = m_fields.self();
        uint64_t currentVersion = paimon::settings::internal::g_settingsVersion.load(std::memory_order_relaxed);
        if (fields->m_settingsCached && static_cast<uint64_t>(fields->m_loadedSettingsVersion) >= currentVersion) return;
        fields->m_settingsCached = true;
        fields->m_loadedSettingsVersion = static_cast<int>(currentVersion);
        fields->m_cachedAnimType = parseAnimType(Mod::get()->getSavedValue<std::string>("levelcell-anim-type", "zoom-slide"));
        fields->m_cachedAnimSpeed = static_cast<float>(Mod::get()->getSavedValue<double>("levelcell-anim-speed", 1.0));
        fields->m_cachedAnimEffect = parseAnimEffect(Mod::get()->getSavedValue<std::string>("levelcell-anim-effect", "none"));
        fields->m_cachedHoverEnabled = Mod::get()->getSettingValue<bool>("levelcell-hover-effects");
        fields->m_cachedCompactMode = shouldUseCompactForLevel(m_level);
        fields->m_cachedTransparentMode = Mod::get()->getSavedValue<bool>("transparent-list-mode", false);
        fields->m_cachedEffectOnGradient = Mod::get()->getSavedValue<bool>("levelcell-effect-on-gradient", false);
        fields->m_cachedBgType = parseBgType(Mod::get()->getSavedValue<std::string>("levelcell-background-type", "thumbnail"));
        fields->m_cachedGalleryTransition = parseGalleryTransition(Mod::get()->getSavedValue<std::string>("levelcell-gallery-transition", "crossfade"));
        fields->m_cachedTransitionDuration = std::clamp(static_cast<float>(Mod::get()->getSavedValue<double>("levelcell-gallery-transition-duration", 0.6)), 0.2f, 2.0f);
        fields->m_cachedShowSeparator = Mod::get()->getSavedValue<bool>("levelcell-show-separator", true);
        fields->m_cachedShowViewButton = Mod::get()->getSavedValue<bool>("levelcell-show-view-button", true);
        fields->m_cachedAnimatedGradient = Mod::get()->getSavedValue<bool>("levelcell-animated-gradient", true);
        fields->m_cachedMythicParticles = Mod::get()->getSavedValue<bool>("levelcell-mythic-particles", true);
        fields->m_cachedGalleryAutocycle = Mod::get()->getSavedValue<bool>("levelcell-gallery-autocycle", true);
        fields->m_cachedThumbWidthFactor = std::clamp(
            static_cast<float>(Mod::get()->getSettingValue<double>("level-thumb-width")),
            PaimonConstants::MIN_THUMB_WIDTH_FACTOR, PaimonConstants::MAX_THUMB_WIDTH_FACTOR);
        fields->m_cachedBackgroundBlur = static_cast<float>(Mod::get()->getSavedValue<double>("levelcell-background-blur", 3.0));
        fields->m_cachedBackgroundDarkness = static_cast<float>(Mod::get()->getSavedValue<double>("levelcell-background-darkness", 0.2));
    }

    // Inline hover detection -- runs every frame inside updateCenterAnimation
    void updateCenterDetection(Fields* fields) {
        if (!fields->m_cachedHoverEnabled) {
            fields->m_wasInCenter = false;
            return;
        }
        if (!this->getParent()) {
            fields->m_wasInCenter = false;
            return;
        }
        auto winSize = CCDirector::get()->getWinSize();
        CCPoint worldPos = this->convertToWorldSpace(CCPointZero);
        float cellCenterY = worldPos.y + this->getContentSize().height / 2.0f;
        float screenCenterY = winSize.height / 2.0f;
        float centerZone = fields->m_cachedCompactMode ? 24.75f : 45.0f;
        float distanceFromCenter = std::abs(cellCenterY - screenCenterY);
        bool isVisible = cellCenterY > 0 && cellCenterY < winSize.height;
        fields->m_wasInCenter = isVisible && distanceFromCenter < centerZone;
    }

    void updateCenterAnimation(float dt) {
        auto fields = m_fields.self();
        if (!fields || fields->m_isBeingDestroyed) return;

        cacheSettings();
        updateCenterDetection(fields);

        if (!fields->m_cachedHoverEnabled) {
            if (fields->m_centerLerp > 0.0f) fields->m_centerLerp = 0.0f;
            return;
        }

        if (fields->m_isGalleryTransitioning) {
            // During a gallery transition, skip all hover calculations to avoid
            // conflicting with the transition's CCActions. Hover resumes when
            // endGalleryTransition() clears the flag.
            return;
        }

        // Ensure the hoverContainer has no residual hover scale/offset after a transition
        {
            auto hoverTarget = fields->m_hoverContainer ? static_cast<CCNode*>(fields->m_hoverContainer.data())
                                                        : static_cast<CCNode*>(fields->m_boundsClipper.data());
            if (hoverTarget && hoverTarget->getParent()) {
                if (fields->m_lastBoundsHoverScale != 1.0f) {
                    float curScale = hoverTarget->getScale();
                    curScale = curScale / std::max(0.001f, fields->m_lastBoundsHoverScale);
                    hoverTarget->setScale(curScale);
                    fields->m_lastBoundsHoverScale = 1.0f;
                }
                if (fields->m_lastBoundsHoverOffsetX != 0.0f) {
                    auto pos = hoverTarget->getPosition();
                    pos.x -= fields->m_lastBoundsHoverOffsetX;
                    hoverTarget->setPosition(pos);
                    fields->m_lastBoundsHoverOffsetX = 0.0f;
                }
            }
        }

        fields->m_animTime += dt;

        PaimonAnimType animType = fields->m_cachedAnimType;
        float speedMult = fields->m_cachedAnimSpeed;
        PaimonAnimEffect animEffect = fields->m_cachedAnimEffect;

        if (animType == PaimonAnimType::None) {
            fields->m_centerLerp = 0.0f;
        } else {
            float target = fields->m_wasInCenter ? 1.0f : 0.0f;
            float speed = 12.0f * speedMult;
            fields->m_centerLerp += (target - fields->m_centerLerp) * std::min(1.0f, dt * speed);
            if (std::abs(fields->m_centerLerp - target) < 0.01f) fields->m_centerLerp = target;
        }

        float lerp = fields->m_centerLerp;
        float offsetX = 0.0f, zoomFactor = 1.0f, rotation = 0.0f;
        float spriteRotation = 0.0f, spriteOffsetX = 0.0f;
        float animT = fields->m_animTime;

        switch (animType) {
        case PaimonAnimType::ZoomSlide:
            offsetX = -10.f * lerp;
            zoomFactor = 1.0f + (0.12f * lerp);
            break;
        case PaimonAnimType::Zoom:
            zoomFactor = 1.0f + (0.15f * lerp);
            break;
        case PaimonAnimType::Slide:
            zoomFactor = 1.0f + (0.1f * lerp);
            spriteOffsetX = -15.f * lerp;
            break;
        case PaimonAnimType::Bounce:
            zoomFactor = 1.0f + (0.20f * lerp);
            break;
        case PaimonAnimType::Rotate:
            zoomFactor = 1.0f + (0.05f * lerp);
            rotation = sinf(animT * 3.0f) * 1.5f * lerp;
            break;
        case PaimonAnimType::RotateContent:
            zoomFactor = 1.0f + (0.15f * lerp);
            spriteRotation = sinf(animT * 4.0f) * 3.0f * lerp;
            break;
        case PaimonAnimType::Shake:
            zoomFactor = 1.0f + (0.05f * lerp);
            offsetX = sinf(animT * 20.0f) * 3.0f * lerp;
            break;
        case PaimonAnimType::Pulse: {
            float pulse = (sinf(animT * 10.0f) + 1.0f) * 0.5f;
            zoomFactor = 1.0f + (0.05f * lerp) + (pulse * 0.05f * lerp);
            break;
        }
        case PaimonAnimType::Swing:
            zoomFactor = 1.0f + (0.05f * lerp);
            rotation = sinf(animT * 4.0f) * 3.0f * lerp;
            break;
        default: break;
        }

        // Compact mode reduction
        if (fields->m_cachedCompactMode) {
            zoomFactor = 1.0f + ((zoomFactor - 1.0f) * 0.55f);
            offsetX *= 0.55f;
            spriteOffsetX *= 0.55f;
            rotation *= 0.55f;
            spriteRotation *= 0.55f;
        }

        if (fields->m_clippingNode) {
            float posAdjustment = 0.0f;
            if (animType != PaimonAnimType::None && animType != PaimonAnimType::Slide) {
                posAdjustment = (zoomFactor - 1.0f) * fields->m_clippingNode->getContentSize().width;
            }

            auto clipPos = fields->m_clippingNode->getPosition();
            clipPos.x -= fields->m_lastClipHoverOffsetX + fields->m_lastClipHoverPosAdjustment;
            fields->m_clippingNode->setPosition(clipPos);
            fields->m_clippingNode->setScaleX(fields->m_clippingNode->getScaleX() / std::max(0.001f, fields->m_lastClipHoverScaleX));
            fields->m_clippingNode->setRotation(fields->m_clippingNode->getRotation() - fields->m_lastClipHoverRotation);

            clipPos = fields->m_clippingNode->getPosition();
            clipPos.x += offsetX + posAdjustment;
            fields->m_clippingNode->setPosition(clipPos);
            fields->m_clippingNode->setScaleX(fields->m_clippingNode->getScaleX() * zoomFactor);
            fields->m_clippingNode->setRotation(fields->m_clippingNode->getRotation() + rotation);

            fields->m_lastClipHoverOffsetX = offsetX;
            fields->m_lastClipHoverPosAdjustment = posAdjustment;
            fields->m_lastClipHoverScaleX = zoomFactor;
            fields->m_lastClipHoverRotation = rotation;
        }

        if (fields->m_separator) {
            auto separatorPos = fields->m_separator->getPosition();
            separatorPos.x -= fields->m_lastSeparatorHoverOffsetX;
            fields->m_separator->setPosition(separatorPos);
            fields->m_separator->setRotation(fields->m_separator->getRotation() - fields->m_lastSeparatorHoverRotation);

            separatorPos = fields->m_separator->getPosition();
            separatorPos.x += offsetX;
            fields->m_separator->setPosition(separatorPos);
            fields->m_separator->setRotation(fields->m_separator->getRotation() + rotation);

            fields->m_lastSeparatorHoverOffsetX = offsetX;
            fields->m_lastSeparatorHoverRotation = rotation;
        }

        if (fields->m_thumbSprite) {
            fields->m_thumbSprite->setScale(fields->m_thumbSprite->getScale() / std::max(0.001f, fields->m_lastSpriteHoverScale));
            fields->m_thumbSprite->setRotation(fields->m_thumbSprite->getRotation() - fields->m_lastSpriteHoverRotation);
            fields->m_thumbSprite->setPosition(fields->m_thumbSprite->getPosition() - CCPoint(fields->m_lastSpriteHoverOffsetX, 0.0f));

            fields->m_thumbSprite->setScale(fields->m_thumbSprite->getScale() * zoomFactor);
            fields->m_thumbSprite->setRotation(fields->m_thumbSprite->getRotation() + spriteRotation);
            fields->m_thumbSprite->setPosition(fields->m_thumbSprite->getPosition() + CCPoint(spriteOffsetX, 0.0f));

            fields->m_lastSpriteHoverScale = zoomFactor;
            fields->m_lastSpriteHoverRotation = spriteRotation;
            fields->m_lastSpriteHoverOffsetX = spriteOffsetX;
        }

        // Build targets on stack (no heap allocation)
        PaimonBgType bgType = fields->m_cachedBgType;
        CCSprite* targets[2];
        int targetCount = 0;
        if (fields->m_thumbSprite) targets[targetCount++] = fields->m_thumbSprite;
        if (fields->m_cachedEffectOnGradient && fields->m_gradientLayer && bgType != PaimonBgType::Thumbnail) {
            targets[targetCount++] = fields->m_gradientLayer;
        }

        for (int ti = 0; ti < targetCount; ++ti) {
            CCSprite* target = targets[ti];
            bool usingShader = false;
            // Perf: use cached type info instead of 3x typeinfo_cast per target per tick
            PaimonShaderSprite* pss = nullptr;
            PaimonShaderGradient* psg = nullptr;
            AnimatedGIFSprite* ags = nullptr;
            if (ti == 0 && fields->m_thumbSprite) {
                // thumbSprite type is stable — cache on first detection
                if (!fields->m_thumbTypeCached) {
                    fields->m_thumbTypeCached = true;
                    fields->m_thumbIsPSS = typeinfo_cast<PaimonShaderSprite*>(target) != nullptr;
                    fields->m_thumbIsAGS = typeinfo_cast<AnimatedGIFSprite*>(target) != nullptr;
                }
                if (fields->m_thumbIsPSS) pss = static_cast<PaimonShaderSprite*>(target);
                if (fields->m_thumbIsAGS) ags = static_cast<AnimatedGIFSprite*>(target);
            } else if (ti == 1) {
                // gradientLayer is always PaimonShaderGradient if it passed the check
                psg = static_cast<PaimonShaderGradient*>(target);
            }

            auto setIntensity = [&](float i) {
                if (pss) pss->m_intensity = i;
                if (psg) psg->m_intensity = i;
                if (ags) ags->m_intensity = i;
            };
            auto setTime = [&](float t) {
                if (pss) pss->m_time = t;
                if (psg) psg->m_time = t;
                if (ags) ags->m_time = t;
            };
            auto setTexSize = [&]() {
                if (pss) {
                    if (auto* targetTex = target->getTexture()) {
                        pss->m_texSize = targetTex->getContentSizeInPixels();
                    } else {
                        pss->m_texSize = target->getContentSize();
                    }
                }
                if (psg) psg->m_texSize = target->getContentSize();
                if (ags) {
                    if (auto* targetTex = target->getTexture()) {
                        ags->m_texSize = targetTex->getContentSizeInPixels();
                    } else {
                        ags->m_texSize = target->getContentSize();
                    }
                }
            };

            auto applyShaderEffect = [&](char const* key, char const* file, bool withTexSize, bool withTime) {
                usingShader = true;
                if (auto sh = paimon::shaders::loadShader(key, "cell_vertex.glsl", file, nullptr, nullptr)) {
                    target->setShaderProgram(sh);
                    setIntensity(lerp);
                    if (withTexSize) setTexSize();
                    if (withTime) setTime(animT);
                }
                target->setColor({255, 255, 255});
            };

            switch (animEffect) {
            case PaimonAnimEffect::Brightness: {
                float b = 180.0f + (75.0f * lerp);
                target->setColor({(GLubyte)b, (GLubyte)b, (GLubyte)b});
                break;
            }
            case PaimonAnimEffect::Darken: {
                float b = 255.0f - (100.0f * lerp);
                target->setColor({(GLubyte)b, (GLubyte)b, (GLubyte)b});
                break;
            }
            case PaimonAnimEffect::Sepia:
                applyShaderEffect("paimon_cell_sepia", "saturation_cell.glsl", false, false);
                break;
            case PaimonAnimEffect::Sharpen:
                applyShaderEffect("paimon_cell_sharpen", "sharpen_cell.glsl", true, false);
                break;
            case PaimonAnimEffect::EdgeDetection:
                applyShaderEffect("paimon_cell_edge", "edge_cell.glsl", true, false);
                break;
            case PaimonAnimEffect::Vignette:
                applyShaderEffect("paimon_cell_vignette", "vignette_cell.glsl", false, false);
                break;
            case PaimonAnimEffect::Pixelate:
                applyShaderEffect("paimon_cell_pixelate", "pixelate_cell.glsl", true, false);
                break;
            case PaimonAnimEffect::Posterize:
                applyShaderEffect("paimon_cell_posterize", "posterize_cell.glsl", false, false);
                break;
            case PaimonAnimEffect::Chromatic:
                applyShaderEffect("paimon_cell_chromatic", "chromatic_cell.glsl", false, false);
                break;
            case PaimonAnimEffect::Scanlines:
                applyShaderEffect("paimon_cell_scanlines", "scanlines_cell.glsl", true, false);
                break;
            case PaimonAnimEffect::Solarize:
                applyShaderEffect("paimon_cell_solarize", "solarize_cell.glsl", false, false);
                break;
            case PaimonAnimEffect::Rainbow:
                applyShaderEffect("paimon_cell_rainbow", "rainbow_cell.glsl", false, true);
                break;
            case PaimonAnimEffect::Red: {
                GLubyte v = (GLubyte)(255.0f - (100.0f * lerp));
                target->setColor({255, v, v});
                break;
            }
            case PaimonAnimEffect::Blue: {
                GLubyte v = (GLubyte)(255.0f - (100.0f * lerp));
                target->setColor({v, v, 255});
                break;
            }
            case PaimonAnimEffect::Gold: {
                GLubyte g = (GLubyte)(255.0f - (40.0f * lerp));
                GLubyte bv = (GLubyte)(255.0f - (255.0f * lerp));
                target->setColor({255, g, bv});
                break;
            }
            case PaimonAnimEffect::Fade:
                target->setColor({255, 255, 255});
                target->setOpacity((GLubyte)(255.0f - (100.0f * lerp)));
                break;
            case PaimonAnimEffect::Grayscale:
                applyShaderEffect("paimon_cell_grayscale", "grayscale_cell.glsl", false, false);
                break;
            case PaimonAnimEffect::Invert:
                applyShaderEffect("paimon_cell_invert", "invert_cell.glsl", false, false);
                break;
            case PaimonAnimEffect::Blur:
                usingShader = true;
                if (auto sh = Shaders::getBlurCellShader()) {
                    target->setShaderProgram(sh); setIntensity(lerp); setTexSize();
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::Glitch:
                applyShaderEffect("paimon_cell_glitch", "glitch_cell.glsl", false, true);
                break;
            default:
                if (!psg) { target->setColor({255, 255, 255}); target->setOpacity(255); }
                break;
            }

            // Reset opacity for effects that don't manage it (Fade controls it explicitly)
            if (animEffect != PaimonAnimEffect::Fade && animEffect != PaimonAnimEffect::None) {
                target->setOpacity(255);
            }

            if (!usingShader) {
                target->setShaderProgram(CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor));
            }
        }

        // Update view overlay position
        if (fields->m_viewOverlay) {
            auto cs = this->getContentSize();
            float overlayW = 90.f;
            float overlayH = cs.height;
            CCPoint centerLocal;
            if (fields->m_clippingNode) {
                centerLocal = CCPoint(fields->m_clippingNode->getPosition().x - overlayW / 2.f - 15.f, cs.height / 2.f - 1.f);
            } else {
                centerLocal = CCPoint(cs.width - overlayW / 2.f - 15.f, cs.height / 2.f - 1.f);
            }
            CCPoint centerWorld = this->convertToWorldSpace(centerLocal);
            if (auto p = fields->m_viewOverlay->getParent()) {
                fields->m_viewOverlay->setPosition(p->convertToNodeSpace(centerWorld));
            } else {
                fields->m_viewOverlay->setPosition(centerLocal);
            }

            auto adjustState = [overlayW, overlayH](CCNode* n) {
                if (auto sp = typeinfo_cast<CCSprite*>(n)) {
                    sp->setContentSize({overlayW, overlayH});
                }
            };
            adjustState(fields->m_viewOverlay->getNormalImage());
            adjustState(fields->m_viewOverlay->getSelectedImage());
            adjustState(fields->m_viewOverlay->getDisabledImage());
        }
    }

    // animateToCenter/animateFromCenter removed: deprecated (now uses lerp system)

    // Detect whether this cell is inside a DailyLevelNode/DailyLevelPage
    bool isDailyCell() {
        auto fields = m_fields.self();
        if (fields->m_isDailyCellCached) return fields->m_isDailyCell;

        bool result = false;
        CCNode* parent = this->getParent();
        for (int depth = 0; parent && depth < 10; ++depth, parent = parent->getParent()) {
            if (typeinfo_cast<DailyLevelNode*>(parent)) {
                result = true;
                break;
            }
            std::string_view className(typeid(*parent).name());
            if (className.find("DailyLevelNode") != std::string_view::npos ||
                className.find("DailyLevelPage") != std::string_view::npos) {
                result = true;
                break;
            }
        }
        fields->m_isDailyCell = result;
        fields->m_isDailyCellCached = true;
        return result;
    }
    
    // fixDailyCell removed: was empty (logic moved to DailyLevelNode)

    // Removed onPaimonDailyPlay as per user request to remove animation
    
    // Removed onLevelInfo hook as it's not available in binding
    
    // Reset thumbnail/gallery/video state when the cell switches to a different level.
    void resetThumbnailStateForLevelChange(int32_t levelID) {
        auto bundle = thumbResetBundle();
        paimon::thumbnails::levelcell::resetForLevelChange(bundle, levelID);
    }

    // Register (once) the listener that retries the load when the level's
    // thumbnail is invalidated remotely.
    void ensureInvalidationListener() {
        auto fields = m_fields.self();
        if (fields->m_invalidationListenerId != 0) return;

        WeakRef<PaimonLevelCell> safeRef = this;
        fields->m_invalidationListenerId = ThumbnailLoader::get().addInvalidationListener(
            [safeRef](int invalidLevelID) {
                auto* self = static_cast<PaimonLevelCell*>(safeRef.lock().data());
                if (!self || !self->getParent()) return;
                self->handleRemoteThumbnailInvalidation(invalidLevelID);
            }
        );
    }

    // If the thumbnail is already applied and visible, skip re-requesting
    // (resumes the gallery cycle if applicable). Returns true if there's nothing more to do.
    bool tryReuseAppliedThumbnail(int32_t levelID) {
        auto fields = m_fields.self();
        bool const spriteAlive = fields->m_thumbSprite && fields->m_thumbSprite->getParent();
        if (!spriteAlive) return false;

        if (!fields->m_thumbnailApplied) {
            fields->m_thumbnailApplied = true;
        }
        fields->m_thumbnailRequested = true;
        log::debug("[LevelCell] tryLoadThumbnail: thumbnail still on-screen for levelID={}, skipping re-request", levelID);
        refreshRuntimeScheduling();
        if (fields->m_galleryRequested && fields->m_galleryThumbnails.size() > 1) {
            scheduleDeferredGalleryPrefetch();
            if (fields->m_cachedGalleryAutocycle) {
                scheduleGalleryAutocycle();
            }
        }
        return true;
    }

    // If a local .mp4 exists for the level, start the video player.
    // Returns true if it took that path (caller should return).
    bool tryStartLocalVideoThumbnail(int32_t levelID, bool enableSpinners) {
        return paimon::thumbnails::levelcell::tryStartLocalVideoThumbnail(
            levelID,
            enableSpinners,
            makeLocalVideoHost()
        );
    }

    bool tryStartServerVideoThumbnail(int32_t levelID, int currentRequestId, bool enableSpinners) {
        auto fields = m_fields.self();
        if (fields->m_galleryThumbnails.empty()) return false;

        return paimon::thumbnails::levelcell::tryStartServerVideoThumbnail(
            levelID,
            currentRequestId,
            enableSpinners,
            fields->m_galleryThumbnails[0],
            WeakRef<CCNode>(this),
            fields->m_asyncCancelToken,
            makeServerVideoHost()
        );
    }

    // Gallery metadata is only needed if the user uses autocycle or we already
    // have a local list / transport cache to resume the cycle.
    bool shouldRequestGalleryMetadata(int32_t levelID) {
        auto fields = m_fields.self();
        cacheSettings();
        if (fields->m_cachedGalleryAutocycle) return true;
        if (!fields->m_galleryThumbnails.empty()) return true;
        return ThumbnailTransportClient::get().hasGalleryMetadataCached(levelID);
    }

    // Request (once) the level's gallery thumbnail list; on arrival, normalize
    // it and start the auto-cycle if applicable.
    void requestGalleryData(int32_t levelID) {
        auto fields = m_fields.self();
        if (fields->m_galleryRequested) return;
        if (!shouldRequestGalleryMetadata(levelID)) {
            return;
        }
        fields->m_galleryRequested = true;
        int const galleryToken = ++fields->m_galleryToken;
        log::debug("[LevelCell] tryLoadThumbnail: requesting gallery for levelID={} token={}", levelID, galleryToken);
        WeakRef<PaimonLevelCell> safeGalleryRef = this;
        auto cancelToken = fields->m_asyncCancelToken;

        paimon::thumbnails::levelcell::requestGalleryList(
            levelID,
            cancelToken,
            [safeGalleryRef, levelID, galleryToken](paimon::thumbnails::levelcell::GalleryListResult result) {
                auto cellRef = safeGalleryRef.lock();
                auto* cell = static_cast<PaimonLevelCell*>(cellRef.data());
                if (!cell || !cell->getParent() || !cell->m_level || cell->m_level->m_levelID != levelID) {
                    return;
                }
                auto fields = cell->m_fields.self();
                if (!fields || fields->m_galleryToken != galleryToken) return;
                fields->m_galleryPendingUrls.clear();
                fields->m_galleryThumbnails = std::move(result.thumbnails);
                fields->m_galleryIndex = 0;
                fields->m_galleryTimer = 0.f;
                bool const autoCycleEnabled = fields->m_cachedGalleryAutocycle;
                if (autoCycleEnabled && fields->m_galleryThumbnails.size() > 1) {
                    cell->requestGalleryWindow(0);
                    cell->scheduleGalleryAutocycle();
                }
            }
        );
    }

    // Default path: request the thumbnail from the server (/t/{id} returns
    // GIF/WebP/PNG; the decoder detects the format by magic bytes).
    void requestStaticThumbnailLoad(int32_t levelID, int currentRequestId, bool enableSpinners, bool isOnScreen) {
        auto fields = m_fields.self();
        if (enableSpinners) showLoadingSpinner();
        log::debug("[LevelCell] tryLoadThumbnail: requesting load levelID={} requestId={}", levelID, currentRequestId);

        paimon::thumbnails::levelcell::StaticThumbRequest req{
            .levelID = levelID,
            .requestId = currentRequestId,
            .invalidationVersion = fields->m_loadedInvalidationVersion,
            .enableSpinners = enableSpinners,
            .isOnScreen = isOnScreen,
            .cancelToken = fields->m_asyncCancelToken,
        };

        WeakRef<CCNode> safeRef = this;
        WeakRef<PaimonLevelCell> cellRef = this;
        int const capturedVersion = fields->m_loadedInvalidationVersion;

        paimon::thumbnails::levelcell::requestStaticThumbnail(
            req,
            safeRef,
            [cellRef, levelID, enableSpinners, currentRequestId, capturedVersion](
                CCTexture2D* tex,
                bool success
            ) {
                auto locked = cellRef.lock();
                auto* cell = static_cast<PaimonLevelCell*>(locked.data());
                if (!cell || !cell->getParent() || !cell->shouldHandleThumbnailCallback(levelID, currentRequestId)) {
                    return;
                }
                {
                    auto f = cell->m_fields.self();
                    if (f && f->m_loadedInvalidationVersion != capturedVersion) return;
                }
                if (!success || !tex) {
                    auto f = cell->m_fields.self();
                    if (f && f->m_thumbnailFailed) {
                        log::debug(
                            "[LevelCell] tryLoadThumbnail: load FAILED levelID={} (cached fail)",
                            levelID
                        );
                    } else {
                        log::warn("[LevelCell] tryLoadThumbnail: load FAILED levelID={}", levelID);
                    }
                    cell->applyStaticThumbnailTexture(levelID, currentRequestId, nullptr, enableSpinners);
                    return;
                }
                log::debug("[LevelCell] tryLoadThumbnail: texture loaded OK levelID={}", levelID);
                if (auto f = cell->m_fields.self()) {
                    f->m_hasGif = ThumbnailLoader::get().hasGIFData(levelID);
                }
                cell->applyStaticThumbnailTexture(levelID, currentRequestId, tex, enableSpinners);
            }
        );
    }

    void tryLoadThumbnail() {
        configureThumbnailLoader();

        if (!m_level) {
            log::warn("[LevelCell] tryLoadThumbnail: m_level is null, aborting");
            return;
        }

        WeakRef<PaimonLevelCell> self = this;
        auto fieldRefs = thumbLoadFieldRefs();

        paimon::thumbnails::levelcell::runThumbnailLoadPipeline(
            {
                .levelID = m_level->m_levelID.value(),
                .dailyID = m_level->m_dailyID.value(),
                .isOnScreen = paimon::levelcell::isLevelCellLikelyOnScreen(this),
            },
            fieldRefs,
            {
                .ensureMaintenance = [self]() {
                    if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                        cell->ensureMaintenanceTickScheduled();
                    }
                },
                .ensureInvalidationListener = [self]() {
                    if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                        cell->ensureInvalidationListener();
                    }
                },
                .resetForLevelChange = [self](int32_t levelID) {
                    if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                        cell->resetThumbnailStateForLevelChange(levelID);
                    }
                },
                .tryReuseApplied = [self](int32_t levelID) {
                    if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                        return cell->tryReuseAppliedThumbnail(levelID);
                    }
                    return false;
                },
                .applyStatic = [self](int32_t levelID, int requestId, CCTexture2D* tex, bool spinners) {
                    if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                        cell->applyStaticThumbnailTexture(levelID, requestId, tex, spinners);
                    }
                },
                .requestGallery = [self](int32_t levelID) {
                    if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                        cell->requestGalleryData(levelID);
                    }
                },
                .tryLocalVideo = [self](int32_t levelID, bool spinners) {
                    if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                        return cell->tryStartLocalVideoThumbnail(levelID, spinners);
                    }
                    return false;
                },
                .tryServerVideo = [self](int32_t levelID, int requestId, bool spinners) {
                    if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                        return cell->tryStartServerVideoThumbnail(levelID, requestId, spinners);
                    }
                    return false;
                },
                .requestStatic = [self](int32_t levelID, int requestId, bool spinners, bool onScreen) {
                    if (auto* cell = static_cast<PaimonLevelCell*>(self.lock().data())) {
                        cell->requestStaticThumbnailLoad(levelID, requestId, spinners, onScreen);
                    }
                },
            }
        );
    }

    $override void update(float dt) {
        LevelCell::update(dt);

        auto fields = m_fields.self();
        if (!fields) return;

        if (fields->m_isBeingDestroyed) return;

        // Video player frame update (runs on GL/main thread)
        if (fields->m_hasVideo && fields->m_videoPlayer && fields->m_videoPlayer->isPlaying()) {
            fields->m_videoPlayer->update(dt);

            if (!fields->m_thumbnailApplied && fields->m_videoPlayer->hasVisibleFrame()) {
                if (auto* videoTex = fields->m_videoPlayer->getCurrentFrameTexture()) {
                    fields->m_thumbnailApplied = true;
                    hideLoadingSpinner();
                    addOrUpdateThumb(videoTex);
                }
            }
        }

        // Viewport check: if a texture is pending because the cell was off-viewport
        // when the callback arrived, do the full setup now that it's visible.
        // Must run BEFORE unscheduleUpdate(): for cells without video, the unschedule
        // would stop update() forever and the pending texture would never apply.

        // No video: unschedule update().
        if (!(fields->m_hasVideo && fields->m_videoPlayer && fields->m_videoPlayer->isPlaying())) {
            this->unscheduleUpdate();
        }
    }

    // CompactLists-inspired layout adjustments for compact mode.
    // When m_compactView is true and the level is at list position 0,
    // hide the place label and shift all elements left to fill the gap.
    void applyCompactLayoutAdjustments() {
        if (!m_compactView || !m_level) return;
        // Only adjust for the first page (listPosition == 0)
        if (m_level->m_listPosition != 0) return;
        // Skip if already adjusted (avoid double-shift on reload)
        if (this->getUserFlag("compact-adjusted"_spr)) return;
        this->setUserFlag("compact-adjusted"_spr, true);

        if (auto label = m_mainLayer->getChildByID("level-place")) {
            label->setVisible(false);

            // Shift all mainLayer children left (except main-menu)
            for (auto* child : CCArrayExt<CCNode*>(m_mainLayer->getChildren())) {
                if (child->getID() != "main-menu") {
                    child->setPositionX(child->getPositionX() - 20.f);
                }
            }

            // Shift menu children left (except view-button)
            if (auto menu = m_mainLayer->getChildByID("main-menu")) {
                for (auto* child : CCArrayExt<CCNode*>(menu->getChildren())) {
                    if (child->getID() != "view-button") {
                        child->setPositionX(child->getPositionX() - 20.f);
                    }
                }
            }

            // Move completed-icon and percentage-label next to view button
            auto moveNextToView = [this](CCNode* node) {
                if (!node) return;
                auto viewButton = m_mainMenu ? m_mainMenu->getChildByID("view-button") : nullptr;
                if (!viewButton) return;
                node->setPosition({276.f, 25.f});
            };
            moveNextToView(m_mainLayer->getChildByID("completed-icon"));
            moveNextToView(m_mainLayer->getChildByID("percentage-label"));
        }
    }

    // Transparent Lists-inspired: make cell backgrounds invisible.
    // Called before tryLoadThumbnail. setupGradient handles bg transparency
    // after creating the clipper with the thumbnails.
    void applyTransparentMode() {
        // No-op here: real transparency is applied in setupGradient after the
        // clipper is created with the correct contentSize. For cells without a
        // thumbnail, apply it here.
        auto fields = m_fields.self();
        if (!fields) return;
        bool transparentMode = Mod::get()->getSavedValue<bool>("transparent-list-mode", false);
        if (!transparentMode) return;

        if (auto bg = m_backgroundLayer) {
            // Save contentSize before hiding
            auto size = bg->getContentSize();
            bg->changeWidthAndHeight(0.f, 0.f);
            // Restore contentSize so children position correctly
            bg->setContentSize(size);
        }
    }

    bool isInCompactExcludedContext() {
        // Fast path: if the thread_local flag is set (by LevelListLayer::init and
        // LevelBrowserLayer::setupLevelBrowser), we're in an excluded context even
        // before the cell is parented. Critical during initial creation: the cell
        // is added to contentLayer -> BoomListView (still parentless), so walking
        // the parent chain wouldn't find LevelListLayer/LevelBrowserLayer.
        if (paimon::hooks::g_suppressCompactLevelCellsInContext) {
            return true;
        }

        CCNode* parent = this->getParent();
        for (int depth = 0; parent && depth < 16; ++depth, parent = parent->getParent()) {
            std::string_view className(typeid(*parent).name());
            if (className.find("LevelListLayer") != std::string_view::npos ||
                typeinfo_cast<LevelListLayer*>(parent)) {
                return true;
            }
        }
        // Check LevelBrowserLayer separately - but only if NOT inside a LevelListLayer
        parent = this->getParent();
        for (int depth = 0; parent && depth < 16; ++depth, parent = parent->getParent()) {
            if (auto browser = typeinfo_cast<LevelBrowserLayer*>(parent)) {
                return browser->m_searchObject &&
                       browser->m_searchObject->m_searchType == SearchType::MyLevels;
            }
        }

        // Extra fallback: check the running scene in case the cell isn't parented
        // yet (initial creation). Exclude if the on-scene layer is LevelListLayer
        // or a LevelBrowserLayer in MyLevels.
        if (auto* scene = cocos2d::CCDirector::get()->getRunningScene()) {
            for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
                if (typeinfo_cast<LevelListLayer*>(child)) {
                    return true;
                }
                if (auto* browser = typeinfo_cast<LevelBrowserLayer*>(child)) {
                    if (browser->m_searchObject &&
                        browser->m_searchObject->m_searchType == SearchType::MyLevels) {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    bool isInsideLevelListLayerContext() {
        CCNode* parent = this->getParent();
        for (int depth = 0; parent && depth < 16; ++depth, parent = parent->getParent()) {
            std::string_view className(typeid(*parent).name());
            if (className.find("LevelListLayer") != std::string_view::npos ||
                typeinfo_cast<LevelListLayer*>(parent)) {
                return true;
            }
        }
        return false;
    }

    bool isInsideRealtimeSearchPreviewContext() {
        if (paimon::hooks::g_forceCompactLevelCells) {
            return true;
        }

        CCNode* parent = this->getParent();
        for (int depth = 0; parent && depth < 16; ++depth, parent = parent->getParent()) {
            auto id = parent->getID();
            if (id == "paimon-realtime-results-list"_spr ||
                id == "paimon-realtime-results-scroll"_spr ||
                id == "paimon-realtime-search-preview-container"_spr ||
                id == "paimon-realtime-search-preview"_spr) {
                return true;
            }
        }
        return false;
    }

    bool shouldUseCompactForLevel(GJGameLevel* level) {
        if (paimon::hooks::g_forceCompactLevelCells) {
            return true;
        }

        if (paimon::hooks::g_suppressCompactLevelCellsInContext) {
            return false;
        }

        bool compact = Mod::get()->getSettingValue<bool>("compact-list-mode");
        if (isInsideRealtimeSearchPreviewContext()) {
            return true;
        }
        if (isInCompactExcludedContext()) {
            return false;
        }
        // Don't apply compact mode to timed levels (daily/weekly/event).
        if (level && level->m_dailyID > 0) {
            compact = false;
        }
        return compact;
    }

    void resetCompactLayoutState() {
        this->setUserFlag("compact-adjusted"_spr, false);
        if (auto fields = m_fields.self()) {
            fields->m_compactLayoutApplied = false;
        }
    }

    void applyCompactViewFromSetting(GJGameLevel* level = nullptr) {
        // In excluded contexts (My Levels and LevelListLayer), force
        // m_compactView=false so cells render with GD's vanilla layout. Leaving it
        // untouched let a cell recycled from a compact context keep
        // m_compactView=true and mix layouts.
        if (isInCompactExcludedContext()) {
            m_compactView = false;
            return;
        }
        m_compactView = shouldUseCompactForLevel(level ? level : m_level);
    }

    $override void loadCustomLevelCell() {
        // In excluded contexts (My Levels, LevelListLayer), skip all mod logic and
        // let GD render the cell exactly as vanilla. The cell type (Level vs Level4)
        // already determines the layout, and the CustomListView hook avoids
        // converting them to Level4 in these contexts.
        if (isInCompactExcludedContext()) {
            LevelCell::loadCustomLevelCell();
            return;
        }

        applyCompactViewFromSetting();
        resetCompactLayoutState();
        LevelCell::loadCustomLevelCell();
        if (auto fields = m_fields.self()) {
            fields->m_isBeingDestroyed = false;
            fields->m_cachedCompactMode = m_compactView;
            // Reset thumbnail state so recycled cells don't keep the previous
            // level's flags (bug: last cell didn't load).
            fields->m_thumbnailRequested = false;
            fields->m_thumbnailApplied = false;
            fields->m_cellLevelID = 0;
            fields->m_thumbnailRequestTime = std::chrono::steady_clock::time_point{};
        }
        if (paimon::hooks::g_suppressLevelCellEnhancements) {
            return;
        }
        if (!isInsideLevelListLayerContext()) {
            applyCompactLayoutAdjustments();
        }
        applyTransparentMode();
        log::debug("[LevelCell] loadCustomLevelCell levelID={} compact={}", m_level ? m_level->m_levelID.value() : 0, m_compactView);
        tryLoadThumbnail();
    }

    $override void loadFromLevel(GJGameLevel* level) {
        // In excluded contexts (My Levels, LevelListLayer), skip all mod logic and
        // let GD render the cell exactly as vanilla. The cell type (Level vs Level4)
        // already determines the layout, and the CustomListView hook avoids
        // converting them to Level4 in these contexts.
        if (isInCompactExcludedContext()) {
            LevelCell::loadFromLevel(level);
            return;
        }

        applyCompactViewFromSetting(level);
        resetCompactLayoutState();
        LevelCell::loadFromLevel(level);
        if (auto fields = m_fields.self()) {
            fields->m_isBeingDestroyed = false;
            fields->m_cachedCompactMode = m_compactView;

            int32_t newLevelID = level ? level->m_levelID.value() : 0;
            bool sameLevel = (newLevelID > 0 && fields->m_cellLevelID == newLevelID);

            if (!sameLevel) {
                fields->m_thumbnailRequested = false;
                fields->m_thumbnailApplied = false;
                fields->m_cellLevelID = 0;
            } else {
                bool const spriteAlive = fields->m_thumbSprite && fields->m_thumbSprite->getParent();
                if (!spriteAlive) {
                    fields->m_thumbnailRequested = false;
                    fields->m_thumbnailApplied = false;
                }
            }
            fields->m_thumbnailRequestTime = std::chrono::steady_clock::time_point{};
        }
        if (paimon::hooks::g_suppressLevelCellEnhancements) {
            return;
        }
        applyTransparentMode();
        log::debug("[LevelCell] loadFromLevel levelID={} compact={}", level ? level->m_levelID.value() : 0, m_compactView);
        tryLoadThumbnail();
    }

};
