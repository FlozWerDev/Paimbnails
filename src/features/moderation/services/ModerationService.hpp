#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/function.hpp>
#include "PendingQueue.hpp"
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <unordered_map>
#include <mutex>

/**
 * ModerationService — autenticacion de moderadores y operaciones sobre la
 * cola de verificacion.  Extraido de ThumbnailAPI.
 */
class ModerationService {
public:
    using ModeratorCallback = geode::CopyableFunction<void(bool isModerator, bool isAdmin)>;
    using ActionCallback    = geode::CopyableFunction<void(bool success, std::string const& message)>;
    using QueueCallback     = geode::CopyableFunction<void(bool success, std::vector<PendingItem> const& items)>;

    static ModerationService& get() {
        static ModerationService instance;
        return instance;
    }

    void setServerEnabled(bool enabled) { m_serverEnabled = enabled; }

    // chequeo mod del usuario actual (con efectos secundarios: saved-values, .dat)
    void checkModerator(std::string const& username, ModeratorCallback callback);
    // chequeo seguro con accountID explícito
    void checkModeratorAccount(std::string const& username, int accountID, ModeratorCallback callback);
    // chequeo público sin efectos secundarios (para badges)
    void checkUserStatus(std::string const& username, ModeratorCallback callback);

    // admin ops
    void addModerator(std::string const& username, std::string const& adminUser, ActionCallback callback);
    void removeModerator(std::string const& username, std::string const& adminUser, ActionCallback callback);

    // cola de verificacion
    void syncVerificationQueue(PendingCategory category, QueueCallback callback);
    void claimQueueItem(int levelId, PendingCategory category,
                        std::string const& username, ActionCallback callback,
                        std::string const& type = "");
    void acceptQueueItem(int levelId, PendingCategory category,
                         std::string const& username, ActionCallback callback,
                         std::string const& targetFilename = "",
                         std::string const& type = "");
    void rejectQueueItem(int levelId, PendingCategory category,
                         std::string const& username, std::string const& reason,
                         ActionCallback callback,
                         std::string const& type = "");
    // reportes
    void submitReport(int levelId, std::string const& username,
                      std::string const& note, ActionCallback callback);

    // invalidar cache mod (fuerza refresh)
    void resetModCache() { m_modCache.reset(); }
    // invalidar cache de status público por username
    void resetUserStatusCache();
    void resetUserStatusCache(std::string const& username);
    // actualizar cache de status público desde bundle (evita request individual)
    void updateUserStatusCache(std::string const& username, bool isMod, bool isAdmin);

private:
    ModerationService() = default;
    ModerationService(ModerationService const&) = delete;
    ModerationService& operator=(ModerationService const&) = delete;

    bool m_serverEnabled = true;

    struct ModCacheEntry {
        bool isMod   = false;
        bool isAdmin = false;
        std::chrono::steady_clock::time_point timestamp;
    };
    std::optional<ModCacheEntry> m_modCache;
    static constexpr int MOD_CACHE_TTL_SECONDS = 1800; // 30 minutes

    bool tryModCache(ModeratorCallback& callback);
    void updateModCache(bool isMod, bool isAdmin);

    // per-username cache for checkUserStatus (public status, no side effects)
    struct UserStatusCacheEntry {
        bool isMod   = false;
        bool isAdmin = false;
        std::chrono::steady_clock::time_point cachedAt;
    };
    mutable std::mutex m_userStatusMutex;
    std::unordered_map<std::string, UserStatusCacheEntry> m_userStatusCache;
    static constexpr int USER_STATUS_CACHE_TTL_SECONDS = 600; // 10 minutes

    bool tryUserStatusCache(std::string const& username, ModeratorCallback& callback);
};
