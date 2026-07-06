#pragma once
#include <Geode/Geode.hpp>
#include <string>
#include <unordered_map>
#include <chrono>
#include <mutex>

class CustomBadgeService {
public:
    using BadgeCallback  = geode::CopyableFunction<void(bool success, std::string const& emoteName)>;
    using ActionCallback = geode::CopyableFunction<void(bool success, std::string const& message)>;

    static CustomBadgeService& get() {
        static CustomBadgeService instance;
        return instance;
    }

    void fetchBadge(int accountID, BadgeCallback callback);

    void setBadge(int accountID, std::string const& emoteName, ActionCallback callback);

    void clearBadge(int accountID, ActionCallback callback);

    void invalidateCache(int accountID);

    void updateCacheFromBundle(int accountID, std::string const& emoteName);

private:
    CustomBadgeService() = default;
    CustomBadgeService(CustomBadgeService const&) = delete;
    CustomBadgeService& operator=(CustomBadgeService const&) = delete;

    struct CacheEntry {
        std::string emoteName;
        std::chrono::steady_clock::time_point cachedAt;
    };

    struct PendingBadgeRequest {
        int accountID;
        BadgeCallback callback;
    };

    mutable std::mutex m_mutex;
    std::unordered_map<int, CacheEntry> m_cache;
    static constexpr auto CACHE_TTL = std::chrono::minutes(30);

    std::vector<PendingBadgeRequest> m_pendingRequests;
    bool m_flushScheduled = false;
    void flushPendingRequests();
};