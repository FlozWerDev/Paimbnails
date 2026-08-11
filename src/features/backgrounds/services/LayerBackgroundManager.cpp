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
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../dynamic-songs/services/DynamicSongManager.hpp"
#include "../../../utils/AudioInterop.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "LayerBackgroundManager.hpp"
#include <Geode/utils/random.hpp>
#include <filesystem>
#include <fstream>
#include <thread>
#include <atomic>
#include <cstring>
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

// Cache custom textures by path, mtime, and size; keep the cache alive through GL shutdown.
struct CustomBgCacheEntry {
    geode::Ref<CCTexture2D> texture;
    std::filesystem::file_time_type mtime{};
    uintmax_t fileSize = 0;
    uint64_t lastUse = 0;
};

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

// Heap-owned so GL-context reload can reset it without static destruction.
Ref<CCTexture2D>& proceduralBaseTextureSlot() {
    static auto* s_texture = new Ref<CCTexture2D>();
    return *s_texture;
}

CCTexture2D* createProceduralBaseTexture() {
    auto& slot = proceduralBaseTextureSlot();
    if (slot) {
        return slot.data();
    }

    unsigned char whitePixel[4] = {255, 255, 255, 255};
    auto* tex = new CCTexture2D();
    if (!tex->initWithData(whitePixel, kCCTexture2DPixelFormat_RGBA8888, 1, 1, CCSizeMake(1.f, 1.f))) {
        tex->release();
        return nullptr;
    }
    tex->autorelease();
    slot = tex;
    return slot.data();
}

// Async video callbacks check this flag before touching a container.
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

// ---- Video background poster frames -------------------------------------
//
// The poster frame only ever fills the screen while the decoder warms up, so it
// is stored downscaled: a full-res RGBA blob costs ~8 MB per 1080p video, and
// reading/uploading that on the main thread stalls the layer transition.

// Longest edge of a stored poster frame.
constexpr int kVideoPreviewMaxDim = 512;

// Delay between the first visible frame and the poster-frame readback.
constexpr float kPreviewCaptureDelay = 1.5f;

// Header tag; also invalidates the old headerless full-res format.
constexpr char kVideoPreviewMagic[] = "PAIMPV02";

std::unordered_map<std::string, Ref<CCTexture2D>>& videoPreviewCache() {
    static auto* s_cache = new std::unordered_map<std::string, Ref<CCTexture2D>>();
    return *s_cache;
}

bool videoPreviewIsFresh(std::string const& videoPath,
                         std::filesystem::path const& previewPath) {
    std::error_code ec;
    auto previewTime = std::filesystem::last_write_time(previewPath, ec);
    if (ec) return false;
    auto sourceTime = std::filesystem::last_write_time(paimon::assets::pathFromUtf8(videoPath), ec);
    return ec || sourceTime <= previewTime;
}

bool loadVideoPreviewFile(std::filesystem::path const& previewPath,
                          std::string const& videoPath,
                          std::vector<uint8_t>& outPixels, int& outW, int& outH) {
    if (!videoPreviewIsFresh(videoPath, previewPath)) return false;

    std::ifstream f(previewPath, std::ios::binary);
    if (!f) return false;

    char magic[sizeof(kVideoPreviewMagic) - 1] = {};
    f.read(magic, sizeof(magic));
    if (!f || std::memcmp(magic, kVideoPreviewMagic, sizeof(magic)) != 0) return false;

    uint32_t w = 0, h = 0;
    f.read(reinterpret_cast<char*>(&w), sizeof(w));
    f.read(reinterpret_cast<char*>(&h), sizeof(h));
    if (!f || w == 0 || h == 0 || w > 8192 || h > 8192) return false;

    outPixels.resize(static_cast<size_t>(w) * h * 4);
    f.read(reinterpret_cast<char*>(outPixels.data()),
           static_cast<std::streamsize>(outPixels.size()));
    if (!f.good()) return false;

    outW = static_cast<int>(w);
    outH = static_cast<int>(h);
    return true;
}

// Box-average by an integer factor; returns src unchanged when it already fits.
std::vector<uint8_t> downscaleRGBA(std::vector<uint8_t> const& src, int w, int h,
                                   int maxDim, int& outW, int& outH) {
    int factor = (std::max(w, h) + maxDim - 1) / maxDim;
    if (factor <= 1 || w / factor < 1 || h / factor < 1) {
        outW = w;
        outH = h;
        return src;
    }

    outW = w / factor;
    outH = h / factor;

    std::vector<uint8_t> dst(static_cast<size_t>(outW) * outH * 4);
    uint32_t const samples = static_cast<uint32_t>(factor) * factor;

    for (int y = 0; y < outH; ++y) {
        for (int x = 0; x < outW; ++x) {
            uint32_t acc[4] = {0, 0, 0, 0};
            for (int sy = 0; sy < factor; ++sy) {
                uint8_t const* row = src.data() +
                    (static_cast<size_t>(y * factor + sy) * w + x * factor) * 4;
                for (int sx = 0; sx < factor; ++sx) {
                    acc[0] += row[sx * 4 + 0];
                    acc[1] += row[sx * 4 + 1];
                    acc[2] += row[sx * 4 + 2];
                    acc[3] += row[sx * 4 + 3];
                }
            }
            uint8_t* out = dst.data() + (static_cast<size_t>(y) * outW + x) * 4;
            out[0] = static_cast<uint8_t>(acc[0] / samples);
            out[1] = static_cast<uint8_t>(acc[1] / samples);
            out[2] = static_cast<uint8_t>(acc[2] / samples);
            out[3] = static_cast<uint8_t>(acc[3] / samples);
        }
    }
    return dst;
}

struct VideoBackgroundUpdateNode : public CCNode {
    std::unique_ptr<paimon::video::VideoPlayer> player;
    std::shared_ptr<paimon::video::VideoPlayer> sharedPlayer;
    Ref<CCSprite> m_visibleSprite = nullptr;
    bool m_didSuspendDynSong = false;
    bool m_ownsVideoAudioFlag = false;
    bool m_suppressResume = false;
    bool m_shutdown = false;
    bool m_firstVisibleFrameShown = false;
    bool m_previewSaved = false;
    // Seconds left before capturing the poster frame; 0 means nothing pending.
    float m_previewCaptureCountdown = 0.f;
    bool m_audioFadeOutPending = false;
    std::string m_videoPath;
    uint64_t m_lastResolvedFrame = 0;

    bool m_pausedForVisibility = false;

    bool m_lazyCreate = false;
    CCSize m_lazyWinSize;
    std::string m_lazyBlurType;
    float m_lazyBlurIntensity = 0.f;
    bool m_lazyDarkMode = false;
    float m_lazyDarkIntensity = 0.5f;
    std::function<void()> m_createVisuals;

    static float computeTickInterval() {
        int fps = paimon::settings::video::fpsLimit();
        if (fps < 1) fps = 1;
        if (fps > 240) fps = 240;
        return 1.0f / static_cast<float>(fps);
    }

    void scheduleVideoTick() {
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
        // Reparenting can call onExit() temporarily.
        CCNode::onExit();
    }

    paimon::video::VideoPlayer* getPlayer() {
        return player ? player.get() : sharedPlayer.get();
    }

    void shutdown(bool duringSceneTeardown, bool suppressResume) {
        if (m_shutdown) return;
        m_shutdown = true;
        m_suppressResume = suppressResume;
        this->unschedule(schedule_selector(VideoBackgroundUpdateNode::tick));

        auto* p = getPlayer();
        bool doingSmoothFadeOut = false;
        if (p && m_ownsVideoAudioFlag && p->hasAudio() && p->isAudioPlaying()) {
            if (duringSceneTeardown) {
                p->fadeAudioOut(0.0f);
                paimon::setVideoAudioInteropActive(false);
                m_ownsVideoAudioFlag = false;
                if (DynamicSongManager::get()->hasSuspendedPlayback()) {
                    DynamicSongManager::get()->resumeSuspendedPlayback();
                    m_didSuspendDynSong = false;
                }
            } else {
                p->fadeAudioOut(0.5f, []() {
                    paimon::setVideoAudioInteropActive(false);
                    if (DynamicSongManager::get()->hasSuspendedPlayback()) {
                        DynamicSongManager::get()->resumeSuspendedPlayback();
                    }
                });
                m_ownsVideoAudioFlag = false;
                m_didSuspendDynSong = false;
                doingSmoothFadeOut = true;
            }
        }

        bool keepUniquePlayerAliveForFade = false;
        if (player) {
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
            if (!m_videoPath.empty()) {
                LayerBackgroundManager::get().releaseSharedVideo(m_videoPath);
            }
            sharedPlayer.reset();
        }

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

    bool isPipelineVisible() {
        CCNode* n = this;
        while (n) {
            if (!n->isVisible()) return false;
            n = n->getParent();
        }
        return true;
    }

    void tick(float dt) {
        if (m_shutdown) return;

        auto* p = getPlayer();
        if (!p) return;

        bool visible = isPipelineVisible();
        if (!visible) {
            if (player && player->isPlaying() && !m_pausedForVisibility) {
                player->pause();
                m_pausedForVisibility = true;
            }
            return;
        } else if (m_pausedForVisibility) {
            if (player && !player->isPlaying()) {
                player->resume();
            }
            m_pausedForVisibility = false;
        }

        if (p->isPlaying()) {
            p->update(dt);

            // Restore game audio if video audio initialization fails.
            if (m_ownsVideoAudioFlag && p->didAudioInitFail()) {
                log::warn("[VideoBg] Video audio init failed - restoring game music");
                paimon::setVideoAudioInteropActive(false);
                m_ownsVideoAudioFlag = false;

                if (m_didSuspendDynSong && DynamicSongManager::get()->hasSuspendedPlayback()) {
                    DynamicSongManager::get()->resumeSuspendedPlayback();
                    m_didSuspendDynSong = false;
                } else {
                    GameManager::get()->fadeInMenuMusic();
                }
            }

            if (m_lazyCreate && !m_visibleSprite && p->hasVisibleFrame()) {
                m_lazyCreate = false;
                if (m_createVisuals) {
                    m_createVisuals();
                }
            }

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

                // Capturing the poster frame costs a full GPU readback, so it
                // waits until playback has settled instead of landing on the
                // very first frames.
                if (!m_previewSaved && !m_videoPath.empty()
                    && !LayerBackgroundManager::hasVideoBgPreview(m_videoPath)) {
                    m_previewCaptureCountdown = kPreviewCaptureDelay;
                }
            }

            if (m_previewCaptureCountdown > 0.f) {
                m_previewCaptureCountdown -= dt;
                if (m_previewCaptureCountdown <= 0.f) {
                    m_previewCaptureCountdown = 0.f;
                    m_previewSaved = true;
                    LayerBackgroundManager::saveVideoBgPreview(m_videoPath, p);
                }
            }
        }
    }

    ~VideoBackgroundUpdateNode() override {
        // Avoid scene-flag access while CCNode teardown runs.
        paimon::InteropSceneTeardownScope teardownGuard;
        shutdown(true, m_suppressResume);
    }
};

}

std::filesystem::path LayerBackgroundManager::getVideoBgPreviewDir() {
    return geode::Mod::get()->getSaveDir() / "bg_previews";
}

std::filesystem::path LayerBackgroundManager::getVideoBgPreviewPath(std::string const& videoPath) {
    size_t h = std::hash<std::string>{}(videoPath);
    return getVideoBgPreviewDir() / (std::to_string(h) + ".bin");
}

bool LayerBackgroundManager::hasVideoBgPreview(std::string const& videoPath) {
    return videoPreviewIsFresh(videoPath, getVideoBgPreviewPath(videoPath));
}

CCTexture2D* LayerBackgroundManager::getVideoBgPreviewTexture(std::string const& videoPath) {
    if (videoPath.empty()) return nullptr;

    auto& cache = videoPreviewCache();
    if (auto it = cache.find(videoPath); it != cache.end()) {
        return it->second.data();
    }

    // Remember misses too, so a layer without a cached poster frame does not
    // stat the disk on every entry.
    Ref<CCTexture2D> texture = nullptr;

    std::vector<uint8_t> pixels;
    int w = 0, h = 0;
    if (loadVideoPreviewFile(getVideoBgPreviewPath(videoPath), videoPath, pixels, w, h)) {
        auto* tex = new (std::nothrow) CCTexture2D();
        if (tex) {
            bool ok = tex->initWithData(
                pixels.data(), kCCTexture2DPixelFormat_RGBA8888, w, h,
                CCSizeMake(static_cast<float>(w), static_cast<float>(h)));
            if (ok) texture = tex;
            tex->release();
        }
    }

    cache[videoPath] = texture;
    return texture.data();
}

void LayerBackgroundManager::saveVideoBgPreview(std::string const& videoPath,
                                                paimon::video::VideoPlayer const* player) {
    if (!player || videoPath.empty()) return;

    auto previewPath = getVideoBgPreviewPath(videoPath);
    // A fresh preview means the GPU readback below can be skipped entirely.
    if (videoPreviewIsFresh(videoPath, previewPath)) return;

    std::vector<uint8_t> pixels;
    int w = 0, h = 0;
    if (!player->copyCurrentFramePixels(pixels, w, h)) return;
    if (pixels.empty() || w <= 0 || h <= 0) return;

    auto dir = getVideoBgPreviewDir();
    bool spawned = paimon::ThreadTracker::get().spawn(
        [previewPath, dir, videoPath, pixels = std::move(pixels), w, h]() mutable {
            geode::utils::thread::setName("VideoBg Preview Save");

            int outW = w, outH = h;
            auto scaled = downscaleRGBA(pixels, w, h, kVideoPreviewMaxDim, outW, outH);

            std::error_code ec;
            std::filesystem::create_directories(dir, ec);

            {
                std::ofstream f(previewPath, std::ios::binary | std::ios::trunc);
                if (!f) return;
                uint32_t uw = static_cast<uint32_t>(outW);
                uint32_t uh = static_cast<uint32_t>(outH);
                f.write(kVideoPreviewMagic, sizeof(kVideoPreviewMagic) - 1);
                f.write(reinterpret_cast<const char*>(&uw), sizeof(uw));
                f.write(reinterpret_cast<const char*>(&uh), sizeof(uh));
                f.write(reinterpret_cast<const char*>(scaled.data()),
                        static_cast<std::streamsize>(scaled.size()));
                if (!f.good()) return;
            }

            log::info("[LayerBgMgr] Saved video background preview: {}x{} (from {}x{})",
                      outW, outH, w, h);

            // Let the next layer entry pick up the freshly written file.
            geode::Loader::get()->queueInMainThread([videoPath]() {
                videoPreviewCache().erase(videoPath);
            });
        });

    if (!spawned) {
        log::debug("[LayerBgMgr] Preview save skipped: thread spawn rejected");
    }
}

LayerBackgroundManager& LayerBackgroundManager::get() {
    // Detached video teardown may still be active at process exit.
    static auto* s_instance = new LayerBackgroundManager();
    return *s_instance;
}

void LayerBackgroundManager::onGLContextReload() {
    customBgCache().clear();
    videoPreviewCache().clear();
    proceduralBaseTextureSlot() = nullptr;
}

LayerBgConfig LayerBackgroundManager::getConfig(std::string const& key) const {
    {
        std::lock_guard<std::mutex> lock(m_configCacheMutex);
        auto it = m_configCache.find(key);
        if (it != m_configCache.end()) return it->second;
    }

    LayerBgConfig cfg;
    auto* mod = Mod::get();
    std::string const prefix = "layerbg-" + key + "-";
    cfg.type          = mod->getSavedValue<std::string>(prefix + "type", "default");
    cfg.customPath    = mod->getSavedValue<std::string>(prefix + "path", "");
    cfg.levelId       = mod->getSavedValue<int>(prefix + "id", 0);
    cfg.darkMode      = mod->getSavedValue<bool>(prefix + "dark", false);
    cfg.darkIntensity = mod->getSavedValue<float>(prefix + "dark-intensity", 0.5f);
    cfg.shader        = mod->getSavedValue<std::string>(prefix + "shader", "none");

    {
        std::lock_guard<std::mutex> lock(m_configCacheMutex);
        m_configCache[key] = cfg;
    }
    return cfg;
}

void LayerBackgroundManager::saveConfig(std::string const& key, LayerBgConfig const& cfg) {
    log::info("[LayerBgMgr] saveConfig: key={} type={}", key, cfg.type);
    {
        std::lock_guard<std::mutex> lock(m_configCacheMutex);
        m_configCache.erase(key);
    }
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
            resolvedType = refCfg.type;
            if (resolvedType == "default") return resolvedCfg;
            continue;
        }
        break;
    }
    return resolvedCfg;
}

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

LayerMusicConfig LayerBackgroundManager::getGlobalMusicConfig() const {
    return getMusicConfig("global");
}

void LayerBackgroundManager::saveGlobalMusicConfig(LayerMusicConfig const& cfg) {
    saveMusicConfig("global", cfg);
}

void LayerBackgroundManager::migrateFromLegacy() {
    if (Mod::get()->getSavedValue<bool>("layerbg-migrated-v2", false)) return;

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

    std::string profileType = Mod::get()->getSavedValue<std::string>("profile-bg-type", "");
    if (!profileType.empty() && profileType != "none") {
        LayerBgConfig profileCfg;
        profileCfg.type = profileType;
        profileCfg.customPath = Mod::get()->getSavedValue<std::string>("profile-bg-path", "");
        saveConfig("profile", profileCfg);
    }

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

void LayerBackgroundManager::hideOriginalBg(CCLayer* layer) {
    static char const* bgNodeIDs[] = {
        "main-menu-bg",
        "background",
        nullptr
    };

    for (int i = 0; bgNodeIDs[i]; i++) {
        if (auto bg = layer->getChildByID(bgNodeIDs[i])) {
            bg->setVisible(false);
        }
    }

    if (auto children = layer->getChildren()) {
        auto ws = CCDirector::get()->getWinSize();
        bool foundByID = false;
        for (int j = 0; bgNodeIDs[j]; j++) {
            if (layer->getChildByID(bgNodeIDs[j])) { foundByID = true; break; }
        }
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

void LayerBackgroundManager::showOriginalBg(CCLayer* layer) {
    if (!layer) return;

    static char const* bgNodeIDs[] = {
        "main-menu-bg",
        "background",
        nullptr
    };

    bool foundByID = false;
    for (int i = 0; bgNodeIDs[i]; ++i) {
        if (auto* bg = layer->getChildByID(bgNodeIDs[i])) {
            bg->setVisible(true);
            foundByID = true;
        }
    }
    if (foundByID) return;

    auto* children = layer->getChildren();
    if (!children) return;

    auto winSize = CCDirector::get()->getWinSize();
    for (auto* child : CCArrayExt<CCNode*>(children)) {
        if (!child || std::string(child->getID()).rfind("paimon-", 0) == 0) continue;
        auto* sprite = typeinfo_cast<CCSprite*>(child);
        if (!sprite || !sprite->getTexture()) continue;
        auto size = sprite->getContentSize();
        if (size.width >= winSize.width * 0.5f && size.height >= winSize.height * 0.5f) {
            sprite->setVisible(true);
            return;
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

CCTexture2D* LayerBackgroundManager::loadTextureForConfig(LayerBgConfig const& cfg) {
    log::debug("[LayerBgMgr] loadTextureForConfig: type={} id={}", cfg.type, cfg.levelId);
    if (cfg.type == "custom" && !cfg.customPath.empty()) {
        std::error_code ec;
        auto normalizedPath = paimon::assets::normalizePath(cfg.customPath);
        if (std::filesystem::exists(normalizedPath, ec)) {
            auto ext = geode::utils::string::pathToString(normalizedPath.extension());
            for (auto& c : ext) c = (char)std::tolower(c);
            if (ext == ".gif") return nullptr;

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
            return LocalThumbs::get().loadTexture(geode::utils::random::choice(ids));
        }
    } else if (cfg.type == "menu") {
        LayerBgConfig menuCfg = getConfig("menu");
        if (menuCfg.type == "default") {
            std::string menuType = Mod::get()->getSavedValue<std::string>("bg-type", "default");
            if (menuType == "default" || menuType.empty()) return nullptr;
            menuCfg.type = (menuType == "thumbnails") ? "random" : menuType;
            menuCfg.customPath = Mod::get()->getSavedValue<std::string>("bg-custom-path", "");
            menuCfg.levelId = Mod::get()->getSavedValue<int>("bg-id", 0);
        }
        menuCfg.darkMode = cfg.darkMode;
        menuCfg.darkIntensity = cfg.darkIntensity;
        return loadTextureForConfig(menuCfg);
    }
    return nullptr;
}

bool LayerBackgroundManager::applyStaticBg(CCLayer* layer, CCTexture2D* tex, LayerBgConfig const& cfg) {
    log::debug("[LayerBgMgr] applyStaticBg: dark={} shader={}", cfg.darkMode, cfg.shader);
    if (!layer || !tex) return false;
    auto winSize = CCDirector::get()->getWinSize();

    auto container = CCNode::create();
    container->setContentSize(winSize);
    container->setPosition({0, 0});
    container->setAnchorPoint({0, 0});
    container->setID("paimon-layerbg-container"_spr);
    container->setZOrder(-10);

    bool useShader = !cfg.shader.empty() && cfg.shader != "none";
    CCSprite* sprite = nullptr;

    if (useShader) {
        auto shaderSpr = ShaderBgSprite::createWithTexture(tex);
        if (!shaderSpr) return false;

        auto* program = getBgShaderProgram(cfg.shader);
        if (program) {
            shaderSpr->setShaderProgram(program);
            shaderSpr->m_shaderIntensity = geode::Mod::get()->getSavedValue<float>("layerbg-shader-intensity", 0.5f);
            shaderSpr->m_screenW = winSize.width;
            shaderSpr->m_screenH = winSize.height;
            shaderSpr->m_shaderTime = 0.f;
            shaderSpr->schedule(schedule_selector(ShaderBgSprite::updateShaderTime));
        }

        sprite = shaderSpr;
    } else {
        sprite = CCSprite::createWithTexture(tex);
        if (!sprite) return false;
    }

    if (sprite->getContentWidth() <= 0 || sprite->getContentHeight() <= 0) return false;

    float scX = winSize.width / sprite->getContentWidth();
    float scY = winSize.height / sprite->getContentHeight();
    sprite->setScale(std::max(scX, scY));
    sprite->setPosition(winSize / 2);
    sprite->setAnchorPoint({0.5f, 0.5f});


    clearAppliedBackground(layer, false);
    container->addChild(sprite);

    addLayerBgDarkOverlay(container, winSize, cfg.darkMode, cfg.darkIntensity);

    hideOriginalBg(layer);
    layer->addChild(container);
    return true;
}

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

        if (!shaderName.empty() && shaderName != "none") {
            auto* program = getBgShaderProgram(shaderName);
            if (program) {
                anim->setShaderProgram(program);
                anim->m_intensity = 0.5f;
                anim->m_texSize = CCSize(winSize.width, winSize.height);
            }
        }

        if (auto* parentLayer = typeinfo_cast<CCLayer*>(container->getParent())) {
            LayerBackgroundManager::get().hideOriginalBg(parentLayer);
        }
        container->addChild(anim);

        addLayerBgDarkOverlay(container, winSize, darkMode, darkIntensity);

        {
            std::lock_guard lk(g_containerAliveMutex);
            g_containerAliveFlags.erase(rawContainer);
        }
    });
}

bool LayerBackgroundManager::applyProceduralShaderBg(CCLayer* layer, LayerBgConfig const& cfg) {
    if (!layer) return false;
    auto* program = getProceduralBgShaderProgram(cfg.shader);
    if (!program) return false;

    auto winSize = CCDirector::get()->getWinSize();
    auto winPixels = CCDirector::get()->getWinSizeInPixels();

    auto container = CCNode::create();
    container->setContentSize(winSize);
    container->setPosition({0, 0});
    container->setAnchorPoint({0, 0});
    container->setID("paimon-layerbg-container"_spr);
    container->setZOrder(-10);

    auto* sprite = PaimonShaderGradient::create({255, 255, 255, 255}, {255, 255, 255, 255});
    if (!sprite) return false;

    sprite->setShaderProgram(program);
    sprite->m_intensity = 1.0f;
    sprite->m_time = 0.f;
    sprite->m_texSize = winPixels.width > 0.f && winPixels.height > 0.f ? winPixels : winSize;
    sprite->setAnchorPoint({0.f, 0.f});
    sprite->setPosition({0.f, 0.f});
    sprite->setContentSize(winSize);
    sprite->schedule(schedule_selector(PaimonShaderGradient::updateShaderTime));

    clearAppliedBackground(layer, false);
    container->addChild(sprite);

    addLayerBgDarkOverlay(container, winSize, cfg.darkMode, cfg.darkIntensity);

    hideOriginalBg(layer);
    layer->addChild(container);
    return true;
}

// Blur node with render targets allocated once.
struct VideoBlurNode : public CCNode {
    std::shared_ptr<paimon::video::VideoPlayer> m_player;

    Ref<CCRenderTexture> m_rtA;
    Ref<CCRenderTexture> m_rtB;
    Ref<CCSprite>        m_srcSprite;
    Ref<CCSprite>        m_midSprite;
    CCGLProgram*         m_blurH = nullptr;
    CCGLProgram*         m_blurV = nullptr;
    float                m_gaussRadius = 0.1f;

    Ref<CCRenderTexture> m_pD0, m_pD1, m_pD2;
    Ref<CCRenderTexture> m_pU1, m_pU0;
    Ref<CCRenderTexture> m_pFinal;
    Ref<CCSprite>  m_pSpr0;
    Ref<CCSprite>  m_pSpr1;
    Ref<CCSprite>  m_pSpr2;
    Ref<CCSprite>  m_pSprU2;
    Ref<CCSprite>  m_pSprU1;
    Ref<CCSprite>  m_pSprU0;
    CCGLProgram*   m_blurDown = nullptr;
    CCGLProgram*   m_blurUp   = nullptr;
    float m_hp0x,  m_hp0y;
    float m_hp1x,  m_hp1y;
    float m_hp2x,  m_hp2y;
    float m_hpu2x, m_hpu2y;
    float m_hpu1x, m_hpu1y;
    float m_hpu0x, m_hpu0y;

    Ref<CCSprite> m_displaySprite;
    CCSize m_winSize, m_halfSize, m_quarterSize, m_eighthSize;
    bool   m_isPaimon = false;
    uint64_t m_lastFrameCounter = 0;
    ccTexParams m_linear = {GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};

    GLint m_locGaussScreenSizeH = -1;
    GLint m_locGaussRadiusH     = -1;
    GLint m_locGaussScreenSizeV = -1;
    GLint m_locGaussRadiusV     = -1;
    GLint m_locKawaseDownHalfpixel = -1;
    GLint m_locKawaseUpHalfpixel   = -1;

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

    void fitSprite(CCSprite* spr, CCSize const& dstSize, bool flip = true) {
        CCSize content = spr->getContentSize();
        float sx = dstSize.width  / std::max(content.width,  1.f);
        float sy = dstSize.height / std::max(content.height, 1.f);
        spr->setScale(std::max(sx, sy));
        spr->setAnchorPoint({0.5f, 0.5f});
        spr->setPosition(dstSize * 0.5f);
        spr->setFlipY(flip);
    }

    CCSprite* makeRTSprite(CCRenderTexture* rt, CCSize const& dstSize) {
        auto* tex = rt->getSprite()->getTexture();
        tex->setTexParameters(&m_linear);
        auto spr = CCSprite::createWithTexture(tex);
        fitSprite(spr, dstSize, true);
        return spr;
    }

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

        CCSize dc = m_displaySprite->getContentSize();
        float dsx = winSize.width  / std::max(dc.width,  1.f);
        float dsy = winSize.height / std::max(dc.height, 1.f);
        m_displaySprite->setScale(std::max(dsx, dsy));
        m_displaySprite->setAnchorPoint({0.5f, 0.5f});
        m_displaySprite->setPosition(winSize * 0.5f);
        m_displaySprite->setFlipY(false);
        m_displaySprite->setVisible(false);
        this->addChild(m_displaySprite);
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
        fitSprite(m_srcSprite, m_halfSize, true);

        m_midSprite = makeRTSprite(m_rtA, m_halfSize);
        if (!m_midSprite) return false;

        m_displaySprite = CCSprite::createWithTexture(m_rtB->getSprite()->getTexture());
        if (!m_displaySprite) return false;

        m_gaussRadius = 0.04f + intensity * 0.16f;

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
        fitSprite(m_pSpr0, m_halfSize, true);

        m_pSpr1  = makeRTSprite(m_pD0, m_quarterSize);
        m_pSpr2  = makeRTSprite(m_pD1, m_eighthSize);
        m_pSprU2 = makeRTSprite(m_pD2, m_quarterSize);
        m_pSprU1 = makeRTSprite(m_pU1, m_halfSize);
        m_pSprU0 = makeRTSprite(m_pU0, m_winSize);
        if (!m_pSpr1||!m_pSpr2||!m_pSprU2||!m_pSprU1||!m_pSprU0) return false;

        m_displaySprite = CCSprite::createWithTexture(m_pFinal->getSprite()->getTexture());
        if (!m_displaySprite) return false;

        float scale = intensity * 4.0f;
        m_hp0x  = (0.5f / m_halfSize.width)     * scale;  m_hp0y  = (0.5f / m_halfSize.height)    * scale;
        m_hp1x  = (0.5f / m_halfSize.width)     * scale;  m_hp1y  = (0.5f / m_halfSize.height)    * scale;
        m_hp2x  = (0.5f / m_quarterSize.width)  * scale;  m_hp2y  = (0.5f / m_quarterSize.height) * scale;
        m_hpu2x = (0.5f / m_eighthSize.width)   * scale;  m_hpu2y = (0.5f / m_eighthSize.height)  * scale;
        m_hpu1x = (0.5f / m_quarterSize.width)  * scale;  m_hpu1y = (0.5f / m_quarterSize.height) * scale;
        m_hpu0x = (0.5f / m_halfSize.width)     * scale;  m_hpu0y = (0.5f / m_halfSize.height)    * scale;

        m_locKawaseDownHalfpixel = m_blurDown->getUniformLocationForName("u_halfpixel");
        m_locKawaseUpHalfpixel   = m_blurUp->getUniformLocationForName("u_halfpixel");
        return true;
    }

    void onExit() override {
        CCNode::onExit();
    }

    void tick(float dt) {
        if (!m_player || !m_player->isPlaying()) return;
        if (!isVisible()) return;
        uint64_t fc = m_player->getFrameCounter();
        if (fc == m_lastFrameCounter) return;
        m_lastFrameCounter = fc;

        (void)m_player->getResolvedRGBATexture();

        if (m_isPaimon) renderPaimon();
        else            renderGaussian();
    }

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
        doGauss(m_srcSprite, m_rtA, m_blurH, m_locGaussScreenSizeH, m_locGaussRadiusH);
        doGauss(m_midSprite, m_rtB, m_blurV, m_locGaussScreenSizeV, m_locGaussRadiusV);
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
        doDown(m_pSpr0,  m_pD0,    m_hp0x,  m_hp0y);
        doDown(m_pSpr1,  m_pD1,    m_hp1x,  m_hp1y);
        doDown(m_pSpr2,  m_pD2,    m_hp2x,  m_hp2y);
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
        // Mobile tile GPUs need a barrier between downsample and upsample.
        glFlush();
#endif
        doUp(m_pSprU2, m_pU1,    m_hpu2x, m_hpu2y);
        doUp(m_pSprU1, m_pU0,    m_hpu1x, m_hpu1y);
        doUp(m_pSprU0, m_pFinal, m_hpu0x, m_hpu0y);
    }

    ~VideoBlurNode() override = default;
};

// Apply saved rotation without losing screen coverage.
static void applyVideoRotation(CCNode* sprite, CCSize const& winSize) {
    if (!sprite) return;
    int rot = paimon::settings::video::videoRotation();
    if (rot == 0) return;
    sprite->setRotation(static_cast<float>(rot));
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

    cleanupOldVideoCache(layer, path);
    clearAppliedBackground(layer, videoAudio);

    auto winSize = CCDirector::get()->getWinSize();

    auto container = CCNode::create();
    container->setContentSize(winSize);
    container->setPosition({0, 0});
    container->setAnchorPoint({0, 0});
    container->setID("paimon-layerbg-container"_spr);
    container->setZOrder(-10);

    layer->addChild(container);

    // Reuse-only: falling through to the async path is far better than building
    // a decoder here, which would freeze the layer transition.
    {
        auto shared = acquireExistingSharedVideo(path);
        if (shared) {
            if (!shared->isPlaying()) {
                shared->setLoop(true);
                shared->setTargetFPS(paimon::settings::video::fpsLimit());
                shared->play();
            }

            auto* videoTex = shared->getResolvedRGBATexture();
            if (videoTex) {
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
                    applyVideoRotation(visibleSprite, winSize);
                    addLayerBgDarkOverlay(container, winSize, cfg.darkMode, cfg.darkIntensity);

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
                auto* self = updateNode;
                self->m_createVisuals = [self, shared, winSize, blurType, blurIntensity, cfg]() {
                    auto* container = self->getParent();
                    if (!container) return;
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
                    if (auto* preview = container->getChildByID("paimon-video-preview"_spr)) {
                        preview->removeFromParentAndCleanup(true);
                    }
                    if (visibleSprite) {
                        if (auto* parentLayer = typeinfo_cast<CCLayer*>(container->getParent())) {
                            LayerBackgroundManager::get().hideOriginalBg(parentLayer);
                        }
                    }
                    self->m_visibleSprite = visibleSprite;
                    applyVideoRotation(visibleSprite, winSize);
                };
                container->addChild(updateNode);
            }
            return;
        }
    }

    // Show a cached first frame while the decoder initializes.
    if (auto* previewTex = LayerBackgroundManager::getVideoBgPreviewTexture(path)) {
        if (auto* previewSprite = cocos2d::CCSprite::createWithTexture(previewTex)) {
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

        if (!shared->isPlaying()) {
            shared->play();
        }

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
            auto* self = updateNode;
            self->m_createVisuals = [self, shared, winSize, blurType, blurIntensity, cfgCopy]() {
                auto* container = self->getParent();
                if (!container) return;
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
                if (auto* preview = container->getChildByID("paimon-video-preview"_spr)) {
                    preview->removeFromParentAndCleanup(true);
                }
                if (visibleSprite) {
                    if (auto* parentLayer = typeinfo_cast<CCLayer*>(container->getParent())) {
                        LayerBackgroundManager::get().hideOriginalBg(parentLayer);
                    }
                }
                // Let the update node fade in the first frame.
                self->m_visibleSprite = visibleSprite;
                applyVideoRotation(visibleSprite, winSize);
            };
            targetContainer->addChild(updateNode);
        }
    };

#if defined(GEODE_IS_ANDROID)
    finishSharedSetup(container, LayerBackgroundManager::get().acquireSharedVideo(path, videoAudio));
#else
    // Keep cocos2d retain/release on the main thread.
    Ref<CCNode> containerRef = container;
    CCNode* containerRaw = container;
    auto containerAlive = registerContainerAliveFlag(container);

    paimon::ThreadTracker::get().spawn([containerRef, containerRaw, containerAlive, path, videoAudio, finishSharedSetup]() mutable {
        geode::utils::thread::setName("VideoBg Normalizer");

        auto shared = LayerBackgroundManager::get().acquireSharedVideo(path, videoAudio);

        Loader::get()->queueInMainThread([containerRef = std::move(containerRef), containerRaw, containerAlive, shared, path, finishSharedSetup]() {
            if (g_layerBgShutdown.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) {
                unregisterContainerAliveFlag(containerRaw, containerAlive);
                return;
            }
            if (!containerAlive->load(std::memory_order_acquire) || !containerRef || !containerRef->getParent()) {
                if (shared) {
                    LayerBackgroundManager::get().releaseSharedVideo(path);
                }
                unregisterContainerAliveFlag(containerRaw, containerAlive);
                return;
            }

            finishSharedSetup(containerRef.data(), shared);

            unregisterContainerAliveFlag(containerRaw, containerAlive);
        });
    });
#endif
}

void LayerBackgroundManager::clearAppliedBackground(CCLayer* layer, bool suppressAudioResume) {
    if (!layer) return;

    // Cancel callbacks targeting this layer before removing its container.
    clearContainerAliveFlag(layer, nullptr, true);

    if (auto oldContainer = layer->getChildByID("paimon-layerbg-container"_spr)) {
        if (auto updateNode = oldContainer->getChildByID("paimon-video-update"_spr)) {
            static_cast<VideoBackgroundUpdateNode*>(updateNode)->shutdown(false, suppressAudioResume);
        } else if (paimon::isVideoAudioInteropActive()) {
            paimon::setVideoAudioInteropActive(false);
        }

        clearContainerAliveFlag(oldContainer, nullptr, true);

        oldContainer->removeFromParentAndCleanup(true);
    }

    if (auto menuContainer = layer->getChildByID("paimon-bg-container"_spr)) {
        if (auto updateNode = menuContainer->getChildByID("paimon-video-update"_spr)) {
            static_cast<VideoBackgroundUpdateNode*>(updateNode)->shutdown(false, suppressAudioResume);
        }
    }
}

bool LayerBackgroundManager::applyBackground(CCLayer* layer, std::string const& layerKey) {
    log::info("[LayerBgMgr] applyBackground: layerKey={}", layerKey);
    auto cfg = getConfig(layerKey);

    if (cfg.type == "default" || !paimon::modules::isEnabled("paimbnails.backgrounds.global")) {
        clearAppliedBackground(layer, false);
        showOriginalBg(layer);
        forceEvictAllStaleVideos();
        applyVanillaBackgroundTintFix(layer);
        return false;
    }

    // Resolve layer references with a bounded cycle guard.
    std::string resolvedPath = cfg.customPath;
    std::string resolvedType = cfg.type;
    LayerBgConfig resolvedCfg = cfg;
    int maxHops = 5;

    while (maxHops-- > 0) {
        if (resolvedType == "menu") {
            LayerBgConfig menuCfg = getConfig("menu");
            if (menuCfg.type != "default") {
                resolvedType = menuCfg.type;
                resolvedPath = menuCfg.customPath;
                resolvedCfg.type = menuCfg.type;
                resolvedCfg.customPath = menuCfg.customPath;
                resolvedCfg.levelId = menuCfg.levelId;
                resolvedCfg.shader = menuCfg.shader;
                continue;
            } else {
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
                    resolvedType = "default";
                    resolvedCfg.type = "default";
                    break;
                }
                break;
            }
        }

        bool isLayerRef = false;
        for (auto& [k, n] : LAYER_OPTIONS) {
            if (resolvedType == k) { isLayerRef = true; break; }
        }
        if (isLayerRef) {
            resolvedCfg = getConfig(resolvedType);
            resolvedCfg.darkMode = cfg.darkMode;
            resolvedCfg.darkIntensity = cfg.darkIntensity;
            if (resolvedCfg.type != "shader") {
                resolvedCfg.shader = cfg.shader;
            }
            resolvedType = resolvedCfg.type;
            resolvedPath = resolvedCfg.customPath;
            if (resolvedType == "default") break;
            continue;
        }

        break;
    }

    if (resolvedType == "default") {
        cleanupOldVideoCache(layer, "");
        clearAppliedBackground(layer, false);
        showOriginalBg(layer);
        forceEvictAllStaleVideos();
        applyVanillaBackgroundTintFix(layer);
        return false;
    }

    if (resolvedType != "video") {
        cleanupOldVideoCache(layer, "");
    }

    clearAppliedBackground(
        layer,
        resolvedType == "video" && paimon::settings::video::audioEnabled());
    showOriginalBg(layer);

    if (resolvedType == "custom" && !resolvedPath.empty()) {
        auto ext = geode::utils::string::pathToString(paimon::assets::pathFromUtf8(resolvedPath).extension());
        for (auto& c : ext) c = (char)std::tolower(c);
        if (ext == ".gif" && paimon::assets::exists(resolvedPath)) {
            applyGifBg(layer, resolvedPath, cfg);
            return true;
        }
    }

    if (resolvedType == "video" && !resolvedPath.empty()) {
        if (paimon::assets::exists(resolvedPath)) {
            applyVideoBg(layer, resolvedPath, cfg);
            return true;
        }
        log::warn("[LayerBgMgr] Video file not found: {}  reverting to default", resolvedPath);
        PaimonNotify::show(
            "Video file not found, reverting to default background.",
            geode::NotificationIcon::Warning, 3.0f);
        forceReleaseSharedVideoByPath(resolvedPath);
        showOriginalBg(layer);
        applyVanillaBackgroundTintFix(layer);
        return false;
    }

    if (resolvedType == "shader") {
        if (applyProceduralShaderBg(layer, resolvedCfg)) return true;
        showOriginalBg(layer);
        applyVanillaBackgroundTintFix(layer);
        return false;
    }

    auto* tex = loadTextureForConfig(resolvedCfg);
    if (tex) {
        if (applyStaticBg(layer, tex, cfg)) return true;
        showOriginalBg(layer);
        return false;
    }

    // Download missing ID textures asynchronously.
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
                if (!applyStaticBg(liveLayer, textureRef.data(), capturedCfg)) {
                    showOriginalBg(liveLayer);
                }
                unregisterContainerAliveFlag(layerRaw, layerAlive);
            });
        });
        return true;
    }

    return false;
}

// Stop decoders off-thread; release GL resources on the main thread.
static void scheduleSharedVideoTeardown(std::shared_ptr<paimon::video::VideoPlayer> player) {
    if (!player) return;
    paimon::ThreadTracker::get().spawn([player]() mutable {
        geode::utils::thread::setName("VideoBg Teardown");
        player->forceStop();
        if (g_layerBgShutdown.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) {
            parkSharedVideoForShutdown(std::move(player));
            return;
        }
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

    auto countTotal = [&]() {
        return static_cast<int>(m_sharedVideos.size());
    };

    while (countTotal() >= maxConcurrent) {
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
        if (victim.empty()) break;

        auto it = m_sharedVideos.find(victim);
        if (it == m_sharedVideos.end()) break;
        if (it->second.player) {
            evicted.push_back(std::move(it->second.player));
        }
        log::info("[LayerBgMgr] LRU eviction (inactive, budget {} reached): {}",
                  maxConcurrent, victim);
        m_sharedVideos.erase(it);
    }

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

std::shared_ptr<paimon::video::VideoPlayer> LayerBackgroundManager::acquireExistingSharedVideo(
    std::string const& path) {
    if (path.empty()) return nullptr;
    if (g_layerBgShutdown.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) {
        return nullptr;
    }

    std::lock_guard lk(m_sharedVideosMutex);
    evictExpiredSharedVideos();

    auto it = m_sharedVideos.find(path);
    if (it == m_sharedVideos.end() || !it->second.player) return nullptr;

    auto discard = [&](char const* why) {
        log::warn("[LayerBgMgr] Discarding shared video player ({}): {}", why, path);
        auto playerToRelease = std::move(it->second.player);
        m_sharedVideos.erase(it);
        scheduleSharedVideoTeardown(std::move(playerToRelease));
    };

    if (it->second.player->isTerminal()) {
        discard("terminal");
        return nullptr;
    }
    if (!it->second.player->isPlaying() || !it->second.player->hasVisibleFrame()) {
        discard("unhealthy");
        return nullptr;
    }

    bool revived = it->second.stale;
    it->second.stale = false;
    it->second.refCount++;
    it->second.expiry = std::chrono::steady_clock::time_point::max();
    it->second.lastUsed = std::chrono::steady_clock::now();
    it->second.player->resume();
    if (!it->second.player->isPlaying()) {
        log::info("[LayerBgMgr] Resuming shared video playback: {}", path);
        it->second.player->play();
    }
    rebalanceAdaptiveFPS_locked();
    log::info("[LayerBgMgr] Reusing {} shared video player: {} (refCount={})",
              revived ? "TTL-parked" : "live", path, it->second.refCount);
    return it->second.player;
}

std::shared_ptr<paimon::video::VideoPlayer> LayerBackgroundManager::acquireSharedVideo(
    std::string const& path, bool requireCanonicalAudio) {
    if (path.empty()) return nullptr;
    if (g_layerBgShutdown.load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) {
        return nullptr;
    }

    if (auto existing = acquireExistingSharedVideo(path)) {
        return existing;
    }

    {
        std::lock_guard lk(m_sharedVideosMutex);
        m_pendingSharedVideoCreates[path]++;
    }

    // Decoder setup can take seconds, so create outside the lock.

    // Apply concurrent-video and platform RAM budgets.
    std::shared_ptr<paimon::video::VideoPlayer> evictedPlayer;
    std::vector<std::shared_ptr<paimon::video::VideoPlayer>> lruEvicted;
    {
        std::lock_guard lk(m_sharedVideosMutex);

        int maxConcurrent = paimon::settings::video::maxConcurrentVideos();
        if (maxConcurrent > 0) {
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
    // Tear down evicted players outside the lock.
    for (auto& p : lruEvicted) {
        if (p) scheduleSharedVideoTeardown(std::move(p));
    }
    if (evictedPlayer) {
        scheduleSharedVideoTeardown(std::move(evictedPlayer));
    }

    paimon::video::VideoPlayerCreateOptions playerOptions;
    playerOptions.requireCanonicalAudio = requireCanonicalAudio;
    playerOptions.enableAudio = requireCanonicalAudio;
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

    // Double-check the cache before inserting a concurrently-created player.
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

        auto it = m_sharedVideos.find(path);
        if (it != m_sharedVideos.end() && it->second.player) {
            it->second.refCount++;
            it->second.expiry = std::chrono::steady_clock::time_point::max();
            it->second.lastUsed = std::chrono::steady_clock::now();
            auto duplicatePlayer = std::shared_ptr<paimon::video::VideoPlayer>(player.release());
            scheduleSharedVideoTeardown(std::move(duplicatePlayer));
            rebalanceAdaptiveFPS_locked();
            log::info("[LayerBgMgr] Reusing shared video player (created concurrently): {} (refCount={})",
                      path, it->second.refCount);
            return it->second.player;
        }

        auto shared = std::shared_ptr<paimon::video::VideoPlayer>(player.release());
        SharedVideoEntry entry;
        entry.player = shared;
        entry.refCount = 1;
        entry.expiry = std::chrono::steady_clock::time_point::max();
        entry.lastUsed = std::chrono::steady_clock::now();
        m_sharedVideos[path] = std::move(entry);
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
            // Park it: the decode thread stalls on the full ring by itself, and
            // keeping the GPU resolve cache is what makes a return instant.
            it->second.stale = true;
            it->second.expiry = std::chrono::steady_clock::now() + kSharedVideoTTL;

            log::info("[LayerBgMgr] Shared video entering TTL grace (parked): {} ({}s)",
                      path, static_cast<int>(kSharedVideoTTL.count()));
#endif
        }

        rebalanceAdaptiveFPS_locked();

        evictExpiredSharedVideos();
    }

    if (playerToHalt) {
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
    // Snapshot under the lock; fadeAudioOut may re-enter the scheduler.
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
            p->fadeAudioOut(0.0f);
        }
    }

    if (anyHadAudio || paimon::isVideoAudioInteropActive()) {
        paimon::setVideoAudioInteropActive(false);
    }

    if (DynamicSongManager::get()->hasSuspendedPlayback()) {
        DynamicSongManager::get()->resumeSuspendedPlayback();
    }
}

bool LayerBackgroundManager::hasSharedVideo(std::string const& path) const {
    std::lock_guard lk(m_sharedVideosMutex);
    auto it = m_sharedVideos.find(path);
    return it != m_sharedVideos.end() && it->second.player;
}

void LayerBackgroundManager::cleanupOldVideoCache(cocos2d::CCLayer* layer, std::string const& nextVideoPath) {
    if (!layer) return;

    std::string oldVideoPath;
    if (auto oldContainer = layer->getChildByID("paimon-layerbg-container"_spr)) {
        if (auto updateNode = oldContainer->getChildByID("paimon-video-update"_spr)) {
            auto* vbn = static_cast<VideoBackgroundUpdateNode*>(updateNode);
            oldVideoPath = vbn->m_videoPath;
            if (oldVideoPath.empty()) {
                auto* p = vbn->getPlayer();
                if (p) oldVideoPath = p->getFilePath();
            }
        }
    }
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
            paimon::video::VideoDiskCache::deleteCache(path);
            log::info("[LayerBgMgr] broadcastFPSUpdate: {} fps ? {} (cache invalidated)", newFPS, path);
        }
    }
}

void LayerBackgroundManager::broadcastRotationUpdate(int newRotationDegrees) {
    // Rotate the visual child, not the container or update node.
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene) return;

    auto winSize = CCDirector::get()->getWinSize();

    for (auto* sceneChild : CCArrayExt<CCNode*>(scene->getChildren())) {
        auto* container = sceneChild->getChildByID("paimon-layerbg-container"_spr);
        if (!container) {
            for (auto* layerChild : CCArrayExt<CCNode*>(sceneChild->getChildren())) {
                container = layerChild->getChildByID("paimon-layerbg-container"_spr);
                if (container) break;
            }
        }
        if (!container) continue;

        for (auto* child : CCArrayExt<CCNode*>(container->getChildren())) {
            if (child->getID() == "paimon-video-update"_spr) continue;
            if (typeinfo_cast<CCLayerColor*>(child)) continue;
            if (child->getID() == "paimon-video-preview"_spr) continue;

            child->setRotation(static_cast<float>(newRotationDegrees));

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
            break;
        }
    }

    log::info("[LayerBgMgr] broadcastRotationUpdate: {} deg", newRotationDegrees);
}
