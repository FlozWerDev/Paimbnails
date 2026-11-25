#include "DiskManifest.hpp"

#include <algorithm>
#include <utility>

namespace paimon::cache {

std::string DiskManifest::makeKey(int levelID, bool isGif) const {
    return isGif ? ("-" + std::to_string(levelID)) : std::to_string(levelID);
}

bool DiskManifest::contains(int levelID, bool isGif) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return containsLocked(levelID, isGif);
}

std::optional<DiskManifestEntry> DiskManifest::getEntry(int levelID, bool isGif) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto const* entry = getEntryLocked(levelID, isGif);
    if (!entry) return std::nullopt;
    return *entry;
}

bool DiskManifest::containsLocked(int levelID, bool isGif) const {
    return m_entries.count(makeKey(levelID, isGif)) > 0;
}

DiskManifestEntry const* DiskManifest::getEntryLocked(int levelID, bool isGif) const {
    auto it = m_entries.find(makeKey(levelID, isGif));
    return it != m_entries.end() ? &it->second : nullptr;
}

void DiskManifest::upsert(int levelID, bool isGif, DiskManifestEntry entry) {
    std::lock_guard lock(mutex);
    auto key = makeKey(levelID, isGif);
    if (!entry.sourceUrl.empty()) {
        m_urlToKey[entry.sourceUrl] = key;
    }
    if (auto old = m_entries.find(key); old != m_entries.end()) {
        m_totalBytes = old->second.byteSize <= m_totalBytes
            ? m_totalBytes - old->second.byteSize : 0;
    }
    m_totalBytes += entry.byteSize;
    m_entries[key] = std::move(entry);
    m_dirty = true;
}

void DiskManifest::remove(int levelID, bool isGif) {
    std::lock_guard lock(mutex);
    auto key = makeKey(levelID, isGif);
    auto it = m_entries.find(key);
    if (it != m_entries.end()) {
        if (!it->second.sourceUrl.empty()) {
            m_urlToKey.erase(it->second.sourceUrl);
        }
        m_totalBytes = it->second.byteSize <= m_totalBytes
            ? m_totalBytes - it->second.byteSize : 0;
        m_entries.erase(it);
        m_dirty = true;
    }
}

void DiskManifest::clear() {
    std::lock_guard lock(mutex);
    m_entries.clear();
    m_urlToKey.clear();
    m_totalBytes = 0;
    m_dirty = true;
}

void DiskManifest::touchAccess(int levelID, bool isGif) {
    std::lock_guard lock(mutex);
    auto it = m_entries.find(makeKey(levelID, isGif));
    if (it != m_entries.end()) {
        it->second.touchAccess();
        // dirty every 20 touches so access times survive crashes.
        if (++m_accessCounter % 20 == 0) {
            m_dirty = true;
        }
    }
}

DiskManifest::PruneResult DiskManifest::computePrune(size_t maxBytes, std::chrono::hours maxAge) const {
    std::lock_guard lock(mutex);
    PruneResult result;
    auto nowEpoch = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    auto maxAgeSeconds = std::chrono::duration_cast<std::chrono::seconds>(maxAge).count();

    size_t currentTotal = totalBytesLocked();

    std::vector<PruneItem> candidates;
    candidates.reserve(m_entries.size());

    for (auto const& [key, me] : m_entries) {
        // main levels (1-22) never evict.
        int realID = me.levelID > 0 ? me.levelID : 0;
        if (realID >= 1 && realID <= 22) continue;

        int64_t age = nowEpoch - me.lastAccessEpoch;
        if (age > maxAgeSeconds) {
            result.items.push_back({key, me.filename, me.byteSize, me.lastAccessEpoch});
            result.freedBytes += me.byteSize;
            currentTotal = (currentTotal >= me.byteSize) ? (currentTotal - me.byteSize) : 0;
        } else {
            candidates.push_back({key, me.filename, me.byteSize, me.lastAccessEpoch});
        }
    }

    // over quota: evict oldest-first (disk lru).
    if (currentTotal > maxBytes) {
        std::sort(candidates.begin(), candidates.end(), [](PruneItem const& a, PruneItem const& b) {
            return a.lastAccessEpoch < b.lastAccessEpoch;
        });

        for (auto const& c : candidates) {
            if (currentTotal <= maxBytes) break;
            result.items.push_back(c);
            result.freedBytes += c.byteSize;
            currentTotal = (currentTotal >= c.byteSize) ? (currentTotal - c.byteSize) : 0;
        }
    }

    return result;
}

size_t DiskManifest::totalBytes() const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return totalBytesLocked();
}

size_t DiskManifest::totalBytesLocked() const {
    return m_totalBytes;
}

size_t DiskManifest::entryCount() const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return m_entries.size();
}

} // namespace paimon::cache
