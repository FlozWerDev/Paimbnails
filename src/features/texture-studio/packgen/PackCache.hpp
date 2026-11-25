#pragma once
// byte-budgeted lru blob cache (memory + disk). stale entries read as misses, never poison.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <list>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../../utils/AtomicFileWrite.hpp"
#include "ContentHash.hpp"

namespace paimon::texture_studio::packgen {

// disk entry layout: {magic u32, version u32, payloadLen u64, fnv1a64 u64}
// followed by payloadLen raw bytes. version mirrors NodeKey::version so a
// pipeline bump invalidates entries even if the file name collides.
inline constexpr std::uint32_t kPackCacheDiskMagic = 0x50434b31u;  // "PCK1"
inline constexpr std::size_t kPackCacheDiskHeaderSize = 24u;
inline constexpr std::size_t kDefaultMaxDiskEntryBytes = 256u * 1024u * 1024u;

class PackCache {
public:
    using Bytes = std::vector<std::uint8_t>;

    struct DiskHeader {
        std::uint32_t magic = 0;
        std::uint32_t version = 0;
        std::uint64_t payloadLen = 0;
        std::uint64_t checksum = 0;
    };
    static_assert(sizeof(DiskHeader) == kPackCacheDiskHeaderSize,
                  "disk header must stay a packed 24 bytes");

    struct Stats {
        std::size_t hits = 0;
        std::size_t misses = 0;
        std::size_t evictions = 0;
        std::size_t diskHits = 0;
        std::size_t diskStores = 0;
        std::size_t bytesHeld = 0;
        double hitRate() const {
            std::size_t total = hits + misses;
            return total ? static_cast<double>(hits) / static_cast<double>(total) : 1.0;
        }
    };

    explicit PackCache(std::size_t memoryBudgetBytes = 256u * 1024u * 1024u)
        : m_budget(memoryBudgetBytes ? memoryBudgetBytes : 1) {}

    void setDiskDir(std::filesystem::path dir) {
        m_diskDir = std::move(dir);
        if (!m_diskDir.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(m_diskDir, ec);
        }
    }
    void setMemoryBudget(std::size_t bytes) {
        m_budget = bytes ? bytes : 1;
        enforceBudget();
    }
    void setMaxDiskEntryBytes(std::size_t bytes) {
        m_maxDiskEntryBytes = bytes ? bytes : 1;
    }
    std::size_t maxDiskEntryBytes() const { return m_maxDiskEntryBytes; }

    void clear() {
        m_map.clear();
        m_lru.clear();
        m_bytes = 0;
    }

    bool contains(NodeKey const& key) {
        return findLive(key) != m_map.end();
    }

    Bytes const* lookup(NodeKey const& key) {
        auto it = findLive(key);
        if (it == m_map.end()) {
            ++m_stats.misses;
            if (!m_diskDir.empty()) {
                Bytes disk;
                if (loadFromDisk(key, disk)) {
                    ++m_stats.diskHits;
                    return storeLocked(key, std::move(disk));
                }
            }
            return nullptr;
        }
        touch(it);
        ++m_stats.hits;
        return &it->second.bytes;
    }

    void store(NodeKey const& key, Bytes blob) {
        storeLocked(key, std::move(blob));
        if (!m_diskDir.empty()) {
            auto it = m_map.find(key);
            if (it != m_map.end()) saveToDisk(key, it->second.bytes);
        }
    }

    void store(NodeKey const& key, std::uint8_t const* data, std::size_t size) {
        Bytes b;
        if (data && size) b.assign(data, data + size);
        store(key, std::move(b));
    }

    bool erase(NodeKey const& key) {
        auto it = m_map.find(key);
        if (it == m_map.end()) return false;
        m_bytes -= it->second.bytes.size();
        m_lru.erase(it->second.lruIt);
        m_map.erase(it);
        if (!m_diskDir.empty()) {
            std::error_code ec;
            std::filesystem::remove(diskPath(key), ec);
        }
        return true;
    }

    Stats const& stats() const { return m_stats; }
    std::size_t bytesHeld() const { return m_bytes; }
    std::size_t entries() const { return m_map.size(); }

private:
    struct Entry {
        Bytes bytes;
        typename std::list<NodeKey>::iterator lruIt;
    };

    std::unordered_map<NodeKey, Entry, NodeKeyHasher> m_map;
    std::list<NodeKey> m_lru;  // front = most recently used
    std::size_t m_bytes = 0;
    std::size_t m_budget = 256u * 1024u * 1024u;
    std::size_t m_maxDiskEntryBytes = kDefaultMaxDiskEntryBytes;
    std::filesystem::path m_diskDir;
    Stats m_stats;

    typename decltype(m_map)::iterator findLive(NodeKey const& key) {
        return m_map.find(key);
    }

    void touch(typename decltype(m_map)::iterator it) {
        m_lru.erase(it->second.lruIt);
        m_lru.push_front(it->first);
        it->second.lruIt = m_lru.begin();
    }

    Bytes const* storeLocked(NodeKey const& key, Bytes blob) {
        auto it = m_map.find(key);
        if (it != m_map.end()) {
            m_bytes -= it->second.bytes.size();
            it->second.bytes = std::move(blob);
            m_bytes += it->second.bytes.size();
            touch(it);
            enforceBudget();
            return &it->second.bytes;
        }
        m_lru.push_front(key);
        Entry e{std::move(blob), m_lru.begin()};
        m_bytes += e.bytes.size();
        m_map.emplace(key, std::move(e));
        enforceBudget();
        auto live = m_map.find(key);
        return live == m_map.end() ? nullptr : &live->second.bytes;
    }

    void enforceBudget() {
        while (!m_lru.empty() && m_bytes > m_budget) {
            NodeKey victim = m_lru.back();
            auto it = m_map.find(victim);
            if (it == m_map.end()) {
                m_lru.pop_back();
                continue;
            }
            m_bytes -= it->second.bytes.size();
            m_map.erase(it);
            m_lru.pop_back();
            ++m_stats.evictions;
        }
    }

    std::filesystem::path diskPath(NodeKey const& key) const {
        char name[48];
        std::snprintf(name, sizeof(name), "%016llx_v%d.bin",
                      static_cast<unsigned long long>(key.hash), key.version);
        return m_diskDir / name;
    }

    bool loadFromDisk(NodeKey const& key, Bytes& out) {
        out.clear();
        auto const path = diskPath(key);
        // NOTE (Windows): MSVC opens ifstream without delete-sharing, so
        // remove() fails while the handle is open. dropPoison always closes
        // the stream first; close() on a non-open stream is a no-op.
        std::ifstream f;
        auto const dropPoison = [&path, &f] {
            f.close();
            std::error_code rmEc;
            std::filesystem::remove(path, rmEc);
        };
        // pre-size bound: a corrupt/foreign multi-GB .bin must never reach
        // vector::resize (bad_alloc/OOM, crash #6). oversize reads as a miss.
        std::error_code ec;
        auto const fileBytes = std::filesystem::file_size(path, ec);
        if (ec) return false;
        if (fileBytes < kPackCacheDiskHeaderSize ||
            fileBytes > kPackCacheDiskHeaderSize + m_maxDiskEntryBytes) {
            dropPoison();
            return false;
        }
        f.open(path, std::ios::binary);
        if (!f) return false;
        DiskHeader header{};
        static_assert(sizeof(header) == kPackCacheDiskHeaderSize);
        f.read(reinterpret_cast<char*>(&header), static_cast<std::streamsize>(sizeof(header)));
        if (!f) {
            dropPoison();
            return false;
        }
        if (header.magic != kPackCacheDiskMagic ||
            header.version != static_cast<std::uint32_t>(key.version) ||
            header.payloadLen > m_maxDiskEntryBytes ||
            header.payloadLen >
                static_cast<std::uint64_t>((std::numeric_limits<std::streamsize>::max)()) ||
            static_cast<std::uint64_t>(fileBytes) !=
                kPackCacheDiskHeaderSize + header.payloadLen) {
            dropPoison();
            return false;
        }
        out.resize(static_cast<std::size_t>(header.payloadLen));
        if (!out.empty()) {
            f.read(reinterpret_cast<char*>(out.data()),
                   static_cast<std::streamsize>(out.size()));
            if (!f) {
                out.clear();
                dropPoison();
                return false;
            }
        }
        // rename-published entries are never truncated mid-write, but a
        // foreign .bin with trailing garbage must not be accepted as a hit.
        if (f.get() != std::char_traits<char>::eof()) {
            out.clear();
            dropPoison();
            return false;
        }
        if (hashBytes(out.data(), out.size()) != header.checksum) {
            out.clear();
            dropPoison();
            return false;
        }
        return true;
    }

    void saveToDisk(NodeKey const& key, Bytes const& blob) {
        if (blob.size() > m_maxDiskEntryBytes) return;
        if (blob.size() >
            static_cast<std::size_t>((std::numeric_limits<std::streamsize>::max)())) {
            return;
        }
        DiskHeader header{};
        header.magic = kPackCacheDiskMagic;
        header.version = static_cast<std::uint32_t>(key.version);
        header.payloadLen = static_cast<std::uint64_t>(blob.size());
        header.checksum = hashBytes(blob.data(), blob.size());
        // single contiguous wire image so publication via tmp+rename is
        // all-or-nothing: readers never observe a truncated entry.
        Bytes wire(sizeof(header) + blob.size());
        std::memcpy(wire.data(), &header, sizeof(header));
        if (!blob.empty()) std::memcpy(wire.data() + sizeof(header), blob.data(), blob.size());
        if (paimon::file::writeAtomically(
                diskPath(key), std::span<std::uint8_t const>(wire.data(), wire.size()))) {
            ++m_stats.diskStores;
        }
    }
};

}  // namespace paimon::texture_studio::packgen
