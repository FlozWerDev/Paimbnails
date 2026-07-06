#include "LayerBackgroundManager.hpp"
#include "../../thumbnails/services/LocalThumbs.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include "../../../utils/PaimonShaderSprite.hpp"
#include "../../../managers/ThumbnailAPI.hpp"
#include "../../../video/VideoPlayer.hpp"
#include "../../../video/VideoDiskCache.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/Settings.hpp"
#include "../../dynamic-songs/services/DynamicSongManager.hpp"
#include "../../../utils/AudioInterop.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "LayerBackgroundManager.hpp"
#include <random>
#include <filesystem>
#include <fstream>
#include <thread>
#include <atomic>
#include <unordered_map>

#include "../../../utils/ThreadTracker.hpp"
#include "../../../utils/Shaders.hpp"
#include "../../../utils/GLSLLoader.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace Shaders;

namespace {

std::atomic<uint32_t> g_layerBgSaveGeneration{0};
std::atomic<bool> g_layerBgShutdown{false};

// RAM cache for decoded custom-background textures. Every menu transition
// calls loadTextureForConfig() on the main thread, and the "custom" branch
// used to stb-decode the full image each time (30-150ms hitch for large PNGs)
// and create a separate texture per stacked layer. Keyed by path + mtime +
// size so edits to the file invalidate the entry. Heap-allocated and never
// destroyed: releasing GL textures during static destruction (GL context
// already gone) is UB — same rationale as PaiblurNode's shutdown guard.
struct CustomBgCacheEntry {
    geode::Ref<CCTexture2D> texture;
    std::filesystem::file_time_type mtime{};
    uintmax_t fileSize = 0;
    uint64_t lastUse = 0;
};

// Main-thread only (loadTextureForConfig runs on the main thread).
std::unordered_map<std::string, CustomBgCacheEntry>& customBgCache() {
    static auto* s_cache = new std::unordered_map<std::string, CustomBgCacheEntry>();
    return *s_cache;
}

uint64_t& customBgCacheUseCounter() {
    static uint64_t s_counter = 0;
    return s_counter;
}

CCTexture2D* customBgCacheGet(std::filesystem::path const& path) {
    auto& cache = customBgCache();
    auto it = cache.find(geode::utils::string::pathToString(path));
    if (it == cache.end()) return nullptr;

    std::error_code ec;
    auto mtime = std::filesystem::last_write_time(path, ec);
    if (ec) return nullptr;
    auto size = std::filesystem::file_size(path, ec);
    if (ec) return nullptr;

    if (it->second.mtime != mtime || it->second.fileSize != size) {
        cache.erase(it);
        return nullptr;
    }
    it->second.lastUse = ++customBgCacheUseCounter();
    return it->second.texture.data();
}

void customBgCachePut(std::filesystem::path const& path, CCTexture2D* tex) {
    if (!tex) return;
    std::error_code ec;
    auto mtime = std::filesystem::last_write_time(path, ec);
    if (ec) return;
    auto size = std::filesystem::file_size(path, ec);
    if (ec) return;

    auto& cache = customBgCache();
    // Small cap: different layers can use different custom backgrounds, but a
    // handful covers real usage; evict least-recently-used beyond that.
    constexpr size_t kMaxEntries = 4;
    while (cache.size() >= kMaxEntries) {
        auto oldest = cache.begin();
        for (auto it = cache.begin(); it != cache.end(); ++it) {
            if (it->second.lastUse < oldest->second.lastUse) oldest = it;
        }
        cache.erase(oldest);
    }

    CustomBgCacheEntry entry;
    entry.texture = tex;
    entry.mtime = mtime;
    entry.fileSize = size;
    entry.lastUse = ++customBgCacheUseCounter();
    cache.emplace(geode::utils::string::pathToString(path), std::move(entry));
}

// Fullscreen black overlay (alpha = darkIntensity*200) above content, zOrder 1.
// Shared by the image / gif / procedural / video paths.
void addLayerBgDarkOverlay(CCNode* container, CCSize const& winSize, bool darkMode, float darkIntensity) {
    if (!darkMode || !container) return;
    GLubyte alpha = static_cast<GLubyte>(darkIntensity * 200.f);
    auto overlay = CCLayerColor::create({0, 0, 0, alpha});
    overlay->setContentSize(winSize);
    overlay->setZOrder(1);
    container->addChild(overlay);
}

bool tintVanillaBackgroundNode(CCNode* node) {
    if (!node) return false;

    if (auto* sprite = typeinfo_cast<CCSprite*>(node)) {
        sprite->setColor({255, 255, 255});
        return true;
    }

    if (auto* colorLayer = typeinfo_cast<CCLayerColor*>(node)) {
        colorLayer->setColor({255, 255, 255});
        return true;
    }

    return false;
}

CCTexture2D* createProceduralBaseTexture() {
    // Intentionally heap-allocated to avoid atexit destructor crash
    static auto* s_texture = new Ref<CCTexture2D>();
    if (*s_texture) {
        return s_texture->data();
    }

    unsigned char whitePixel[4] = {255, 255, 255, 255};
    auto* tex = new CCTexture2D();
    if (!tex->initWithData(whitePixel, kCCTexture2DPixelFormat_RGBA8888, 1, 1, CCSizeMake(1.f, 1.f))) {
        // CCObject subclass: use release() (not delete) to respect refcount.
        tex->release();
        return nullptr;
    }
    tex->autorelease();
    *s_texture = tex;
    return s_texture->data();
}

// Tracks whether a container node is still alive for async video callbacks.
// When clearAppliedBackground removes a container, it sets the flag to false
// so the pending async callback knows not to use the dangling pointer.
std::unordered_map<cocos2d::CCNode*, std::shared_ptr<std::atomic<bool>>> g_containerAliveFlags;
std::mutex g_containerAliveMutex;

void clearContainerAliveFlag(
    cocos2d::CCNode* node,
    std::shared_ptr<std::atomic<bool>> const& expectedAlive = nullptr,
    bool markDead = false
) {
    if (!node) return;
    std::lock_guard lk(g_containerAliveMutex);
    auto it = g_containerAliveFlags.find(node);
    if (it == g_containerAliveFlags.end()) return;
    if (expectedAlive && it->second != expectedAlive) return;
    if (markDead) {
        it->second->store(false, std::memory_order_release);
    }
    g_containerAliveFlags.erase(it);
}

std::shared_ptr<std::atomic<bool>> registerContainerAliveFlag(cocos2d::CCNode* node) {
    if (!node) return nullptr;
    auto alive = std::make_shared<std::atomic<bool>>(true);
    std::lock_guard lk(g_containerAliveMutex);
    if (auto it = g_containerAliveFlags.find(node); it != g_containerAliveFlags.end()) {
        it->second->store(false, std::memory_order_release);
    }
    g_containerAliveFlags[node] = alive;
    return alive;
}

void unregisterContainerAliveFlag(
    cocos2d::CCNode* node,
    std::shared_ptr<std::atomic<bool>> const& expectedAlive = nullptr
) {
    clearContainerAliveFlag(node, expectedAlive, false);
}

// Concurrent video backgrounds are supported; the single-video hard limit was replaced
// by resource budget enforcement (RAM cap, max concurrent videos, adaptive FPS).

std::mutex& parkedSharedVideoMutex() {
    static auto* mutex = new std::mutex();
    return *mutex;
}

std::vector<std::shared_ptr<paimon::video::VideoPlayer>>& parkedSharedVideos() {
    static auto* vec = new std::vector<std::shared_ptr<paimon::video::VideoPlayer>>();
    return *vec;
}

void parkSharedVideoForShutdown(std::shared_ptr<paimon::video::VideoPlayer> player) {
    if (!player) return;
    std::lock_guard lk(parkedSharedVideoMutex());
    parkedSharedVideos().push_back(std::move(player));
}

std::vector<std::shared_ptr<paimon::video::VideoPlayer>> takeParkedSharedVideos() {
    std::lock_guard lk(parkedSharedVideoMutex());
    auto& parked = parkedSharedVideos();
    auto out = std::move(parked);
    parked.clear();
    return out;
}

void scheduleLayerBgSave() {
    auto generation = ++g_layerBgSaveGeneration;
    paimon::scheduleMainThreadDelay(0.2f, [generation]() {
        if (generation != g_layerBgSaveGeneration.load(std::memory_order_acquire)) {
            return;
        }

        if (auto result = Mod::get()->saveData(); result.isErr()) {
            log::warn("[LayerBgMgr] Failed to persist background settings: {}", result.unwrapErr());
        }
    });
}

struct VideoBackgroundUpdateNode : public CCNode {
    std::unique_ptr<paimon::video::VideoPlayer> player;
    std::shared_ptr<paimon::video::VideoPlayer> sharedPlayer; // for "Same As" reuse
    Ref<CCSprite> m_visibleSprite = nullptr;
    bool m_didSuspendDynSong = false;
    bool m_ownsVideoAudioFlag = false;
    bool m_suppressResume = false;
    bool m_shutdown = false;
    bool m_firstVisibleFrameShown = false;
    bool m_previewSaved = false;
    bool m_audioFadeOutPending = false;
    std::string m_videoPath; // for shared video release
    uint64_t m_lastResolvedFrame = 0; // track last resolved frame to avoid redundant GPU work

    // True while the decoder is paused because the node is offscreen (saves CPU).
    bool m_pausedForVisibility = false;

    // Lazy creation fields (used when the first frame isn't ready yet)
    bool m_lazyCreate = false;
    // Visual params applied when the first frame arrives
    CCSize m_lazyWinSize;
    std::string m_lazyBlurType;
    float m_lazyBlurIntensity = 0.f;
    bool m_lazyDarkMode = false;
    float m_lazyDarkIntensity = 0.5f;
    // Visual-creation callback set by applyVideoBg; uses getParent() to avoid dangling pointers.
    std::function<void()> m_createVisuals;

    // Per-update interval from the video FPS limit, so we tick at the video rate, not every game frame.
    static float computeTickInterval() {
        int fps = paimon::settings::video::fpsLimit();
        if (fps < 1) fps = 1;
        if (fps > 240) fps = 240;
        return 1.0f / static_cast<float>(fps);
    }

    void scheduleVideoTick() {
        // Re-scheduling the same selector updates its interval rather than firing twice.
        this->schedule(schedule_selector(VideoBackgroundUpdateNode::tick),
                       computeTickInterval());
    }

    static VideoBackgroundUpdateNode* create(
        std::unique_ptr<paimon::video::VideoPlayer> p,
        bool suspendedDynSong,
        bool ownsVideoAudio,
        CCSprite* visibleSprite
    ) {
        auto ret = new VideoBackgroundUpdateNode();
        if (ret && ret->init()) {
            ret->player = std::move(p);
            ret->m_visibleSprite = visibleSprite;
            ret->m_didSuspendDynSong = suspendedDynSong;
            ret->m_ownsVideoAudioFlag = ownsVideoAudio;
            ret->m_firstVisibleFrameShown = ret->player && ret->player->hasVisibleFrame();
            ret->autorelease();
            ret->scheduleVideoTick();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    // Factory for shared video player (Same As reuse)
    static VideoBackgroundUpdateNode* createShared(
        std::shared_ptr<paimon::video::VideoPlayer> sp,
        std::string const& videoPath,
        bool suspendedDynSong,
        bool ownsVideoAudio,
        CCSprite* visibleSprite
    ) {
        auto ret = new VideoBackgroundUpdateNode();
        if (ret && ret->init()) {
            ret->sharedPlayer = sp;
            ret->m_videoPath = videoPath;
            ret->m_visibleSprite = visibleSprite;
            ret->m_didSuspendDynSong = suspendedDynSong;
            ret->m_ownsVideoAudioFlag = ownsVideoAudio;
            ret->m_firstVisibleFrameShown = sp && sp->hasVisibleFrame();
            ret->autorelease();
            ret->scheduleVideoTick();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    // Lazy-visual factory; caller must set m_createVisuals right after creation.
    static VideoBackgroundUpdateNode* createLazyShared(
        std::shared_ptr<paimon::video::VideoPlayer> sp,
        std::string const& videoPath,
        bool suspendedDynSong,
        bool ownsVideoAudio
    ) {
        auto ret = new VideoBackgroundUpdateNode();
        if (ret && ret->init()) {
            ret->sharedPlayer = sp;
            ret->m_videoPath = videoPath;
            ret->m_lazyCreate = true;
            ret->m_didSuspendDynSong = suspendedDynSong;
            ret->m_ownsVideoAudioFlag = ownsVideoAudio;
            ret->autorelease();
            ret->scheduleVideoTick();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void onExit() override {
        // Don't shutdown() here: transition reparenting calls onExit() temporarily,
        // and that would permanently stop and release the player.
        CCNode::onExit();
    }

    paimon::video::VideoPlayer* getPlayer() {
        return player ? player.get() : sharedPlayer.get();
    }

    void shutdown(bool duringSceneTeardown, bool suppressResume) {
        if (m_shutdown) return;
        m_shutdown = true;
        m_suppressResume = suppressResume;
        // Stop our throttled tick selector (scheduleUpdate() isn't used here).
        this->unschedule(schedule_selector(VideoBackgroundUpdateNode::tick));

        // Fade out audio before stopping player
        auto* p = getPlayer();
        bool doingSmoothFadeOut = false;
        if (p && m_ownsVideoAudioFlag && p->hasAudio() && p->isAudioPlaying()) {
            if (duringSceneTeardown) {
                // Synchronous stop so the next scene's playSong sees a free channel
                // (the async 50ms ramp would leave playSong gated on a stale flag).
                p->fadeAudioOut(0.0f);
                paimon::setVideoAudioInteropActive(false);
                m_ownsVideoAudioFlag = false;
                if (DynamicSongManager::get()->hasSuspendedPlayback()) {
                    DynamicSongManager::get()->resumeSuspendedPlayback();
                    m_didSuspendDynSong = false;
                }
            } else {
                // Smooth fade-out; the fade node outlives this node and releases FMOD when done.
                p->fadeAudioOut(0.5f, []() {
                    paimon::setVideoAudioInteropActive(false);
                    // Resume dynamic song after fade-out completes
                    if (DynamicSongManager::get()->hasSuspendedPlayback()) {
                        DynamicSongManager::get()->resumeSuspendedPlayback();
                    }
                });
                // Clear the flag now so the fade-out callback handles it
                m_ownsVideoAudioFlag = false;
                m_didSuspendDynSong = false;
                doingSmoothFadeOut = true;
            }
        }

        bool keepUniquePlayerAliveForFade = false;
        if (player) {
            // Don't stop() during smooth fade-out; let the player die naturally so
            // releaseAudio() can transfer FMOD ownership to the fade node.
            if (!doingSmoothFadeOut) {
                player->stop();
            } else {
                keepUniquePlayerAliveForFade = true;
            }
            if (!keepUniquePlayerAliveForFade) {
                player.reset();
            }
        }
        if (sharedPlayer) {
            // Release our reference; don't stop — other layers may still use it.
            if (!m_videoPath.empty()) {
                LayerBackgroundManager::get().releaseSharedVideo(m_videoPath);
            }
            sharedPlayer.reset();
        }

        // Clear the flag via the public setter so a new scene doesn't inherit a stale gating signal.
        if (m_ownsVideoAudioFlag) {
            paimon::setVideoAudioInteropActive(false);
            m_ownsVideoAudioFlag = false;
        }

        if (!m_suppressResume && m_didSuspendDynSong && DynamicSongManager::get()->hasSuspendedPlayback()) {
            DynamicSongManager::get()->resumeSuspendedPlayback();
        }

        if (keepUniquePlayerAliveForFade) {
            auto fadingPlayer = std::shared_ptr<paimon::video::VideoPlayer>(player.release());
            paimon::scheduleMainThreadDelay(0.6f, [fadingPlayer]() mutable {
                fadingPlayer.reset();
            });
        }
    }

    // True if every ancestor is visible; used to pause the decoder when nobody can see it.
    // Not const: RobTop's Cocos2d-x 2.2.3 declares isVisible()/getParent() as non-const virtuals.
    bool isPipelineVisible() {
        CCNode* n = this;
        while (n) {
            if (!n->isVisible()) return false;
            n = n->getParent();
        }
        return true;
    }

    void tick(float dt) {
        // Guard against the scheduler firing after shutdown.
        if (m_shutdown) return;

        auto* p = getPlayer();
        if (!p) return;

        // Pause decoding while hidden. Unique players are paused; shared players
        // keep decoding for other visible nodes.
        bool visible = isPipelineVisible();
        if (!visible) {
            // Pause unique players only; a shared player feeds other visible nodes.
            if (player && player->isPlaying() && !m_pausedForVisibility) {
                player->pause();
                m_pausedForVisibility = true;
            }
            return;
        } else if (m_pausedForVisibility) {
            // Coming back from hidden — resume the unique decoder.
            if (player && !player->isPlaying()) {
                player->resume();
            }
            m_pausedForVisibility = false;
        }

        if (p->isPlaying()) {
            p->update(dt);

            // Detect video audio init failure — clear interop flag and restore game music
            if (m_ownsVideoAudioFlag && p->didAudioInitFail()) {
                log::warn("[VideoBg] Video audio init failed — restoring game music");
                paimon::setVideoAudioInteropActive(false);
                m_ownsVideoAudioFlag = false;

                if (m_didSuspendDynSong && DynamicSongManager::get()->hasSuspendedPlayback()) {
                    DynamicSongManager::get()->resumeSuspendedPlayback();
                    m_didSuspendDynSong = false;
                } else {
                    // Interop flag already cleared — the hook will pass through
                    GameManager::get()->fadeInMenuMusic();
                }
            }

            // Build visuals once the first frame is ready (callback uses getParent() for safety).
            if (m_lazyCreate && !m_visibleSprite && p->hasVisibleFrame()) {
                m_lazyCreate = false;
                if (m_createVisuals) {
                    m_createVisuals();
                }
            }

            // Plain-sprite GPU YUV path: re-resolve YUV->RGBA each frame so the sprite updates.
            // VideoBlurNode does this itself in its own update().
            if (m_firstVisibleFrameShown && m_visibleSprite && p->isUsingGPUYuv()) {
                uint64_t fc = p->getFrameCounter();
                if (fc != m_lastResolvedFrame) {
                    m_lastResolvedFrame = fc;
                    p->getResolvedRGBATexture();
                }
            }

            if (!m_firstVisibleFrameShown && m_visibleSprite && p->hasVisibleFrame()) {
                m_firstVisibleFrameShown = true;
                m_visibleSprite->stopAllActions();
                m_visibleSprite->setVisible(true);
                m_visibleSprite->setOpacity(0);
                m_visibleSprite->runAction(CCFadeTo::create(0.15f, 255));

                // Save first frame as disk preview for fast display on the next launch
                if (!m_previewSaved && !m_videoPath.empty()) {
                    m_previewSaved = true;
                    LayerBackgroundManager::saveVideoBgPreview(m_videoPath, p);
                }
            }
        }
        // Audio sync is now handled inside VideoPlayer::update()
    }

    ~VideoBackgroundUpdateNode() override {
        // Guard against touching a mid-destruction scene during the ~CCNode teardown
        // chain; skip the scene-flag mirror and resync when the new scene runs.
        paimon::InteropSceneTeardownScope teardownGuard;
        shutdown(true, m_suppressResume);
    }
};

} // namespace

// Video preview cache helpers

std::filesystem::path LayerBackgroundManager::getVideoBgPreviewDir() {
    return geode::Mod::get()->getSaveDir() / "bg_previews";
}

std::filesystem::path LayerBackgroundManager::getVideoBgPreviewPath(std::string const& videoPath) {
    auto dir = getVideoBgPreviewDir();
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        std::filesystem::create_directories(dir, ec);
    }
    // Use a hash of the full path as the filename
    size_t h = std::hash<std::string>{}(videoPath);
    return dir / (std::to_string(h) + ".bin");
}

bool LayerBackgroundManager::loadVideoBgPreview(std::string const& videoPath,
                                                std::vector<uint8_t>& outPixels,
                                                int& outW, int& outH) {
    auto previewPath = getVideoBgPreviewPath(videoPath);
    std::error_code ec;
    if (!std::filesystem::exists(previewPath, ec) || ec) return false;

    // If the source video is newer than the preview, the preview is stale
    auto previewTime = std::filesystem::last_write_time(previewPath, ec);
    if (ec) return false;
    auto sourceTime = std::filesystem::last_write_time(videoPath, ec);
    if (!ec && sourceTime > previewTime) return false;

    std::ifstream f(previewPath, std::ios::binary);
    if (!f) return false;

    uint32_t w = 0, h = 0;
    f.read(reinterpret_cast<char*>(&w), sizeof(w));
    f.read(reinterpret_cast<char*>(&h), sizeof(h));
    if (!f || w == 0 || h == 0 || w > 8192 || h > 8192) return false;

    size_t sz = static_cast<size_t>(w) * h * 4;
    outPixels.resize(sz);
    f.read(reinterpret_cast<char*>(outPixels.data()), static_cast<std::streamsize>(sz));
    if (!f.good()) return false;

    outW = static_cast<int>(w);
    outH = static_cast<int>(h);
    return true;
}

void LayerBackgroundManager::saveVideoBgPreview(std::string const& videoPath,
                                                paimon::video::VideoPlayer const* player) {
    if (!player || videoPath.empty()) return;

    std::vector<uint8_t> pixels;
    int w = 0, h = 0;
    if (!player->copyCurrentFramePixels(pixels, w, h)) return;
    if (pixels.empty() || w <= 0 || h <= 0) return;

    // Defer the small file write to the main thread to avoid concurrent detached-thread writes.
    auto previewPath = LayerBackgroundManager::getVideoBgPreviewPath(videoPath);
    geode::Loader::get()->queueInMainThread([previewPath, pixels = std::move(pixels), w, h]() {
        std::ofstream f(previewPath, std::ios::binary);
        if (!f) return;
        uint32_t uw = static_cast<uint32_t>(w);
        uint32_t uh = static_cast<uint32_t>(h);
        f.write(reinterpret_cast<const char*>(&uw), sizeof(uw));
        f.write(reinterpret_cast<const char*>(&uh), sizeof(uh));
        f.write(reinterpret_cast<const char*>(pixels.data()),
                static_cast<std::streamsize>(pixels.size()));
        f.flush();
        log::info("[LayerBgMgr] Saved video background preview: {}x{}", w, h);
    });
}

LayerBackgroundManager& LayerBackgroundManager::get() {
    // Released explicitly during RuntimeLifecycle shutdown. Avoid atexit
    // destruction races with detached video teardown threads.
    static auto* s_instance = new LayerBackgroundManager();
    return *s_instance;
}

LayerBgConfig LayerBackgroundManager::getConfig(std::string const& key) const {
    LayerBgConfig cfg;
    cfg.type          = Mod::get()->getSavedValue<std::string>("layerbg-" + key + "-type", "default");
    cfg.customPath    = Mod::get()->getSavedValue<std::string>("layerbg-" + key + "-path", "");
    cfg.levelId       = Mod::get()->getSavedValue<int>("layerbg-" + key + "-id", 0);
    cfg.darkMode      = Mod::get()->getSavedValue<bool>("layerbg-" + key + "-dark", false);
    cfg.darkIntensity = Mod::get()->getSavedValue<float>("layerbg-" + key + "-dark-intensity", 0.5f);
    cfg.shader        = Mod::get()->getSavedValue<std::string>("layerbg-" + key + "-shader", "none");
    return cfg;
}

void LayerBackgroundManager::saveConfig(std::string const& key, LayerBgConfig const& cfg) {
    log::info("[LayerBgMgr] saveConfig: key={} type={}", key, cfg.type);
    Mod::get()->setSavedValue("layerbg-" + key + "-type", cfg.type);
    Mod::get()->setSavedValue("layerbg-" + key + "-path", cfg.customPath);
    Mod::get()->setSavedValue("layerbg-" + key + "-id", cfg.levelId);
    Mod::get()->setSavedValue("layerbg-" + key + "-dark", cfg.darkMode);
    Mod::get()->setSavedValue("layerbg-" + key + "-dark-intensity", cfg.darkIntensity);
    Mod::get()->setSavedValue("layerbg-" + key + "-shader", cfg.shader);
    scheduleLayerBgSave();
}

bool LayerBackgroundManager::hasCustomBackground(std::string const& layerKey) const {
    auto resolved = resolveConfig(layerKey);
    return resolved.type != "default";
}

LayerBgConfig LayerBackgroundManager::resolveConfig(std::string const& layerKey) const {
    auto cfg = getConfig(layerKey);
    log::debug("[LayerBgMgr] resolveConfig: key={} type={}", layerKey, cfg.type);
    if (cfg.type == "default") return cfg;

    std::string resolvedType = cfg.type;
    LayerBgConfig resolvedCfg = cfg;
    int maxHops = 5;

    while (maxHops-- > 0) {
        if (resolvedType == "menu") {
            LayerBgConfig menuCfg = getConfig("menu");
            if (menuCfg.type != "default") {
                resolvedCfg.type = menuCfg.type;
                resolvedCfg.customPath = menuCfg.customPath;
                resolvedCfg.levelId = menuCfg.levelId;
                resolvedType = menuCfg.type;
                continue;
            } else {
                std::string menuType = Mod::get()->getSavedValue<std::string>("bg-type", "default");
                if (menuType == "default" || menuType.empty()) { resolvedCfg.type = "default"; return resolvedCfg; }
                resolvedCfg.type = (menuType == "thumbnails") ? "random" : menuType;
                resolvedCfg.customPath = Mod::get()->getSavedValue<std::string>("bg-custom-path", "");
                resolvedCfg.levelId = Mod::get()->getSavedValue<int>("bg-id", 0);
                return resolvedCfg;
            }
        }

        bool isLayerRef = false;
        for (auto& [k, n] : LAYER_OPTIONS) {
            if (resolvedType == k) { isLayerRef = true; break; }
        }
        if (isLayerRef) {
            auto refCfg = getConfig(resolvedType);
            resolvedCfg.type = refCfg.type;
            resolvedCfg.customPath = refCfg.customPath;
            resolvedCfg.levelId = refCfg.levelId;
            // Keep original dark mode / shader
            resolvedType = refCfg.type;
            if (resolvedType == "default") return resolvedCfg;
            continue;
        }
        break;
    }
    return resolvedCfg;
}

// hasOtherVideoConfigured() removed: concurrent video backgrounds are supported.

// Per-layer music
LayerMusicConfig LayerBackgroundManager::getMusicConfig(std::string const& key) const {
    LayerMusicConfig cfg;
    cfg.mode        = Mod::get()->getSavedValue<std::string>("layermusic-" + key + "-mode", "default");
    cfg.songID      = Mod::get()->getSavedValue<int>("layermusic-" + key + "-songid", 0);
    cfg.customPath  = Mod::get()->getSavedValue<std::string>("layermusic-" + key + "-path", "");
    cfg.speed       = Mod::get()->getSavedValue<float>("layermusic-" + key + "-speed", 1.0f);
    cfg.randomStart = Mod::get()->getSavedValue<bool>("layermusic-" + key + "-randomstart", false);
    cfg.startMs     = Mod::get()->getSavedValue<int>("layermusic-" + key + "-startms", 0);
    cfg.endMs       = Mod::get()->getSavedValue<int>("layermusic-" + key + "-endms", 0);
    cfg.filter      = Mod::get()->getSavedValue<std::string>("layermusic-" + key + "-filter", "none");
    return cfg;
}

void LayerBackgroundManager::saveMusicConfig(std::string const& key, LayerMusicConfig const& cfg) {
    Mod::get()->setSavedValue("layermusic-" + key + "-mode", cfg.mode);
    Mod::get()->setSavedValue("layermusic-" + key + "-songid", cfg.songID);
    Mod::get()->setSavedValue("layermusic-" + key + "-path", cfg.customPath);
    Mod::get()->setSavedValue("layermusic-" + key + "-speed", cfg.speed);
    Mod::get()->setSavedValue("layermusic-" + key + "-randomstart", cfg.randomStart);
    Mod::get()->setSavedValue("layermusic-" + key + "-startms", cfg.startMs);
    Mod::get()->setSavedValue("layermusic-" + key + "-endms", cfg.endMs);
    Mod::get()->setSavedValue("layermusic-" + key + "-filter", cfg.filter);
    scheduleLayerBgSave();
}

// Global music (one config for all layers)
LayerMusicConfig LayerBackgroundManager::getGlobalMusicConfig() const {
    return getMusicConfig("global");
}

void LayerBackgroundManager::saveGlobalMusicConfig(LayerMusicConfig const& cfg) {
    saveMusicConfig("global", cfg);
}

// Migrate legacy saved values to the unified format
void LayerBackgroundManager::migrateFromLegacy() {
    if (Mod::get()->getSavedValue<bool>("layerbg-migrated-v2", false)) return;

    // migrate menu bg: bg-type -> layerbg-menu-type
    std::string menuType = Mod::get()->getSavedValue<std::string>("bg-type", "");
    if (!menuType.empty() && menuType != "default") {
        LayerBgConfig menuCfg;
        menuCfg.type = (menuType == "thumbnails") ? "random" : menuType;
        menuCfg.customPath = Mod::get()->getSavedValue<std::string>("bg-custom-path", "");
        menuCfg.levelId = Mod::get()->getSavedValue<int>("bg-id", 0);
        menuCfg.darkMode = Mod::get()->getSavedValue<bool>("bg-dark-mode", false);
        menuCfg.darkIntensity = Mod::get()->getSavedValue<float>("bg-dark-intensity", 0.5f);
        saveConfig("menu", menuCfg);
    }

    // migrate profile bg: profile-bg-type -> layerbg-profile-type
    std::string profileType = Mod::get()->getSavedValue<std::string>("profile-bg-type", "");
    if (!profileType.empty() && profileType != "none") {
        LayerBgConfig profileCfg;
        profileCfg.type = profileType;
        profileCfg.customPath = Mod::get()->getSavedValue<std::string>("profile-bg-path", "");
        saveConfig("profile", profileCfg);
    }

    // migrate dynamic-song -> layermusic-levelinfo
    bool dynSong = Mod::get()->getSavedValue<bool>("dynamic-song", false);
    if (dynSong) {
        LayerMusicConfig mcfg;
        mcfg.mode = "dynamic";
        saveMusicConfig("levelinfo", mcfg);
        saveMusicConfig("levelselect", mcfg);
    }

    Mod::get()->setSavedValue("layerbg-migrated-v2", true);
    (void)Mod::get()->saveData();
    log::info("[LayerBackgroundManager] Legacy settings migrated to v2 format");

    // Migrate per-layer music to global music config
    migrateToGlobalMusic();
}

void LayerBackgroundManager::migrateExternalAssetsToManagedStorage() {
    if (Mod::get()->getSavedValue<bool>("layerbg-assets-migrated-v1", false)) return;

    bool changed = false;

    auto migrateBgConfig = [&](std::string const& key, std::string const& bucket, paimon::assets::Kind kind) {
        auto cfg = getConfig(key);
        if ((cfg.type != "custom" && cfg.type != "video") || cfg.customPath.empty()) {
            return;
        }

        auto imported = paimon::assets::importStoredPath(cfg.customPath, bucket, kind);
        if (imported.success && imported.changed && !imported.path.empty()) {
            cfg.customPath = paimon::assets::normalizePathString(imported.path);
            saveConfig(key, cfg);
            changed = true;
        }
    };

    migrateBgConfig("menu", "background_menu", paimon::assets::Kind::Media);
    for (auto const& [key, _] : LAYER_OPTIONS) {
        migrateBgConfig(key, "background_" + key, paimon::assets::Kind::Media);
    }

    std::string legacyMenuPath = Mod::get()->getSavedValue<std::string>("bg-custom-path", "");
    std::string legacyMenuType = Mod::get()->getSavedValue<std::string>("bg-type", "default");
    if (!legacyMenuPath.empty() && (legacyMenuType == "custom" || legacyMenuType == "video")) {
        auto imported = paimon::assets::importStoredPath(
            legacyMenuPath,
            "background_menu",
            legacyMenuType == "video" ? paimon::assets::Kind::Video : paimon::assets::Kind::Image
        );
        if (imported.success && imported.changed && !imported.path.empty()) {
            Mod::get()->setSavedValue("bg-custom-path", paimon::assets::normalizePathString(imported.path));
            changed = true;
        }
    }

    std::string profileType = Mod::get()->getSavedValue<std::string>("profile-bg-type", "none");
    std::string profilePath = Mod::get()->getSavedValue<std::string>("profile-bg-path", "");
    if (profileType == "custom" && !profilePath.empty()) {
        auto imported = paimon::assets::importStoredPath(profilePath, "profile_picture", paimon::assets::Kind::Image);
        if (imported.success && imported.changed && !imported.path.empty()) {
            auto normalized = paimon::assets::normalizePathString(imported.path);
            Mod::get()->setSavedValue("profile-bg-path", normalized);
            auto profileCfg = getConfig("profile");
            if (profileCfg.type == "custom" && profileCfg.customPath == profilePath) {
                profileCfg.customPath = normalized;
                saveConfig("profile", profileCfg);
            }
            changed = true;
        }
    }

    Mod::get()->setSavedValue("layerbg-assets-migrated-v1", true);
    if (changed) {
        (void)Mod::get()->saveData();
        log::info("[LayerBackgroundManager] Migrated external local assets to managed storage");
    } else {
        (void)Mod::get()->saveData();
    }
}

void LayerBackgroundManager::migrateToGlobalMusic() {
    if (Mod::get()->getSavedValue<bool>("layermusic-migrated-global", false)) return;

    // Prefer "menu" config first, then try other layers
    static std::vector<std::string> priority = {
        "menu", "creator", "browser", "search", "leaderboards",
        "profile", "levelselect", "levelinfo"
    };

    for (auto const& key : priority) {
        auto cfg = getMusicConfig(key);
        if (cfg.mode != "default" && cfg.mode != "dynamic") {
            saveGlobalMusicConfig(cfg);
            log::info("[LayerBackgroundManager] Migrated per-layer music from '{}' to global config", key);
            break;
        }
    }

    Mod::get()->setSavedValue("layermusic-migrated-global", true);
    (void)Mod::get()->saveData();
}

// Hide GD's original background
void LayerBackgroundManager::hideOriginalBg(CCLayer* layer) {
    // Geode node-ids: each layer has a distinct ID for its background.
    // MenuLayer = "main-menu-bg", most layers = "background".
    static char const* bgNodeIDs[] = {
        "main-menu-bg",   // MenuLayer
        "background",     // CreatorLayer, LevelSearchLayer, LeaderboardsLayer, LevelBrowserLayer, etc.
        nullptr
    };

    for (int i = 0; bgNodeIDs[i]; i++) {
        if (auto bg = layer->getChildByID(bgNodeIDs[i])) {
            bg->setVisible(false);
        }
    }

    // Also hide GJGroundLayer if present (decorative corners/ground)
    if (auto children = layer->getChildren()) {
        auto ws = CCDirector::get()->getWinSize();
        bool foundByID = false;
        for (int j = 0; bgNodeIDs[j]; j++) {
            if (layer->getChildByID(bgNodeIDs[j])) { foundByID = true; break; }
        }
        // No ID match: fall back to hiding the first large sprite (the GD bg)
        if (!foundByID) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                auto* sprite = typeinfo_cast<CCSprite*>(child);
                if (!sprite) continue;
                auto* tex = sprite->getTexture();
                if (!tex) continue;
                auto cs = sprite->getContentSize();
                if (cs.width >= ws.width * 0.5f && cs.height >= ws.height * 0.5f) {
                    sprite->setVisible(false);
                    return;
                }
            }
        }
    }
}

void LayerBackgroundManager::applyVanillaBackgroundTintFix(CCLayer* layer) {
    if (!layer || !paimon::settings::backgrounds::transparentBackgroundMode()) {
        return;
    }

    static char const* bgNodeIDs[] = {
        "main-menu-bg",
        "background",
        "bg",
        "bg-texture",
        nullptr,
    };

    bool changed = false;
    for (int i = 0; bgNodeIDs[i]; ++i) {
        changed = tintVanillaBackgroundNode(layer->getChildByID(bgNodeIDs[i])) || changed;
    }
    if (changed) {
        return;
    }

    auto* children = layer->getChildren();
    if (!children) {
        return;
    }

    auto winSize = CCDirector::get()->getWinSize();
    for (auto* child : CCArrayExt<CCNode*>(children)) {
        if (!child || !child->isVisible()) continue;
        auto id = std::string(child->getID());
        if (!id.empty() && id.rfind("paimon-", 0) == 0) continue;

        auto* sprite = typeinfo_cast<CCSprite*>(child);
        if (!sprite || !sprite->getTexture()) continue;

        auto size = sprite->getContentSize();
        if (size.width >= winSize.width * 0.5f && size.height >= winSize.height * 0.5f) {
            sprite->setColor({255, 255, 255});
            return;
        }
    }
}

// Load texture per config (cache-first)
CCTexture2D* LayerBackgroundManager::loadTextureForConfig(LayerBgConfig const& cfg) {
    log::debug("[LayerBgMgr] loadTextureForConfig: type={} id={}", cfg.type, cfg.levelId);
    if (cfg.type == "custom" && !cfg.customPath.empty()) {
        std::error_code ec;
        auto normalizedPath = paimon::assets::normalizePath(std::filesystem::path(cfg.customPath));
        if (std::filesystem::exists(normalizedPath, ec)) {
            // no GIF here; GIF is handled separately
            auto ext = geode::utils::string::pathToString(normalizedPath.extension());
            for (auto& c : ext) c = (char)std::tolower(c);
            if (ext == ".gif") return nullptr; // signal to use applyGifBg

            // Cache hit: skip the full stb decode (30-150ms on large PNGs) that
            // used to run on every menu transition, and share one texture across
            // stacked layers instead of one copy each.
            if (auto* cached = customBgCacheGet(normalizedPath)) {
                return cached;
            }

            auto img = ImageLoadHelper::loadStaticImage(normalizedPath, 32);
            if (img.success && img.texture) {
                img.texture->autorelease();
                customBgCachePut(normalizedPath, img.texture);
                return img.texture;
            }
        }
    } else if (cfg.type == "id" && cfg.levelId > 0) {
        return LocalThumbs::get().loadTexture(cfg.levelId);
    } else if (cfg.type == "random") {
        auto ids = LocalThumbs::get().getAllLevelIDs();
        if (!ids.empty()) {
            static std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<size_t> dist(0, ids.size() - 1);
            return LocalThumbs::get().loadTexture(ids[dist(rng)]);
        }
    } else if (cfg.type == "menu") {
        // use the same config as the main menu
        LayerBgConfig menuCfg = getConfig("menu");
        if (menuCfg.type == "default") {
            // fall back to legacy keys if migration hasn't run yet
            std::string menuType = Mod::get()->getSavedValue<std::string>("bg-type", "default");
            if (menuType == "default" || menuType.empty()) return nullptr;
            menuCfg.type = (menuType == "thumbnails") ? "random" : menuType;
            menuCfg.customPath = Mod::get()->getSavedValue<std::string>("bg-custom-path", "");
            menuCfg.levelId = Mod::get()->getSavedValue<int>("bg-id", 0);
        }
        menuCfg.darkMode = cfg.darkMode; // use the layer's dark mode, not the menu's
        menuCfg.darkIntensity = cfg.darkIntensity;
        return loadTextureForConfig(menuCfg);
    }
    return nullptr;
}

// Apply static background
void LayerBackgroundManager::applyStaticBg(CCLayer* layer, CCTexture2D* tex, LayerBgConfig const& cfg) {
    log::debug("[LayerBgMgr] applyStaticBg: dark={} shader={}", cfg.darkMode, cfg.shader);
    clearAppliedBackground(layer, false);
    auto winSize = CCDirector::get()->getWinSize();
    auto winPixels = CCDirector::get()->getWinSizeInPixels();

    auto container = CCNode::create();
    container->setContentSize(winSize);
    container->setPosition({0, 0});
    container->setAnchorPoint({0, 0});
    container->setID("paimon-layerbg-container"_spr);
    container->setZOrder(-10);

    bool useShader = !cfg.shader.empty() && cfg.shader != "none";
    CCSprite* sprite = nullptr;

    if (useShader) {
        // ShaderBgSprite re-applies uniforms in draw()
        auto shaderSpr = ShaderBgSprite::createWithTexture(tex);
        if (!shaderSpr) return;

        auto* program = getBgShaderProgram(cfg.shader);
        if (program) {
            shaderSpr->setShaderProgram(program);
            shaderSpr->m_shaderIntensity = geode::Mod::get()->getSavedValue<float>("layerbg-shader-intensity", 0.5f);
            shaderSpr->m_screenW = winSize.width;
            shaderSpr->m_screenH = winSize.height;
            shaderSpr->m_shaderTime = 0.f;
            // Schedule for animated shaders (scanlines, etc.)
            shaderSpr->schedule(schedule_selector(ShaderBgSprite::updateShaderTime));
        }

        sprite = shaderSpr;
    } else {
        sprite = CCSprite::createWithTexture(tex);
        if (!sprite) return;
    }

    float scX = winSize.width / sprite->getContentWidth();
    float scY = winSize.height / sprite->getContentHeight();
    sprite->setScale(std::max(scX, scY));
    sprite->setPosition(winSize / 2);
    sprite->setAnchorPoint({0.5f, 0.5f});


    container->addChild(sprite);

    addLayerBgDarkOverlay(container, winSize, cfg.darkMode, cfg.darkIntensity);

    layer->addChild(container);
}

// Apply GIF background
void LayerBackgroundManager::applyGifBg(CCLayer* layer, std::string const& path, LayerBgConfig const& cfg) {
    log::info("[LayerBgMgr] applyGifBg: path={} dark={}", path, cfg.darkMode);
    clearAppliedBackground(layer, false);
    auto winSize = CCDirector::get()->getWinSize();

    auto container = CCNode::create();
    container->setContentSize(winSize);
    container->setPosition({0, 0});
    container->setAnchorPoint({0, 0});
    container->setID("paimon-layerbg-container"_spr);
    container->setZOrder(-10);
    layer->addChild(container);

    CCNode* rawContainer = container;
    auto containerAlive = std::make_shared<std::atomic<bool>>(true);
    {
        std::lock_guard lk(g_containerAliveMutex);
        g_containerAliveFlags[rawContainer] = containerAlive;
    }
    bool darkMode = cfg.darkMode;
    float darkIntensity = cfg.darkIntensity;
    std::string shaderName = cfg.shader;

    AnimatedGIFSprite::pinGIF(path);
    AnimatedGIFSprite::createAsync(path, [rawContainer, containerAlive, winSize, darkMode, darkIntensity, shaderName](AnimatedGIFSprite* anim) {
        auto* container = rawContainer;
        if (!anim || !containerAlive->load(std::memory_order_acquire) || !container->getParent()) return;

        float cw = anim->getContentWidth();
        float ch = anim->getContentHeight();
        if (cw <= 0 || ch <= 0) return;

        float sc = std::max(winSize.width / cw, winSize.height / ch);
        anim->setAnchorPoint({0.5f, 0.5f});
        anim->setPosition(winSize / 2);
        anim->setScale(sc);

        // Apply shader to the GIF (AnimatedGIFSprite re-applies uniforms in draw())
        if (!shaderName.empty() && shaderName != "none") {
            auto* program = getBgShaderProgram(shaderName);
            if (program) {
                anim->setShaderProgram(program);
                anim->m_intensity = 0.5f;
                anim->m_texSize = CCSize(winSize.width, winSize.height);
            }
        }

        container->addChild(anim);

        addLayerBgDarkOverlay(container, winSize, darkMode, darkIntensity);

        // Clean up the alive flag  container is now fully set up
        {
            std::lock_guard lk(g_containerAliveMutex);
            g_containerAliveFlags.erase(rawContainer);
        }
    });
}

void LayerBackgroundManager::applyProceduralShaderBg(CCLayer* layer, LayerBgConfig const& cfg) {
    clearAppliedBackground(layer, false);

    auto* program = getProceduralBgShaderProgram(cfg.shader);
    if (!layer || !program) {
        return;
    }

    auto winSize = CCDirector::get()->getWinSize();
    auto winPixels = CCDirector::get()->getWinSizeInPixels();

    auto container = CCNode::create();
    container->setContentSize(winSize);
    container->setPosition({0, 0});
    container->setAnchorPoint({0, 0});
    container->setID("paimon-layerbg-container"_spr);
    container->setZOrder(-10);

    auto* sprite = PaimonShaderGradient::create({255, 255, 255, 255}, {255, 255, 255, 255});
    if (!sprite) {
        return;
    }

    sprite->setShaderProgram(program);
    sprite->m_intensity = 1.0f;
    sprite->m_time = 0.f;
    sprite->m_texSize = winPixels.width > 0.f && winPixels.height > 0.f ? winPixels : winSize;
    sprite->setAnchorPoint({0.f, 0.f});
    sprite->setPosition({0.f, 0.f});
    sprite->setContentSize(winSize);
    sprite->schedule(schedule_selector(PaimonShaderGradient::updateShaderTime));

    container->addChild(sprite);

    addLayerBgDarkOverlay(container, winSize, cfg.darkMode, cfg.darkIntensity);

    layer->addChild(container);
}

// VideoBlurNode: multi-pass GPU blur on the video texture each frame.
// Pre-allocates render textures once; zero heap allocs in update().
// "blur" = Gaussian H+V at 1/2 res (2 RT ops/frame); "paimonblur" = Dual Kawase 3+3 passes.
struct VideoBlurNode : public CCNode {
    std::shared_ptr<paimon::video::VideoPlayer> m_player;

    // Gaussian resources
    Ref<CCRenderTexture> m_rtA;       // H-blur target (half-res)
    Ref<CCRenderTexture> m_rtB;       // V-blur target (half-res) ? display texture
    Ref<CCSprite>        m_srcSprite; // source: video tex, flipY=true
    Ref<CCSprite>        m_midSprite; // intermediate: rtA tex, flipY=true
    CCGLProgram*         m_blurH = nullptr;
    CCGLProgram*         m_blurV = nullptr;
    float                m_gaussRadius = 0.1f;

    // Paimon blur resources
    Ref<CCRenderTexture> m_pD0, m_pD1, m_pD2;  // downsample: 1/2, 1/4, 1/8
    Ref<CCRenderTexture> m_pU1, m_pU0;          // upsample:   1/4, 1/2
    Ref<CCRenderTexture> m_pFinal;              // upsample to full res ? display
    Ref<CCSprite>  m_pSpr0;   // video ? D0
    Ref<CCSprite>  m_pSpr1;   // D0   ? D1
    Ref<CCSprite>  m_pSpr2;   // D1   ? D2
    Ref<CCSprite>  m_pSprU2;  // D2   ? U1
    Ref<CCSprite>  m_pSprU1;  // U1   ? U0
    Ref<CCSprite>  m_pSprU0;  // U0   ? Final
    CCGLProgram*   m_blurDown = nullptr;
    CCGLProgram*   m_blurUp   = nullptr;
    // Pre-computed halfpixel uniforms (constant per session, set once in init)
    float m_hp0x,  m_hp0y;   // D0   pass  source appears at halfSize
    float m_hp1x,  m_hp1y;   // D1   pass  source is halfSize RT
    float m_hp2x,  m_hp2y;   // D2   pass  source is quarterSize RT
    float m_hpu2x, m_hpu2y;  // U1   pass  source is eighthSize RT
    float m_hpu1x, m_hpu1y;  // U0   pass  source is quarterSize RT
    float m_hpu0x, m_hpu0y;  // Final pass  source is halfSize RT

    // Common
    Ref<CCSprite> m_displaySprite; // child of this node; faded in by VideoBackgroundUpdateNode
    CCSize m_winSize, m_halfSize, m_quarterSize, m_eighthSize;
    bool   m_isPaimon = false;
    uint64_t m_lastFrameCounter = 0;  // skip blur when video frame unchanged
    ccTexParams m_linear = {GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};

    // Cached uniform locations: looked up once at init() since per-frame string lookups are costly.
    GLint m_locGaussScreenSizeH = -1;
    GLint m_locGaussRadiusH     = -1;
    GLint m_locGaussScreenSizeV = -1;
    GLint m_locGaussRadiusV     = -1;
    GLint m_locKawaseDownHalfpixel = -1;
    GLint m_locKawaseUpHalfpixel   = -1;

    // Factory
    static VideoBlurNode* create(
        std::shared_ptr<paimon::video::VideoPlayer> player,
        CCSize const& winSize, float intensity, bool isPaimon
    ) {
        auto ret = new VideoBlurNode();
        if (ret && ret->initBlur(std::move(player), winSize, intensity, isPaimon)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    CCSprite* getDisplaySprite() { return m_displaySprite; }

    // Helpers
    // Scale+position a sprite to fill dstSize (in RT coordinate space)
    void fitSprite(CCSprite* spr, CCSize const& dstSize, bool flip = true) {
        CCSize content = spr->getContentSize();
        float sx = dstSize.width  / std::max(content.width,  1.f);
        float sy = dstSize.height / std::max(content.height, 1.f);
        spr->setScale(std::max(sx, sy));
        spr->setAnchorPoint({0.5f, 0.5f});
        spr->setPosition(dstSize * 0.5f);
        spr->setFlipY(flip);
    }

    // Create a sprite from an RT's texture sized to fill dstSize
    CCSprite* makeRTSprite(CCRenderTexture* rt, CCSize const& dstSize) {
        auto* tex = rt->getSprite()->getTexture();
        tex->setTexParameters(&m_linear);
        auto spr = CCSprite::createWithTexture(tex);
        fitSprite(spr, dstSize, true);
        return spr;
    }

    // Initialisation
    bool initBlur(
        std::shared_ptr<paimon::video::VideoPlayer> player,
        CCSize const& winSize, float intensity, bool isPaimon
    ) {
        if (!CCNode::init()) return false;
        m_player   = std::move(player);
        m_isPaimon = isPaimon;
        m_winSize  = winSize;
        m_halfSize    = CCSize(std::max(std::floor(winSize.width  * 0.5f),   4.f),
                               std::max(std::floor(winSize.height * 0.5f),   4.f));
        m_quarterSize = CCSize(std::max(std::floor(winSize.width  * 0.25f),  2.f),
                               std::max(std::floor(winSize.height * 0.25f),  2.f));
        m_eighthSize  = CCSize(std::max(std::floor(winSize.width  * 0.125f), 2.f),
                               std::max(std::floor(winSize.height * 0.125f), 2.f));

        if (!m_player) return false;
        auto* videoTex = m_player->getResolvedRGBATexture();
        if (!videoTex) return false;

        bool ok = isPaimon ? initPaimon(videoTex, intensity)
                           : initGaussian(videoTex, intensity);
        if (!ok || !m_displaySprite) return false;

        // Display sprite fills the screen; NO flipY (pipeline handles orientation)
        CCSize dc = m_displaySprite->getContentSize();
        float dsx = winSize.width  / std::max(dc.width,  1.f);
        float dsy = winSize.height / std::max(dc.height, 1.f);
        m_displaySprite->setScale(std::max(dsx, dsy));
        m_displaySprite->setAnchorPoint({0.5f, 0.5f});
        m_displaySprite->setPosition(winSize * 0.5f);
        m_displaySprite->setFlipY(false);
        m_displaySprite->setVisible(false); // VideoBackgroundUpdateNode fades it in
        this->addChild(m_displaySprite);
        // Throttle the blur tick to the video FPS instead of every game frame.
        int fps = paimon::settings::video::fpsLimit();
        if (fps < 1)   fps = 1;
        if (fps > 240) fps = 240;
        this->schedule(schedule_selector(VideoBlurNode::tick),
                       1.0f / static_cast<float>(fps));
        return true;
    }

    bool initGaussian(CCTexture2D* videoTex, float intensity) {
        m_blurH = paimon::shaders::getBlurHorizontalShader();
        m_blurV = paimon::shaders::getBlurVerticalShader();
        if (!m_blurH || !m_blurV) return false;

        m_rtA = CCRenderTexture::create((int)m_halfSize.width, (int)m_halfSize.height);
        m_rtB = CCRenderTexture::create((int)m_halfSize.width, (int)m_halfSize.height);
        if (!m_rtA || !m_rtB) return false;
        m_rtA->getSprite()->getTexture()->setTexParameters(&m_linear);
        m_rtB->getSprite()->getTexture()->setTexParameters(&m_linear);

        videoTex->setTexParameters(&m_linear);
        m_srcSprite = CCSprite::createWithTexture(videoTex);
        if (!m_srcSprite) return false;
        fitSprite(m_srcSprite, m_halfSize, true); // video fills half-res RT

        m_midSprite = makeRTSprite(m_rtA, m_halfSize);
        if (!m_midSprite) return false;

        m_displaySprite = CCSprite::createWithTexture(m_rtB->getSprite()->getTexture());
        if (!m_displaySprite) return false;

        // radius 0.040.20 mapped from 0.11.0 intensity
        m_gaussRadius = 0.04f + intensity * 0.16f;

        // Cache uniform locations once; per-frame string lookups are pure overhead.
        m_locGaussScreenSizeH = m_blurH->getUniformLocationForName("u_screenSize");
        m_locGaussRadiusH     = m_blurH->getUniformLocationForName("u_radius");
        m_locGaussScreenSizeV = m_blurV->getUniformLocationForName("u_screenSize");
        m_locGaussRadiusV     = m_blurV->getUniformLocationForName("u_radius");
        return true;
    }

    bool initPaimon(CCTexture2D* videoTex, float intensity) {
        m_blurDown = paimon::shaders::getKawaseDownShader();
        m_blurUp   = paimon::shaders::getKawaseUpShader();
        if (!m_blurDown || !m_blurUp) return false;

        m_pD0    = CCRenderTexture::create((int)m_halfSize.width,    (int)m_halfSize.height);
        m_pD1    = CCRenderTexture::create((int)m_quarterSize.width, (int)m_quarterSize.height);
        m_pD2    = CCRenderTexture::create((int)m_eighthSize.width,  (int)m_eighthSize.height);
        m_pU1    = CCRenderTexture::create((int)m_quarterSize.width, (int)m_quarterSize.height);
        m_pU0    = CCRenderTexture::create((int)m_halfSize.width,    (int)m_halfSize.height);
        m_pFinal = CCRenderTexture::create((int)m_winSize.width,     (int)m_winSize.height);
        if (!m_pD0||!m_pD1||!m_pD2||!m_pU1||!m_pU0||!m_pFinal) return false;

        m_pD0->getSprite()->getTexture()->setTexParameters(&m_linear);
        m_pD1->getSprite()->getTexture()->setTexParameters(&m_linear);
        m_pD2->getSprite()->getTexture()->setTexParameters(&m_linear);
        m_pU1->getSprite()->getTexture()->setTexParameters(&m_linear);
        m_pU0->getSprite()->getTexture()->setTexParameters(&m_linear);
        m_pFinal->getSprite()->getTexture()->setTexParameters(&m_linear);

        videoTex->setTexParameters(&m_linear);
        m_pSpr0 = CCSprite::createWithTexture(videoTex);
        if (!m_pSpr0) return false;
        fitSprite(m_pSpr0, m_halfSize, true);       // video ? D0

        m_pSpr1  = makeRTSprite(m_pD0, m_quarterSize); // D0  ? D1
        m_pSpr2  = makeRTSprite(m_pD1, m_eighthSize);  // D1  ? D2
        m_pSprU2 = makeRTSprite(m_pD2, m_quarterSize); // D2  ? U1
        m_pSprU1 = makeRTSprite(m_pU1, m_halfSize);    // U1  ? U0
        m_pSprU0 = makeRTSprite(m_pU0, m_winSize);     // U0  ? Final
        if (!m_pSpr1||!m_pSpr2||!m_pSprU2||!m_pSprU1||!m_pSprU0) return false;

        m_displaySprite = CCSprite::createWithTexture(m_pFinal->getSprite()->getTexture());
        if (!m_displaySprite) return false;

        // halfpixel uniforms: (0.5 / srcDisplaySize) * intensityScale
        float scale = intensity * 4.0f;
        m_hp0x  = (0.5f / m_halfSize.width)     * scale;  m_hp0y  = (0.5f / m_halfSize.height)    * scale;
        m_hp1x  = (0.5f / m_halfSize.width)     * scale;  m_hp1y  = (0.5f / m_halfSize.height)    * scale;
        m_hp2x  = (0.5f / m_quarterSize.width)  * scale;  m_hp2y  = (0.5f / m_quarterSize.height) * scale;
        m_hpu2x = (0.5f / m_eighthSize.width)   * scale;  m_hpu2y = (0.5f / m_eighthSize.height)  * scale;
        m_hpu1x = (0.5f / m_quarterSize.width)  * scale;  m_hpu1y = (0.5f / m_quarterSize.height) * scale;
        m_hpu0x = (0.5f / m_halfSize.width)     * scale;  m_hpu0y = (0.5f / m_halfSize.height)    * scale;

        // Cache uniform locations once (stable for the shader's lifetime).
        m_locKawaseDownHalfpixel = m_blurDown->getUniformLocationForName("u_halfpixel");
        m_locKawaseUpHalfpixel   = m_blurUp->getUniformLocationForName("u_halfpixel");
        return true;
    }

    void onExit() override {
        // Don't unschedule the tick here: onExit() already pauses the scheduler and
        // onEnter() resumes it; unschedule would stop the blur permanently.
        CCNode::onExit();
    }

    // Per-tick rendering (zero allocations), throttled to the video FPS.
    void tick(float dt) {
        if (!m_player || !m_player->isPlaying()) return;
        // Skip rendering while hidden to avoid wasted FBO passes.
        if (!isVisible()) return;
        // Only re-render blur when the video texture has actually changed.
        uint64_t fc = m_player->getFrameCounter();
        if (fc == m_lastFrameCounter) return;
        m_lastFrameCounter = fc;

        // Trigger the FBO resolve so the source sprite reads up-to-date pixels.
        (void)m_player->getResolvedRGBATexture();

        if (m_isPaimon) renderPaimon();
        else            renderGaussian();
    }

    // One Gaussian blur pass (src -> dst), using cached uniform locations.
    void doGauss(CCSprite* src, CCRenderTexture* dst, CCGLProgram* prog,
                 GLint locScreen, GLint locRadius) {
        src->setShaderProgram(prog);
        prog->use();
        if (locScreen != -1) {
            prog->setUniformLocationWith2f(locScreen,
                m_halfSize.width, m_halfSize.height);
        }
        if (locRadius != -1) {
            prog->setUniformLocationWith1f(locRadius, m_gaussRadius);
        }
        dst->begin();
        src->visit();
        dst->end();
    }

    void renderGaussian() {
        if (!m_rtA||!m_rtB||!m_blurH||!m_blurV||!m_srcSprite||!m_midSprite) return;
        doGauss(m_srcSprite, m_rtA, m_blurH, m_locGaussScreenSizeH, m_locGaussRadiusH); // H pass
        doGauss(m_midSprite, m_rtB, m_blurV, m_locGaussScreenSizeV, m_locGaussRadiusV); // V pass
        // m_displaySprite reads rtB texture  auto-updated by RT
    }

    void doDown(CCSprite* src, CCRenderTexture* dst, float hpx, float hpy) {
        src->setShaderProgram(m_blurDown);
        m_blurDown->use();
        if (m_locKawaseDownHalfpixel != -1) {
            m_blurDown->setUniformLocationWith2f(m_locKawaseDownHalfpixel, hpx, hpy);
        }
        dst->begin(); src->visit(); dst->end();
    }

    void doUp(CCSprite* src, CCRenderTexture* dst, float hpx, float hpy) {
        src->setShaderProgram(m_blurUp);
        m_blurUp->use();
        if (m_locKawaseUpHalfpixel != -1) {
            m_blurUp->setUniformLocationWith2f(m_locKawaseUpHalfpixel, hpx, hpy);
        }
        dst->begin(); src->visit(); dst->end();
    }

    void renderPaimon() {
        if (!m_pD0||!m_blurDown||!m_blurUp||!m_pSpr0) return;
        // 3 downsample passes (each halves resolution)
        doDown(m_pSpr0,  m_pD0,    m_hp0x,  m_hp0y);
        doDown(m_pSpr1,  m_pD1,    m_hp1x,  m_hp1y);
        doDown(m_pSpr2,  m_pD2,    m_hp2x,  m_hp2y);
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
        // Tile-based mobile GPUs (Adreno, Mali, PowerVR) need a barrier between the
        // downsample writes and upsample reads; desktop GL syncs FBOs internally.
        glFlush();
#endif
        // 3 upsample passes (each doubles resolution back to full)
        doUp(m_pSprU2, m_pU1,    m_hpu2x, m_hpu2y);
        doUp(m_pSprU1, m_pU0,    m_hpu1x, m_hpu1y);
        doUp(m_pSprU0, m_pFinal, m_hpu0x, m_hpu0y);
        // m_displaySprite reads m_pFinal texture — auto-updated by RT
    }

    ~VideoBlurNode() override = default;
};

// Apply video background
// Apply saved video rotation to a sprite. Call after scale/position are set for
// orientation 0; 90/270 bumps the scale so the rotated video still covers the screen.
static void applyVideoRotation(CCNode* sprite, CCSize const& winSize) {
    if (!sprite) return;
    int rot = paimon::settings::video::videoRotation();
    if (rot == 0) return;
    sprite->setRotation(static_cast<float>(rot));
    // For 90/270 the width maps to screen height, so scale up to keep full coverage.
    if (rot == 90 || rot == 270) {
        float cw = sprite->getContentWidth();
        float ch = sprite->getContentHeight();
        if (cw > 0 && ch > 0) {
            float scX = winSize.width / cw;
            float scY = winSize.height / ch;
            float scXr = winSize.width / ch;
            float scYr = winSize.height / cw;
            sprite->setScale(std::max({scX, scY, scXr, scYr}));
        }
    }
}

void LayerBackgroundManager::applyVideoBg(CCLayer* layer, std::string const& path, LayerBgConfig const& cfg) {
    log::info("[LayerBgMgr] applyVideoBg: path={} dark={}", path, cfg.darkMode);
    bool videoAudio = paimon::settings::video::audioEnabled();

    // Drop our reference to the previous video first so the budget logic in
    // acquireSharedVideo sees an accurate refCount.
    cleanupOldVideoCache(layer, path);
    clearAppliedBackground(layer, videoAudio);

    // Concurrent video backgrounds are allowed; budget enforcement lives in acquireSharedVideo.

    auto winSize = CCDirector::get()->getWinSize();

    auto container = CCNode::create();
    container->setContentSize(winSize);
    container->setPosition({0, 0});
    container->setAnchorPoint({0, 0});
    container->setID("paimon-layerbg-container"_spr);
    container->setZOrder(-10);

    // Add container early so the layer has a placeholder
    layer->addChild(container);

    // "Same As" fast path: reuse a video already playing in another layer (no decoding).
    if (canReuseSharedVideo(path)) {
        auto shared = acquireSharedVideo(path, videoAudio);
        if (shared) {
            // Ensure playback is running; a cached player may be stopped.
            if (!shared->isPlaying()) {
                shared->setLoop(true);
                shared->setTargetFPS(paimon::settings::video::fpsLimit());
                shared->play();
            }

            auto* videoTex = shared->getResolvedRGBATexture();
            if (videoTex) {
                // Try multi-pass blur node; fall back to plain sprite if init fails
                std::string blurType      = paimon::settings::video::videoBlurType();
                float       blurIntensity = paimon::settings::video::videoBlurIntensity();
                CCSprite*   visibleSprite = nullptr;

                if (blurType != "none") {
                    bool isPaimon = (blurType == "paimonblur");
                    if (auto blurNode = VideoBlurNode::create(shared, winSize, blurIntensity, isPaimon)) {
                        blurNode->getDisplaySprite()->setVisible(true);
                        container->addChild(blurNode);
                        visibleSprite = blurNode->getDisplaySprite();
                    }
                }

                if (!visibleSprite) {
                    // If a shader effect is configured, use ShaderBgSprite
                    bool useShader = !cfg.shader.empty() && cfg.shader != "none";
                    CCSprite* rawSprite = nullptr;
                    if (useShader) {
                        auto* shaderSpr = Shaders::ShaderBgSprite::createWithTexture(videoTex);
                        if (shaderSpr) {
                            auto* program = Shaders::getBgShaderProgram(cfg.shader);
                            if (program) {
                                shaderSpr->setShaderProgram(program);
                                shaderSpr->m_shaderIntensity = geode::Mod::get()->getSavedValue<float>("layerbg-shader-intensity", 0.5f);
                                shaderSpr->m_screenW = winSize.width;
                                shaderSpr->m_screenH = winSize.height;
                                shaderSpr->m_shaderTime = 0.f;
                                shaderSpr->schedule(schedule_selector(Shaders::ShaderBgSprite::updateShaderTime));
                            }
                            rawSprite = shaderSpr;
                        }
                    }
                    if (!rawSprite) {
                        rawSprite = CCSprite::createWithTexture(videoTex);
                    }
                    if (rawSprite) {
                        float scX = winSize.width  / rawSprite->getContentWidth();
                        float scY = winSize.height / rawSprite->getContentHeight();
                        rawSprite->setScale(std::max(scX, scY));
                        rawSprite->setPosition(winSize / 2);
                        rawSprite->setAnchorPoint({0.5f, 0.5f});
                        rawSprite->setVisible(true);
                        container->addChild(rawSprite);
                        visibleSprite = rawSprite;
                    }
                }

                if (visibleSprite) {
                    hideOriginalBg(layer);
                    // Apply user's saved video rotation
                    applyVideoRotation(visibleSprite, winSize);
                    addLayerBgDarkOverlay(container, winSize, cfg.darkMode, cfg.darkIntensity);

                    // Video audio setup (same logic as async path)
                    bool didSuspendDynSong = false;
                    bool ownsVideoAudio = false;
                    if (videoAudio) {
                        shared->setVolume(1.0f);

                        if (DynamicSongManager::get()->isActive()) {
                            DynamicSongManager::get()->suspendPlaybackForExternalAudio();
                            didSuspendDynSong = DynamicSongManager::get()->hasSuspendedPlayback();
                        }

                        // No stopAllMusic needed: initAudio() uses playMusic() to replace the track.

                        paimon::setVideoAudioInteropActive(true);
                        shared->fadeAudioIn(0.5f);
                        ownsVideoAudio = true;
                    } else {
                        shared->setVolume(0.0f);
                        paimon::setVideoAudioInteropActive(false);
                    }

                    // Create updateNode AFTER blurNode so its scheduleUpdate() fires after VideoBlurNode
                    auto updateNode = VideoBackgroundUpdateNode::createShared(
                        shared, path, didSuspendDynSong, ownsVideoAudio, visibleSprite);
                    if (updateNode) {
                        updateNode->setID("paimon-video-update"_spr);
                        container->addChild(updateNode);
                    }
                    log::info("[LayerBgMgr] applyVideoBg: instant reuse via Same As for: {}", path);
                    return;
                }
            }

            // Player exists but no frame yet; use lazy creation to avoid blocking the main thread.
            log::info("[LayerBgMgr] Shared video exists but no frame yet, using lazy init");
            std::string blurType      = paimon::settings::video::videoBlurType();
            float       blurIntensity = paimon::settings::video::videoBlurIntensity();

            bool didSuspendDynSong = false;
            bool ownsVideoAudio = false;
            if (videoAudio) {
                shared->setVolume(1.0f);

                if (DynamicSongManager::get()->isActive()) {
                    DynamicSongManager::get()->suspendPlaybackForExternalAudio();
                    didSuspendDynSong = DynamicSongManager::get()->hasSuspendedPlayback();
                }

                paimon::setVideoAudioInteropActive(true);
                shared->fadeAudioIn(0.5f);
                ownsVideoAudio = true;
            } else {
                shared->setVolume(0.0f);
                paimon::setVideoAudioInteropActive(false);
            }

            auto updateNode = VideoBackgroundUpdateNode::createLazyShared(
                shared, path, didSuspendDynSong, ownsVideoAudio);
            if (updateNode) {
                updateNode->setID("paimon-video-update"_spr);
                // Raw pointer capture: m_createVisuals runs only from this node's
                // update(), so 'self' is valid; a Ref<> here would create a cycle.
                auto* self = updateNode;
                self->m_createVisuals = [self, shared, winSize, blurType, blurIntensity, cfg]() {
                    auto* container = self->getParent();
                    if (!container) return; // parent already destroyed, nothing to do
                    CCSprite* visibleSprite = nullptr;
                    if (!blurType.empty() && blurType != "none") {
                        bool isPaimon = (blurType == "paimonblur");
                        if (auto blurNode = VideoBlurNode::create(shared, winSize, blurIntensity, isPaimon)) {
                            blurNode->getDisplaySprite()->setVisible(true);
                            container->addChild(blurNode);
                            visibleSprite = blurNode->getDisplaySprite();
                        }
                    }
                    if (!visibleSprite) {
                        auto* videoTex = shared->getResolvedRGBATexture();
                        // If a shader effect is configured, use ShaderBgSprite
                        bool useShader = !cfg.shader.empty() && cfg.shader != "none";
                        CCSprite* rawSprite = nullptr;
                        if (useShader) {
                            auto* shaderSpr = Shaders::ShaderBgSprite::createWithTexture(videoTex);
                            if (shaderSpr) {
                                auto* program = Shaders::getBgShaderProgram(cfg.shader);
                                if (program) {
                                    shaderSpr->setShaderProgram(program);
                                    shaderSpr->m_shaderIntensity = geode::Mod::get()->getSavedValue<float>("layerbg-shader-intensity", 0.5f);
                                    shaderSpr->m_screenW = winSize.width;
                                    shaderSpr->m_screenH = winSize.height;
                                    shaderSpr->m_shaderTime = 0.f;
                                    shaderSpr->schedule(schedule_selector(Shaders::ShaderBgSprite::updateShaderTime));
                                }
                                rawSprite = shaderSpr;
                            }
                        }
                        if (!rawSprite) {
                            rawSprite = CCSprite::createWithTexture(videoTex);
                        }
                        if (rawSprite) {
                            float scX = winSize.width  / rawSprite->getContentWidth();
                            float scY = winSize.height / rawSprite->getContentHeight();
                            rawSprite->setScale(std::max(scX, scY));
                            rawSprite->setPosition(winSize / 2);
                            rawSprite->setAnchorPoint({0.5f, 0.5f});
                            rawSprite->setVisible(true);
                            container->addChild(rawSprite);
                            visibleSprite = rawSprite;
                        }
                    }
                    if (visibleSprite) addLayerBgDarkOverlay(container, winSize, cfg.darkMode, cfg.darkIntensity);
                    // Remove preview placeholder now that the real video is visible
                    if (auto* preview = container->getChildByID("paimon-video-preview"_spr)) {
                        preview->removeFromParentAndCleanup(true);
                    }
                    if (visibleSprite) {
                        if (auto* parentLayer = typeinfo_cast<CCLayer*>(container->getParent())) {
                            LayerBackgroundManager::get().hideOriginalBg(parentLayer);
                        }
                    }
                    // Link the created sprite back so the update node can fade it in
                    self->m_visibleSprite = visibleSprite;
                    // Apply user's saved video rotation
                    applyVideoRotation(visibleSprite, winSize);
                };
                container->addChild(updateNode);
            }
            return;
        }
    }

    // Preview: show the cached first frame while the decoder initialises (avoids a black flash).
    {
        std::vector<uint8_t> previewPixels;
        int previewW = 0, previewH = 0;
        if (LayerBackgroundManager::loadVideoBgPreview(path, previewPixels, previewW, previewH)) {
            auto* previewTex = new cocos2d::CCTexture2D();
            bool texOk = previewTex->initWithData(
                previewPixels.data(),
                cocos2d::kCCTexture2DPixelFormat_RGBA8888,
                previewW, previewH,
                cocos2d::CCSizeMake(static_cast<float>(previewW), static_cast<float>(previewH)));
            if (texOk) {
                auto* previewSprite = cocos2d::CCSprite::createWithTexture(previewTex);
                previewTex->release(); // CCSprite retains it
                if (previewSprite) {
                    float scX = winSize.width  / previewSprite->getContentWidth();
                    float scY = winSize.height / previewSprite->getContentHeight();
                    previewSprite->setScale(std::max(scX, scY));
                    previewSprite->setPosition(winSize / 2);
                    previewSprite->setAnchorPoint({0.5f, 0.5f});
                    previewSprite->setVisible(true);
                    previewSprite->setID("paimon-video-preview"_spr);
                    applyVideoRotation(previewSprite, winSize);
                    container->addChild(previewSprite);
                    hideOriginalBg(layer);
                    log::info("[LayerBgMgr] Showing video preview frame while decoder loads: {}x{}",
                              previewW, previewH);
                }
            } else {
                previewTex->release();
            }
        }
    }

    LayerBgConfig cfgCopy = cfg;

    auto finishSharedSetup = [path, cfgCopy, videoAudio](CCNode* targetContainer,
                                                         std::shared_ptr<paimon::video::VideoPlayer> const& shared) {
        if (!targetContainer) return;
        if (!shared) {
            log::warn("[LayerBgMgr] applyVideoBg: player creation failed");
            return;
        }

        shared->setLoop(true);
        shared->setTargetFPS(paimon::settings::video::fpsLimit());

        // Start playback first; the decoder needs to produce frames before a texture exists.
        if (!shared->isPlaying()) {
            shared->play();
        }

        // Don't block the main thread; the update node creates the sprite lazily when the frame arrives.
        auto winSize = CCDirector::get()->getWinSize();
        std::string blurType      = paimon::settings::video::videoBlurType();
        float       blurIntensity = paimon::settings::video::videoBlurIntensity();

        bool didSuspendDynSong = false;
        bool ownsVideoAudio = false;
        if (videoAudio) {
            shared->setVolume(1.0f);

            if (DynamicSongManager::get()->isActive()) {
                DynamicSongManager::get()->suspendPlaybackForExternalAudio();
                didSuspendDynSong = DynamicSongManager::get()->hasSuspendedPlayback();
            }

            // No stopAllMusic needed: initAudio() uses playMusic() to replace the track.

            paimon::setVideoAudioInteropActive(true);
            // Fade audio in smoothly instead of instant full volume
            shared->fadeAudioIn(0.5f);
            ownsVideoAudio = true;
        } else {
            shared->setVolume(0.0f);
            paimon::setVideoAudioInteropActive(false);
        }

        auto updateNode = VideoBackgroundUpdateNode::createLazyShared(
            shared, path, didSuspendDynSong, ownsVideoAudio);
        if (updateNode) {
            updateNode->setID("paimon-video-update"_spr);
            // Raw pointer capture: m_createVisuals runs only from this node's
            // update(), so 'self' is valid; a Ref<> here would create a cycle.
            auto* self = updateNode;
            self->m_createVisuals = [self, shared, winSize, blurType, blurIntensity, cfgCopy]() {
                auto* container = self->getParent();
                if (!container) return; // parent already destroyed, nothing to do
                CCSprite* visibleSprite = nullptr;
                if (!blurType.empty() && blurType != "none") {
                    bool isPaimon = (blurType == "paimonblur");
                    if (auto blurNode = VideoBlurNode::create(shared, winSize, blurIntensity, isPaimon)) {
                        blurNode->getDisplaySprite()->setVisible(true);
                        container->addChild(blurNode);
                        visibleSprite = blurNode->getDisplaySprite();
                    }
                }
                if (!visibleSprite) {
                    auto* videoTex = shared->getResolvedRGBATexture();
                    // If a shader effect is configured, use ShaderBgSprite.
                    bool useShader = !cfgCopy.shader.empty() && cfgCopy.shader != "none";
                    CCSprite* rawSprite = nullptr;
                    if (useShader) {
                        auto* shaderSpr = Shaders::ShaderBgSprite::createWithTexture(videoTex);
                        if (shaderSpr) {
                            auto* program = Shaders::getBgShaderProgram(cfgCopy.shader);
                            if (program) {
                                shaderSpr->setShaderProgram(program);
                                shaderSpr->m_shaderIntensity = geode::Mod::get()->getSavedValue<float>("layerbg-shader-intensity", 0.5f);
                                shaderSpr->m_screenW = winSize.width;
                                shaderSpr->m_screenH = winSize.height;
                                shaderSpr->m_shaderTime = 0.f;
                                shaderSpr->schedule(schedule_selector(Shaders::ShaderBgSprite::updateShaderTime));
                            }
                            rawSprite = shaderSpr;
                        }
                    }
                    if (!rawSprite) {
                        rawSprite = CCSprite::createWithTexture(videoTex);
                    }
                    if (rawSprite) {
                        float scX = winSize.width  / rawSprite->getContentWidth();
                        float scY = winSize.height / rawSprite->getContentHeight();
                        rawSprite->setScale(std::max(scX, scY));
                        rawSprite->setPosition(winSize / 2);
                        rawSprite->setAnchorPoint({0.5f, 0.5f});
                        rawSprite->setVisible(true);
                        container->addChild(rawSprite);
                        visibleSprite = rawSprite;
                    }
                }
                if (visibleSprite) addLayerBgDarkOverlay(container, winSize, cfgCopy.darkMode, cfgCopy.darkIntensity);
                // Remove preview placeholder now that the real video is visible
                if (auto* preview = container->getChildByID("paimon-video-preview"_spr)) {
                    preview->removeFromParentAndCleanup(true);
                }
                if (visibleSprite) {
                    if (auto* parentLayer = typeinfo_cast<CCLayer*>(container->getParent())) {
                        LayerBackgroundManager::get().hideOriginalBg(parentLayer);
                    }
                }
                // Link the created sprite back so the update node can fade it in
                self->m_visibleSprite = visibleSprite;
                // Apply user's saved video rotation
                applyVideoRotation(visibleSprite, winSize);
            };
            targetContainer->addChild(updateNode);
        }
    };

#if defined(GEODE_IS_ANDROID)
    // Android: keep setup on the main thread (more stable; decoder creation is cheap here).
    finishSharedSetup(container, LayerBackgroundManager::get().acquireSharedVideo(path, videoAudio));
#else
    // Create the VideoPlayer on a background thread (it may transcode). The
    // container is already owned by its parent layer for the whole async window;
    // we hand a strong Ref to the main-thread continuation via std::move so that
    // cocos2d's non-atomic retain()/release() (and any resulting ~CCNode) only
    // ever run on the main thread — capturing a Ref by copy into the worker
    // lambda would run those refcount ops on this worker thread and race the
    // scene's own refcount ops on the same container.
    Ref<CCNode> containerRef = container;
    CCNode* containerRaw = container;
    auto containerAlive = registerContainerAliveFlag(container);

    paimon::ThreadTracker::get().spawn([containerRef, containerRaw, containerAlive, path, videoAudio, finishSharedSetup]() mutable {
        geode::utils::thread::setName("VideoBg Normalizer");

        // Acquire via shared cache; reuses a player another layer just created.
        auto shared = LayerBackgroundManager::get().acquireSharedVideo(path, videoAudio);

        Loader::get()->queueInMainThread([containerRef = std::move(containerRef), containerRaw, containerAlive, shared, path, finishSharedSetup]() {
            if (g_layerBgShutdown.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) {
                unregisterContainerAliveFlag(containerRaw, containerAlive);
                return;
            }
            // Container was destroyed or removed from the scene graph
            if (!containerAlive->load(std::memory_order_acquire) || !containerRef || !containerRef->getParent()) {
                if (shared) {
                    LayerBackgroundManager::get().releaseSharedVideo(path);
                }
                unregisterContainerAliveFlag(containerRaw, containerAlive);
                return;
            }

            finishSharedSetup(containerRef.data(), shared);

            // Clean up the alive flag; the container is fully set up now.
            unregisterContainerAliveFlag(containerRaw, containerAlive);
        });
    });
#endif
}

void LayerBackgroundManager::clearAppliedBackground(CCLayer* layer, bool suppressAudioResume) {
    if (!layer) return;

    // Cancel any async thumbnail/background callback still targeting this layer.
    clearContainerAliveFlag(layer, nullptr, true);

    if (auto oldContainer = layer->getChildByID("paimon-layerbg-container"_spr)) {
        if (auto updateNode = oldContainer->getChildByID("paimon-video-update"_spr)) {
            static_cast<VideoBackgroundUpdateNode*>(updateNode)->shutdown(false, suppressAudioResume);
        } else if (paimon::isVideoAudioInteropActive()) {
            paimon::setVideoAudioInteropActive(false);
        }

        // Signal async video callbacks that this container is about to be destroyed,
        // so they don't use the dangling pointer.
        clearContainerAliveFlag(oldContainer, nullptr, true);

        oldContainer->removeFromParentAndCleanup(true);
    }

    // Also check MenuLayer's container; a video update node may live in paimon-bg-container.
    if (auto menuContainer = layer->getChildByID("paimon-bg-container"_spr)) {
        if (auto updateNode = menuContainer->getChildByID("paimon-video-update"_spr)) {
            static_cast<VideoBackgroundUpdateNode*>(updateNode)->shutdown(false, suppressAudioResume);
        }
        // Don't remove paimon-bg-container; MenuLayer manages it. Just shut down the video node.
    }
}

// Main API
bool LayerBackgroundManager::applyBackground(CCLayer* layer, std::string const& layerKey) {
    log::info("[LayerBgMgr] applyBackground: layerKey={}", layerKey);
    auto cfg = getConfig(layerKey);

    if (cfg.type == "default") {
        clearAppliedBackground(layer, false);
        forceEvictAllStaleVideos();
        applyVanillaBackgroundTintFix(layer);
        return false;
    }

    // resolve references to other layers (avoid cycles, max 5 hops)
    std::string resolvedPath = cfg.customPath;
    std::string resolvedType = cfg.type;
    LayerBgConfig resolvedCfg = cfg;
    int maxHops = 5;

    while (maxHops-- > 0) {
        if (resolvedType == "menu") {
            // Check unified config first
            LayerBgConfig menuCfg = getConfig("menu");
            if (menuCfg.type != "default") {
                resolvedType = menuCfg.type;
                resolvedPath = menuCfg.customPath;
                resolvedCfg.type = menuCfg.type;
                resolvedCfg.customPath = menuCfg.customPath;
                resolvedCfg.levelId = menuCfg.levelId;
                resolvedCfg.shader = menuCfg.shader;
                // Keep original dark mode / shader settings
                continue; // keep resolving in case menu points to another layer ref
            } else {
                // Fallback to legacy keys (for pre-migration compat)
                std::string menuType = Mod::get()->getSavedValue<std::string>("bg-type", "default");
                if (menuType == "custom") {
                    resolvedPath = Mod::get()->getSavedValue<std::string>("bg-custom-path", "");
                    resolvedType = "custom";
                    resolvedCfg.type = "custom";
                    resolvedCfg.customPath = resolvedPath;
                } else if (menuType == "thumbnails" || menuType == "random") {
                    resolvedType = "random";
                    resolvedCfg.type = "random";
                } else if (menuType == "id") {
                    resolvedType = "id";
                    resolvedCfg.type = "id";
                    resolvedCfg.levelId = Mod::get()->getSavedValue<int>("bg-id", 0);
                } else {
                    return false; // menu is default
                }
                break;
            }
        }

        // reference to another layer? (creator, browser, search, leaderboards)
        bool isLayerRef = false;
        for (auto& [k, n] : LAYER_OPTIONS) {
            if (resolvedType == k) { isLayerRef = true; break; }
        }
        if (isLayerRef) {
            // load the referenced layer's config
            resolvedCfg = getConfig(resolvedType);
            resolvedCfg.darkMode = cfg.darkMode; // keep the original's dark mode
            resolvedCfg.darkIntensity = cfg.darkIntensity;
            if (resolvedCfg.type != "shader") {
                resolvedCfg.shader = cfg.shader;
            }
            resolvedType = resolvedCfg.type;
            resolvedPath = resolvedCfg.customPath;
            if (resolvedType == "default") return false;
            continue;
        }

        break; // concrete type (custom, random, id)
    }

    if (resolvedType == "default") {
        cleanupOldVideoCache(layer, "");
        clearAppliedBackground(layer, false);
        forceEvictAllStaleVideos();
        applyVanillaBackgroundTintFix(layer);
        return false;
    }

    // When switching from video to a non-video type, clean up the old video cache
    if (resolvedType != "video") {
        cleanupOldVideoCache(layer, "");
    }

    clearAppliedBackground(
        layer,
        resolvedType == "video" && paimon::settings::video::audioEnabled());

    // check if GIF
    if (resolvedType == "custom" && !resolvedPath.empty()) {
        auto ext = geode::utils::string::pathToString(std::filesystem::path(resolvedPath).extension());
        for (auto& c : ext) c = (char)std::tolower(c);
        std::error_code ec;
        if (ext == ".gif" && std::filesystem::exists(resolvedPath, ec)) {
            hideOriginalBg(layer);
            applyGifBg(layer, resolvedPath, cfg);
            return true;
        }
    }

    // check if video
    if (resolvedType == "video" && !resolvedPath.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(resolvedPath, ec) && !ec) {
            applyVideoBg(layer, resolvedPath, cfg);
            return true;
        }
        // Video file not found; warn and revert to default gracefully.
        log::warn("[LayerBgMgr] Video file not found: {}  reverting to default", resolvedPath);
        PaimonNotify::show(
            "Video file not found, reverting to default background.",
            geode::NotificationIcon::Warning, 3.0f);
        // Force-release any stale shared video for this path
        forceReleaseSharedVideoByPath(resolvedPath);
        applyVanillaBackgroundTintFix(layer);
        return false;
    }

    if (resolvedType == "shader") {
        applyProceduralShaderBg(layer, resolvedCfg);
        return true;
    }

    auto* tex = loadTextureForConfig(resolvedCfg);
    if (tex) {
        hideOriginalBg(layer);
        applyStaticBg(layer, tex, cfg);
        return true;
    }

    // Texture not in local cache; try an async download for "id" type.
    if (resolvedCfg.type == "id" && resolvedCfg.levelId > 0) {
        Ref<CCLayer> layerRef = layer;
        CCLayer* layerRaw = layer;
        auto layerAlive = registerContainerAliveFlag(layer);
        LayerBgConfig capturedCfg = cfg;
        ThumbnailAPI::get().getThumbnail(resolvedCfg.levelId, [this, layerRef, layerRaw, layerAlive, capturedCfg](bool success, CCTexture2D* dlTex) {
            geode::Ref<CCTexture2D> textureRef = success && dlTex ? geode::Ref<CCTexture2D>(dlTex) : nullptr;
            Loader::get()->queueInMainThread([this, layerRef, layerRaw, layerAlive, capturedCfg, success, textureRef]() {
                auto* liveLayer = layerRef.data();
                if (!liveLayer) {
                    unregisterContainerAliveFlag(layerRaw, layerAlive);
                    return;
                }
                if (!layerAlive || !layerAlive->load(std::memory_order_acquire)) {
                    unregisterContainerAliveFlag(layerRaw, layerAlive);
                    return;
                }
                if (!success || !textureRef || !liveLayer->getParent()) {
                    unregisterContainerAliveFlag(layerRaw, layerAlive);
                    return;
                }
                hideOriginalBg(liveLayer);
                applyStaticBg(liveLayer, textureRef.data(), capturedCfg);
                unregisterContainerAliveFlag(layerRaw, layerAlive);
            });
        });
        return true; // downloading; will be applied async
    }

    return false;
}

// Shared video cache for "Same As" reuse

// Stop a discarded shared player off the main thread: forceStop() blocks for
// seconds on the decoder join, and doing that on the main thread froze/crashed
// the game. Run forceStop() on a worker (MF/D3D only), then queue the GL-owning
// destructor (PBO + CCTexture2D) on the main thread.
static void scheduleSharedVideoTeardown(std::shared_ptr<paimon::video::VideoPlayer> player) {
    if (!player) return;
    paimon::ThreadTracker::get().spawn([player]() mutable {
        geode::utils::thread::setName("VideoBg Teardown");
        // Blocks until the decoder thread is joined or detached; safe on this worker.
        player->forceStop();
        if (g_layerBgShutdown.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) {
            // Late shutdown: keep the stopped player parked until the process
            // exits rather than letting its GL-owning destructor run here.
            parkSharedVideoForShutdown(std::move(player));
            return;
        }
        // Final destruction (texture release, PBO shutdown) must run on the
        // GL thread. The decoder thread is already gone, so ~VideoPlayer's
        // own stopDecoding becomes a no-op and won't block the main thread.
        Loader::get()->queueInMainThread([player]() mutable {
            player.reset();
        });
    });
}

void LayerBackgroundManager::evictExpiredSharedVideos() {
    auto now = std::chrono::steady_clock::now();
    for (auto it = m_sharedVideos.begin(); it != m_sharedVideos.end(); ) {
        if (it->second.refCount <= 0 && it->second.expiry <= now) {
            auto playerToRelease = std::move(it->second.player);
            scheduleSharedVideoTeardown(std::move(playerToRelease));
            log::info("[LayerBgMgr] Evicted expired shared video player: {}", it->first);
            it = m_sharedVideos.erase(it);
        } else {
            ++it;
        }
    }
}

// Multi-video budget helpers

int LayerBackgroundManager::activeVideoCount_locked() const {
    int n = 0;
    for (const auto& [path, entry] : m_sharedVideos) {
        if (entry.refCount > 0 && !entry.stale && entry.player) {
            ++n;
        }
    }
    return n;
}

int LayerBackgroundManager::adaptiveFPSForCount(int activeCount) const {
    int baseFPS = paimon::settings::video::fpsLimit();
    if (baseFPS <= 0) baseFPS = 30;
    if (!paimon::settings::video::adaptiveFPS() || activeCount <= 1) {
        return baseFPS;
    }
    int minFPS = paimon::settings::video::minVideoFPS();
    if (minFPS < 1) minFPS = 1;
    if (minFPS > baseFPS) minFPS = baseFPS;
    int target = baseFPS / activeCount;
    if (target < minFPS) target = minFPS;
    if (target > baseFPS) target = baseFPS;
    return target;
}

void LayerBackgroundManager::rebalanceAdaptiveFPS_locked() {
    int active = activeVideoCount_locked();
    int target = adaptiveFPSForCount(active);
    for (auto& [path, entry] : m_sharedVideos) {
        if (entry.refCount > 0 && !entry.stale && entry.player) {
            entry.player->setTargetFPS(target);
        }
    }
    if (active > 0) {
        log::info("[LayerBgMgr] Adaptive FPS rebalance: {} active videos -> {} fps each",
                  active, target);
    }
}

std::vector<std::shared_ptr<paimon::video::VideoPlayer>>
LayerBackgroundManager::evictLRUForBudget_locked(std::string const& reservedPath,
                                                 int maxConcurrent) {
    std::vector<std::shared_ptr<paimon::video::VideoPlayer>> evicted;
    if (maxConcurrent <= 0) return evicted;

    // Phase 1: drop inactive entries (refCount <= 0) in LRU order until under budget.
    // Never evict reservedPath — it's about to be acquired again.
    auto countTotal = [&]() {
        return static_cast<int>(m_sharedVideos.size());
    };

    while (countTotal() >= maxConcurrent) {
        // Find oldest inactive entry (refCount <= 0, never reservedPath).
        std::string victim;
        std::chrono::steady_clock::time_point oldest =
            std::chrono::steady_clock::time_point::max();
        for (const auto& [p, entry] : m_sharedVideos) {
            if (p == reservedPath) continue;
            if (entry.refCount > 0) continue;
            if (entry.lastUsed < oldest) {
                oldest = entry.lastUsed;
                victim = p;
            }
        }
        if (victim.empty()) break;  // No inactive victim — phase 2.

        auto it = m_sharedVideos.find(victim);
        if (it == m_sharedVideos.end()) break;
        if (it->second.player) {
            evicted.push_back(std::move(it->second.player));
        }
        log::info("[LayerBgMgr] LRU eviction (inactive, budget {} reached): {}",
                  maxConcurrent, victim);
        m_sharedVideos.erase(it);
    }

    // Phase 2 safety valve: if still over budget, evict the LRU active entry
    // (never reservedPath). Normal usage stays in phase 1 thanks to TTL grace.
    while (countTotal() >= maxConcurrent) {
        std::string victim;
        std::chrono::steady_clock::time_point oldest =
            std::chrono::steady_clock::time_point::max();
        for (const auto& [p, entry] : m_sharedVideos) {
            if (p == reservedPath) continue;
            if (entry.lastUsed < oldest) {
                oldest = entry.lastUsed;
                victim = p;
            }
        }
        if (victim.empty()) break;

        auto it = m_sharedVideos.find(victim);
        if (it == m_sharedVideos.end()) break;
        if (it->second.player) {
            evicted.push_back(std::move(it->second.player));
        }
        log::warn("[LayerBgMgr] LRU eviction (ACTIVE, budget {} exceeded): {} (refCount={})",
                  maxConcurrent, victim, it->second.refCount);
        m_sharedVideos.erase(it);
    }

    return evicted;
}

std::shared_ptr<paimon::video::VideoPlayer> LayerBackgroundManager::acquireSharedVideo(
    std::string const& path, bool requireCanonicalAudio) {
    if (path.empty()) return nullptr;
    if (g_layerBgShutdown.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) {
        return nullptr;
    }

    // Fast path: check cache without creating the player (mutex held briefly)
    {
        std::lock_guard lk(m_sharedVideosMutex);
        // Lazily evict expired entries to prevent unbounded growth
        evictExpiredSharedVideos();
        auto it = m_sharedVideos.find(path);
        if (it != m_sharedVideos.end() && it->second.player) {
            // Don't reuse a stale player (decoder may be desynced); evict and recreate.
            if (it->second.stale) {
                auto playerToRelease = std::move(it->second.player);
                log::info("[LayerBgMgr] Discarding stale shared video player: {}", path);
                m_sharedVideos.erase(it);
                scheduleSharedVideoTeardown(std::move(playerToRelease));
            } else {
                // Validate the cached player is healthy; not-playing or no-frame means don't reuse.
                if (!it->second.player->isPlaying() || !it->second.player->hasVisibleFrame()) {
                    log::warn("[LayerBgMgr] Shared video player unhealthy (playing={} visible={}), discarding: {}",
                              it->second.player->isPlaying(), it->second.player->hasVisibleFrame(), path);
                    auto playerToRelease = std::move(it->second.player);
                    m_sharedVideos.erase(it);
                    scheduleSharedVideoTeardown(std::move(playerToRelease));
                } else {
                    if (it->second.player->isTerminal()) {
                        log::warn("[LayerBgMgr] Shared video player became terminal, discarding: {}", path);
                        auto playerToRelease = std::move(it->second.player);
                        m_sharedVideos.erase(it);
                        scheduleSharedVideoTeardown(std::move(playerToRelease));
                        return nullptr;
                    }
                    it->second.refCount++;
                    it->second.expiry = std::chrono::steady_clock::time_point::max();
                    it->second.lastUsed = std::chrono::steady_clock::now();
                    it->second.player->resume();
                    if (!it->second.player->isPlaying()) {
                        log::info("[LayerBgMgr] Resuming shared video playback: {}", path);
                        it->second.player->play();
                    }
                    // Rebalance FPS now that this entry is active again.
                    rebalanceAdaptiveFPS_locked();
                    log::info("[LayerBgMgr] Reusing shared video player: {} (refCount={})",
                              path, it->second.refCount);
                    return it->second.player;
                }
            }
        }

        std::string conflictingPath;
        int conflictingRefs = 0;
        // Concurrent videos are allowed; these vars are kept only for diagnostic logging, not gating.
        (void)conflictingPath;
        (void)conflictingRefs;

        m_pendingSharedVideoCreates[path]++;
    }  // <- Release mutex before the expensive VideoPlayer::create call

    // Slow path: create the player without holding the mutex (create can take seconds on Windows).

    // Hard RAM cap: evict the oldest inactive player if total video RAM exceeds the cap.
    std::shared_ptr<paimon::video::VideoPlayer> evictedPlayer;
    std::vector<std::shared_ptr<paimon::video::VideoPlayer>> lruEvicted;
    {
        std::lock_guard lk(m_sharedVideosMutex);

        // Concurrent-videos budget: enforce maxConcurrentVideos() via LRU eviction
        // before considering RAM. Inactive entries go first, then active ones if needed.
        int maxConcurrent = paimon::settings::video::maxConcurrentVideos();
        if (maxConcurrent > 0) {
            // We are about to (re)insert `path` so reserve a slot for it.
            lruEvicted = evictLRUForBudget_locked(path, maxConcurrent);
        }

#if defined(GEODE_IS_ANDROID)
        static constexpr size_t kMaxVideoRAM = 160ULL * 1024 * 1024;
#elif defined(GEODE_IS_IOS)
        static constexpr size_t kMaxVideoRAM = 256ULL * 1024 * 1024;
#else
        static constexpr size_t kMaxVideoRAM = 512ULL * 1024 * 1024;
#endif
        size_t totalRAM = 0;
        std::string oldestPath;
        std::chrono::steady_clock::time_point oldestExpiry =
            std::chrono::steady_clock::time_point::max();
        for (const auto& [p, entry] : m_sharedVideos) {
            if (entry.player) {
                totalRAM += entry.player->getEstimatedRAMBytes();
                // Track the oldest expired entry for potential eviction
                if (entry.refCount <= 0 && entry.expiry < oldestExpiry) {
                    oldestExpiry = entry.expiry;
                    oldestPath = p;
                }
            }
        }
        if (totalRAM > kMaxVideoRAM && !oldestPath.empty()) {
            auto it = m_sharedVideos.find(oldestPath);
            if (it != m_sharedVideos.end() && it->second.player) {
                evictedPlayer = std::move(it->second.player);
                m_sharedVideos.erase(it);
                log::info("[LayerBgMgr] RAM cap ({:.0f} MB > {:.0f} MB): evicted oldest '{}'",
                          static_cast<float>(totalRAM) / (1024.0f * 1024.0f),
                          static_cast<float>(kMaxVideoRAM) / (1024.0f * 1024.0f),
                          oldestPath);
            }
        }
    }
    // Tear down evicted players outside the lock — scheduleSharedVideoTeardown
    // may take time on Windows (DXVA shutdown).
    for (auto& p : lruEvicted) {
        if (p) scheduleSharedVideoTeardown(std::move(p));
    }
    if (evictedPlayer) {
        scheduleSharedVideoTeardown(std::move(evictedPlayer));
    }

    paimon::video::VideoPlayerCreateOptions playerOptions;
    playerOptions.requireCanonicalAudio = requireCanonicalAudio;
    playerOptions.enableAudio = requireCanonicalAudio;
    // GPU YUV->RGBA resolve: VideoPlayer resolves planes via getResolvedRGBATexture(),
    // so forceRGBA is off (GPU does the conversion instead of CPU SIMD).
    playerOptions.forceRGBA = false;

    auto player = paimon::video::VideoPlayer::create(path, playerOptions);
    if (!player) {
        std::lock_guard lk(m_sharedVideosMutex);
        auto it = m_pendingSharedVideoCreates.find(path);
        if (it != m_pendingSharedVideoCreates.end() && --it->second <= 0) {
            m_pendingSharedVideoCreates.erase(it);
        }
        return nullptr;
    }

    // Re-acquire mutex to insert into the cache (double-checked locking)
    {
        std::lock_guard lk(m_sharedVideosMutex);
        auto pendingIt = m_pendingSharedVideoCreates.find(path);
        if (pendingIt != m_pendingSharedVideoCreates.end() && --pendingIt->second <= 0) {
            m_pendingSharedVideoCreates.erase(pendingIt);
        }

        if (g_layerBgShutdown.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) {
            auto latePlayer = std::shared_ptr<paimon::video::VideoPlayer>(player.release());
            scheduleSharedVideoTeardown(std::move(latePlayer));
            return nullptr;
        }

        // Check again: another thread may have inserted the same path while we worked
        auto it = m_sharedVideos.find(path);
        if (it != m_sharedVideos.end() && it->second.player) {
            // Another thread beat us; use the existing player and discard ours.
            it->second.refCount++;
            it->second.expiry = std::chrono::steady_clock::time_point::max();
            it->second.lastUsed = std::chrono::steady_clock::now();
            auto duplicatePlayer = std::shared_ptr<paimon::video::VideoPlayer>(player.release());
            scheduleSharedVideoTeardown(std::move(duplicatePlayer));
            // Rebalance FPS in case this is now the Nth concurrent video.
            rebalanceAdaptiveFPS_locked();
            log::info("[LayerBgMgr] Reusing shared video player (created concurrently): {} (refCount={})",
                      path, it->second.refCount);
            return it->second.player;
        }

        // Concurrent videos permitted; the RAM/eviction logic above enforced the budget.

        auto shared = std::shared_ptr<paimon::video::VideoPlayer>(player.release());
        SharedVideoEntry entry;
        entry.player = shared;
        entry.refCount = 1;
        entry.expiry = std::chrono::steady_clock::time_point::max();
        entry.lastUsed = std::chrono::steady_clock::now();
        m_sharedVideos[path] = std::move(entry);
        // Apply adaptive FPS to all active players including the brand-new one.
        rebalanceAdaptiveFPS_locked();
        log::info("[LayerBgMgr] Created shared video player: {} (active count now {})",
                  path, activeVideoCount_locked());
        return shared;
    }
}

void LayerBackgroundManager::releaseSharedVideo(std::string const& path) {
    std::shared_ptr<paimon::video::VideoPlayer> playerToHalt;
    {
        std::lock_guard lk(m_sharedVideosMutex);

        auto it = m_sharedVideos.find(path);
        if (it == m_sharedVideos.end()) return;

        it->second.refCount--;
        it->second.lastUsed = std::chrono::steady_clock::now();
        log::info("[LayerBgMgr] Released shared video: {} (refCount={})",
                  path, it->second.refCount);

        if (it->second.refCount <= 0) {
#if defined(GEODE_IS_ANDROID)
            playerToHalt = std::move(it->second.player);
            m_sharedVideos.erase(it);
#else
            // Mark stale so the next acquire creates a fresh player. Don't stop the
            // decoder here: forceStop()/pause() would race a re-acquire or block the
            // main thread mid-transition. The TTL evicts it off-thread instead.
            it->second.stale = true;
            it->second.expiry = std::chrono::steady_clock::now() + kSharedVideoTTL;

            // Free the GPU resolve FBO now; it's dead weight during the TTL window and
            // getResolvedRGBATexture() lazily recreates it on re-acquire.
            if (it->second.player) {
                it->second.player->releaseGPUResolveCache();
            }

            log::info("[LayerBgMgr] Shared video entering TTL grace (stale): {} ({}ms)",
                      path, static_cast<int>(kSharedVideoTTL.count()));
#endif
        }

        // The active count just dropped (or one entry became stale): redistribute
        // FPS so the surviving videos can speed back up.
        rebalanceAdaptiveFPS_locked();

        // Evict any sibling entries whose TTL already expired while we hold the mutex.
        evictExpiredSharedVideos();
    }

    if (playerToHalt) {
        // Never forceStop() on the main thread; AMediaCodec_stop() can block for
        // seconds on some drivers. Use the same async teardown as Windows/macOS.
        scheduleSharedVideoTeardown(std::move(playerToHalt));
        log::info("[LayerBgMgr] Android: scheduled async video teardown for: {}", path);
    }
}

void LayerBackgroundManager::releaseAllSharedVideos() {
    g_layerBgShutdown.store(true, std::memory_order_release);
    std::vector<std::shared_ptr<paimon::video::VideoPlayer>> playersToRelease;
    size_t count = 0;
    {
        std::lock_guard lk(m_sharedVideosMutex);
        m_pendingSharedVideoCreates.clear();
        for (auto& [path, entry] : m_sharedVideos) {
            if (entry.player) {
                playersToRelease.push_back(std::move(entry.player));
                ++count;
            }
        }
        m_sharedVideos.clear();
    }
    for (auto& player : playersToRelease) {
        if (player) {
            // Non-blocking shutdown: the worker stops decoding and parks the player
            // until process exit instead of running the GL-owning destructor off-thread.
            scheduleSharedVideoTeardown(std::move(player));
        }
    }
    for (auto& parked : takeParkedSharedVideos()) {
        parked.reset();
    }
    if (count > 0) {
        log::info("[LayerBgMgr] Released all {} shared video players during shutdown", count);
    }
}

void LayerBackgroundManager::forceReleaseSharedVideoByPath(std::string const& path) {
    if (path.empty()) return;

    std::shared_ptr<paimon::video::VideoPlayer> playerToRelease;
    {
        std::lock_guard lk(m_sharedVideosMutex);
        auto it = m_sharedVideos.find(path);
        if (it == m_sharedVideos.end()) return;

        // Force refCount to 0 and evict immediately regardless of TTL
        playerToRelease = std::move(it->second.player);
        log::info("[LayerBgMgr] Force-releasing shared video: {} (was refCount={})",
                  path, it->second.refCount);
        m_sharedVideos.erase(it);
    }

    if (playerToRelease) {
        scheduleSharedVideoTeardown(std::move(playerToRelease));
    }
}

void LayerBackgroundManager::forceEvictAllStaleVideos() {
    std::vector<std::shared_ptr<paimon::video::VideoPlayer>> playersToRelease;
    {
        std::lock_guard lk(m_sharedVideosMutex);
        for (auto it = m_sharedVideos.begin(); it != m_sharedVideos.end(); ) {
            if (it->second.refCount <= 0 || it->second.stale) {
                if (it->second.player) {
                    playersToRelease.push_back(std::move(it->second.player));
                }
                log::info("[LayerBgMgr] Force-evicting stale/unreferenced video: {}", it->first);
                it = m_sharedVideos.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& player : playersToRelease) {
        if (player) {
            scheduleSharedVideoTeardown(std::move(player));
        }
    }
}

void LayerBackgroundManager::releaseAllVideoAudio() {
    // Snapshot players under the lock, then act outside it (fadeAudioOut may re-enter the scheduler).
    std::vector<std::shared_ptr<paimon::video::VideoPlayer>> players;
    {
        std::lock_guard lk(m_sharedVideosMutex);
        players.reserve(m_sharedVideos.size());
        for (auto& [path, entry] : m_sharedVideos) {
            if (entry.player) {
                players.push_back(entry.player);
            }
        }
    }

    bool anyHadAudio = false;
    for (auto& p : players) {
        if (!p) continue;
        if (p->hasAudio() && p->isAudioPlaying()) {
            anyHadAudio = true;
            // Synchronous stop: closes the FMOD bg channel inline, no async ramp.
            p->fadeAudioOut(0.0f);
        }
    }

    // Always reset the flag via the public setter; a prior shutdown may have left it set.
    if (anyHadAudio || paimon::isVideoAudioInteropActive()) {
        paimon::setVideoAudioInteropActive(false);
    }

    // Resume any dynamic song suspended for video audio now that the channel is free.
    if (DynamicSongManager::get()->hasSuspendedPlayback()) {
        DynamicSongManager::get()->resumeSuspendedPlayback();
    }
}

bool LayerBackgroundManager::hasSharedVideo(std::string const& path) const {
    std::lock_guard lk(m_sharedVideosMutex);
    auto it = m_sharedVideos.find(path);
    return it != m_sharedVideos.end() && it->second.player;
}

bool LayerBackgroundManager::canReuseSharedVideo(std::string const& path) const {
    std::lock_guard lk(m_sharedVideosMutex);
    auto it = m_sharedVideos.find(path);
    return it != m_sharedVideos.end() &&
           it->second.player &&
           !it->second.stale &&
           it->second.player->hasVisibleFrame();
}

void LayerBackgroundManager::cleanupOldVideoCache(cocos2d::CCLayer* layer, std::string const& nextVideoPath) {
    if (!layer) return;

    std::string oldVideoPath;
    // Check the standard container used by applyVideoBg
    if (auto oldContainer = layer->getChildByID("paimon-layerbg-container"_spr)) {
        if (auto updateNode = oldContainer->getChildByID("paimon-video-update"_spr)) {
            auto* vbn = static_cast<VideoBackgroundUpdateNode*>(updateNode);
            oldVideoPath = vbn->m_videoPath;
            // Fallback: get path from the player itself (non-shared players)
            if (oldVideoPath.empty()) {
                auto* p = vbn->getPlayer();
                if (p) oldVideoPath = p->getFilePath();
            }
        }
    }
    // Also check MenuLayer's custom container (paimon-bg-container) which may
    // contain a video update node if the video was applied through MenuLayer's path
    if (oldVideoPath.empty()) {
        if (auto menuContainer = layer->getChildByID("paimon-bg-container"_spr)) {
            if (auto updateNode = menuContainer->getChildByID("paimon-video-update"_spr)) {
                auto* vbn = static_cast<VideoBackgroundUpdateNode*>(updateNode);
                oldVideoPath = vbn->m_videoPath;
                if (oldVideoPath.empty()) {
                    auto* p = vbn->getPlayer();
                    if (p) oldVideoPath = p->getFilePath();
                }
            }
        }
    }

    if (!oldVideoPath.empty() && oldVideoPath != nextVideoPath) {
        std::string normalizedOld = geode::utils::string::replace(oldVideoPath, "\\", "/");
        bool stillConfigured = false;
        for (auto const& [key, name] : LAYER_OPTIONS) {
            (void)name;
            auto resolved = resolveConfig(key);
            if (resolved.type != "video" || resolved.customPath.empty()) continue;
            auto normalizedCfg = geode::utils::string::replace(resolved.customPath, "\\", "/");
            if (normalizedCfg == normalizedOld) {
                stillConfigured = true;
                break;
            }
        }
        if (stillConfigured) return;
        paimon::video::VideoDiskCache::deleteCache(oldVideoPath);
    }
}

size_t LayerBackgroundManager::getTotalVideoRAMBytes() const {
    std::lock_guard lk(m_sharedVideosMutex);
    size_t total = 0;
    for (const auto& [path, entry] : m_sharedVideos) {
        if (entry.player) {
            total += entry.player->getEstimatedRAMBytes();
        }
    }
    return total;
}

void LayerBackgroundManager::broadcastFPSUpdate(int newFPS) {
    std::lock_guard lk(m_sharedVideosMutex);
    for (auto& [path, entry] : m_sharedVideos) {
        if (entry.player) {
            entry.player->setTargetFPS(newFPS);
            // Invalidate cache built at the old FPS; next playback rebuilds at the new rate.
            paimon::video::VideoDiskCache::deleteCache(path);
            log::info("[LayerBgMgr] broadcastFPSUpdate: {} fps ? {} (cache invalidated)", newFPS, path);
        }
    }
}

void LayerBackgroundManager::broadcastRotationUpdate(int newRotationDegrees) {
    // Rotate the video sprite (first visual child), not the container, which also
    // holds the dark overlay and update node.
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene) return;

    auto winSize = CCDirector::get()->getWinSize();

    // Walk all layers in the scene looking for our container
    for (auto* sceneChild : CCArrayExt<CCNode*>(scene->getChildren())) {
        auto* container = sceneChild->getChildByID("paimon-layerbg-container"_spr);
        if (!container) {
            // Also check if the sceneChild IS the container (e.g. during transitions)
            for (auto* layerChild : CCArrayExt<CCNode*>(sceneChild->getChildren())) {
                container = layerChild->getChildByID("paimon-layerbg-container"_spr);
                if (container) break;
            }
        }
        if (!container) continue;

        // Apply rotation to the first visual child (sprite or blur node)
        for (auto* child : CCArrayExt<CCNode*>(container->getChildren())) {
            // Skip the update node and dark overlay
            if (child->getID() == "paimon-video-update"_spr) continue;
            if (typeinfo_cast<CCLayerColor*>(child)) continue;
            if (child->getID() == "paimon-video-preview"_spr) continue;

            child->setRotation(static_cast<float>(newRotationDegrees));

            // For 90 and 270 degrees, scale up to cover the screen because the
            // video's width now maps to screen height and vice versa
            auto cw = child->getContentWidth();
            auto ch = child->getContentHeight();
            if (cw > 0 && ch > 0) {
                if (newRotationDegrees == 90 || newRotationDegrees == 270) {
                    float scX = winSize.width / cw;
                    float scY = winSize.height / ch;
                    float scXr = winSize.width / ch;
                    float scYr = winSize.height / cw;
                    child->setScale(std::max({scX, scY, scXr, scYr}));
                } else {
                    float scX = winSize.width / cw;
                    float scY = winSize.height / ch;
                    child->setScale(std::max(scX, scY));
                }
            }
            break; // only the first visual child
        }
    }

    log::info("[LayerBgMgr] broadcastRotationUpdate: {}�", newRotationDegrees);
}


