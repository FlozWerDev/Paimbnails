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
    bool containsUrl(std::string const& url) const;
    DiskManifestEntry const* getEntry(int levelID, bool isGif) const;
    DiskManifestEntry const* getEntryByUrl(std::string const& url) const;

    bool containsLegacyKey(int key) const;

    // Consultas sin lock (caller DEBE tener mutex)
    bool containsLocked(int levelID, bool isGif) const;
    DiskManifestEntry const* getEntryLocked(int levelID, bool isGif) const;
    DiskManifestEntry const* getEntryByUrlLocked(std::string const& url) const;

    void upsert(int levelID, bool isGif, DiskManifestEntry entry);
    void upsertUrl(std::string const& url, DiskManifestEntry entry);
    void remove(int levelID, bool isGif);
    void removeUrl(std::string const& url);
    void clear();
    void clearPreservingMainLevels();

    // touch lastAccess without marking the whole manifest dirty
    void touchAccess(int levelID, bool isGif);
    void touchAccessUrl(std::string const& url);

    struct PruneResult {
        std::vector<std::string> filesToDelete;
        size_t freedBytes = 0;
    };
    PruneResult computePrune(size_t maxBytes, std::chrono::hours maxAge) const;
    void applyPrune(PruneResult const& result);

    size_t totalBytes() const;
    size_t totalBytesLocked() const; // caller DEBE tener mutex
    size_t entryCount() const;

    std::unordered_set<int> legacyKeySet() const;

    mutable std::recursive_mutex mutex;

private:
    // key = "levelID" or "-levelID" for gif, or "url:<hash>" for gallery
    std::unordered_map<std::string, DiskManifestEntry> m_entries;
    std::unordered_map<std::string, std::string> m_urlToKey;
    std::filesystem::path m_cacheDir;
    std::filesystem::path m_manifestPath;
    bool m_dirty = false;
    int m_accessCounter = 0;

    std::string makeKey(int levelID, bool isGif) const;
    std::string makeUrlKey(std::string const& url) const;
    void rebuildFromDirectory(std::filesystem::path const& cacheDir);
};

} // namespace paimon::cache
