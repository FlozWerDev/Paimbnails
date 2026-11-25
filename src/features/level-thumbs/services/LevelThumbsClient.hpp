#pragma once

#include <Geode/Geode.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// same public API as cdc.level_thumbnails. The fallback only runs while
// that mod is installed and enabled; otherwise it stays off and the user
// is asked once per session to download or enable it.

namespace paimon::levelthumbs {

enum class Quality {
    Small,
    Medium,
    High
};

class LevelThumbsClient {
public:
    using DataCallback = geode::CopyableFunction<void(bool success, std::vector<uint8_t> const& data)>;

    static LevelThumbsClient& get();
    static bool isInitialized();

    void requestShutdown();
    void waitShutdown(std::chrono::steady_clock::time_point deadline);
    void shutdown();
    bool isShuttingDown() const { return m_shuttingDown.load(std::memory_order_acquire); }

    // main thread only; the callback lands on the main thread too and never
    // before this returns.
    void fetchThumbnail(int levelID, Quality quality, DataCallback callback);

    bool isNotFound(int levelID) const;
    void clearNotFound(int levelID);
    void clearCache();
    void clearDiskCache();

    std::string apiBaseUrl() const;
    bool isLegacyApi() const;
    std::string thumbnailUrl(int levelID, Quality quality) const;

private:
    LevelThumbsClient();

    struct Request {
        int levelID = 0;
        Quality quality = Quality::Medium;
        std::vector<DataCallback> callbacks;
    };

    // resolved from the other mod's settings, so both are main thread only.
    std::filesystem::path cacheDir() const;
    std::filesystem::path entryPath(int levelID, Quality quality) const;

    void pump();
    void startRequest(std::shared_ptr<Request> request);
    void download(std::shared_ptr<Request> request, std::string const& url,
                  std::filesystem::path const& path);
    void finish(std::shared_ptr<Request> const& request, bool success, std::vector<uint8_t> const& data);
    void markNotFound(int levelID);

    // queue state stays on the main thread: fetchthumbnail runs from
    // thumbnailloader::finishtask and every web callback lands there too.
    std::unordered_map<int64_t, std::shared_ptr<Request>> m_inflight;
    std::deque<std::shared_ptr<Request>> m_queue;
    int m_activeRequests = 0;
    bool m_pumping = false;

    mutable std::unordered_map<int, std::chrono::steady_clock::time_point> m_notFound;
    mutable std::mutex m_notFoundMutex;
    std::atomic<bool> m_shuttingDown{false};

#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    static constexpr int MAX_CONCURRENT_REQUESTS = 3;
#else
    static constexpr int MAX_CONCURRENT_REQUESTS = 6;
#endif
    static constexpr auto NOT_FOUND_TTL = std::chrono::minutes(30);
};

// the cdc.level_thumbnails mod on disk, and loaded (installed + enabled).
bool isLevelThumbsModInstalled();
bool isLevelThumbsModActive();

// module toggle on and the Level Thumbnails mod installed and enabled.
bool fallbackEnabled();
// asks once per session to download (or enable) the Level Thumbnails mod.
// safe to call from any thread; the popup itself opens on the main thread.
void maybePromptToInstallLevelThumbs();
// whether the fallback may answer for this id at all; gif keys and levels we
// already know the database has nothing for are excluded.
bool shouldFallback(int levelID);
Quality qualityForThumbnail(bool highQuality);

} // namespace paimon::levelthumbs
