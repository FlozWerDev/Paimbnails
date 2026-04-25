#include <Geode/modify/LevelCell.hpp>
#include <Geode/binding/BoomListView.hpp>
#include <Geode/binding/DailyLevelNode.hpp>
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
#include <unordered_set>
#include "../core/Settings.hpp"
#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../features/thumbnails/services/LevelColors.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/Constants.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../utils/Shaders.hpp"
#include "../blur/BlurSystem.hpp"
#include "../utils/PaimonShaderSprite.hpp"
#include "../utils/RetainedLazyTextureLoad.hpp"
#include "../features/thumbnails/ui/LevelCellSettingsPopup.hpp"
#include "../framework/compat/ModCompat.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../video/VideoPlayer.hpp"
#include "../utils/VideoThumbnailSprite.hpp"
#include "../utils/HttpClient.hpp"

using namespace geode::prelude;
using namespace Shaders;

// Enums para settings cached
enum class PaimonAnimType : uint8_t {
    None, ZoomSlide, Zoom, Slide, Bounce, Rotate, RotateContent, Shake, Pulse, Swing
};

enum class PaimonAnimEffect : uint8_t {
    None, Brightness, Darken, Sepia, Sharpen, EdgeDetection, Vignette, Pixelate,
    Posterize, Chromatic, Scanlines, Solarize, Rainbow, Red, Blue, Gold, Fade,
    Grayscale, Invert, Blur, Glitch
};

enum class PaimonBgType : uint8_t { Gradient, Thumbnail };

enum class PaimonGalleryTransition : uint8_t {
    Crossfade, SlideLeft, SlideRight, SlideUp, SlideDown,
    ZoomIn, ZoomOut, FlipHorizontal, FlipVertical,
    RotateCW, RotateCCW, Cube, Dissolve, Swipe, Bounce,
    ElasticSlide, DirectionalElastic, Spiral, Wave, Pop,
    Random
};

static float safeCoverScale(float targetWidth, float targetHeight, float contentWidth, float contentHeight, float fallback = 1.0f) {
    if (targetWidth <= 0.0f || targetHeight <= 0.0f || contentWidth <= 0.0f || contentHeight <= 0.0f) {
        return fallback;
    }
    float scale = std::max(targetWidth / contentWidth, targetHeight / contentHeight);
    if (scale <= 0.0f) return fallback;
    return std::clamp(scale, 0.01f, 64.0f);
}

static float getLevelCellThumbWidthFactor() {
    float widthFactor = static_cast<float>(Mod::get()->getSettingValue<double>("level-thumb-width"));
    return std::clamp(widthFactor, PaimonConstants::MIN_THUMB_WIDTH_FACTOR, PaimonConstants::MAX_THUMB_WIDTH_FACTOR);
}

static float calculateLevelCellThumbCoverScale(CCSprite* sprite, float bgWidth, float bgHeight, float widthFactor, float fallback = 1.0f) {
    if (!sprite) {
        return fallback;
    }

    return safeCoverScale(
        bgWidth * widthFactor,
        bgHeight,
        sprite->getContentSize().width,
        sprite->getContentSize().height,
        fallback
    );
}

static std::vector<ThumbnailAPI::ThumbnailInfo> normalizeLevelCellGalleryThumbnails(
    int32_t levelID,
    std::vector<ThumbnailAPI::ThumbnailInfo> thumbnails
) {
    std::erase_if(thumbnails, [](ThumbnailAPI::ThumbnailInfo const& thumb) {
        return thumb.url.empty();
    });

    std::stable_sort(thumbnails.begin(), thumbnails.end(), [](ThumbnailAPI::ThumbnailInfo const& a, ThumbnailAPI::ThumbnailInfo const& b) {
        if (a.position != b.position) return a.position < b.position;
        if (a.id != b.id) return a.id < b.id;
        return a.url < b.url;
    });

    // Elimina duplicados por URL
    std::unordered_set<std::string_view> seenUrls;
    seenUrls.reserve(thumbnails.size());
    std::vector<ThumbnailAPI::ThumbnailInfo> normalized;
    normalized.reserve(thumbnails.size() + 1);

    for (auto& thumb : thumbnails) {
        if (!seenUrls.insert(thumb.url).second) {
            continue;
        }
        normalized.push_back(std::move(thumb));
    }

    if (normalized.empty() && levelID > 0) {
        ThumbnailAPI::ThumbnailInfo mainThumb;
        mainThumb.id = "0";
        mainThumb.url = ThumbnailAPI::get().getThumbnailURL(levelID);
        mainThumb.type = "static";
        mainThumb.position = 0;
        normalized.push_back(std::move(mainThumb));
    }

    return normalized;
}

static constexpr int LEVELCELL_GALLERY_LOOKAHEAD = 2;
static constexpr int LEVELCELL_GALLERY_SEARCH_WINDOW = 3;
static constexpr size_t LEVELCELL_GALLERY_MAX_PENDING = 3;
static constexpr float LEVELCELL_GALLERY_RETRY_DELAY = 8.0f;
static constexpr int LEVELCELL_GALLERY_MAX_MISSES = 2;

static PaimonAnimType parseAnimType(std::string const& s) {
    static constexpr std::pair<std::string_view, PaimonAnimType> table[] = {
        {"zoom-slide", PaimonAnimType::ZoomSlide}, {"zoom", PaimonAnimType::Zoom},
        {"slide", PaimonAnimType::Slide}, {"bounce", PaimonAnimType::Bounce},
        {"rotate", PaimonAnimType::Rotate}, {"rotate-content", PaimonAnimType::RotateContent},
        {"shake", PaimonAnimType::Shake}, {"pulse", PaimonAnimType::Pulse},
        {"swing", PaimonAnimType::Swing},
    };
    for (auto const& [key, val] : table) {
        if (key == s) return val;
    }
    return PaimonAnimType::None;
}

static PaimonAnimEffect parseAnimEffect(std::string const& s) {
    static constexpr std::pair<std::string_view, PaimonAnimEffect> table[] = {
        {"brightness", PaimonAnimEffect::Brightness}, {"darken", PaimonAnimEffect::Darken},
        {"sepia", PaimonAnimEffect::Sepia}, {"sharpen", PaimonAnimEffect::Sharpen},
        {"edge-detection", PaimonAnimEffect::EdgeDetection}, {"vignette", PaimonAnimEffect::Vignette},
        {"pixelate", PaimonAnimEffect::Pixelate}, {"posterize", PaimonAnimEffect::Posterize},
        {"chromatic", PaimonAnimEffect::Chromatic}, {"scanlines", PaimonAnimEffect::Scanlines},
        {"solarize", PaimonAnimEffect::Solarize}, {"rainbow", PaimonAnimEffect::Rainbow},
        {"red", PaimonAnimEffect::Red}, {"blue", PaimonAnimEffect::Blue},
        {"gold", PaimonAnimEffect::Gold}, {"fade", PaimonAnimEffect::Fade},
        {"grayscale", PaimonAnimEffect::Grayscale}, {"invert", PaimonAnimEffect::Invert},
        {"blur", PaimonAnimEffect::Blur}, {"glitch", PaimonAnimEffect::Glitch},
    };
    for (auto const& [key, val] : table) {
        if (key == s) return val;
    }
    return PaimonAnimEffect::None;
}

static PaimonBgType parseBgType(std::string const& s) {
    return s == "thumbnail" ? PaimonBgType::Thumbnail : PaimonBgType::Gradient;
}

static PaimonGalleryTransition parseGalleryTransition(std::string const& s) {
    static constexpr std::pair<std::string_view, PaimonGalleryTransition> table[] = {
        {"crossfade", PaimonGalleryTransition::Crossfade}, {"slide-left", PaimonGalleryTransition::SlideLeft},
        {"slide-right", PaimonGalleryTransition::SlideRight}, {"slide-up", PaimonGalleryTransition::SlideUp},
        {"slide-down", PaimonGalleryTransition::SlideDown}, {"zoom-in", PaimonGalleryTransition::ZoomIn},
        {"zoom-out", PaimonGalleryTransition::ZoomOut}, {"flip-horizontal", PaimonGalleryTransition::FlipHorizontal},
        {"flip-vertical", PaimonGalleryTransition::FlipVertical}, {"rotate-cw", PaimonGalleryTransition::RotateCW},
        {"rotate-ccw", PaimonGalleryTransition::RotateCCW}, {"cube", PaimonGalleryTransition::Cube},
        {"dissolve", PaimonGalleryTransition::Dissolve}, {"swipe", PaimonGalleryTransition::Swipe},
        {"bounce", PaimonGalleryTransition::Bounce}, {"elastic-slide", PaimonGalleryTransition::ElasticSlide},
        {"directional-elastic", PaimonGalleryTransition::DirectionalElastic},
        {"spiral", PaimonGalleryTransition::Spiral}, {"wave", PaimonGalleryTransition::Wave},
        {"pop", PaimonGalleryTransition::Pop}, {"random", PaimonGalleryTransition::Random},
    };
    for (auto const& [key, val] : table) {
        if (key == s) return val;
    }
    return PaimonGalleryTransition::Crossfade;
}

static PaimonGalleryTransition resolveRandomTransition() {
    thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 19); // Excluye Random
    return static_cast<PaimonGalleryTransition>(dist(rng));
}

class $modify(PaimonLevelCell, LevelCell) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityAfterPost("LevelCell::loadFromLevel", "geode.node-ids");
        (void)self.setHookPriorityAfterPost("LevelCell::loadCustomLevelCell", "geode.node-ids");
    }

    struct Fields {
        Ref<CCClippingNode> m_clippingNode = nullptr;
        Ref<CCNode> m_separator = nullptr;
        Ref<CCNode> m_gradient = nullptr;
        Ref<CCParticleSystemQuad> m_mythicParticles = nullptr;
        Ref<CCDrawNode> m_darkOverlay = nullptr;
        float m_gradientTime = 0.0f;
        ccColor3B m_gradientColorA = {0, 0, 0};
        ccColor3B m_gradientColorB = {0, 0, 0};
        Ref<CCSprite> m_gradientLayer = nullptr;
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
        int m_lastRequestedLevelID = 0; 
        bool m_thumbnailApplied = false; 
        bool m_wasInCenter = false; 
        float m_centerLerp = 0.0f; 
        Ref<CCMenuItemSpriteExtra> m_viewOverlay = nullptr; 
        
        float m_animTime = 0.0f;
        bool m_hasGif = false;
        Ref<CCTexture2D> m_staticTexture = nullptr;
        Ref<CCTexture2D> m_pendingStaggerTexture = nullptr;
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

        int m_loadedInvalidationVersion = 0;

        int m_loadedSettingsVersion = 0;

        int m_cellLevelID = 0;
        bool m_isDailyCell = false;
        bool m_isDailyCellCached = false;
        std::vector<ThumbnailAPI::ThumbnailInfo> m_galleryThumbnails;
        std::unordered_set<std::string> m_galleryPendingUrls;
        int m_galleryIndex = 0;
        float m_galleryTimer = 0.f;
        bool m_galleryRequested = false;
        int m_galleryToken = 0;
        int m_invalidationListenerId = 0;
        float m_updateCheckTimer = 0.f; // Throttle checks update()
        std::chrono::steady_clock::time_point m_galleryTransitionStart{};
        static constexpr float GALLERY_TRANSITION_SAFETY_TIMEOUT = 2.0f;
        int m_galleryConsecutiveMisses = 0;
        ccColor3B m_lastBgColor = {0, 0, 0};
        int m_bgBlurToken = 0;
    };
    
    ~PaimonLevelCell() {
        auto fields = m_fields.self();
        if (fields) {
            fields->m_isBeingDestroyed = true;
        }
    }
    
    static void calculateLevelCellThumbScale(CCSprite* sprite, float bgWidth, float bgHeight, float widthFactor, float& outScaleX, float& outScaleY) {
        if (!sprite) return;
        
        const float contentWidth = sprite->getContentSize().width;
        const float contentHeight = sprite->getContentSize().height;
        if (contentWidth <= 0.f || contentHeight <= 0.f || bgWidth <= 0.f || bgHeight <= 0.f) {
            outScaleX = 1.f;
            outScaleY = 1.f;
            return;
        }
        const float desiredWidth = bgWidth * widthFactor;
        
        outScaleY = bgHeight / contentHeight;
        
        float minScaleX = outScaleY; 
        float desiredScaleX = desiredWidth / contentWidth;
        outScaleX = std::max(minScaleX, desiredScaleX);
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
        
        // Fade-in suave
        spinner->setOpacity(0);
        spinner->runAction(CCFadeTo::create(0.3f, 180));
    }
    
    void hideLoadingSpinner() {
        auto fields = m_fields.self();
        if (fields->m_loadingSpinner) {
            // Fade out y remueve
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

    void configureThumbnailLoader() {
        static bool s_loaderConfigured = false;
        if (!s_loaderConfigured) {
            int maxDownloads = static_cast<int>(Mod::get()->getSettingValue<int64_t>("thumbnail-concurrent-downloads"));
            ThumbnailLoader::get().setMaxConcurrentTasks(maxDownloads);
            s_loaderConfigured = true;
        }
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
        if (levelID <= 0 || levelID > 100) {
            return;
        }

        auto* blackTex = new CCTexture2D();
        blackTex->autorelease(); // Previene leak en early-return
        uint8_t blackPixel[4] = {0, 0, 0, 255};
        if (blackTex->initWithData(blackPixel, kCCTexture2DPixelFormat_RGBA8888, 1, 1, CCSize(1, 1))) {
            this->addOrUpdateThumb(blackTex);
        }
    }

    // Stagger: global slot counter for smooth one-by-one thumbnail appearance
    static inline int s_staggerSlot = 0;
    static inline std::chrono::steady_clock::time_point s_lastStaggerApply{};

    void applyPendingStaggerThumb(float /*dt*/) {
        auto fields = m_fields.self();
        if (!fields || fields->m_isBeingDestroyed) return;
        auto tex = fields->m_pendingStaggerTexture;
        fields->m_pendingStaggerTexture = nullptr;
        if (!tex) return;
        this->addOrUpdateThumb(tex);
        this->flashThumbnailSprite();
    }

    void applyStaticThumbnailTexture(int32_t levelID, int currentRequestId, CCTexture2D* texture, bool enableSpinners) {
        if (!this->shouldHandleThumbnailCallback(levelID, currentRequestId)) {
            return;
        }

        auto fields = m_fields.self();
        fields->m_staticThumbLoad.reset();
        this->unschedule(schedule_selector(PaimonLevelCell::applyPendingStaggerThumb));
        fields->m_pendingStaggerTexture = nullptr;

        if (enableSpinners) {
            this->hideLoadingSpinner();
        }

        if (fields->m_thumbnailApplied) {
            return;
        }

        if (!texture) {
            this->applyMainLevelFallbackThumbnail(levelID);
            return;
        }

        fields->m_thumbnailApplied = true;
        fields->m_staticTexture = texture;

        // Sin stagger: muestra thumbnails inmediatamente
        this->addOrUpdateThumb(texture);
        this->flashThumbnailSprite();
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
            if (!cell) {
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
        
        // Elimina nodos rastreados de forma segura
        auto removeNodeSafe = [](auto& node) {
            if (node) {
                if (node->getParent()) node->removeFromParent();
                node = nullptr; 
            }
        };

        removeNodeSafe(fields->m_clippingNode);
        removeNodeSafe(fields->m_separator);
        removeNodeSafe(fields->m_gradient);
        removeNodeSafe(fields->m_hoverContainer);
        removeNodeSafe(fields->m_boundsClipper);
        
        // CCParticleSystemQuad requiere manejo manual
        if (fields->m_mythicParticles) {
            if (fields->m_mythicParticles->getParent()) fields->m_mythicParticles->removeFromParent();
            fields->m_mythicParticles = nullptr;
        }

        removeNodeSafe(fields->m_darkOverlay);
        
        // Anula otras referencias
        fields->m_gradientLayer = nullptr;
        fields->m_thumbSprite = nullptr;
        fields->m_staticThumbLoad.reset();
        fields->m_loadingSpinner = nullptr; // spinner suele gestionarse con show/hide, limpiar aqui por seguridad
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

        // Limpia restos con id "paimon"
        auto cleanByID = [](CCNode* parent) {
            if (!parent) return;
            auto children = parent->getChildren();
            if (!children) return;
            std::vector<CCNode*> toRemove;
            toRemove.reserve(8);
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (child) {
                    std::string_view id = child->getID();
                    // IDs propios empiezan con "paimon"
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
            // No setea opacity(0) para evitar celdas en blanco
            
            WeakRef<PaimonLevelCell> safeRef = this;
            int currentRequestId = m_fields->m_requestId;
            AnimatedGIFSprite::createAsync(gifPath, [safeRef, levelIDForGIF, currentRequestId](AnimatedGIFSprite* anim) {
                auto cellRef = safeRef.lock();
                auto* cell = static_cast<PaimonLevelCell*>(cellRef.data());
                if (!cell) return;
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
                // Si GIF falla, sprite estatico sigue visible
            });
        }
        
        if (sprite) {
            sprite->setID("paimon-thumbnail"_spr);
            // Asegura ID para deteccion de shader
            if (auto pss = typeinfo_cast<PaimonShaderSprite*>(sprite)) {
                 pss->setID("paimon-shader-sprite"_spr);
            }
            
            std::string bgType = Mod::get()->getSettingValue<std::string>("levelcell-background-type");

            // Efectos manejados en updateCenterAnimation
        }
        return sprite;
    }

    CCClippingNode* createThumbnailClippingNode(CCNode* bg, CCSprite* sprite, float& outCoverScale) {
        if (!bg || !sprite) {
            log::warn("[LevelCell] createThumbnailClippingNode: null bg or sprite");
            return nullptr;
        }

        float kThumbWidthFactor = getLevelCellThumbWidthFactor();
        const float bgWidth = bg->getContentWidth();
        const float bgHeight = bg->getContentHeight();
        const float desiredWidth = bgWidth * kThumbWidthFactor;

        float scaleX, scaleY;
        calculateLevelCellThumbScale(sprite, bgWidth, bgHeight, kThumbWidthFactor, scaleX, scaleY);
        outCoverScale = calculateLevelCellThumbCoverScale(sprite, bgWidth, bgHeight, kThumbWidthFactor);
        sprite->setScale(outCoverScale);
        log::debug("[LevelCell] createThumbnailClippingNode: bgSize=({:.1f},{:.1f}) widthFactor={:.2f} coverScale={:.4f} scaleX={:.4f} scaleY={:.4f}",
            bgWidth, bgHeight, kThumbWidthFactor, outCoverScale, scaleX, scaleY);

        CCSize scaledSize{ desiredWidth, bgHeight };
        const float kDiagonalSkew = 35.f; // Desplazamiento diagonal del borde izquierdo
        auto drawMask = paimon::SpriteHelper::createDiagonalStencil(scaledSize.width, scaledSize.height, kDiagonalSkew);
        if (!drawMask) return nullptr;
        drawMask->setAnchorPoint({1,0});
        drawMask->ignoreAnchorPointForPosition(true);

        auto clippingNode = CCClippingNode::create();
        if (!clippingNode) return nullptr;

        clippingNode->setStencil(drawMask);
        clippingNode->setContentSize(scaledSize);
        clippingNode->setAnchorPoint({1,0});
        clippingNode->setPosition({ bgWidth, 0.f });
        clippingNode->setID("paimon-clipping-node"_spr);
        clippingNode->setZOrder(-1);

        sprite->setPosition(clippingNode->getContentSize() * 0.5f);
        clippingNode->addChild(sprite);
        return clippingNode;
    }

    void setupClippingAndSeparator(CCNode* bg, CCSprite* sprite) {
        auto fields = m_fields.self();
        if (!fields) return;
        cacheSettings();
        log::debug("[LevelCell] setupClippingAndSeparator: entering");

        // forzar ancho completo pa celdas Daily
        bool isDaily = false;
        if (m_level && m_level->m_dailyID > 0) isDaily = true;

        float coverScale = 1.0f;
        auto clippingNode = createThumbnailClippingNode(bg, sprite, coverScale);
        if (!clippingNode) return;

        auto bgSize = bg->getContentSize();
        auto bgPos = bg->getPosition();

        // Clipper maestro que limita al area visible de la celda
        auto boundsStencil = paimon::SpriteHelper::createRectStencil(bgSize.width, bgSize.height);
        if (!boundsStencil) return;
        auto boundsClipper = CCClippingNode::create(boundsStencil);
        if (!boundsClipper) return;
        boundsClipper->setContentSize(bgSize);
        boundsClipper->setPosition(bgPos);
        boundsClipper->setAnchorPoint({0, 0});
        boundsClipper->setZOrder(-1);
        boundsClipper->setID("paimon-bounds-clipper"_spr);
        this->addChild(boundsClipper);

        // Contenedor intermedio para hover zoom
        auto hoverContainer = CCNode::create();
        hoverContainer->setContentSize(bgSize);
        hoverContainer->setAnchorPoint({0, 0});
        hoverContainer->setPosition({0, 0});
        hoverContainer->setID("paimon-hover-container"_spr);
        boundsClipper->addChild(hoverContainer);

        // Miniatura dentro del hover container
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
        
        bool hoverEnabled = fields->m_cachedHoverEnabled;

        if (hoverEnabled) {
            if (!fields->m_isCenterAnimScheduled) {
                this->schedule(schedule_selector(PaimonLevelCell::updateCenterAnimation));
                fields->m_isCenterAnimScheduled = true;
            }
        }

        fields->m_clippingNode = clippingNode;

        bool showSeparator = fields->m_cachedShowSeparator;
        const float bgWidth = bgSize.width;
        CCSize scaledSize = clippingNode->getContentSize();

        if (showSeparator && !isDaily) { // No separator for Daily
            float separatorXMul = m_compactView ? 0.75f : 1.0f;
            const float kDiagonalSkew = 35.f;
            const float sepW = 3.f; // grosor visual del separador

            auto separator = PaimonDrawNode::create();
            separator->setID("paimon-separator"_spr);

            // Paralelogramo diagonal que coincide con el corte de la miniatura
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

    void setupGradient(CCNode* bg, int levelID, CCTexture2D* texture) {
        auto fields = m_fields.self();
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

        if (bgType == PaimonBgType::Thumbnail && texture) {
             // Oculta bg pero mantiene nodo visible para hijos
             bg->setVisible(true);
             if (auto* bgLayer = typeinfo_cast<CCLayerColor*>(bg)) {
                 bgLayer->setOpacity(0);
             }
             float blurIntensity = static_cast<float>(Mod::get()->getSettingValue<double>("levelcell-background-blur"));
             bool hasGifBackground = ThumbnailLoader::get().hasGIFData(levelID);
             std::string gifPath = hasGifBackground
                 ? geode::utils::string::pathToString(ThumbnailLoader::get().getCachePath(levelID, true))
                 : std::string();

             auto ensureBgClipper = [bg]() -> CCClippingNode* {
                 if (auto existing = typeinfo_cast<CCClippingNode*>(static_cast<CCNode*>(bg->getChildByID("paimon-bg-clipper"_spr)))) {
                     return existing;
                 }

                 auto stencil = paimon::SpriteHelper::createRectStencil(bg->getContentWidth(), bg->getContentHeight());
                 if (!stencil) return nullptr;
                 auto clipper = CCClippingNode::create(stencil);
                 if (!clipper) {
                     return nullptr;
                 }
                 clipper->setContentSize(bg->getContentSize());
                 clipper->setPosition({0,0});
                 clipper->setZOrder(10);
                 clipper->setID("paimon-bg-clipper"_spr);
                 bg->addChild(clipper);
                 bg->reorderChild(clipper, 10);
                 return clipper;
             };

             auto attachOverlay = [bg, fields](CCClippingNode* clipper) {
                 if (!clipper) return;
                 if (auto oldOverlay = clipper->getChildByID("paimon-level-background-overlay"_spr)) {
                     oldOverlay->removeFromParent();
                 }

                 float darkness = fields->m_cachedBackgroundDarkness;
                 GLubyte opacity = static_cast<GLubyte>(std::clamp(darkness, 0.0f, 1.0f) * 255.0f);
                 auto overlay = paimon::SpriteHelper::createDarkPanel(bg->getContentWidth(), bg->getContentHeight(), opacity, 0.f);
                 if (!overlay) return;
                 overlay->setPosition({0, 0});
                 overlay->setID("paimon-level-background-overlay"_spr);
                 clipper->addChild(overlay);
                 fields->m_darkOverlay = overlay;
             };

             auto attachBackgroundSprite = [bg, fields, ensureBgClipper, attachOverlay](CCSprite* mediaSprite) {
                 auto clipper = ensureBgClipper();
                 if (!clipper) return;

                 if (auto oldMedia = clipper->getChildByID("paimon-level-background"_spr)) {
                     oldMedia->removeFromParent();
                 }

                 if (mediaSprite) {
                     float targetW = bg->getContentWidth();
                     float targetH = bg->getContentHeight();
                     float scale = safeCoverScale(
                         targetW, targetH,
                         mediaSprite->getContentSize().width, mediaSprite->getContentSize().height,
                         1.0f
                     );
                     mediaSprite->setScale(scale);
                     mediaSprite->setPosition(bg->getContentSize() / 2);
                     mediaSprite->setID("paimon-level-background"_spr);
                     clipper->addChild(mediaSprite);
                     fields->m_gradientLayer = mediaSprite;
                 } else {
                     fields->m_gradientLayer = nullptr;
                 }

                 attachOverlay(clipper);
             };

             // Retain del texture para vida de las lambdas
             Ref<CCTexture2D> texRef = texture;

             // Placeholder sin blur; blur real async via dispatchAsyncBlur
             auto createStaticBackground = [texRef]() -> CCSprite* {
                 return PaimonShaderSprite::createWithTexture(texRef.data());
             };

             // Despacha blur async + swap del placeholder cuando termine.
             // Usa cache RAM (LRU) para re-entries instantaneos.
             int captured_requestId = fields->m_requestId;
             int captured_blurToken = ++fields->m_bgBlurToken;
             WeakRef<PaimonLevelCell> blurSafeRef = this;
             auto dispatchAsyncBlur = [blurSafeRef, levelID, texRef, blurIntensity, captured_requestId, captured_blurToken]() {
                 auto* texPtr = texRef.data();
                 if (!texPtr) return;
                 auto cellRef = blurSafeRef.lock();
                 auto* cell = static_cast<PaimonLevelCell*>(cellRef.data());
                 if (!cell) return;
                 auto bgNode = cell->m_backgroundLayer;
                 if (!bgNode) return;

                 CCSize targetSize = bgNode->getContentSize();
                 targetSize.width = std::max(targetSize.width, 512.f);
                 targetSize.height = std::max(targetSize.height, 256.f);

                 BlurSystem::getInstance()->buildPaimonBlurAsync(
                     texPtr, targetSize, blurIntensity,
                     [blurSafeRef, levelID, captured_requestId, captured_blurToken](CCSprite* blurred) {
                         if (!blurred) return;
                         auto cellRef2 = blurSafeRef.lock();
                         auto* cell2 = static_cast<PaimonLevelCell*>(cellRef2.data());
                         if (!cell2 || !cell2->shouldHandleThumbnailCallback(levelID, captured_requestId)) return;
                         auto fields2 = cell2->m_fields.self();
                         if (!fields2 || fields2->m_isBeingDestroyed) return;
                         if (fields2->m_bgBlurToken != captured_blurToken) return;

                         auto bg2 = cell2->m_backgroundLayer;
                         if (!bg2) return;
                         auto* clipper = typeinfo_cast<CCClippingNode*>(static_cast<CCNode*>(bg2->getChildByID("paimon-bg-clipper"_spr)));
                         if (!clipper) return;

                         // Swap placeholder por sprite blureado
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

        // Oculta bg completo para gradiente
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
        grad->setPosition({0.0f, 0.0f}); // Reset to 0,0 relative to this if bg is at 0,0? No, bg checks its pos
        
        // Agrega gradient a 'this' para evitar BatchNode issues
        int bgZ = bg->getZOrder();
        grad->setZOrder(bgZ - 1); // Behind bg
        grad->setID("paimon-level-gradient"_spr);
        
        // Posiciona gradient igual que bg
        grad->setPosition(bg->getPosition());
        // Bg anchor is usually 0,0?
        if (bg->isIgnoreAnchorPointForPosition()) {
             // Si bg ignora anchor, grad debe imitar
        }
        
        // bg ya fue ocultado
        this->addChild(grad);
        
        // fields->m_gradient = grad; // m_gradient is CCNode* in struct
        fields->m_gradient = grad;
        // bg->addChild(grad); // REMOVED
        // bg->reorderChild(grad, 10); // REMOVED

        fields->m_gradientLayer = grad;
        fields->m_gradientColorA = colorA;
        fields->m_gradientColorB = colorB;

        if (animatedGradient) {
            this->schedule(schedule_selector(PaimonLevelCell::updateGradientAnim), 0.0f);
        }
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

    // Verifica si un menu item es el boton "view"
    static bool isViewButtonItem(CCMenuItemSpriteExtra* menuItem, bool checkDailyPos, bool isDaily, CCSize const& cellSize) {
        std::string_view id = menuItem->getID();
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

    // Busca boton "view" via DFS unico
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

        // DFS sobre todos los hijos
        std::vector<CCNode*> stack;
        stack.reserve(32);
        stack.push_back(this);

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
                        // Crea sprites overlay invisibles
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

                        // Posiciona overlay en borde de miniatura
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
                    return; // Encontrado y configurado
                }
            }

            if (auto arr = cur->getChildren()) {
                for (auto* child : CCArrayExt<CCNode*>(arr)) {
                    if (child) stack.push_back(child);
                }
            }
        }
    }

    // Aplica transicion configurada manteniendo cover-scale dentro del clip
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
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCDelayTime::create(removeDelay),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::SlideLeft: {
            newNode->setPosition({targetPos.x + clipSize.width, targetPos.y});
            newNode->runAction(CCEaseOut::create(CCMoveTo::create(dur, targetPos), 2.5f));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseIn::create(CCMoveTo::create(dur, {targetPos.x - clipSize.width, targetPos.y}), 2.0f),
                    oldSprite ? static_cast<CCFiniteTimeAction*>(CCFadeTo::create(dur * 0.8f, 0)) : static_cast<CCFiniteTimeAction*>(CCDelayTime::create(dur)),
                    nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::SlideRight: {
            newNode->setPosition({targetPos.x - clipSize.width, targetPos.y});
            newNode->runAction(CCEaseOut::create(CCMoveTo::create(dur, targetPos), 2.5f));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseIn::create(CCMoveTo::create(dur, {targetPos.x + clipSize.width, targetPos.y}), 2.0f),
                    oldSprite ? static_cast<CCFiniteTimeAction*>(CCFadeTo::create(dur * 0.8f, 0)) : static_cast<CCFiniteTimeAction*>(CCDelayTime::create(dur)),
                    nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::SlideUp: {
            newNode->setPosition({targetPos.x, targetPos.y - clipSize.height});
            newNode->runAction(CCEaseOut::create(CCMoveTo::create(dur, targetPos), 2.5f));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseIn::create(CCMoveTo::create(dur, {targetPos.x, targetPos.y + clipSize.height}), 2.0f),
                    oldSprite ? static_cast<CCFiniteTimeAction*>(CCFadeTo::create(dur * 0.8f, 0)) : static_cast<CCFiniteTimeAction*>(CCDelayTime::create(dur)),
                    nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::SlideDown: {
            newNode->setPosition({targetPos.x, targetPos.y + clipSize.height});
            newNode->runAction(CCEaseOut::create(CCMoveTo::create(dur, targetPos), 2.5f));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCSpawn::create(
                    CCEaseIn::create(CCMoveTo::create(dur, {targetPos.x, targetPos.y - clipSize.height}), 2.0f),
                    oldSprite ? static_cast<CCFiniteTimeAction*>(CCFadeTo::create(dur * 0.8f, 0)) : static_cast<CCFiniteTimeAction*>(CCDelayTime::create(dur)),
                    nullptr),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::ZoomIn: {
            newNode->setScaleX(sx * 1.12f);
            newNode->setScaleY(sy * 1.12f);
            newSprite->setOpacity(0);
            newNode->runAction(CCEaseOut::create(CCScaleTo::create(dur, sx, sy), 2.5f));
            newSprite->runAction(CCFadeTo::create(dur * 0.7f, 255));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCDelayTime::create(removeDelay),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::ZoomOut: {
            newNode->setScaleX(sx * 1.6f);
            newNode->setScaleY(sy * 1.6f);
            newSprite->setOpacity(0);
            newNode->runAction(CCEaseOut::create(CCScaleTo::create(dur, sx, sy), 2.5f));
            newSprite->runAction(CCFadeTo::create(dur * 0.7f, 255));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCDelayTime::create(removeDelay),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
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
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCDelayTime::create(removeDelay),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;

        case PaimonGalleryTransition::Swipe: {
            // new covers old from right — old stays still
            newNode->setPosition({targetPos.x + clipSize.width, targetPos.y});
            newNode->runAction(CCEaseOut::create(CCMoveTo::create(dur, targetPos), 3.0f));
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCDelayTime::create(removeDelay),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
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
            // Nueva entra desde la derecha con efecto elastico
            newNode->setPosition({targetPos.x + clipSize.width, targetPos.y});
            newNode->runAction(CCEaseElasticOut::create(
                CCMoveTo::create(dur * 1.2f, targetPos), 0.3f));
            // Vieja: zoom pequeno + slide a la izquierda con back-ease + fade
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
            // Nueva: rota desde 360 + escala desde 0 + fade in
            newNode->setScaleX(sx * 0.01f);
            newNode->setScaleY(sy * 0.01f);
            newNode->setRotation(360.0f);
            newSprite->setOpacity(0);
            newNode->runAction(CCSpawn::create(
                CCEaseOut::create(CCScaleTo::create(dur, sx, sy), 2.5f),
                CCEaseOut::create(CCRotateTo::create(dur, 0.0f), 2.5f),
                nullptr));
            newSprite->runAction(CCFadeTo::create(dur * 0.7f, 255));
            // Vieja: rota + escala a 0 + fade out
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
            // Nueva: slide suave desde la derecha
            newNode->setPosition({targetPos.x + clipSize.width, targetPos.y});
            newNode->runAction(CCEaseOut::create(CCMoveTo::create(dur, targetPos), 3.0f));
            newSprite->setOpacity(0);
            newSprite->runAction(CCFadeTo::create(dur * 0.5f, 255));
            // Vieja: ondulacion (pulsos de escala alternando X/Y) + fade out
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
            // Secuencial: vieja escala a 0 primero, luego nueva pop desde 0
            newNode->setScaleX(0.01f);
            newNode->setScaleY(0.01f);
            newSprite->setOpacity(0);
            // Nueva aparece despues de que la vieja desaparezca
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
            // Vieja escala a 0 con back-ease
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
            if (oldNode) oldNode->runAction(CCSequence::create(
                CCDelayTime::create(removeDelay),
                CCCallFunc::create(oldNode, callfunc_selector(CCNode::removeFromParent)), nullptr));
        } break;
        }
    }

    // Aplica transicion de entrada (primera aparicion)
    void applyEntryTransition(CCNode* clipNode, CCSprite* sprite, PaimonGalleryTransition type,
                              float dur, CCSize clipSize) {
        log::debug("[LevelCell] applyEntryTransition: type={} dur={:.2f} clipSize=({:.1f},{:.1f})", static_cast<int>(type), dur, clipSize.width, clipSize.height);
        applyGalleryTransition(clipNode, sprite, nullptr, nullptr, type, dur, clipSize);
    }

    // Marca fin de transicion de galeria
    void endGalleryTransition(float /*dt*/) {
        auto fields = m_fields.self();
        if (fields && !fields->m_isBeingDestroyed) {
            fields->m_isGalleryTransitioning = false;
        }
    }

    // Activa guard de transicion y programa su fin
    void beginGalleryTransitionGuard(float dur) {
        auto fields = m_fields.self();
        if (!fields) return;
        fields->m_isGalleryTransitioning = true;
        fields->m_galleryTransitionStart = std::chrono::steady_clock::now();

        log::debug("[LevelCell] beginGalleryTransitionGuard: dur={:.2f} centerLerp={:.2f}", dur, fields->m_centerLerp);
        // Cancela callback anterior si hay transicion previa pendiente
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

        // Fallback a rebuild completo si falta clip/sprite
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

        // No aplicar shader de saturacion aqui — updateCenterAnimation lo maneja

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

        // Añadir al hoverContainer si existe, sino a this como fallback
        if (fields->m_hoverContainer && fields->m_hoverContainer->getParent()) {
            fields->m_hoverContainer->addChild(newClip);
        } else if (fields->m_boundsClipper && fields->m_boundsClipper->getParent()) {
            fields->m_boundsClipper->addChild(newClip);
        } else {
            this->addChild(newClip);
        }

        // Fuerza tick de hover antes de transicion para evitar salto visual
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
                    texture, targetSize, blurIntensity,
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

                        // Elimina viejo al terminar
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

        // Descarga MP4 y crea VideoThumbnailSprite
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
                        fields->m_videoPlayer->stop();
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

        // Verifica cache compartida de URL primero
        if (ThumbnailLoader::get().isUrlLoaded(thumb.url)) {
            if (fields->m_galleryIndex == index) {
                log::debug("[LevelCell] requestGalleryThumbnail: shared cache hit index={} url={}", index, thumb.url);
                WeakRef<PaimonLevelCell> safeRef = this;
                ThumbnailLoader::get().requestUrlLoad(thumb.url, [safeRef, levelID, galleryToken, index](CCTexture2D* tex, bool ok) {
                    auto cellRef = safeRef.lock();
                    auto* cell = static_cast<PaimonLevelCell*>(cellRef.data());
                    if (!cell || !cell->m_level || cell->m_level->m_levelID != levelID) return;
                    auto fields = cell->m_fields.self();
                    if (!fields || fields->m_galleryToken != galleryToken) return;
                    if (ok && tex && fields->m_galleryIndex == index) cell->crossfadeToThumb(tex);
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

        // Timeout de seguridad: libera guard si se atasca >2s
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

        // Patron scan→show→prefetch con ventana acotada
        int foundIndex = -1;
        int searchWindow = std::min(gallerySize - 1, LEVELCELL_GALLERY_SEARCH_WINDOW);

        for (int i = 1; i <= searchWindow; ++i) {
            int idx = (fields->m_galleryIndex + i) % gallerySize;
            auto const& thumb = fields->m_galleryThumbnails[idx];
            if (thumb.url.empty()) continue;

            if (thumb.isVideo()) {
                // Videos se cachean por VideoThumbnailSprite, verifica su cache
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
            // Imagen lista: muestra y avanza indice
            fields->m_galleryIndex = foundIndex;
            fields->m_galleryConsecutiveMisses = 0;
            this->requestGalleryThumbnail(foundIndex, true);

            if (gallerySize > 1) {
                this->requestGalleryWindow((foundIndex + 1) % gallerySize);
            }

            // reschedule a intervalo normal (4.5s)
            this->unschedule(schedule_selector(PaimonLevelCell::updateGalleryCycle));
            this->schedule(schedule_selector(PaimonLevelCell::updateGalleryCycle), 4.5f);
        } else {
            fields->m_galleryConsecutiveMisses++;

            // despues de N misses consecutivos, parar el ciclo para no gastar requests
            if (fields->m_galleryConsecutiveMisses >= LEVELCELL_GALLERY_MAX_MISSES) {
                log::debug("[LevelCell] updateGalleryCycle: {} consecutive misses, stopping gallery cycle",
                    fields->m_galleryConsecutiveMisses);
                this->unschedule(schedule_selector(PaimonLevelCell::updateGalleryCycle));
                return;
            }

            if (gallerySize > 1) {
                this->requestGalleryWindow((fields->m_galleryIndex + 1) % gallerySize);
            }

            // Nada listo, reintenta con backoff creciente
            float delay = LEVELCELL_GALLERY_RETRY_DELAY * fields->m_galleryConsecutiveMisses;
            this->unschedule(schedule_selector(PaimonLevelCell::updateGalleryCycle));
            this->schedule(schedule_selector(PaimonLevelCell::updateGalleryCycle), delay);
        }
    }

    void addOrUpdateThumb(CCTexture2D* texture, VideoThumbnailSprite* activeVideoDriver = nullptr) {
        if (!texture) {
            log::warn("[LevelCell] addOrUpdateThumb called with null texture");
            return;
        }
        
        // Verifica parent antes de agregar hijos
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

            setVideoDriver(activeVideoDriver, 0.0f);
            
            // Limpia nodos paimon antes de agregar nuevos
            
            cleanPaimonNodes(bg);

            bg->setZOrder(-2);

            CCSprite* sprite = createThumbnailSprite(texture);
            if (!sprite) {
                log::warn("[LevelCell] Failed to create sprite from texture");
                return;
            }

            setupClippingAndSeparator(bg, sprite);
            log::debug("[LevelCell] addOrUpdateThumb: setup complete, baseScaleX={:.4f}", fields->m_thumbBaseScaleX);

            // Animacion de entrada para primera aparicion
            if (fields->m_clippingNode && fields->m_thumbSprite) {
                cacheSettings();
                auto transType = fields->m_cachedGalleryTransition;
                float dur = fields->m_cachedTransitionDuration;
                CCSize clipSize = fields->m_clippingNode->getContentSize();
                beginGalleryTransitionGuard(dur);
                applyEntryTransition(fields->m_clippingNode, fields->m_thumbSprite, transType, dur, clipSize);
            }

            if (m_level) {
                int32_t levelID = m_level->m_levelID.value();
                setupGradient(bg, levelID, texture);
                setupMythicParticles(bg, levelID);
            }

            findAndSetupViewButton();

            // Activa update() para ciclo de galeria y hover
            this->scheduleUpdate();

            // Actualiza colores del gradiente
            if (m_level && fields && !fields->m_isBeingDestroyed && fields->m_gradientLayer) {
                if (!fields->m_gradientLayer->getParent()) {
                    fields->m_gradientLayer = nullptr;
                } else {
                    int32_t levelID = m_level->m_levelID.value();
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

    bool checkMenuCollision(CCNode* node, CCPoint worldPoint, CCNode* ignoreNode) {
        if (!node || !node->isVisible()) return false;
        
        // Si es CCMenu, revisa sus items
        if (auto menu = typeinfo_cast<CCMenu*>(node)) {
            if (!menu->isEnabled()) return false;
            
            auto children = menu->getChildren();
            if (!children) return false;
            
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (!child || !child->isVisible()) continue;

                // Salta nodo ignorado (m_viewOverlay)
                if (child == ignoreNode) continue;
                
                auto item = typeinfo_cast<CCMenuItem*>(child);
                if (item && item->isEnabled()) {
                    // Verifica colision
                    CCPoint local = item->getParent()->convertToNodeSpace(worldPoint);
                    CCRect r = item->boundingBox();
                    if (r.containsPoint(local)) {
                        return true;
                    }
                }
            }
        }
        
        // Recursividad
        auto children = node->getChildren();
        if (children) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (child && checkMenuCollision(child, worldPoint, ignoreNode)) return true;
            }
        }
        return false;
    }

    bool isTouchOnMenu(CCTouch* touch) {
        auto fields = m_fields.self();
        CCPoint worldPoint = touch->getLocation();
        // Incluye m_viewOverlay en collision check para que CCMenu lo maneje
        return checkMenuCollision(this, worldPoint, nullptr);
    }

    $override void onExit() {
        log::debug("[LevelCell] onExit: levelID={}", m_level ? m_level->m_levelID.value() : 0);
        // Detiene animaciones (evita logica pesada en destructor)
        this->unscheduleUpdate();
        this->unschedule(schedule_selector(PaimonLevelCell::updateGalleryCycle));
        this->unschedule(schedule_selector(PaimonLevelCell::updateGradientAnim));
        this->unschedule(schedule_selector(PaimonLevelCell::updateCenterAnimation));

        if (auto fields = m_fields.self()) {
            fields->m_isCenterAnimScheduled = false;
            fields->m_isBeingDestroyed = true;
            fields->m_requestId++;
            // Detiene video player
            if (fields->m_videoPlayer) {
                fields->m_videoPlayer->stop();
                fields->m_videoPlayer.reset();
                fields->m_hasVideo = false;
            }
            if (fields->m_videoDriver) {
                if (fields->m_videoDriver->getParent()) {
                    fields->m_videoDriver->removeFromParent();
                }
                fields->m_videoDriver = nullptr;
            }
            // Solo invalida flags si el sprite no sigue en el arbol
            bool spriteStillAttached = fields->m_thumbSprite && fields->m_thumbSprite->getParent();
            if (!spriteStillAttached) {
                fields->m_thumbnailRequested = false;
                fields->m_thumbnailApplied = false;
            }
            fields->m_staticThumbLoad.reset();
            // Mantiene datos de galeria, invalida callbacks en vuelo
            fields->m_galleryToken++;
            fields->m_galleryPendingUrls.clear();
            // Cancela stagger pendiente
            this->unschedule(schedule_selector(PaimonLevelCell::applyPendingStaggerThumb));
            fields->m_pendingStaggerTexture = nullptr;
            if (fields->m_invalidationListenerId != 0) {
                ThumbnailLoader::get().removeInvalidationListener(fields->m_invalidationListenerId);
                fields->m_invalidationListenerId = 0;
            }
        }

        if (m_level) {
            ThumbnailLoader::get().cancelLoad(m_level->m_levelID.value());
            ThumbnailLoader::get().cancelLoad(m_level->m_levelID.value(), true);
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
        // Usa parent disponible para stacking mayor
        CCNode* flashParent = this;
        if (m_mainMenu) flashParent = m_mainMenu;
        else if (m_button && m_button->getParent()) flashParent = m_button->getParent();

        auto flash = CCLayerColor::create(ccc4(255,255,255,0));
        if (!flash) return;
        flash->setContentSize(cellSize);
        flash->ignoreAnchorPointForPosition(false);
        flash->setAnchorPoint({0.5f,0.5f});
        
        // Calcula posicion en espacio de flashParent para centrar correctamente
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

    // Flash al hacer click
    $override void onClick(CCObject* sender) {
        playTapFlashAnimation();
        LevelCell::onClick(sender);
    }

    // Aclara color
    static inline ccColor3B brightenColor(ccColor3B const& c, int add) {
        auto clamp = [](int v){ return std::max(0, std::min(255, v)); };
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

            // Dynamic GIF gradient support
            if (fields->m_thumbSprite) {
                // GifSprite does not support getCurrentFrameColors yet
            }

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
        fields->m_cachedAnimType = parseAnimType(Mod::get()->getSettingValue<std::string>("levelcell-anim-type"));
        fields->m_cachedAnimSpeed = static_cast<float>(Mod::get()->getSettingValue<double>("levelcell-anim-speed"));
        fields->m_cachedAnimEffect = parseAnimEffect(Mod::get()->getSettingValue<std::string>("levelcell-anim-effect"));
        fields->m_cachedHoverEnabled = Mod::get()->getSettingValue<bool>("levelcell-hover-effects");
        fields->m_cachedCompactMode = Mod::get()->getSettingValue<bool>("compact-list-mode");
        fields->m_cachedTransparentMode = Mod::get()->getSettingValue<bool>("transparent-list-mode");
        fields->m_cachedEffectOnGradient = Mod::get()->getSettingValue<bool>("levelcell-effect-on-gradient");
        fields->m_cachedBgType = parseBgType(Mod::get()->getSettingValue<std::string>("levelcell-background-type"));
        fields->m_cachedGalleryTransition = parseGalleryTransition(Mod::get()->getSettingValue<std::string>("levelcell-gallery-transition"));
        fields->m_cachedTransitionDuration = std::clamp(static_cast<float>(Mod::get()->getSettingValue<double>("levelcell-gallery-transition-duration")), 0.2f, 2.0f);
        fields->m_cachedShowSeparator = Mod::get()->getSettingValue<bool>("levelcell-show-separator");
        fields->m_cachedShowViewButton = Mod::get()->getSettingValue<bool>("levelcell-show-view-button");
        fields->m_cachedAnimatedGradient = Mod::get()->getSettingValue<bool>("levelcell-animated-gradient");
        fields->m_cachedMythicParticles = Mod::get()->getSettingValue<bool>("levelcell-mythic-particles");
        fields->m_cachedGalleryAutocycle = Mod::get()->getSettingValue<bool>("levelcell-gallery-autocycle");
        fields->m_cachedThumbWidthFactor = std::clamp(
            static_cast<float>(Mod::get()->getSettingValue<double>("level-thumb-width")),
            PaimonConstants::MIN_THUMB_WIDTH_FACTOR, PaimonConstants::MAX_THUMB_WIDTH_FACTOR);
        fields->m_cachedBackgroundBlur = static_cast<float>(Mod::get()->getSettingValue<double>("levelcell-background-blur"));
        fields->m_cachedBackgroundDarkness = static_cast<float>(Mod::get()->getSettingValue<double>("levelcell-background-darkness"));
    }

    // Inline hover detection — runs every frame inside updateCenterAnimation
    void updateCenterDetection(Fields* fields) {
        if (!fields->m_cachedHoverEnabled) {
            fields->m_wasInCenter = false;
            return;
        }
        if (!this->getParent()) {
            fields->m_wasInCenter = false;
            return;
        }
        auto winSize = CCDirector::sharedDirector()->getWinSize();
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
            // Durante transicion de galeria: saltar TODOS los calculos de hover
            // para evitar conflictos con las CCActions de la transicion.
            // El hover se retomara cuando endGalleryTransition() limpie el flag.
            return;
        }

        // Asegurar que el hoverContainer no tenga hover scale/offset residual al salir de transicion
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
        cacheSettings();

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
            PaimonShaderSprite* pss = typeinfo_cast<PaimonShaderSprite*>(target);
            PaimonShaderGradient* psg = typeinfo_cast<PaimonShaderGradient*>(target);
            AnimatedGIFSprite* ags = typeinfo_cast<AnimatedGIFSprite*>(target);

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
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_sepia", vertexShaderCell, fragmentShaderSaturationCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp);
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::Sharpen:
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_sharpen", vertexShaderCell, fragmentShaderSharpenCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp); setTexSize();
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::EdgeDetection:
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_edge", vertexShaderCell, fragmentShaderEdgeCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp); setTexSize();
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::Vignette:
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_vignette", vertexShaderCell, fragmentShaderVignetteCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp);
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::Pixelate:
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_pixelate", vertexShaderCell, fragmentShaderPixelateCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp); setTexSize();
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::Posterize:
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_posterize", vertexShaderCell, fragmentShaderPosterizeCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp);
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::Chromatic:
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_chromatic", vertexShaderCell, fragmentShaderChromaticCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp);
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::Scanlines:
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_scanlines", vertexShaderCell, fragmentShaderScanlinesCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp); setTexSize();
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::Solarize:
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_solarize", vertexShaderCell, fragmentShaderSolarizeCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp);
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::Rainbow:
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_rainbow", vertexShaderCell, fragmentShaderRainbowCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp); setTime(animT);
                }
                target->setColor({255, 255, 255});
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
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_grayscale", vertexShaderCell, fragmentShaderGrayscaleCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp);
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::Invert:
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_invert", vertexShaderCell, fragmentShaderInvertCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp);
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::Blur:
                usingShader = true;
                if (auto sh = Shaders::getBlurCellShader()) {
                    target->setShaderProgram(sh); setIntensity(lerp); setTexSize();
                }
                target->setColor({255, 255, 255});
                break;
            case PaimonAnimEffect::Glitch:
                usingShader = true;
                if (auto sh = getOrCreateShader("paimon_cell_glitch", vertexShaderCell, fragmentShaderGlitchCell)) {
                    target->setShaderProgram(sh); setIntensity(lerp); setTime(animT);
                }
                target->setColor({255, 255, 255});
                break;
            default:
                if (!psg) { target->setColor({255, 255, 255}); target->setOpacity(255); }
                break;
            }

            // Resetear opacidad para efectos que no la manejan (Fade la controla explicitamente)
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

    // animateToCenter/animateFromCenter removed Ã¢â‚¬â€ deprecated (now uses lerp system)

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
    
    // fixDailyCell removed Ã¢â‚¬â€ was empty (logic moved to DailyLevelNode)

    // Removed onPaimonDailyPlay as per user request to remove animation
    
    // Removed onLevelInfo hook as it's not available in binding
    
    void tryLoadThumbnail() {
            configureThumbnailLoader();

            if (!m_level) {
                log::warn("[LevelCell] tryLoadThumbnail: m_level is null, aborting");
                return;
            }

            int dailyID = m_level->m_dailyID.value();
            bool isDaily = dailyID > 0;
            if (isDaily) {
                log::debug("[LevelCell] tryLoadThumbnail: skipping daily cell dailyID={}", dailyID);
                return;
            }

            int32_t levelID = m_level->m_levelID.value();
            if (levelID <= 0) {
                log::debug("[LevelCell] tryLoadThumbnail: invalid levelID={}", levelID);
                return;
            }

            // Visibility check: if cell is offscreen, use low priority instead of
            // PriorityVisibleCell. This prevents flooding the queue when returning
            // from LevelInfoLayer where ALL cells call tryLoadThumbnail at once.
            // Only visible cells get high priority for immediate loading.
            auto winSize = cocos2d::CCDirector::sharedDirector()->getWinSize();
            CCPoint worldPos = this->convertToWorldSpace(CCPointZero);
            float cellH = this->getContentSize().height;
            bool isOnScreen = (worldPos.y + cellH > 0.f && worldPos.y < winSize.height &&
                               worldPos.x + this->getContentSize().width > 0.f && worldPos.x < winSize.width);

            log::debug("[LevelCell] tryLoadThumbnail: levelID={} onScreen={}", levelID, isOnScreen);
            
            auto fields = m_fields.self();
            // Reset destroyed flag — cell may be re-entering the scene after onExit()
            fields->m_isBeingDestroyed = false;
            if (fields->m_thumbnailApplied && (!fields->m_thumbSprite || !fields->m_thumbSprite->getParent())) {
                fields->m_thumbnailApplied = false;
                fields->m_thumbnailRequested = false;
            }
            if (fields->m_invalidationListenerId == 0) {
                WeakRef<PaimonLevelCell> safeRef = this;
                fields->m_invalidationListenerId = ThumbnailLoader::get().addInvalidationListener([safeRef](int invalidLevelID) {
                    auto selfRef = safeRef.lock();
                    auto* self = static_cast<PaimonLevelCell*>(selfRef.data());
                    if (!self || !self->getParent() || !self->m_level) return;
                    if (self->m_level->m_levelID.value() != invalidLevelID) return;
                    auto fields = self->m_fields.self();
                    if (!fields) return;
                    fields->m_thumbnailRequested = false;
                    fields->m_thumbnailApplied = false;
                    fields->m_galleryRequested = false;
                    fields->m_galleryThumbnails.clear();
                    fields->m_galleryPendingUrls.clear();
                    fields->m_galleryIndex = -1;
                    fields->m_galleryTimer = 0.f;
                    fields->m_galleryToken++;
                    fields->m_loadedInvalidationVersion = ThumbnailLoader::get().getInvalidationVersion(invalidLevelID);
                    self->tryLoadThumbnail();
                });
            }
            
            // comprobar si el level cambio
            if (fields->m_lastRequestedLevelID != levelID) {
                log::debug("[LevelCell] tryLoadThumbnail: level changed {} -> {}, resetting state", fields->m_lastRequestedLevelID, levelID);
                fields->m_thumbnailRequested = false;
                fields->m_thumbnailApplied = false;
                fields->m_lastRequestedLevelID = levelID;
                fields->m_hasGif = false;
                fields->m_staticTexture = nullptr;
                fields->m_staticThumbLoad.reset();
                fields->m_loadedInvalidationVersion = 0;
                fields->m_isDailyCellCached = false;
                fields->m_galleryThumbnails.clear();
                fields->m_galleryPendingUrls.clear();
                fields->m_galleryIndex = 0;
                fields->m_galleryTimer = 0.f;
                fields->m_galleryRequested = false;
                fields->m_galleryToken++;
                // Reset video player on level change
                if (fields->m_videoPlayer) {
                    fields->m_videoPlayer->stop();
                    fields->m_videoPlayer.reset();
                    fields->m_hasVideo = false;
                }
                if (fields->m_videoDriver) {
                    if (fields->m_videoDriver->getParent()) {
                        fields->m_videoDriver->removeFromParent();
                    }
                    fields->m_videoDriver = nullptr;
                }
            }

            // comprobar si la miniatura fue invalidada (usuario subiÃƒÂ³ una nueva)
            int currentVersion = ThumbnailLoader::get().getInvalidationVersion(levelID);
            if (fields->m_thumbnailApplied && currentVersion != fields->m_loadedInvalidationVersion) {
                log::debug("[LevelCell] tryLoadThumbnail: thumbnail invalidated for levelID={}, ver {} -> {}", levelID, fields->m_loadedInvalidationVersion, currentVersion);
                fields->m_thumbnailRequested = false;
                fields->m_thumbnailApplied = false;
                fields->m_hasGif = false;
                fields->m_staticTexture = nullptr;
                fields->m_staticThumbLoad.reset();
            }
            fields->m_loadedInvalidationVersion = currentVersion;

            // If thumbnail is already applied and visible, skip re-requesting
            if (fields->m_thumbnailApplied && fields->m_thumbSprite &&
                fields->m_thumbSprite->getParent()) {
                log::debug("[LevelCell] tryLoadThumbnail: thumbnail still applied for levelID={}, skipping re-request", levelID);
                fields->m_thumbnailRequested = true;
                this->scheduleUpdate();
                // Re-start gallery cycling if data is still available
                if (fields->m_galleryRequested && fields->m_galleryThumbnails.size() > 1) {
                    bool autoCycle = fields->m_cachedGalleryAutocycle;
                    this->requestGalleryWindow(fields->m_galleryIndex);
                    if (autoCycle) {
                        this->unschedule(schedule_selector(PaimonLevelCell::updateGalleryCycle));
                        this->schedule(schedule_selector(PaimonLevelCell::updateGalleryCycle), 4.5f);
                    }
                }
                return;
            }

            if (!fields->m_galleryRequested) {
                fields->m_galleryRequested = true;
                int galleryToken = ++fields->m_galleryToken;
                log::debug("[LevelCell] tryLoadThumbnail: requesting gallery for levelID={} token={}", levelID, galleryToken);
                WeakRef<PaimonLevelCell> safeGalleryRef = this;
                ThumbnailAPI::get().getThumbnails(levelID, [safeGalleryRef, levelID, galleryToken](bool success, std::vector<ThumbnailAPI::ThumbnailInfo> const& thumbs) {
                    auto cellRef = safeGalleryRef.lock();
                    auto* cell = static_cast<PaimonLevelCell*>(cellRef.data());
                    if (!cell || !cell->m_level || cell->m_level->m_levelID != levelID) return;
                    auto fields = cell->m_fields.self();
                    if (!fields || fields->m_galleryToken != galleryToken) return;
                    fields->m_galleryPendingUrls.clear();
                    fields->m_galleryThumbnails = normalizeLevelCellGalleryThumbnails(
                        levelID,
                        success ? thumbs : std::vector<ThumbnailAPI::ThumbnailInfo>{}
                    );
                    fields->m_galleryIndex = 0;
                    fields->m_galleryTimer = 0.f;
                    if (!fields->m_galleryThumbnails.empty()) {
                        cell->requestGalleryWindow(0);
                    }
                    bool autoCycleEnabled = fields->m_cachedGalleryAutocycle;
                    cell->unschedule(schedule_selector(PaimonLevelCell::updateGalleryCycle));
                    if (autoCycleEnabled && fields->m_galleryThumbnails.size() > 1) {
                        // Start at 4.5s — the cycle will use a bounded retry window if the next image is not ready yet.
                        cell->schedule(schedule_selector(PaimonLevelCell::updateGalleryCycle), 4.5f);
                    }
                });
            }

            if (fields->m_thumbnailRequested) {
                log::debug("[LevelCell] tryLoadThumbnail: already requested for levelID={}", levelID);
                return;
            }
            
            fields->m_requestId++;
            int currentRequestId = fields->m_requestId;
            fields->m_thumbnailRequested = true;
            fields->m_lastRequestedLevelID = levelID;
            fields->m_hasGif = ThumbnailLoader::get().hasGIFData(levelID);

            bool enableSpinners = true;

            // 0) Check for local .mp4 video thumbnail first
            {
                auto localPath = LocalThumbs::get().findAnyThumbnail(levelID);
                if (localPath) {
                    auto lowerPath = geode::utils::string::toLower(*localPath);
                    if (lowerPath.ends_with(".mp4")) {
                        log::debug("[LevelCell] tryLoadThumbnail: found MP4 for levelID={}: {}", levelID, *localPath);
                        auto player = paimon::video::VideoPlayer::create(*localPath);
                        if (player) {
                            fields->m_hasVideo = true;
                            fields->m_videoPlayer = std::move(player);
                            fields->m_videoPlayer->setLoop(true);
                            fields->m_videoPlayer->setVolume(0.0f); // muted until hover/focus
                            fields->m_videoPlayer->play();
                            this->scheduleUpdate();

                            // Wait for a real decoded frame instead of swapping in the
                            // black prewarm texture immediately.
                            if (fields->m_videoPlayer->hasVisibleFrame()) {
                                auto* videoTex = fields->m_videoPlayer->getCurrentFrameTexture();
                                fields->m_thumbnailApplied = true;
                                if (enableSpinners) hideLoadingSpinner();
                                addOrUpdateThumb(videoTex);
                                log::debug("[LevelCell] tryLoadThumbnail: video player started for levelID={}", levelID);
                                return;
                            }
                            log::debug("[LevelCell] tryLoadThumbnail: waiting for first MP4 frame for levelID={}", levelID);
                            return;
                        }
                        log::warn("[LevelCell] tryLoadThumbnail: MP4 player creation failed for levelID={}", levelID);
                    }
                }
            }
            
            // 0.5) Check if gallery data indicates the main thumbnail is a video from server
            if (!fields->m_galleryThumbnails.empty()) {
                auto const& mainThumb = fields->m_galleryThumbnails[0];
                if (mainThumb.isVideo() && !mainThumb.url.empty()) {
                    log::debug("[LevelCell] tryLoadThumbnail: main thumb is server video for levelID={}", levelID);
                    if (enableSpinners) showLoadingSpinner();
                    std::string cacheKey = fmt::format("thumb_video_{}", levelID);
                    WeakRef<PaimonLevelCell> safeRef = this;
                    int currentReqId = currentRequestId;
                    VideoThumbnailSprite::createAsync(mainThumb.url, cacheKey, [safeRef, levelID, currentReqId, enableSpinners](VideoThumbnailSprite* videoSprite) {
                        auto cellRef = safeRef.lock();
                        auto* cell = static_cast<PaimonLevelCell*>(cellRef.data());
                        if (!cell || !cell->shouldHandleThumbnailCallback(levelID, currentReqId)) return;
                        auto fields = cell->m_fields.self();
                        if (!fields) return;

                        if (!videoSprite) {
                            log::warn("[LevelCell] tryLoadThumbnail: server video creation failed for levelID={}", levelID);
                            // Fall through to GIF/static via retry
                            fields->m_thumbnailRequested = false;
                            cell->tryLoadThumbnail();
                            return;
                        }

                        videoSprite->setVolume(0.0f);
                        videoSprite->setLoop(true);
                        videoSprite->setVisible(false);
                    videoSprite->setID("video-driver-pending"_spr);
                        cell->addChild(videoSprite, -1000);
                        videoSprite->setOnFirstVisibleFrame([safeRef, levelID, currentReqId, enableSpinners](VideoThumbnailSprite* readySprite) {
                            auto cellRefInner = safeRef.lock();
                            auto* currentCell = static_cast<PaimonLevelCell*>(cellRefInner.data());
                            if (!currentCell || !currentCell->shouldHandleThumbnailCallback(levelID, currentReqId)) {
                                if (readySprite->getParent()) {
                                    readySprite->removeFromParent();
                                }
                                return;
                            }

                            auto currentFields = currentCell->m_fields.self();
                            if (!currentFields) {
                                if (readySprite->getParent()) {
                                    readySprite->removeFromParent();
                                }
                                return;
                            }

                            if (auto* tex = readySprite->getTexture()) {
                                currentFields->m_hasVideo = true;
                                currentFields->m_thumbnailApplied = true;
                                if (enableSpinners) {
                                    currentCell->hideLoadingSpinner();
                                }
                                currentCell->addOrUpdateThumb(tex, readySprite);

                                if (currentFields->m_galleryThumbnails.size() > 1) {
                                    currentFields->m_galleryIndex = 0;
                                    bool autoCycle = currentFields->m_cachedGalleryAutocycle;
                                    if (autoCycle) {
                                        currentCell->unschedule(schedule_selector(PaimonLevelCell::updateGalleryCycle));
                                        currentCell->schedule(schedule_selector(PaimonLevelCell::updateGalleryCycle), 4.5f);
                                    }
                                }

                                log::debug("[LevelCell] tryLoadThumbnail: server video first frame ready for levelID={}", levelID);
                            }
                        });
                        videoSprite->play();
                    });
                    return;
                }
            }

            std::string fileName = fmt::format("{}.png", levelID);
            
            if (enableSpinners) showLoadingSpinner();
            
            log::debug("[LevelCell] tryLoadThumbnail: requesting load levelID={} requestId={}", levelID, currentRequestId);
            WeakRef<PaimonLevelCell> safeRef = this;
            int capturedVersion = fields->m_loadedInvalidationVersion;

            // Request unificado: el servidor retorna el formato correcto (GIF/WebP/PNG)
            // automaticamente via /t/{levelId}. El decodificador detecta el formato
            // por magic bytes, asi que no necesitamos bifurcar GIF vs estatico aqui.
            // Esto elimina el doble request serial GIF→Static que duplicaba latencia.
            ThumbnailLoader::get().requestLoad(levelID, fileName, [safeRef, levelID, enableSpinners, currentRequestId, capturedVersion](CCTexture2D* tex, bool success) {
                auto cellRef = safeRef.lock();
                auto* cell = static_cast<PaimonLevelCell*>(cellRef.data());
                if (!cell || !cell->shouldHandleThumbnailCallback(levelID, currentRequestId)) return;
                {
                    auto f = cell->m_fields.self();
                    if (f && f->m_loadedInvalidationVersion != capturedVersion) return;
                }
                if (!success || !tex) {
                    log::warn("[LevelCell] tryLoadThumbnail: load FAILED levelID={}", levelID);
                    cell->applyStaticThumbnailTexture(levelID, currentRequestId, nullptr, enableSpinners);
                    return;
                }
                log::debug("[LevelCell] tryLoadThumbnail: texture loaded OK levelID={}", levelID);
                auto fields = cell->m_fields.self();
                if (fields) fields->m_hasGif = ThumbnailLoader::get().hasGIFData(levelID);
                cell->applyStaticThumbnailTexture(levelID, currentRequestId, tex, enableSpinners);
            }, isOnScreen ? ThumbnailLoader::PriorityVisibleCell : ThumbnailLoader::PriorityPredictivePrefetch, false);
    }

    $override void update(float dt) {
        LevelCell::update(dt);

        auto fields = m_fields.self();
        if (!fields) return;

        // Settings check: instant — no throttle. When user toggles a setting
        // in the popup, it should apply immediately without waiting.
        {
            int globalSettingsVer = LevelCellSettingsPopup::s_settingsVersion;
            if (globalSettingsVer != fields->m_loadedSettingsVersion) {
                fields->m_loadedSettingsVersion = globalSettingsVer;
                // invalidar cache de settings pa que se re-lean
                fields->m_settingsCached = false;

                // Check if compact mode changed — requires full cell reload + list relayout
                bool newCompact = Mod::get()->getSettingValue<bool>("compact-list-mode");
                // Don't apply compact mode to timed levels (daily/weekly/event) or editor levels
                if (m_level) {
                    if (m_level->m_dailyID > 0) newCompact = false;
                    if (m_level->m_levelType == GJLevelType::Editor) newCompact = false;
                }
                if (newCompact != fields->m_cachedCompactMode) {
                    m_compactView = newCompact;
                    fields->m_cachedCompactMode = newCompact;
                    // Reload cell content to apply native compact layout
                    if (m_level) {
                        if (m_cellMode == 0) {
                            loadFromLevel(m_level);
                        } else {
                            loadCustomLevelCell();
                        }
                    }

                    // Relayout the list so cell heights update.
                    // Find BoomListView by walking up the parent chain.
                    // setupList() only repositions cells — it does NOT destroy/recreate
                    // them like reloadAll()/reloadData() which crash when called
                    // during active UI interaction.
                    CCNode* parent = this->getParent();
                    for (int depth = 0; parent && depth < 15; ++depth, parent = parent->getParent()) {
                        if (auto* listView = typeinfo_cast<BoomListView*>(parent)) {
                            listView->setupList(0.f);
                            break;
                        }
                    }
                }

                // forzar re-aplicar la miniatura con los nuevos settings
                if (fields->m_thumbnailApplied && m_level) {
                    fields->m_thumbnailRequested = false;
                    fields->m_thumbnailApplied = false;
                    tryLoadThumbnail();
                }
            }
        }

        // Throttle: invalidation checks cada ~0.5s (mutex acquires costosas)
        fields->m_updateCheckTimer += dt;
        if (fields->m_updateCheckTimer >= 0.5f) {
            fields->m_updateCheckTimer = 0.f;

            // Priority promotion: if cell is now visible but thumbnail was requested
            // at low priority (offscreen), promote to PriorityVisibleCell for faster loading.
            // This handles the case where a cell scrolls into view before its low-priority
            // request was processed.
            if (fields->m_thumbnailRequested && !fields->m_thumbnailApplied && m_level) {
                int32_t levelID = m_level->m_levelID.value();
                if (levelID > 0) {
                    auto winSz = cocos2d::CCDirector::sharedDirector()->getWinSize();
                    CCPoint wp = this->convertToWorldSpace(CCPointZero);
                    float ch = this->getContentSize().height;
                    bool nowOnScreen = (wp.y + ch > 0.f && wp.y < winSz.height &&
                                        wp.x + this->getContentSize().width > 0.f && wp.x < winSz.width);
                    if (nowOnScreen) {
                        ThumbnailLoader::get().requestLoad(levelID, "", nullptr,
                            ThumbnailLoader::PriorityVisibleCell, false);
                    }
                }
            }

            // comprobar si la miniatura fue invalidada mientras la celda esta visible
            if (fields->m_thumbnailApplied && m_level) {
                int32_t levelID = m_level->m_levelID.value();
                if (levelID > 0) {

                    // Re-apply transparent mode (GD may reset bg color when recycling cells)
                    applyTransparentMode();
                    int currentVersion = ThumbnailLoader::get().getInvalidationVersion(levelID);
                    if (currentVersion != fields->m_loadedInvalidationVersion) {
                        // miniatura actualizada, recargar
                        tryLoadThumbnail();
                    }
                }
            }
        }

        // gallery cycling is handled by updateGalleryCycle scheduled method

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

        if (fields->m_hasGif && fields->m_thumbSprite) {
            auto* animatedThumb = typeinfo_cast<AnimatedGIFSprite*>(fields->m_thumbSprite.data());
            if (!animatedThumb) {
                return;
            }
            // Siempre animar el GIF (en todas las plataformas)
            if (!animatedThumb->isPlaying()) {
                animatedThumb->play();
            }
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

    // Transparent Lists-inspired: make cell backgrounds transparent.
    // Changes the brown cell background to transparent black, and adjusts
    // the GJListLayer background to be transparent as well.
    void applyTransparentMode() {
        auto fields = m_fields.self();
        if (!fields) return;
        cacheSettings();
        if (!fields->m_cachedTransparentMode) return;

        // Make the cell's own background layer transparent
        if (auto bg = m_backgroundLayer) {
            auto bgColor = bg->getColor();
            if (fields->m_lastBgColor != bgColor) {
                fields->m_lastBgColor = bgColor;
                // GD default cell colors: {161,88,44} (even) or {194,114,62} (odd)
                // Replace with transparent black
                bg->setColor({0, 0, 0});
                bg->setOpacity(50);
            }
        }
    }

    void applyCompactViewFromSetting() {
        bool compact = Mod::get()->getSettingValue<bool>("compact-list-mode");
        // Don't apply compact mode to timed levels (daily/weekly/event) or editor levels
        if (m_level) {
            if (m_level->m_dailyID > 0) compact = false;
            if (m_level->m_levelType == GJLevelType::Editor) compact = false;
        }
        m_compactView = compact;
    }

    $override void loadCustomLevelCell() {
        applyCompactViewFromSetting();
        LevelCell::loadCustomLevelCell();
        if (auto fields = m_fields.self()) {
            fields->m_isBeingDestroyed = false;
            fields->m_cachedCompactMode = m_compactView;
        }
        applyCompactLayoutAdjustments();
        applyTransparentMode();
        log::debug("[LevelCell] loadCustomLevelCell levelID={} compact={}", m_level ? m_level->m_levelID.value() : 0, m_compactView);
        tryLoadThumbnail();
    }

    $override void loadFromLevel(GJGameLevel* level) {
        applyCompactViewFromSetting();
        LevelCell::loadFromLevel(level);
        if (auto fields = m_fields.self()) {
            fields->m_isBeingDestroyed = false;
            fields->m_cachedCompactMode = m_compactView;
        }
        applyTransparentMode();
        log::debug("[LevelCell] loadFromLevel levelID={} compact={}", level ? level->m_levelID.value() : 0, m_compactView);
        tryLoadThumbnail();
    }

};
