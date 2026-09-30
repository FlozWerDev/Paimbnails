#include "LevelThumbsClient.hpp"

#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/Settings.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/ThreadPool.hpp"

#include <algorithm>
#include <fstream>

using namespace geode::prelude;

namespace paimon::levelthumbs {

namespace {

constexpr char const* kLevelThumbsMod = "cdc.level_thumbnails";
constexpr char const* kDefaultApi = "https://levelthumbs.prevter.me";
constexpr char const* kModuleID = "paimbnails.levelthumbs.browser";

char const* qualitySuffix(Quality quality) {
    switch (quality) {
        case Quality::Small:  return "small";
        case Quality::Medium: return "medium";
        case Quality::High:   return "high";
    }
    return "high";
}

// heap pool: no atexit destructor, same as paimon::asyncimg.
paimon::ThreadPool& pool() {
    static auto* p = new paimon::ThreadPool(2, "PaimonLevelThumbs");
    return *p;
}

bool readCacheFile(std::filesystem::path const& path, std::vector<uint8_t>& out) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;

    auto const size = file.tellg();
    if (size <= 0 || static_cast<uint64_t>(size) > 32ull * 1024 * 1024) return false;
    file.seekg(0, std::ios::beg);

    out.resize(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(out.data()), size);
    if (!file) {
        out.clear();
        return false;
    }
    return true;
}

// tmp + rename so a crash never leaves a partial cache file.
void writeCacheFile(std::filesystem::path const& path, std::vector<uint8_t> const& data) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    auto tmpPath = path;
    tmpPath += ".tmp";
    bool writeOk = false;
    {
        std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
        if (file) {
            file.write(reinterpret_cast<char const*>(data.data()), data.size());
            writeOk = file.good();
        }
    }
    if (!writeOk) {
        std::filesystem::remove(tmpPath, ec);
        return;
    }

    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) std::filesystem::remove(tmpPath, ec);
}

constexpr size_t kMaxDiskEntries = 400;
constexpr auto kMaxDiskAge = std::chrono::hours(24 * 21);

void pruneDiskCache(std::filesystem::path const& dir) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) return;

    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> entries;
    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        auto written = entry.last_write_time(ec);
        if (ec) { ec.clear(); continue; }
        entries.emplace_back(written, entry.path());
    }

    auto const now = std::filesystem::file_time_type::clock::now();
    std::erase_if(entries, [&now](auto const& entry) {
        if (now - entry.first < kMaxDiskAge) return false;
        std::error_code rmEc;
        std::filesystem::remove(entry.second, rmEc);
        return true;
    });

    if (entries.size() <= kMaxDiskEntries) return;

    std::sort(entries.begin(), entries.end(),
        [](auto const& a, auto const& b) { return a.first < b.first; });

    size_t const excess = entries.size() - kMaxDiskEntries;
    for (size_t i = 0; i < excess; i++) {
        std::error_code rmEc;
        std::filesystem::remove(entries[i].second, rmEc);
    }
    geode::log::info("[LevelThumbs] pruned {} cached thumbnails", excess);
}

} // namespace

LevelThumbsClient& LevelThumbsClient::get() {
    static LevelThumbsClient instance;
    return instance;
}

LevelThumbsClient::LevelThumbsClient() {
    auto dir = cacheDir();
    pool().enqueue([dir]() { pruneDiskCache(dir); });
}

std::string LevelThumbsClient::apiBaseUrl() const {
    auto* mod = Loader::get()->getLoadedMod(kLevelThumbsMod);
    if (mod && mod->hasSetting("level-thumbnails-api")) {
        auto url = mod->getSettingValue<std::string>("level-thumbnails-api");
        while (!url.empty() && url.back() == '/') url.pop_back();
        if (!url.empty()) return url;
    }
    return kDefaultApi;
}

bool LevelThumbsClient::isLegacyApi() const {
    auto* mod = Loader::get()->getLoadedMod(kLevelThumbsMod);
    return mod && mod->hasSetting("legacy-url") && mod->getSettingValue<bool>("legacy-url");
}

std::string LevelThumbsClient::thumbnailUrl(int levelID, Quality quality) const {
    auto base = apiBaseUrl();
    if (isLegacyApi()) {
        return fmt::format("{}/{}.png", base, levelID);
    }
    if (quality == Quality::High) {
        return fmt::format("{}/thumbnail/{}", base, levelID);
    }
    return fmt::format("{}/thumbnail/{}/{}", base, levelID, qualitySuffix(quality));
}

std::filesystem::path LevelThumbsClient::cacheDir() const {
    // keyed by host: a new api must not serve the previous one's images.
    return Mod::get()->getSaveDir() / "levelthumbs"
        / fmt::to_string(std::hash<std::string>{}(apiBaseUrl()));
}

std::filesystem::path LevelThumbsClient::entryPath(int levelID, Quality quality) const {
    return cacheDir() / fmt::format("{}-{}.img", levelID, qualitySuffix(quality));
}

void LevelThumbsClient::fetchThumbnail(int levelID, Quality quality, DataCallback callback) {
    if (!callback) return;
    if (levelID <= 0) {
        Loader::get()->queueInMainThread([callback = std::move(callback)]() mutable {
            callback(false, {});
        });
        return;
    }

    if (paimon::isRuntimeShuttingDown() || isNotFound(levelID)) {
        Loader::get()->queueInMainThread([callback = std::move(callback)]() mutable {
            callback(false, {});
        });
        return;
    }

    auto it = m_inflight.find(levelID);
    if (it != m_inflight.end()) {
        it->second->callbacks.push_back(std::move(callback));
        return;
    }

    auto request = std::make_shared<Request>();
    request->levelID = levelID;
    request->quality = quality;
    request->callbacks.push_back(std::move(callback));

    m_inflight[levelID] = request;
    m_queue.push_back(std::move(request));
    pump();
}

void LevelThumbsClient::pump() {
    // rejected urls answer synchronously via finish(); guard against stack unwind.
    if (m_pumping) return;
    m_pumping = true;

    while (m_activeRequests < MAX_CONCURRENT_REQUESTS && !m_queue.empty()) {
        auto request = m_queue.front();
        m_queue.pop_front();
        m_activeRequests++;
        startRequest(std::move(request));
    }

    m_pumping = false;
}

void LevelThumbsClient::startRequest(std::shared_ptr<Request> request) {
    auto url = thumbnailUrl(request->levelID, request->quality);
    auto path = paimon::settings::general::enableDiskCache()
        ? entryPath(request->levelID, request->quality)
        : std::filesystem::path{};

    if (path.empty()) {
        download(std::move(request), url, path);
        return;
    }

    pool().enqueue([this, request, path, url]() {
        if (paimon::isRuntimeShuttingDown()) {
            Loader::get()->queueInMainThread([this, request]() { finish(request, false, {}); });
            return;
        }

        std::vector<uint8_t> cached;
        bool const hit = readCacheFile(path, cached) && !cached.empty();

        Loader::get()->queueInMainThread([this, request, path, url, hit, cached = std::move(cached)]() mutable {
            if (paimon::isRuntimeShuttingDown()) {
                finish(request, false, {});
                return;
            }

            if (hit) {
                finish(request, true, cached);
                return;
            }
            download(request, url, path);
        });
    });
}

void LevelThumbsClient::download(std::shared_ptr<Request> request, std::string const& url,
                                 std::filesystem::path const& path) {
    // status separates "no thumbnail" (404/410, cached) from dropped connections (retried).
    HttpClient::get().performBinaryRequestEx(url, {},
        [this, request, path](bool success, std::vector<uint8_t> const& data, int status) {
            if (!success || data.empty()) {
                if (status == 404 || status == 410) markNotFound(request->levelID);
                finish(request, false, {});
                return;
            }

            if (!path.empty()) {
                pool().enqueue([path, data]() { writeCacheFile(path, data); });
            }
            finish(request, true, data);
        });
}

void LevelThumbsClient::finish(std::shared_ptr<Request> const& request, bool success, std::vector<uint8_t> const& data) {
    m_inflight.erase(request->levelID);
    if (m_activeRequests > 0) m_activeRequests--;

    auto callbacks = std::move(request->callbacks);
    request->callbacks.clear();

    pump();

    for (auto& callback : callbacks) {
        if (callback) callback(success, data);
    }
}

void LevelThumbsClient::markNotFound(int levelID) {
    std::lock_guard<std::mutex> lock(m_notFoundMutex);
    m_notFound[levelID] = std::chrono::steady_clock::now();
}

bool LevelThumbsClient::isNotFound(int levelID) const {
    std::lock_guard<std::mutex> lock(m_notFoundMutex);
    auto it = m_notFound.find(levelID);
    if (it == m_notFound.end()) return false;
    if (std::chrono::steady_clock::now() - it->second >= NOT_FOUND_TTL) {
        m_notFound.erase(it);
        return false;
    }
    return true;
}

void LevelThumbsClient::clearNotFound(int levelID) {
    std::lock_guard<std::mutex> lock(m_notFoundMutex);
    m_notFound.erase(levelID);
}

void LevelThumbsClient::clearCache() {
    std::lock_guard<std::mutex> lock(m_notFoundMutex);
    m_notFound.clear();
}

void LevelThumbsClient::clearDiskCache() {
    auto root = Mod::get()->getSaveDir() / "levelthumbs";
    pool().enqueue([root]() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    });
}

bool fallbackEnabled() {
    return paimon::modules::isEnabled(kModuleID);
}

bool shouldFallback(int levelID) {
    return levelID > 0
        && fallbackEnabled()
        && !LevelThumbsClient::get().isNotFound(levelID);
}

Quality qualityForThumbnail(bool highQuality) {
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    return highQuality ? Quality::Medium : Quality::Small;
#else
    return highQuality ? Quality::High : Quality::Medium;
#endif
}

} // namespace paimon::levelthumbs
