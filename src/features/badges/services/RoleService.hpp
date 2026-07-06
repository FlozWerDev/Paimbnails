#pragma once

#include <Geode/Geode.hpp>
#include "../../../utils/HttpClient.hpp"
#include "../../moderation/services/ModeratorCache.hpp"
#include "../../moderation/services/ModerationService.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include <array>
#include <chrono>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace paimon::roles {

// Canonical role identifiers. Kept in sync with the server's role keys.
inline constexpr char const* kAdmin  = "admin";
inline constexpr char const* kMod    = "mod";
inline constexpr char const* kVip    = "vip";
inline constexpr char const* kHelper = "helper";
inline constexpr char const* kIdea   = "idea";

struct UserRoles {
    bool admin  = false;
    bool mod    = false;
    bool vip    = false;
    bool helper = false;
    bool idea   = false;

    bool any() const { return admin || mod || vip || helper || idea; }

    static UserRoles fromFlags(HttpClient::UserRoleFlags const& f) {
        UserRoles r;
        r.admin  = f.isAdmin;
        r.mod    = f.isMod || f.isAdmin;
        r.vip    = f.isVip;
        r.helper = f.isHelper;
        r.idea   = f.isIdea;
        return r;
    }
};

// Small LRU + TTL cache of the full role set per username, with in-flight
// coalescing so the badge renderers in comment cells don't spam the server.
class RoleService {
public:
    using Callback = geode::CopyableFunction<void(UserRoles)>;

    static RoleService& get() {
        static RoleService instance;
        return instance;
    }

    std::optional<UserRoles> lookup(std::string const& username) {
        std::string key = geode::utils::string::toLower(username);
        std::lock_guard<std::mutex> lock(m_mutex);
        auto now = std::chrono::steady_clock::now();
        purgeExpiredLocked(now);
        auto it = m_cache.find(key);
        if (it == m_cache.end()) return std::nullopt;
        touchLruLocked(key);
        return it->second.roles;
    }

    void update(std::string const& username, UserRoles roles) {
        std::string key = geode::utils::string::toLower(username);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            insertLocked(key, roles);
        }
        // Keep the legacy mod/admin caches consistent.
        moderatorCacheInsert(username, roles.mod, roles.admin);
        ModerationService::get().updateUserStatusCache(username, roles.mod, roles.admin);
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cache.clear();
        m_order.clear();
        m_orderSet.clear();
    }

    // Resolve the full role set for a username. The callback always runs on the
    // main thread. Coalesces concurrent requests for the same user.
    void fetch(std::string const& username, Callback cb) {
        if (username.empty()) { dispatch(std::move(cb), {}); return; }

        if (auto cached = lookup(username)) {
            dispatch(std::move(cb), *cached);
            return;
        }

        std::string key = geode::utils::string::toLower(username);
        {
            std::lock_guard<std::mutex> lock(m_inflightMutex);
            auto it = m_inflight.find(key);
            if (it != m_inflight.end()) {
                it->second.push_back(std::move(cb));
                return;
            }
            m_inflight[key].push_back(std::move(cb));
        }

        // The server keys roles purely by username; accountID is only needed for
        // mod-code issuance, so the viewer's own accountID is enough for auth.
        int viewerAccountID = 0;
        if (auto* am = GJAccountManager::get()) viewerAccountID = am->m_accountID;

        HttpClient::get().checkUserRoles(username, viewerAccountID,
            [this, key, username](HttpClient::UserRoleFlags const& flags, bool ok) {
                UserRoles roles = ok ? UserRoles::fromFlags(flags) : UserRoles{};
                if (ok) update(username, roles);
                resolveInflight(key, roles);
            });
    }

private:
    RoleService() = default;
    RoleService(RoleService const&) = delete;
    RoleService& operator=(RoleService const&) = delete;

    struct Entry {
        UserRoles roles;
        std::chrono::steady_clock::time_point timestamp;
    };

    static constexpr size_t MAX_SIZE = 200;
    static constexpr auto TTL = std::chrono::minutes(30);

    std::mutex m_mutex;
    std::unordered_map<std::string, Entry> m_cache;
    std::list<std::string> m_order;
    std::unordered_set<std::string> m_orderSet;

    std::mutex m_inflightMutex;
    std::unordered_map<std::string, std::vector<Callback>> m_inflight;

    static void dispatch(Callback cb, UserRoles roles) {
        geode::Loader::get()->queueInMainThread([cb = std::move(cb), roles]() mutable {
            if (paimon::isRuntimeShuttingDown()) return;
            cb(roles);
        });
    }

    void resolveInflight(std::string const& key, UserRoles roles) {
        std::vector<Callback> waiters;
        {
            std::lock_guard<std::mutex> lock(m_inflightMutex);
            auto it = m_inflight.find(key);
            if (it == m_inflight.end()) return;
            waiters = std::move(it->second);
            m_inflight.erase(it);
        }
        for (auto& cb : waiters) dispatch(std::move(cb), roles);
    }

    void insertLocked(std::string const& key, UserRoles roles) {
        auto now = std::chrono::steady_clock::now();
        purgeExpiredLocked(now);
        if (m_cache.find(key) != m_cache.end()) {
            m_cache[key] = {roles, now};
            touchLruLocked(key);
            return;
        }
        while (m_cache.size() >= MAX_SIZE && !m_order.empty()) {
            auto oldest = m_order.front();
            m_cache.erase(oldest);
            m_orderSet.erase(oldest);
            m_order.pop_front();
        }
        m_cache[key] = {roles, now};
        m_order.push_back(key);
        m_orderSet.insert(key);
    }

    void touchLruLocked(std::string const& key) {
        if (m_orderSet.find(key) == m_orderSet.end()) return;
        m_order.remove(key);
        m_order.push_back(key);
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

} // namespace paimon::roles
