#include "DiskManifest.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>
#include <matjson.hpp>
#include <fstream>
#include <algorithm>

using namespace geode::prelude;

namespace paimon::cache {

static constexpr const char* MANIFEST_FILENAME = "manifest.json";

std::string DiskManifest::makeKey(int levelID, bool isGif) const {
    return isGif ? ("-" + std::to_string(levelID)) : std::to_string(levelID);
}

std::string DiskManifest::makeUrlKey(std::string const& url) const {
    return "url:" + url;
}

void DiskManifest::load(std::filesystem::path const& cacheDir) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    m_cacheDir = cacheDir;
    m_manifestPath = cacheDir / MANIFEST_FILENAME;
    m_entries.clear();
    m_urlToKey.clear();
    m_dirty = false;

    std::error_code ec;
    if (!std::filesystem::exists(m_manifestPath, ec)) {
        // no hay manifest previo — escanear la carpeta para crear uno inicial
        // esto permite migracion desde la version sin manifest
        rebuildFromDirectory(cacheDir);
        return;
    }

    // leer y parsear manifest existente
    auto readRes = file::readString(m_manifestPath);
    if (!readRes) {
        log::warn("[DiskManifest] could not open manifest for reading");
        return;
    }

    auto parseResult = matjson::parse(readRes.unwrap());
    if (!parseResult.isOk()) {
        log::warn("[DiskManifest] manifest parse error, rebuilding from directory scan");
        // Reconstruir desde los archivos que existen en disco para no perder el cache
        rebuildFromDirectory(cacheDir);
        return;
    }
    auto& root = parseResult.unwrap();

    if (!root.isObject()) {
        log::warn("[DiskManifest] manifest root is not object, rebuilding from directory scan");
        rebuildFromDirectory(cacheDir);
        return;
    }

    int loaded = 0;
    int orphans = 0;

    for (auto& [key, val] : root) {
        if (!val.isObject()) continue;

        DiskManifestEntry me;
        me.filename = val["filename"].asString().unwrapOr("");
        me.levelID = val["levelID"].asInt().unwrapOr(0);
        me.sourceUrl = val["sourceUrl"].asString().unwrapOr("");
        me.revisionToken = val["revisionToken"].asString().unwrapOr("");
        me.format = val["format"].asString().unwrapOr("");
        me.width = val["width"].asInt().unwrapOr(0);
        me.height = val["height"].asInt().unwrapOr(0);
        me.byteSize = static_cast<size_t>(val["byteSize"].asInt().unwrapOr(0));
        me.lastAccessEpoch = val["lastAccess"].asInt().unwrapOr(0);
        me.lastValidatedEpoch = val["lastValidated"].asInt().unwrapOr(0);
        me.isGif = val["isGif"].asBool().unwrapOr(false);
        me.qualityTag = val["qualityTag"].asString().unwrapOr("");

        // validar que el archivo existe en disco
        if (!me.filename.empty()) {
            auto filePath = cacheDir / me.filename;
            if (!std::filesystem::exists(filePath, ec)) {
                orphans++;
                m_dirty = true;
                continue; // no lo cargamos al indice
            }
        }

        // registrar en indices
        m_entries[key] = std::move(me);
        if (!m_entries[key].sourceUrl.empty()) {
            m_urlToKey[m_entries[key].sourceUrl] = key;
        }
        loaded++;
    }

    if (orphans > 0) {
        log::info("[DiskManifest] loaded {} entries, {} orphans removed", loaded, orphans);
    } else {
        log::info("[DiskManifest] loaded {} entries", loaded);
    }

    // limpiar archivos .tmp huerfanos (escrituras atomicas interrumpidas)
    {
        int tmpCleaned = 0;
        for (auto const& entry : std::filesystem::directory_iterator(cacheDir, ec)) {
            if (ec || !entry.is_regular_file()) continue;
            auto ext = geode::utils::string::toLower(
                geode::utils::string::pathToString(entry.path().extension()));
            if (ext == ".tmp") {
                std::error_code rmEc;
                std::filesystem::remove(entry.path(), rmEc);
                tmpCleaned++;
            }
        }
        if (tmpCleaned > 0) {
            log::info("[DiskManifest] cleaned {} orphaned .tmp files", tmpCleaned);
        }
    }
}

void DiskManifest::flush() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!m_dirty) return;

    matjson::Value root = matjson::makeObject({});

    for (auto const& [key, me] : m_entries) {
        matjson::Value entry = matjson::makeObject({});
        entry["filename"] = me.filename;
        entry["levelID"] = me.levelID;
        entry["sourceUrl"] = me.sourceUrl;
        entry["revisionToken"] = me.revisionToken;
        entry["format"] = me.format;
        entry["width"] = me.width;
        entry["height"] = me.height;
        entry["byteSize"] = static_cast<int64_t>(me.byteSize);
        entry["lastAccess"] = me.lastAccessEpoch;
        entry["lastValidated"] = me.lastValidatedEpoch;
        entry["isGif"] = me.isGif;
        entry["qualityTag"] = me.qualityTag;
        root[key] = entry;
    }

    std::error_code ec;
    std::filesystem::create_directories(m_cacheDir, ec);

    // Escritura atomica: escribir a .tmp y renombrar, para que un crash
    // a mitad de escritura no corrompa el manifest existente.
    auto tmpPath = m_manifestPath;
    tmpPath += ".tmp";

    {
        std::ofstream file(tmpPath, std::ios::out | std::ios::trunc);
        if (!file.is_open()) {
            log::error("[DiskManifest] could not open manifest .tmp for writing");
            return;
        }
        file << root.dump(matjson::NO_INDENTATION);
        if (!file.good()) {
            log::error("[DiskManifest] write to manifest .tmp failed");
            file.close();
            std::filesystem::remove(tmpPath, ec);
            return;
        }
        file.close();
    }

    // rename atomico
    std::filesystem::remove(m_manifestPath, ec);
    ec.clear();
    std::filesystem::rename(tmpPath, m_manifestPath, ec);
    if (ec) {
        log::error("[DiskManifest] rename .tmp -> manifest failed: {}", ec.message());
        std::filesystem::remove(tmpPath, ec);
        return;
    }

    m_dirty = false;
    log::debug("[DiskManifest] flushed {} entries", m_entries.size());
}

// ── Consultas thread-safe (toman mutex internamente) ───────

bool DiskManifest::contains(int levelID, bool isGif) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return containsLocked(levelID, isGif);
}

bool DiskManifest::containsUrl(std::string const& url) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return m_urlToKey.count(url) > 0;
}

DiskManifestEntry const* DiskManifest::getEntry(int levelID, bool isGif) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return getEntryLocked(levelID, isGif);
}

DiskManifestEntry const* DiskManifest::getEntryByUrl(std::string const& url) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return getEntryByUrlLocked(url);
}

bool DiskManifest::containsLegacyKey(int key) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (key < 0) return containsLocked(-key, true);
    return containsLocked(key, false);
}

// ── Consultas sin lock (caller DEBE tener mutex) ────────────

bool DiskManifest::containsLocked(int levelID, bool isGif) const {
    return m_entries.count(makeKey(levelID, isGif)) > 0;
}

DiskManifestEntry const* DiskManifest::getEntryLocked(int levelID, bool isGif) const {
    auto it = m_entries.find(makeKey(levelID, isGif));
    return it != m_entries.end() ? &it->second : nullptr;
}

DiskManifestEntry const* DiskManifest::getEntryByUrlLocked(std::string const& url) const {
    auto keyIt = m_urlToKey.find(url);
    if (keyIt == m_urlToKey.end()) return nullptr;
    auto it = m_entries.find(keyIt->second);
    return it != m_entries.end() ? &it->second : nullptr;
}

// ── Mutaciones ──────────────────────────────────────────────────

void DiskManifest::upsert(int levelID, bool isGif, DiskManifestEntry entry) {
    auto key = makeKey(levelID, isGif);
    if (!entry.sourceUrl.empty()) {
        m_urlToKey[entry.sourceUrl] = key;
    }
    m_entries[key] = std::move(entry);
    m_dirty = true;
}

void DiskManifest::upsertUrl(std::string const& url, DiskManifestEntry entry) {
    auto key = makeUrlKey(url);
    entry.sourceUrl = url;
    m_urlToKey[url] = key;
    m_entries[key] = std::move(entry);
    m_dirty = true;
}

void DiskManifest::remove(int levelID, bool isGif) {
    auto key = makeKey(levelID, isGif);
    auto it = m_entries.find(key);
    if (it != m_entries.end()) {
        if (!it->second.sourceUrl.empty()) {
            m_urlToKey.erase(it->second.sourceUrl);
        }
        m_entries.erase(it);
        m_dirty = true;
    }
}

void DiskManifest::removeUrl(std::string const& url) {
    auto keyIt = m_urlToKey.find(url);
    if (keyIt != m_urlToKey.end()) {
        m_entries.erase(keyIt->second);
        m_urlToKey.erase(keyIt);
        m_dirty = true;
    }
}

void DiskManifest::clear() {
    m_entries.clear();
    m_urlToKey.clear();
    m_dirty = true;
}

void DiskManifest::touchAccess(int levelID, bool isGif) {
    auto it = m_entries.find(makeKey(levelID, isGif));
    if (it != m_entries.end()) {
        it->second.touchAccess();
        // mark dirty periodically so access times survive crashes
        if (++m_accessCounter % 20 == 0) {
            m_dirty = true;
        }
    }
}

void DiskManifest::touchAccessUrl(std::string const& url) {
    auto keyIt = m_urlToKey.find(url);
    if (keyIt == m_urlToKey.end()) return;
    auto it = m_entries.find(keyIt->second);
    if (it != m_entries.end()) {
        it->second.touchAccess();
    }
}

// ── Poda ────────────────────────────────────────────────────

DiskManifest::PruneResult DiskManifest::computePrune(size_t maxBytes, std::chrono::hours maxAge) const {
    PruneResult result;
    auto nowEpoch = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    auto maxAgeSeconds = std::chrono::duration_cast<std::chrono::seconds>(maxAge).count();

    size_t currentTotal = totalBytesLocked();

    // primero: entradas mas viejas que maxAge
    struct Candidate {
        std::string key;
        std::string filename;
        size_t bytes;
        int64_t lastAccess;
        int levelID;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(m_entries.size());

    for (auto const& [key, me] : m_entries) {
        // proteger main levels (1-22)
        int realID = me.levelID > 0 ? me.levelID : 0;
        if (realID >= 1 && realID <= 22) continue;

        int64_t age = nowEpoch - me.lastAccessEpoch;
        if (age > maxAgeSeconds) {
            result.filesToDelete.push_back(me.filename);
            result.freedBytes += me.byteSize;
            currentTotal = (currentTotal >= me.byteSize) ? (currentTotal - me.byteSize) : 0;
        } else {
            candidates.push_back({key, me.filename, me.byteSize, me.lastAccessEpoch, me.levelID});
        }
    }

    // si sigue por encima de la quota, evictar por lastAccess (LRU en disco)
    if (currentTotal > maxBytes) {
        std::sort(candidates.begin(), candidates.end(), [](Candidate const& a, Candidate const& b) {
            return a.lastAccess < b.lastAccess;
        });

        for (auto const& c : candidates) {
            if (currentTotal <= maxBytes) break;
            result.filesToDelete.push_back(c.filename);
            result.freedBytes += c.bytes;
            currentTotal = (currentTotal >= c.bytes) ? (currentTotal - c.bytes) : 0;
        }
    }

    return result;
}

void DiskManifest::applyPrune(PruneResult const& result) {
    if (result.filesToDelete.empty()) return;

    // indice inverso filename->key para evitar O(entries * filesToDelete)
    std::unordered_map<std::string, std::string> filenameToKey;
    filenameToKey.reserve(m_entries.size());
    for (auto const& [key, me] : m_entries) {
        if (!me.filename.empty()) {
            filenameToKey[me.filename] = key;
        }
    }

    for (auto const& filename : result.filesToDelete) {
        auto keyIt = filenameToKey.find(filename);
        if (keyIt == filenameToKey.end()) continue;

        auto entryIt = m_entries.find(keyIt->second);
        if (entryIt != m_entries.end()) {
            if (!entryIt->second.sourceUrl.empty()) {
                m_urlToKey.erase(entryIt->second.sourceUrl);
            }
            // borrar archivo fisico
            auto filePath = m_cacheDir / filename;
            std::error_code ec;
            std::filesystem::remove(filePath, ec);

            m_entries.erase(entryIt);
            m_dirty = true;
        }
    }
}

// ── Stats ───────────────────────────────────────────────────

size_t DiskManifest::totalBytes() const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return totalBytesLocked();
}

size_t DiskManifest::totalBytesLocked() const {
    size_t total = 0;
    for (auto const& [_, me] : m_entries) {
        total += me.byteSize;
    }
    return total;
}

size_t DiskManifest::entryCount() const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return m_entries.size();
}

// ── Legacy compat ───────────────────────────────────────────

std::unordered_set<int> DiskManifest::legacyKeySet() const {
    std::unordered_set<int> keys;
    keys.reserve(m_entries.size());
    for (auto const& [key, me] : m_entries) {
        if (me.levelID > 0) {
            keys.insert(me.isGif ? -me.levelID : me.levelID);
        }
    }
    return keys;
}

void DiskManifest::rebuildFromDirectory(std::filesystem::path const& cacheDir) {
    std::error_code ec;
    if (!std::filesystem::exists(cacheDir, ec)) return;

    int tmpCleaned = 0;
    for (auto const& entry : std::filesystem::directory_iterator(cacheDir, ec)) {
        if (ec || !entry.is_regular_file()) continue;
        auto ext = geode::utils::string::toLower(
            geode::utils::string::pathToString(entry.path().extension()));

        // limpiar .tmp huerfanos de escrituras atomicas interrumpidas
        if (ext == ".tmp") {
            std::error_code rmEc;
            std::filesystem::remove(entry.path(), rmEc);
            tmpCleaned++;
            continue;
        }

        if (ext != ".png" && ext != ".gif") continue;

        auto stem = geode::utils::string::pathToString(entry.path().stem());
        int id = geode::utils::numFromString<int>(stem).unwrapOr(0);
        if (id <= 0) continue;

        bool isGif = (ext == ".gif");
        size_t bytes = static_cast<size_t>(entry.file_size(ec));
        if (ec) { ec.clear(); bytes = 0; }

        DiskManifestEntry me;
        me.filename = geode::utils::string::pathToString(entry.path().filename());
        me.levelID = id;
        me.format = isGif ? "gif" : "png";
        me.byteSize = bytes;
        me.isGif = isGif;
        me.touchAccess();
        me.touchValidated();

        m_entries[makeKey(id, isGif)] = std::move(me);
    }

    m_dirty = true;
    if (tmpCleaned > 0) {
        log::info("[DiskManifest] cleaned {} orphaned .tmp files", tmpCleaned);
    }
    log::info("[DiskManifest] rebuilt {} entries from directory scan", m_entries.size());
}

} // namespace paimon::cache
