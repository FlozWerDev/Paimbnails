#pragma once

// DiskManifest.hpp — Indice persistente del cache de disco.
// Reemplaza el antiguo almacenamiento en Geode SavedValues por un
// manifest.json independiente con metadata completa (revision, bytes,
// lastAccess, etc.) que se serializa a JSON y se hidrata al arrancar.

#include "CacheModels.hpp"
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <filesystem>
#include <string>

namespace paimon::cache {

class DiskManifest {
public:
    // carga el manifest desde disco; valida archivos faltantes/corruptos
    void load(std::filesystem::path const& cacheDir);

    // persiste el manifest a disco (deferred: solo si dirty)
    void flush();

    // ── Consultas (thread-safe: toman mutex internamente) ────────

    bool contains(int levelID, bool isGif) const;
    bool containsUrl(std::string const& url) const;
    DiskManifestEntry const* getEntry(int levelID, bool isGif) const;
    DiskManifestEntry const* getEntryByUrl(std::string const& url) const;

    // chequeo rapido legacy (para mantener compatibilidad con el loader actual)
    bool containsLegacyKey(int key) const;

    // ── Consultas sin lock (caller DEBE tener mutex) ────────────
    bool containsLocked(int levelID, bool isGif) const;
    DiskManifestEntry const* getEntryLocked(int levelID, bool isGif) const;
    DiskManifestEntry const* getEntryByUrlLocked(std::string const& url) const;

    // ── Mutaciones ──────────────────────────────────────────────

    void upsert(int levelID, bool isGif, DiskManifestEntry entry);
    void upsertUrl(std::string const& url, DiskManifestEntry entry);
    void remove(int levelID, bool isGif);
    void removeUrl(std::string const& url);
    void clear();

    // touch lastAccess sin marcar dirty para flush completo
    void touchAccess(int levelID, bool isGif);
    void touchAccessUrl(std::string const& url);

    // ── Poda ────────────────────────────────────────────────────

    // devuelve entradas que deben purgarse segun quota y edad
    struct PruneResult {
        std::vector<std::string> filesToDelete; // paths relativos al cacheDir
        size_t freedBytes = 0;
    };
    PruneResult computePrune(size_t maxBytes, std::chrono::hours maxAge) const;
    void applyPrune(PruneResult const& result);

    // ── Stats ───────────────────────────────────────────────────

    size_t totalBytes() const;
    size_t totalBytesLocked() const; // caller DEBE tener mutex
    size_t entryCount() const;

    // ── Legacy compat ───────────────────────────────────────────

    // genera un unordered_set<int> con las keys legacy (para la transicion)
    std::unordered_set<int> legacyKeySet() const;

    mutable std::recursive_mutex mutex;

private:
    // key = "levelID" or "-levelID" for gif, or "url:<hash>" for gallery
    std::unordered_map<std::string, DiskManifestEntry> m_entries;
    std::unordered_map<std::string, std::string> m_urlToKey; // url -> manifest key
    std::filesystem::path m_cacheDir;
    std::filesystem::path m_manifestPath;
    bool m_dirty = false;
    int m_accessCounter = 0;

    std::string makeKey(int levelID, bool isGif) const;
    std::string makeUrlKey(std::string const& url) const;
    void rebuildFromDirectory(std::filesystem::path const& cacheDir);
};

} // namespace paimon::cache
