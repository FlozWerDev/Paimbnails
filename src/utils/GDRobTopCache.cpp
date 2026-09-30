#include "GDRobTopCache.hpp"

#include "WebHelper.hpp"
#include "ThreadTracker.hpp"
#include "JsonHelper.hpp"
#include "AtomicFileWrite.hpp"
#include "../core/RuntimeLifecycle.hpp"

#include <Geode/loader/Log.hpp>
#include <Geode/utils/string.hpp>
#include <matjson.hpp>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <algorithm>
#include <limits>
#include <system_error>

using namespace geode::prelude;

namespace paimon::gd {

namespace {

constexpr std::size_t kMaxCachedResponseBytes = 16ull * 1024 * 1024;
constexpr std::streamoff kMaxCachedFileBytes = 32ll * 1024 * 1024;

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

    log::info("[GDRobTopCache] initialized at {}", utils::string::pathToString(dir));

    // pruneexpired() does heavy disk i/o (once froze startup); background only.
    // lookup/readdisk tolerate entries vanishing mid-prune.
    paimon::ThreadTracker::get().spawn([this]() {
        geode::utils::thread::setName("PaimonRobTopPrune");
        if (m_shuttingDown.load(std::memory_order_acquire)) return;
        pruneExpired();
    });
}

void GDRobTopCache::shutdown() {
    m_shuttingDown.store(true, std::memory_order_release);
    {
        std::lock_guard lock(m_mutex);
        m_ram.clear();
        m_ramBytes = 0;
    }
    std::lock_guard lock(pendingMutex());
    pendingRequests().clear();
}

std::optional<std::string> GDRobTopCache::lookup(std::string const& category, std::string const& key) {
    if (!isEnabled() || m_shuttingDown.load(std::memory_order_acquire) || key.empty()) {
        return std::nullopt;
    }

    auto fullKey = entryKey(category, key);
    auto now = std::time(nullptr);
    if (now < 0) return std::nullopt;

    {
        std::lock_guard lock(m_mutex);
        auto it = m_ram.find(fullKey);
        if (it != m_ram.end()) {
            if (it->second.expiresAt >= now) {
                it->second.lastAccess = std::chrono::steady_clock::now();
                m_ramHits.fetch_add(1, std::memory_order_relaxed);
                return it->second.body;
            }
            m_ramBytes -= it->second.body.size();
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
    if (!isCacheableResponse(response) || response.size() > kMaxCachedResponseBytes) return;

    auto now = std::time(nullptr);
    ttl = std::min(ttl, kCacheTTLWeek);
    if (now < 0 || ttl > (std::numeric_limits<std::time_t>::max)() - now) return;
    auto fullKey = entryKey(category, key);
    auto expiresAt = now + ttl;
    touchRam(fullKey, response, expiresAt);

    writeDisk(pathForEntry(category, key), response, ttl);
}

void GDRobTopCache::evictLRU() {
    while (!m_ram.empty() && (m_ram.size() > kMaxRamEntries || m_ramBytes > kMaxRamBytes)) {
        auto oldest = std::min_element(m_ram.begin(), m_ram.end(), [](auto const& a, auto const& b) {
            return a.second.lastAccess < b.second.lastAccess;
        });
        m_ramBytes -= oldest->second.body.size();
        m_ram.erase(oldest);
    }
}

std::optional<GDRobTopCache::DiskEntry> GDRobTopCache::readDisk(
    std::filesystem::path const& path
) const {
    std::lock_guard lock(m_diskMutex);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return std::nullopt;

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return std::nullopt;

    std::streamoff const size = in.tellg();
    if (size <= 0 || size > kMaxCachedFileBytes) return std::nullopt;
    in.seekg(0, std::ios::beg);
    std::string content(static_cast<size_t>(size), '\0');
    if (!in.read(content.data(), size)) return std::nullopt;

    auto parsed = matjson::parse(content);
    if (!parsed.isOk() || !parsed.unwrap().isObject()) return std::nullopt;

    auto const& json = parsed.unwrap();
    if (!json.contains("response") || !json["response"].isString()) return std::nullopt;
    auto body = json["response"].asString().unwrapOr("");
    if (!isCacheableResponse(body) || body.size() > kMaxCachedResponseBytes) return std::nullopt;

    auto cachedAtValue = paimon::json::integerOr<int64_t>(json["cachedAt"]);
    auto ttlValue = paimon::json::integerOr<int64_t>(json["ttl"], kCacheTTLWeek);
    if (cachedAtValue <= 0 || ttlValue <= 0 || ttlValue > kCacheTTLWeek ||
        static_cast<uintmax_t>(cachedAtValue) > static_cast<uintmax_t>((std::numeric_limits<std::time_t>::max)())) return std::nullopt;
    auto cachedAt = static_cast<std::time_t>(cachedAtValue);
    auto ttl = static_cast<std::time_t>(ttlValue);
    if (ttl > (std::numeric_limits<std::time_t>::max)() - cachedAt) return std::nullopt;

    auto expiresAt = cachedAt + ttl;
    auto now = std::time(nullptr);
    if (now < 0 || cachedAt > now || now > expiresAt) return std::nullopt;

    return DiskEntry{std::move(body), expiresAt};
}

void GDRobTopCache::writeDisk(
    std::filesystem::path const& path,
    std::string const& response,
    std::time_t ttl
) const {
    matjson::Value json = matjson::Value::object();
    json["cachedAt"] = static_cast<double>(std::time(nullptr));
    json["ttl"] = static_cast<double>(ttl);
    json["response"] = response;
    auto content = json.dump();
    if (content.size() > static_cast<size_t>(kMaxCachedFileBytes)) return;

    std::lock_guard lock(m_diskMutex);
    if (m_shuttingDown.load(std::memory_order_acquire)) return;
    (void)paimon::file::writeAtomically(path, std::string_view(content));
}

void GDRobTopCache::touchRam(std::string const& entryKey, std::string response, std::time_t expiresAt) {
    std::lock_guard lock(m_mutex);
    if (m_shuttingDown.load(std::memory_order_acquire)) return;
    auto old = m_ram.find(entryKey);
    if (old != m_ram.end()) m_ramBytes -= old->second.body.size();
    auto bytes = response.size();
    m_ram[entryKey] = RamEntry{std::move(response), expiresAt, std::chrono::steady_clock::now()};
    m_ramBytes += bytes;
    evictLRU();
}

void GDRobTopCache::pruneExpired() {
    std::error_code ec;
    auto dir = cacheDir();
    if (!std::filesystem::exists(dir, ec)) return;

    int removed = 0;
    for (std::filesystem::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        auto const& entry = *it;
        if (m_shuttingDown.load(std::memory_order_acquire)) break;
        if (ec) break;
        std::error_code typeEc;
        if (!entry.is_regular_file(typeEc) || typeEc || entry.path().extension() != ".json") continue;
        // keep a fresh replacement from being removed after reading an expired entry.
        std::lock_guard lock(m_diskMutex);
        if (!readDisk(entry.path())) {
            std::filesystem::remove(entry.path(), ec);
            if (!ec) ++removed;
            ec.clear();
        }
    }

    if (removed > 0) {
        log::info("[GDRobTopCache] pruned {} expired entries", removed);
    }
}

std::size_t GDRobTopCache::entryCount() const {
    std::error_code ec;
    std::size_t count = 0;
    auto dir = cacheDir();
    if (!std::filesystem::exists(dir, ec)) return 0;
    for (std::filesystem::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code typeEc;
        if (it->is_regular_file(typeEc) && !typeEc && it->path().extension() == ".json") ++count;
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
                if (!cb || paimon::isRuntimeShuttingDown()) return;
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
        [category, key, dedupeKey, policy](web::WebResponse res) {
            if (paimon::isRuntimeShuttingDown()) return;
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
