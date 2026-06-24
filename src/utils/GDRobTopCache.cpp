#include "GDRobTopCache.hpp"

#include "WebHelper.hpp"
#include "../core/RuntimeLifecycle.hpp"

#include <Geode/loader/Log.hpp>
#include <Geode/utils/string.hpp>
#include <matjson.hpp>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <shared_mutex>
#include <system_error>

using namespace geode::prelude;

namespace paimon::gd {

namespace {

struct PendingBatch {
    std::vector<PostCallback> callbacks;
};

std::mutex& pendingMutex() {
    static std::mutex m;
    return m;
}

std::unordered_map<std::string, PendingBatch>& pendingRequests() {
    static auto* map = new std::unordered_map<std::string, PendingBatch>();
    return *map;
}

std::string hashKey(std::string const& input) {
    auto h = std::hash<std::string>{}(input);
    return fmt::format("{:016x}", static_cast<std::uint64_t>(h));
}

std::string sanitizeCategory(std::string category) {
    for (auto& ch : category) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '_') {
            ch = '_';
        }
    }
    return category;
}

std::time_t commentsTTL() {
    auto rate = Mod::get()->getSettingValue<int64_t>("mentions-refresh-rate");
    rate = std::clamp<int64_t>(rate, 10, 300);
    return static_cast<std::time_t>(std::max<int64_t>(rate, kCacheTTLCommentsMin));
}

std::time_t ttlForPolicy(CachePolicy policy) {
    switch (policy) {
        case CachePolicy::None: return 0;
        case CachePolicy::Comments: return commentsTTL();
        case CachePolicy::Week:
        default: return kCacheTTLWeek;
    }
}

bool isCacheableResponse(std::string const& body) {
    if (body.empty() || body == "-1") return false;
    return true;
}

void trimResponseTail(std::string& body) {
    while (!body.empty() && (body.back() == '\n' || body.back() == '\r' ||
                             body.back() == ' ' || body.back() == '\t')) {
        body.pop_back();
    }
}

} // namespace

CachePolicy policyForEndpoint(std::string const& endpoint) {
    if (endpoint == "getGJMessages20.php" || endpoint == "getGJFriendRequests20.php") {
        return CachePolicy::None;
    }
    if (endpoint == "getGJComments21.php") {
        return CachePolicy::Comments;
    }
    return CachePolicy::Week;
}

std::string cacheCategoryForEndpoint(std::string const& endpoint) {
    if (endpoint == "getGJLevels21.php") return "special-levels";
    if (endpoint == "getGJComments21.php") return "comments";
    if (endpoint == "getGJUsers20.php") return "users";
    if (endpoint == "getGJUserInfo20.php") return "userinfo";
    return "post";
}

std::string cacheKeyForPostBody(std::string const& endpoint, std::string const& body) {
    return hashKey(endpoint + "\n" + body);
}

GDRobTopCache& GDRobTopCache::get() {
    static GDRobTopCache* inst = new GDRobTopCache();
    return *inst;
}

bool GDRobTopCache::isEnabled() const {
    return Mod::get()->getSettingValue<bool>("gd-robtop-cache-enabled");
}

std::filesystem::path GDRobTopCache::cacheDir() const {
    return Mod::get()->getSaveDir() / "gd-cache";
}

std::string GDRobTopCache::entryKey(std::string const& category, std::string const& key) const {
    return sanitizeCategory(category) + "/" + hashKey(key);
}

std::filesystem::path GDRobTopCache::pathForEntry(
    std::string const& category,
    std::string const& key
) const {
    return cacheDir() / (entryKey(category, key) + ".json");
}

void GDRobTopCache::init() {
    bool expected = false;
    if (!m_initialized.compare_exchange_strong(expected, true)) return;

    std::error_code ec;
    auto dir = cacheDir();
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        log::warn("[GDRobTopCache] No se pudo crear directorio: {}", ec.message());
        ec.clear();
    }

    pruneExpired();
    log::info("[GDRobTopCache] Inicializado en {}", utils::string::pathToString(dir));
}

void GDRobTopCache::shutdown() {
    m_shuttingDown.store(true, std::memory_order_release);
    std::lock_guard lock(m_mutex);
    m_ram.clear();
}

std::optional<std::string> GDRobTopCache::lookup(std::string const& category, std::string const& key) {
    if (!isEnabled() || m_shuttingDown.load(std::memory_order_acquire) || key.empty()) {
        return std::nullopt;
    }

    auto fullKey = entryKey(category, key);
    auto now = std::time(nullptr);

    {
        std::lock_guard lock(m_mutex);
        auto it = m_ram.find(fullKey);
        if (it != m_ram.end()) {
            if (it->second.expiresAt >= now) {
                it->second.lastAccess = now;
                m_ramHits.fetch_add(1, std::memory_order_relaxed);
                return it->second.body;
            }
            m_ram.erase(it);
        }
    }

    auto path = pathForEntry(category, key);
    if (auto disk = readDisk(path)) {
        touchRam(fullKey, disk->body, disk->expiresAt);
        m_diskHits.fetch_add(1, std::memory_order_relaxed);
        return disk->body;
    }

    return std::nullopt;
}

void GDRobTopCache::store(
    std::string const& category,
    std::string const& key,
    std::string const& response,
    std::time_t ttl
) {
    if (!isEnabled() || m_shuttingDown.load(std::memory_order_acquire) || key.empty() || ttl <= 0) {
        return;
    }
    if (!isCacheableResponse(response)) return;

    auto fullKey = entryKey(category, key);
    auto expiresAt = std::time(nullptr) + ttl;

    {
        std::lock_guard lock(m_mutex);
        m_ram[fullKey] = RamEntry{response, expiresAt, std::time(nullptr)};
        if (m_ram.size() > kMaxRamEntries) {
            evictLRU();
        }
    }

    writeDisk(pathForEntry(category, key), response, ttl);
}

void GDRobTopCache::evictLRU() {
    if (m_ram.size() <= kMaxRamEntries) return;

    size_t toRemove = m_ram.size() - kMaxRamEntries;
    std::vector<std::pair<std::string, std::time_t>> candidates;
    candidates.reserve(m_ram.size());

    for (auto const& [key, entry] : m_ram) {
        candidates.emplace_back(key, entry.lastAccess);
    }

    std::partial_sort(candidates.begin(),
                      candidates.begin() + toRemove,
                      candidates.end(),
                      [](auto const& a, auto const& b) { return a.second < b.second; });

    for (size_t i = 0; i < toRemove; ++i) {
        m_ram.erase(candidates[i].first);
    }
}

std::optional<GDRobTopCache::DiskEntry> GDRobTopCache::readDisk(
    std::filesystem::path const& path
) const {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return std::nullopt;

    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (content.empty()) return std::nullopt;

    auto parsed = matjson::parse(content);
    if (!parsed.isOk()) return std::nullopt;

    auto json = parsed.unwrap();
    if (!json.contains("response") || !json["response"].isString()) return std::nullopt;
    auto body = json["response"].asString().unwrapOr("");
    if (!isCacheableResponse(body)) return std::nullopt;

    auto cachedAt = static_cast<std::time_t>(json["cachedAt"].asDouble().unwrapOr(0.0));
    auto ttl = static_cast<std::time_t>(json["ttl"].asDouble().unwrapOr(
        static_cast<double>(kCacheTTLWeek)));
    if (cachedAt <= 0 || ttl <= 0) return std::nullopt;

    auto expiresAt = cachedAt + ttl;
    if (std::time(nullptr) > expiresAt) return std::nullopt;

    return DiskEntry{body, expiresAt};
}

void GDRobTopCache::writeDisk(
    std::filesystem::path const& path,
    std::string const& response,
    std::time_t ttl
) const {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    matjson::Value json = matjson::Value::object();
    json["cachedAt"] = static_cast<double>(std::time(nullptr));
    json["ttl"] = static_cast<double>(ttl);
    json["response"] = response;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        log::debug("[GDRobTopCache] No se pudo escribir {}", utils::string::pathToString(path));
        return;
    }
    out << json.dump();
}

void GDRobTopCache::touchRam(std::string const& entryKey, std::string response, std::time_t expiresAt) {
    std::lock_guard lock(m_mutex);
    m_ram[entryKey] = RamEntry{std::move(response), expiresAt, std::time(nullptr)};
    if (m_ram.size() > kMaxRamEntries) {
        evictLRU();
    }
}

void GDRobTopCache::pruneExpired() {
    std::error_code ec;
    auto dir = cacheDir();
    if (!std::filesystem::exists(dir, ec)) return;

    int removed = 0;
    for (auto const& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (m_shuttingDown.load(std::memory_order_acquire)) break;
        if (ec) break;
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        if (!readDisk(entry.path())) {
            std::filesystem::remove(entry.path(), ec);
            if (!ec) ++removed;
            ec.clear();
        }
    }

    if (removed > 0) {
        log::info("[GDRobTopCache] Eliminadas {} entradas expiradas", removed);
    }
}

std::size_t GDRobTopCache::entryCount() const {
    std::error_code ec;
    std::size_t count = 0;
    auto dir = cacheDir();
    if (!std::filesystem::exists(dir, ec)) return 0;
    for (auto const& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (ec) break;
        if (entry.is_regular_file() && entry.path().extension() == ".json") ++count;
    }
    return count;
}

std::size_t GDRobTopCache::ramHitCount() const {
    return m_ramHits.load(std::memory_order_relaxed);
}

std::size_t GDRobTopCache::diskHitCount() const {
    return m_diskHits.load(std::memory_order_relaxed);
}

void postCached(
    std::string endpoint,
    std::string body,
    PostCallback cb,
    CachePolicy policy
) {
    if (paimon::isRuntimeShuttingDown()) {
        if (cb) cb(false, "");
        return;
    }

    auto ttl = ttlForPolicy(policy);
    if (policy == CachePolicy::None || ttl <= 0 || !GDRobTopCache::get().isEnabled()) {
        auto req = web::WebRequest();
        req.timeout(std::chrono::seconds(15));
        req.header("Content-Type", "application/x-www-form-urlencoded");
        req.bodyString(body);
        WebHelper::dispatch(std::move(req), "POST", std::string(kRobTopBaseUrl) + endpoint,
            [cb = std::move(cb)](web::WebResponse res) {
                if (!cb) return;
                if (!res.ok()) { cb(false, ""); return; }
                std::string response = res.string().unwrapOr("");
                trimResponseTail(response);
                if (!isCacheableResponse(response)) { cb(false, ""); return; }
                cb(true, std::move(response));
            });
        return;
    }

    auto category = cacheCategoryForEndpoint(endpoint);
    auto key = cacheKeyForPostBody(endpoint, body);

    if (auto cached = GDRobTopCache::get().lookup(category, key)) {
        if (cb) cb(true, *cached);
        return;
    }

    auto dedupeKey = category + "/" + key;
    {
        std::lock_guard lock(pendingMutex());
        auto& batch = pendingRequests()[dedupeKey];
        batch.callbacks.push_back(std::move(cb));
        if (batch.callbacks.size() > 1) return;
    }

    auto req = web::WebRequest();
    req.timeout(std::chrono::seconds(15));
    req.header("Content-Type", "application/x-www-form-urlencoded");
    req.bodyString(body);

    WebHelper::dispatch(std::move(req), "POST", std::string(kRobTopBaseUrl) + endpoint,
        [endpoint, body, category, key, dedupeKey, policy](web::WebResponse res) {
            bool ok = false;
            std::string response;
            if (res.ok()) {
                response = res.string().unwrapOr("");
                trimResponseTail(response);
                ok = isCacheableResponse(response);
            }

            if (ok) {
                GDRobTopCache::get().store(category, key, response, ttlForPolicy(policy));
            }

            std::vector<PostCallback> waiters;
            {
                std::lock_guard lock(pendingMutex());
                auto it = pendingRequests().find(dedupeKey);
                if (it != pendingRequests().end()) {
                    waiters = std::move(it->second.callbacks);
                    pendingRequests().erase(it);
                }
            }

            for (auto& waiter : waiters) {
                if (waiter) waiter(ok, response);
            }
        });
}

} // namespace paimon::gd