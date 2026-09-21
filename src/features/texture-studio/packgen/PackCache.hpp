#pragma once
// PackCache: byte-budgeted LRU blob cache (memory + disk). Entries validate
// by (hash, version, schema), so stale caches read as misses, never poison.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include "ContentHash.hpp"

namespace paimon::texture_studio::packgen {

class PackCache {
public:
    using Bytes = std::vector<std::uint8_t>;

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
    std::filesystem::path m_diskDir;
    Stats m_stats;

    auto findLive(typename decltype(m_map)::iterator end) { return end; }

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
        std::ifstream f(diskPath(key), std::ios::binary);
        if (!f) return false;
        f.seekg(0, std::ios::end);
        std::streampos end = f.tellg();
        if (end < 0) return false;
        f.seekg(0, std::ios::beg);
        out.resize(static_cast<std::size_t>(end));
        if (!out.empty()) {
            f.read(reinterpret_cast<char*>(out.data()),
                   static_cast<std::streamsize>(out.size()));
            if (!f) {
                out.clear();
                return false;
            }
        }
        return true;
    }

    void saveToDisk(NodeKey const& key, Bytes const& blob) {
        std::ofstream f(diskPath(key), std::ios::binary | std::ios::trunc);
        if (!f) return;
        if (!blob.empty()) {
            f.write(reinterpret_cast<char const*>(blob.data()),
                    static_cast<std::streamsize>(blob.size()));
            if (f) ++m_stats.diskStores;
        } else {
            ++m_stats.diskStores;
        }
    }
};

}  // namespace paimon::texture_studio::packgen
