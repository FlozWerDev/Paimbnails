#pragma once
// reactive dag: evaluate() skips hash-unchanged nodes. single-threaded by design; scheduler parallelizes across graphs.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ContentHash.hpp"
#include "PackCache.hpp"

namespace paimon::texture_studio::packgen {

class PackGraph {
public:
    using NodeId = std::size_t;
    using ComputeFn = std::function<PackCache::Bytes()>;

    struct Node {
        std::string name;
        std::vector<NodeId> deps;
        std::vector<std::uint64_t> inputs;
        int version = 1;
        ComputeFn compute;
        NodeKey lastKey{};
        bool evaluated = false;
        bool cacheable = true;
        PackCache::Bytes cached;
    };

    PackGraph() = default;
    void setCache(PackCache* cache) { m_cache = cache; }

    NodeId addNode(std::string name, ComputeFn fn,
                   std::vector<NodeId> deps = {},
                   std::vector<std::uint64_t> inputs = {},
                   int version = 1, bool cacheable = true) {
        Node n;
        n.name = std::move(name);
        n.deps = std::move(deps);
        n.inputs = std::move(inputs);
        n.version = version;
        n.compute = std::move(fn);
        n.cacheable = cacheable;
        m_nodes.push_back(std::move(n));
        return m_nodes.size() - 1;
    }

    void setInputs(NodeId id, std::vector<std::uint64_t> inputs) {
        m_nodes.at(id).inputs = std::move(inputs);
    }

    void markDirty(NodeId id) {
        m_nodes.at(id).evaluated = false;
        for (NodeId i = 0; i < m_nodes.size(); ++i) {
            for (NodeId d : m_nodes[i].deps) {
                if (d == id) markDirty(i);
            }
        }
    }

    void markAllDirty() {
        for (auto& n : m_nodes) n.evaluated = false;
    }

    struct EvalStats {
        std::size_t computed = 0;
        std::size_t pruned = 0;
        std::size_t cacheHits = 0;
    };

    // deterministic topo order; false only on cycle or missing compute fn.
    bool evaluate(EvalStats* outStats = nullptr) {
        EvalStats stats;
        std::vector<char> state(m_nodes.size(), 0);  // 0=unvisited 1=in-stack 2=done
        std::vector<NodeId> order;
        order.reserve(m_nodes.size());
        for (NodeId i = 0; i < m_nodes.size(); ++i) {
            if (!visit(i, state, order)) return false;
        }
        for (NodeId id : order) {
            Node& n = m_nodes[id];
            NodeKey key{combinedHash(n), n.version};
            if (n.evaluated && n.lastKey == key) {
                ++stats.pruned;
                continue;
            }
            if (n.cacheable && m_cache) {
                if (auto const* hit = m_cache->lookup(key)) {
                    n.lastKey = key;
                    n.evaluated = true;
                    n.cached = *hit;
                    ++stats.cacheHits;
                    ++stats.pruned;
                    continue;
                }
            }
            if (!n.compute) return false;
            PackCache::Bytes out = n.compute();
            n.cached = out;
            if (n.cacheable && m_cache) m_cache->store(key, out);
            n.lastKey = key;
            n.evaluated = true;
            ++stats.computed;
        }
        if (outStats) *outStats = stats;
        m_lastStats = stats;
        return true;
    }

    PackCache::Bytes const* result(NodeId id) const {
        return &m_nodes.at(id).cached;
    }

    EvalStats const& lastStats() const { return m_lastStats; }
    std::size_t nodeCount() const { return m_nodes.size(); }

    Node const& node(NodeId id) const { return m_nodes.at(id); }

private:
    std::vector<Node> m_nodes;
    PackCache* m_cache = nullptr;
    EvalStats m_lastStats;

    bool visit(NodeId id, std::vector<char>& state, std::vector<NodeId>& order) {
        if (id >= m_nodes.size()) return false;
        if (state[id] == 2) return true;
        if (state[id] == 1) return false;  // cycle
        state[id] = 1;
        for (NodeId d : m_nodes[id].deps) {
            if (!visit(d, state, order)) return false;
        }
        state[id] = 2;
        order.push_back(id);
        return true;
    }

    std::uint64_t combinedHash(Node const& n) const {
        std::uint64_t h = kFnvOffsetBasis;
        for (NodeId d : n.deps) {
            h = hashCombine(h, m_nodes.at(d).lastKey.hash);
        }
        for (std::uint64_t in : n.inputs) {
            h = hashCombine(h, in);
        }
        h = hashCombine(h, hashString(n.name));
        return h;
    }
};

// node::cached persists across runs; keep the public layout above stable.
}  // namespace paimon::texture_studio::packgen
