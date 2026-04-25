#pragma once
#include <Geode/Geode.hpp>
#include <Geode/utils/function.hpp>
#include <string>
#include <unordered_map>
#include <chrono>
#include <mutex>

// Cache de badges personalizados de emotes
class CustomBadgeService {
public:
    using BadgeCallback  = geode::CopyableFunction<void(bool success, std::string const& emoteName)>;
    using ActionCallback = geode::CopyableFunction<void(bool success, std::string const& message)>;

    static CustomBadgeService& get() {
        static CustomBadgeService instance;
        return instance;
    }

    // Obtiene el badge del usuario
    void fetchBadge(int accountID, BadgeCallback callback);

    // Sube un badge para el usuario
    void setBadge(int accountID, std::string const& emoteName, ActionCallback callback);

    // Elimina el badge del usuario
    void clearBadge(int accountID, ActionCallback callback);

    // Borra el cache de un usuario
    void invalidateCache(int accountID);

    // Actualiza el cache desde un bundle
    void updateCacheFromBundle(int accountID, std::string const& emoteName);

private:
    CustomBadgeService() = default;
    CustomBadgeService(CustomBadgeService const&) = delete;
    CustomBadgeService& operator=(CustomBadgeService const&) = delete;

    struct CacheEntry {
        std::string emoteName; // vacio = sin badge
        std::chrono::steady_clock::time_point cachedAt;
    };

    struct PendingBadgeRequest {
        int accountID;
        BadgeCallback callback;
    };

    mutable std::mutex m_mutex;
    std::unordered_map<int, CacheEntry> m_cache;
    static constexpr auto CACHE_TTL = std::chrono::minutes(30);

    // Junta peticiones en batch para evitar multiples requests
    std::vector<PendingBadgeRequest> m_pendingRequests;
    bool m_flushScheduled = false;
    void flushPendingRequests();
};