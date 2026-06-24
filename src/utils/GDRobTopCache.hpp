#pragma once
// GDRobTopCache — persistent on-disk cache for RobTop server responses.
// Reduces rate limits by caching read responses (profiles, level/user/list
// searches, comments, etc.) for 7 days. Live polling requests (messages,
// friend requests) aren't cached.

#include <Geode/Geode.hpp>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace paimon::gd {

constexpr char const* kRobTopBaseUrl = "https://www.boomlings.com/database/";

constexpr std::time_t kCacheTTLWeek = 7 * 24 * 60 * 60;
constexpr std::time_t kCacheTTLCommentsMin = 60;

enum class CachePolicy {
    None,
    Week,
    Comments,
};

CachePolicy policyForEndpoint(std::string const& endpoint);

class GDRobTopCache {
public:
    static GDRobTopCache& get();

    void init();
    void shutdown();

    std::optional<std::string> lookup(std::string const& category, std::string const& key);
    void store(std::string const& category, std::string const& key, std::string const& response,
               std::time_t ttl = kCacheTTLWeek);

    bool isEnabled() const;

    std::size_t entryCount() const;
    std::size_t ramHitCount() const;
    std::size_t diskHitCount() const;

private:
    GDRobTopCache() = default;

    std::filesystem::path cacheDir() const;
    std::filesystem::path pathForEntry(std::string const& category, std::string const& key) const;
    std::string entryKey(std::string const& category, std::string const& key) const;

    struct DiskEntry {
        std::string body;
        std::time_t expiresAt = 0;
    };

    void pruneExpired();
    void touchRam(std::string const& entryKey, std::string response, std::time_t expiresAt);
    void evictLRU();
    std::optional<DiskEntry> readDisk(std::filesystem::path const& path) const;
    void writeDisk(std::filesystem::path const& path, std::string const& response,
                   std::time_t ttl) const;

    mutable std::mutex m_mutex;
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_shuttingDown{false};

    struct RamEntry {
        std::string body;
        std::time_t expiresAt = 0;
        std::time_t lastAccess = 0;
    };
    std::unordered_map<std::string, RamEntry> m_ram;
    static constexpr size_t kMaxRamEntries = 500;

    std::atomic<std::size_t> m_ramHits{0};
    std::atomic<std::size_t> m_diskHits{0};
};

using PostCallback = std::function<void(bool ok, std::string body)>;

void postCached(
    std::string endpoint,
    std::string body,
    PostCallback cb,
    CachePolicy policy = CachePolicy::Week
);

std::string cacheCategoryForEndpoint(std::string const& endpoint);
std::string cacheKeyForPostBody(std::string const& endpoint, std::string const& body);

} // namespace paimon::gd