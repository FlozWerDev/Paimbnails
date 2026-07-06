#pragma once

#include <Geode/DefaultInclude.hpp>
#include <Geode/utils/function.hpp>

class GJGameLevel;

#include <atomic>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace paimon::menumusic {

class SongCoverCache {
public:
    using CoversCallback = geode::CopyableFunction<void(std::vector<std::string> const& coverPaths, bool success)>;

    static SongCoverCache& get();

    static constexpr int kMaxLevelsPerSong = 10;

    std::filesystem::path getCoversDir() const;
    std::filesystem::path getSongDir(int songID) const;

    bool hasCachedCovers(int songID) const;
    std::vector<std::string> getCachedCoverPaths(int songID) const;

    void requestCovers(int songID, CoversCallback callback);
    void cancelPending(int songID);
    void cancelAllPending();
    void cleanup();

    void handleLevelSearchResult(int songID, bool customSong, cocos2d::CCArray* levels);
    void handleLevelSearchFailed(int songID);
    void flushDeferredRequests();

private:
    SongCoverCache() = default;

    struct PendingBatch {
        int songID = 0;
        int searchAttempt = 0;
        std::string searchKey;
        std::vector<CoversCallback> callbacks;
    };

    struct SongManifest {
        std::vector<int> levelIds;
        std::vector<std::string> coverPaths;
    };

    bool loadManifest(int songID, SongManifest& out) const;
    void saveManifest(int songID, std::vector<int> const& levelIds,
        std::vector<std::string> const& coverPaths) const;

    void ensureSearchNode();
    void scheduleDebouncedFlush(float delaySec);
    void cancelDebounce();
    void abortActiveWork();
    void noteServerRateLimit();
    bool isCooldownActive() const;
    double nowSeconds() const;
    void pumpQueue();
    void loadThumbnailsForLevels(int songID, std::vector<int> const& levelIds);
    void finishRequest(int songID, std::vector<std::string> const& coverPaths, bool success);
    void dispatchCallbacks(PendingBatch& batch, std::vector<std::string> const& coverPaths, bool success);

    int m_targetSongID = 0;
    bool m_debouncePending = false;
    std::atomic<uint32_t> m_debounceGeneration{0};
    mutable std::unordered_set<int> m_loggedMissingManifests;
    std::unordered_map<int, std::vector<CoversCallback>> m_deferredCallbacks;
    std::vector<PendingBatch> m_queue;
    bool m_searchInFlight = false;
    cocos2d::CCNode* m_searchNode = nullptr;
    double m_cooldownUntil = 0.0;
    double m_nextSearchAllowedAt = 0.0;
};

} // namespace paimon::menumusic