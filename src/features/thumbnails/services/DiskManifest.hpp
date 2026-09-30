#pragma once

#include "CacheModels.hpp"
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <filesystem>
#include <string>

namespace paimon::cache {

class DiskManifest {
public:
    void load(std::filesystem::path const& cacheDir);

    void flush();

    bool contains(int levelID, bool isGif) const;
    DiskManifestEntry const* getEntry(int levelID, bool isGif) const;

    // lock-free queries (caller must hold mutex)
    bool containsLocked(int levelID, bool isGif) const;
    DiskManifestEntry const* getEntryLocked(int levelID, bool isGif) const;

    void upsert(int levelID, bool isGif, DiskManifestEntry entry);
    void remove(int levelID, bool isGif);
    void clear();
    void clearPreservingMainLevels();

    // touch lastaccess without marking the whole manifest dirty
    void touchAccess(int levelID, bool isGif);

    struct PruneResult {
        std::vector<std::string> filesToDelete;
        size_t freedBytes = 0;
    };
    PruneResult computePrune(size_t maxBytes, std::chrono::hours maxAge) const;
    void applyPrune(PruneResult const& result);

    size_t totalBytes() const;
    size_t totalBytesLocked() const; // caller must hold mutex
    size_t entryCount() const;

    mutable std::recursive_mutex mutex;

private:
    // key = "levelid" or "-levelid" for gif, or "url:<hash>" for gallery
    std::unordered_map<std::string, DiskManifestEntry> m_entries;
    std::unordered_map<std::string, std::string> m_urlToKey;
    std::filesystem::path m_cacheDir;
    std::filesystem::path m_manifestPath;
    bool m_dirty = false;
    int m_accessCounter = 0;

    std::string makeKey(int levelID, bool isGif) const;
    void rebuildFromDirectory(std::filesystem::path const& cacheDir);
};

} // namespace paimon::cache
