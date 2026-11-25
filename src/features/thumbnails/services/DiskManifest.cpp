#include "../../../utils/FilesystemPath.hpp"
#include "DiskManifest.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>
#include <matjson.hpp>
#include <fstream>
#include <algorithm>
#include <atomic>
#include "../../../utils/AtomicFileWrite.hpp"

using namespace geode::prelude;

namespace paimon::cache {

static constexpr const char* MANIFEST_FILENAME = "manifest.json";

void DiskManifest::load(std::filesystem::path const& cacheDir) {
    std::lock_guard publishLock(m_flushMutex);
    std::lock_guard<std::recursive_mutex> lock(mutex);
    m_cacheDir = cacheDir;
    m_manifestPath = cacheDir / MANIFEST_FILENAME;
    m_entries.clear();
    m_urlToKey.clear();
    m_totalBytes = 0;
    m_dirty = false;

    std::error_code ec;
    if (!std::filesystem::exists(m_manifestPath, ec)) {
        // no manifest yet: scan the folder to migrate from the pre-manifest version.
        rebuildFromDirectory(cacheDir);
        return;
    }

    // one directory pass collects the files that actually exist and drops
    // .tmp orphans from interrupted atomic writes, replacing one stat per
    // manifest entry plus a second cleanup scan.
    std::unordered_set<std::string> presentFiles;
    {
        int tmpCleaned = 0;
        for (auto const& entry : std::filesystem::directory_iterator(cacheDir, ec)) {
            if (ec || !entry.is_regular_file()) continue;
            auto name = paimon::paths::toUtf8(entry.path().filename());
            auto ext = geode::utils::string::toLower(
                paimon::paths::toUtf8(entry.path().extension()));
            if (ext == ".tmp") {
                std::error_code rmEc;
                std::filesystem::remove(entry.path(), rmEc);
                tmpCleaned++;
                continue;
            }
            presentFiles.insert(std::move(name));
        }
        ec.clear();
        if (tmpCleaned > 0) {
            log::info("[DiskManifest] cleaned {} orphaned .tmp files", tmpCleaned);
        }
    }

    auto readRes = geode::utils::file::readString(paimon::paths::toSdkPath(m_manifestPath));
    if (!readRes) {
        log::warn("[DiskManifest] could not open manifest for reading");
        return;
    }

    auto parseResult = matjson::parse(readRes.unwrap());
    if (!parseResult.isOk()) {
        log::warn("[DiskManifest] manifest parse error, rebuilding from directory scan");
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
        // signed field in json: clamp negatives to 0 instead of wrapping to huge size_t.
        {
            int rawBytes = val["byteSize"].asInt().unwrapOr(0);
            me.byteSize = rawBytes > 0 ? static_cast<size_t>(rawBytes) : 0;
        }
        me.lastAccessEpoch = val["lastAccess"].asInt().unwrapOr(0);
        me.lastValidatedEpoch = val["lastValidated"].asInt().unwrapOr(0);
        me.isGif = val["isGif"].asBool().unwrapOr(false);
        me.qualityTag = val["qualityTag"].asString().unwrapOr("");

        if (!me.filename.empty()) {
            // verbatim join is a traversal risk with a hand-edited manifest:
            // reject absolute paths, separators and dot-segments.
            std::filesystem::path name(me.filename);
            bool badName = name.empty() || name.is_absolute() || name.has_parent_path()
                || me.filename == "." || me.filename == ".."
                || me.filename.find('/') != std::string::npos
                || me.filename.find('\\') != std::string::npos;
            if (badName) {
                orphans++;
                m_dirty = true;
                continue;
            }
            if (!presentFiles.contains(me.filename)) {
                orphans++;
                m_dirty = true;
                continue;
            }
        }

        if (auto old = m_entries.find(key); old != m_entries.end()) {
            m_totalBytes = old->second.byteSize <= m_totalBytes
                ? m_totalBytes - old->second.byteSize : 0;
        }
        m_totalBytes += me.byteSize;
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
}

void DiskManifest::flush() {
    std::lock_guard publishLock(m_flushMutex);
    decltype(m_entries) snapshot;
    std::filesystem::path path;
    {
        std::lock_guard lock(mutex);
        if (!m_dirty) return;
        snapshot = m_entries;
        path = m_manifestPath;
        // a mutation after this snapshot must remain dirty, including while writing.
        m_dirty = false;
    }

    try {
        matjson::Value root = matjson::makeObject({});

        for (auto const& [key, me] : snapshot) {
            // fields left at their parser defaults are omitted; load()
            // reads both this compact form and the legacy full form.
            matjson::Value entry = matjson::makeObject({});
            if (!me.filename.empty()) entry["filename"] = me.filename;
            if (me.levelID != 0) entry["levelID"] = me.levelID;
            if (!me.sourceUrl.empty()) entry["sourceUrl"] = me.sourceUrl;
            if (!me.revisionToken.empty()) entry["revisionToken"] = me.revisionToken;
            if (!me.format.empty()) entry["format"] = me.format;
            if (me.width != 0) entry["width"] = me.width;
            if (me.height != 0) entry["height"] = me.height;
            if (me.byteSize != 0) entry["byteSize"] = static_cast<int64_t>(me.byteSize);
            if (me.lastAccessEpoch != 0) entry["lastAccess"] = me.lastAccessEpoch;
            if (me.lastValidatedEpoch != 0) entry["lastValidated"] = me.lastValidatedEpoch;
            if (me.isGif) entry["isGif"] = me.isGif;
            if (!me.qualityTag.empty()) entry["qualityTag"] = me.qualityTag;
            root[key] = std::move(entry);
        }

        if (!paimon::file::writeAtomically(path, root.dump(matjson::NO_INDENTATION))) {
            std::lock_guard lock(mutex);
            m_dirty = true;
            log::error("[DiskManifest] Could not atomically replace manifest");
            return;
        }

        log::debug("[DiskManifest] flushed {} entries", snapshot.size());
    } catch (...) {
        std::lock_guard lock(mutex);
        m_dirty = true;
        throw;
    }
}

void DiskManifest::clearPreservingMainLevels() {
    std::lock_guard lock(mutex);
    int kept = 0;
    int removed = 0;
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        int realID = it->second.levelID > 0 ? it->second.levelID : 0;
        if (realID >= 1 && realID <= 22) {
            ++kept;
            ++it;
            continue;
        }
        if (!it->second.sourceUrl.empty()) {
            m_urlToKey.erase(it->second.sourceUrl);
        }
        m_totalBytes = it->second.byteSize <= m_totalBytes
            ? m_totalBytes - it->second.byteSize : 0;
        it = m_entries.erase(it);
        ++removed;
    }
    if (removed > 0 || kept > 0) {
        log::info("[DiskManifest] clearPreservingMainLevels: kept {} main-level entries, removed {} others",
            kept, removed);
    }
    m_dirty = true;
}

size_t DiskManifest::applyPrune(PruneResult const& result) {
    std::vector<std::filesystem::path> quarantined;
    {
        std::lock_guard lock(mutex);
        if (result.items.empty()) return 0;

        // renames are short metadata operations. File deletion happens after the
        // lock is released, so a UI lookup never waits for the disk.
        static std::atomic<uint64_t> sequence{0};
        for (auto const& item : result.items) {
            // same traversal guard as load(): prune lists come from disk state.
            if (item.filename.empty() || item.filename == "." || item.filename == ".."
                || item.filename.find('/') != std::string::npos
                || item.filename.find('\\') != std::string::npos) {
                continue;
            }

            auto entryIt = m_entries.find(item.key);
            if (entryIt == m_entries.end()) continue;

            // a download or access may have replaced the entry after computePrune.
            // only prune exactly the version that was selected.
            auto const& entry = entryIt->second;
            if (entry.filename != item.filename
                || entry.byteSize != item.byteSize
                || entry.lastAccessEpoch != item.lastAccessEpoch) {
                continue;
            }

            auto filePath = m_cacheDir / item.filename;
            std::error_code ec;
            bool const exists = std::filesystem::exists(filePath, ec);
            if (exists) {
                auto const quarantineName = item.filename + ".prune-"
                    + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)) + ".tmp";
                auto const quarantinePath = m_cacheDir / quarantineName;
                // a crashed previous run may have left this disposable name behind.
                std::filesystem::remove(quarantinePath, ec);
                ec.clear();
                std::filesystem::rename(filePath, quarantinePath, ec);
                if (ec) {
                    log::warn("[DiskManifest] could not quarantine {}: {}",
                        item.filename, ec.message());
                    // keep the entry so a later prune can retry.
                    continue;
                }
                quarantined.push_back(quarantinePath);
            }

            if (!entry.sourceUrl.empty()) {
                m_urlToKey.erase(entry.sourceUrl);
            }
            m_totalBytes = entry.byteSize <= m_totalBytes
                ? m_totalBytes - entry.byteSize : 0;
            m_entries.erase(entryIt);
            m_dirty = true;
        }
    }

    for (auto const& path : quarantined) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        if (ec) {
            log::warn("[DiskManifest] could not delete quarantined {}: {}",
                path.filename().string(), ec.message());
        }
    }
    return quarantined.size();
}

void DiskManifest::rebuildFromDirectory(std::filesystem::path const& cacheDir) {
    std::error_code ec;
    if (!std::filesystem::exists(cacheDir, ec)) return;

    int tmpCleaned = 0;
    for (auto const& entry : std::filesystem::directory_iterator(cacheDir, ec)) {
        if (ec || !entry.is_regular_file()) continue;
        auto ext = geode::utils::string::toLower(
            paimon::paths::toUtf8(entry.path().extension()));

        if (ext == ".tmp") {
            std::error_code rmEc;
            std::filesystem::remove(entry.path(), rmEc);
            tmpCleaned++;
            continue;
        }

        if (ext != ".png" && ext != ".gif") continue;

        auto stem = paimon::paths::toUtf8(entry.path().stem());
        int id = geode::utils::numFromString<int>(stem).unwrapOr(0);
        if (id <= 0) continue;

        bool isGif = (ext == ".gif");
        size_t bytes = static_cast<size_t>(entry.file_size(ec));
        if (ec) { ec.clear(); bytes = 0; }

        DiskManifestEntry me;
        me.filename = paimon::paths::toUtf8(entry.path().filename());
        me.levelID = id;
        me.format = isGif ? "gif" : "png";
        me.byteSize = bytes;
        me.isGif = isGif;
        me.touchAccess();
        me.touchValidated();

        auto const key = makeKey(id, isGif);
        if (auto old = m_entries.find(key); old != m_entries.end()) {
            m_totalBytes = old->second.byteSize <= m_totalBytes
                ? m_totalBytes - old->second.byteSize : 0;
        }
        m_totalBytes += me.byteSize;
        m_entries[key] = std::move(me);
    }

    m_dirty = true;
    if (tmpCleaned > 0) {
        log::info("[DiskManifest] cleaned {} orphaned .tmp files", tmpCleaned);
    }
    log::info("[DiskManifest] rebuilt {} entries from directory scan", m_entries.size());
}

} // namespace paimon::cache
