#pragma once

#include <Geode/DefaultInclude.hpp>
#include <Geode/utils/function.hpp>

class GJGameLevel;

#include <atomic>
#include <filesystem>
#include <optional>
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

    // cover art plus the level it came from, for the "Miniatura de: ..." caption.
    struct CoverEntry {
        int levelID = 0;
        std::string name;
        std::string path;
    };

    std::filesystem::path getCoversDir() const;
    std::filesystem::path getSongDir(int songID) const;

    bool hasCachedCovers(int songID) const;
    std::vector<std::string> getCachedCoverPaths(int songID) const;
    std::vector<CoverEntry> getCachedCoverEntries(int songID) const;

    // non-blocking variants used by the player popup. Callbacks run on the
    // main thread; filesystem access and manifest parsing happen on a worker.
    void requestCachedCoversAsync(int songID, CoversCallback callback);
    std::vector<std::string> peekCachedCoverPaths(int songID) const;
    std::vector<CoverEntry> peekCachedCoverEntries(int songID) const;
    static void validateCoverPathsAsync(std::vector<std::string> paths, CoversCallback callback);

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
        bool localTried = false;
        std::string searchKey;
        std::vector<CoversCallback> callbacks;
    };

    struct SongManifest {
        std::vector<int> levelIds;
        std::vector<std::string> coverPaths;
        std::unordered_map<int, std::string> levelNames;
    };
    struct DiskCoverSnapshot {
        std::optional<SongManifest> manifest;
        std::vector<std::string> legacyPaths;
        std::unordered_map<std::string, double> negativeEntries;
    };
    struct PendingDiskLookup {
        std::uint64_t serial = 0;
        std::uint64_t version = 0;
        std::vector<CoversCallback> callbacks;
    };

    static DiskCoverSnapshot readDiskCoverSnapshot(std::filesystem::path const& coversDir, int songID);
    void finishCachedCoverLookup(int songID, std::uint64_t serial, std::optional<DiskCoverSnapshot> snapshot);

    bool loadManifest(int songID, SongManifest& out) const;
    void saveManifest(int songID, std::vector<int> const& levelIds,
        std::vector<std::string> const& coverPaths,
        std::unordered_map<int, std::string> const& levelNames) const;

    std::filesystem::path negativeCachePath() const;
    void loadNegativeMemo() const;
    bool isNegativelyCached(int songID) const;
    void markNegative(int songID) const;

    void ensureSearchNode();
    void scheduleDebouncedFlush(float delaySec);
    void cancelDebounce();
    void abortActiveWork();
    void noteServerRateLimit();
    bool isCooldownActive() const;
    double nowSeconds() const;
    void pumpQueue();
    void loadThumbnailsForLevels(int songID, std::vector<int> const& levelIds,
        std::unordered_map<int, std::string> levelNames, bool fromLocal = false);
    void finishRequest(int songID, std::vector<std::string> const& coverPaths, bool success);
    void dispatchCallbacks(PendingBatch& batch, std::vector<std::string> const& coverPaths, bool success);

    int m_targetSongID = 0;
    bool m_debouncePending = false;
    std::atomic<uint32_t> m_debounceGeneration{0};
    mutable std::unordered_set<int> m_loggedMissingManifests;
    // parsed manifest.json per song (nullopt = known to have none); this class
    // is the only writer, and saveManifest drops the entry it rewrites.
    mutable std::unordered_map<int, std::optional<SongManifest>> m_manifestMemo;
    mutable std::unordered_map<int, std::vector<std::string>> m_legacyMemo;
    mutable std::unordered_map<int, std::uint64_t> m_manifestVersions;
    std::unordered_map<int, PendingDiskLookup> m_pendingDiskLookups;
    std::uint64_t m_diskLookupSerial = 0;
    // negative.json contents, loaded once; markNegative updates it in place.
    mutable std::optional<std::unordered_map<std::string, double>> m_negativeMemo;
    std::unordered_map<int, std::vector<CoversCallback>> m_deferredCallbacks;
    std::vector<PendingBatch> m_queue;
    bool m_searchInFlight = false;
    // ref, never added to the scene: searches must survive scene changes
    // with their delegate slot intact.
    geode::Ref<cocos2d::CCNode> m_searchNode = nullptr;
    double m_cooldownUntil = 0.0;
    double m_nextSearchAllowedAt = 0.0;
};

} // namespace paimon::menumusic
