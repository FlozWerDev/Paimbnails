#pragma once
#include <Geode/Geode.hpp>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace paimon::wallpaper {

// decodes wallpaper preview images off the main thread, downscales them and
// keeps the small texture in ram. A miss never blocks: callers get nullptr and
// the texture arrives on a later revision().
class WallpaperThumbnailCache {
public:
    static WallpaperThumbnailCache& get();
    static bool isInitialized();

    // main thread only. cached texture for path, or nullptr while decoding.
    cocos2d::CCTexture2D* request(std::filesystem::path const& path, int maxDim);

    // main thread only. uploads finished decodes; returns how many landed.
    size_t pump();

    // bumps whenever a texture is added, so pickers can refresh lazily.
    uint64_t revision() const { return m_revision.load(std::memory_order_acquire); }

    bool hasPending() const;

    // main thread. drops queued work and releases cached textures.
    void shutdown();

    // main thread, old gl context still current: release uploaded textures
    // but keep the decoder usable (previews decode again on demand).
    void clearTextures();

private:
    struct Pending {
        std::string key;
        std::filesystem::path path;
        int maxDim = 0;
    };

    struct Decoded {
        std::string key;
        std::shared_ptr<uint8_t[]> rgba;
        int width = 0;
        int height = 0;
    };

    WallpaperThumbnailCache() = default;
    ~WallpaperThumbnailCache() = default;

    static std::string makeKey(std::filesystem::path const& path, int maxDim);

    void startQueuedLocked();
    void decode(Pending pending);
    bool addTexture(Decoded const& decoded);

    mutable std::mutex m_mutex;
    std::vector<Pending> m_queue;
    std::unordered_set<std::string> m_requested; // queued, running or failed
    std::unordered_set<std::string> m_failed;
    std::vector<Decoded> m_ready;
    size_t m_running = 0;
    bool m_stopping = false;

    // main thread only.
    std::unordered_map<std::string, geode::Ref<cocos2d::CCTexture2D>> m_textures;
    std::vector<std::string> m_order;
    std::atomic<uint64_t> m_revision{0};
};

} // namespace paimon::wallpaper
