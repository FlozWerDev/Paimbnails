#pragma once

#include "CacheModels.hpp"
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <filesystem>
#include <string>
#include <optional>
#include <vector>

namespace paimon::cache {

class DiskManifest {
public:
    void load(std::filesystem::path const& cacheDir);

    void flush();

    bool contains(int levelID, bool isGif) const;
    std::optional<DiskManifestEntry> getEntry(int levelID, bool isGif) const;

    // lock-free queries (caller must hold mutex)
    bool containsLocked(int levelID, bool isGif) const;
    DiskManifestEntry const* getEntryLocked(int levelID, bool isGif) const;

    void upsert(int levelID, bool isGif, DiskManifestEntry entry);
    void remove(int levelID, bool isGif);
    void clear();
    void clearPreservingMainLevels();

    // touch lastaccess without marking the whole manifest dirty
    void touchAccess(int levelID, bool isGif);

    struct PruneItem {
        std::string key;
        std::string filename;
        size_t byteSize = 0;
        int64_t lastAccessEpoch = 0;
    };
    struct PruneResult {
        std::vector<PruneItem> items;
        size_t freedBytes = 0;
    };
    PruneResult computePrune(size_t maxBytes, std::chrono::hours maxAge) const;
    // removes the pruned entries and renames their files to quarantine names
    // while holding the mutex, then deletes the quarantined files after
    // releasing it. Returns the number of files removed.
    size_t applyPrune(PruneResult const& result);

    size_t totalBytes() const;
    size_t totalBytesLocked() const; // caller must hold mutex
    size_t entryCount() const;

    mutable std::recursive_mutex mutex;

private:
    // serializes publishers, not readers. Never hold mutex during JSON or disk I/O.
    std::mutex m_flushMutex;
    // key = "levelid" or "-levelid" for gif, or "url:<hash>" for gallery
    std::unordered_map<std::string, DiskManifestEntry> m_entries;
    std::unordered_map<std::string, std::string> m_urlToKey;
    // running sum of entry byteSizes, kept in sync by every mutation so
    // totalBytes() stays O(1) instead of rescanning the whole index.
    size_t m_totalBytes = 0;
    std::filesystem::path m_cacheDir;
    std::filesystem::path m_manifestPath;
    bool m_dirty = false;
    int m_accessCounter = 0;

    std::string makeKey(int levelID, bool isGif) const;
    void rebuildFromDirectory(std::filesystem::path const& cacheDir);
};

} // namespace paimon::cache
