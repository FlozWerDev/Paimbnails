#pragma once

#include <string>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <mutex>
#include <chrono>

// Cache de roles de moderador/admin por username.
// Singleton thread-safe con eviction LRU.
// Reemplaza los extern globales de BadgeCache.hpp.

struct ModStatus {
    bool isMod = false;
    bool isAdmin = false;
};

struct ModCacheEntry {
    ModStatus status;
    std::chrono::steady_clock::time_point timestamp;
};

class ModeratorCache {
public:
    static ModeratorCache& get() {
        static ModeratorCache instance;
        return instance;
    }

    // inserta o actualiza el rol de un usuario.
    // admin implica mod (convencion del proyecto).
    void insert(std::string const& username, bool isMod, bool isAdmin) {
        std::lock_guard<std::mutex> lock(m_mutex);

        // normalizar: admin siempre es mod
        if (isAdmin) isMod = true;
        auto now = std::chrono::steady_clock::now();
        purgeExpiredLocked(now);

        if (m_cache.find(username) != m_cache.end()) {
            m_cache[username] = {{isMod, isAdmin}, now};
            touchLruLocked(username);
            return;
        }

        // purgar la mitad mas antigua si superamos el limite
        while (m_cache.size() >= MAX_SIZE && !m_order.empty()) {
            auto oldest = m_order.front();
            m_cache.erase(oldest);
            m_orderSet.erase(oldest);
            m_order.pop_front();
        }

        m_cache[username] = {{isMod, isAdmin}, now};
        m_order.push_back(username);
        m_orderSet.insert(username);
    }

    // devuelve el estado del usuario si esta en cache
    std::optional<ModStatus> lookup(std::string const& username) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto now = std::chrono::steady_clock::now();
        purgeExpiredLocked(now);

        auto it = m_cache.find(username);
        if (it == m_cache.end()) return std::nullopt;
        touchLruLocked(username);
        return it->second.status;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cache.clear();
        m_order.clear();
        m_orderSet.clear();
    }

private:
    ModeratorCache() = default;
    ModeratorCache(ModeratorCache const&) = delete;
    ModeratorCache& operator=(ModeratorCache const&) = delete;

    static constexpr size_t MAX_SIZE = 200;
    static constexpr auto TTL = std::chrono::minutes(30);
    std::mutex m_mutex;
    std::unordered_map<std::string, ModCacheEntry> m_cache;
    std::list<std::string> m_order;
    std::unordered_set<std::string> m_orderSet;

    void touchLruLocked(std::string const& username) {
        if (m_orderSet.find(username) == m_orderSet.end()) return;
        m_order.remove(username);
        m_order.push_back(username);
    }

    void purgeExpiredLocked(std::chrono::steady_clock::time_point now) {
        for (auto it = m_cache.begin(); it != m_cache.end();) {
            if (now - it->second.timestamp > TTL) {
                m_order.remove(it->first);
                m_orderSet.erase(it->first);
                it = m_cache.erase(it);
            } else {
                ++it;
            }
        }
    }
};

// === Wrappers legacy (definidos en BadgeHooks.cpp) ===
// Mantienen compatibilidad con código que usaba BadgeCache.hpp.
void moderatorCacheInsert(std::string const& username, bool isMod, bool isAdmin);
bool moderatorCacheGet(std::string const& username, bool& isMod, bool& isAdmin);

// muestra el popup con info del badge (implementada en BadgeHooks.cpp)
void showBadgeInfoPopup(cocos2d::CCNode* sender);
