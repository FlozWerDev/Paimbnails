#pragma once

#include <Geode/Geode.hpp>
#include "PendingQueue.hpp"
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <unordered_map>
#include <mutex>

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

    void checkModerator(std::string const& username, ModeratorCallback callback);
    void checkModeratorAccount(std::string const& username, int accountID, ModeratorCallback callback);
    void checkUserStatus(std::string const& username, ModeratorCallback callback);

    void addModerator(std::string const& username, std::string const& adminUser, ActionCallback callback);
    void removeModerator(std::string const& username, std::string const& adminUser, ActionCallback callback);

    void syncVerificationQueue(PendingCategory category, QueueCallback callback);
    void claimQueueItem(int levelId, PendingCategory category,
                        std::string const& username, ActionCallback callback,
                        std::string const& type = "");
    // targetFilename picks one entry out of a level's review gallery; leaving
    // it empty acts on the newest. acceptAll publishes the whole gallery.
    void acceptQueueItem(int levelId, PendingCategory category,
                         std::string const& username, ActionCallback callback,
                         std::string const& targetFilename = "",
                         std::string const& type = "",
                         bool acceptAll = false);
    void rejectQueueItem(int levelId, PendingCategory category,
                         std::string const& username, std::string const& reason,
                         ActionCallback callback,
                         std::string const& type = "",
                         std::string const& targetFilename = "");
    void submitReport(int levelId, std::string const& username,
                      std::string const& note, ActionCallback callback);

    void resetModCache() { m_modCache.reset(); }
    void resetUserStatusCache();
    void resetUserStatusCache(std::string const& username);
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
    static constexpr int MOD_CACHE_TTL_SECONDS = 1800;

    bool tryModCache(ModeratorCallback& callback);
    void updateModCache(bool isMod, bool isAdmin);

    struct UserStatusCacheEntry {
        bool isMod   = false;
        bool isAdmin = false;
        std::chrono::steady_clock::time_point cachedAt;
    };
    mutable std::mutex m_userStatusMutex;
    std::unordered_map<std::string, UserStatusCacheEntry> m_userStatusCache;
    static constexpr int USER_STATUS_CACHE_TTL_SECONDS = 600;

    bool tryUserStatusCache(std::string const& username, ModeratorCallback& callback);
};
