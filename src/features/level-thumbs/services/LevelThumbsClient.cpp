#include "../../../utils/FilesystemPath.hpp"
#include "LevelThumbsClient.hpp"

#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/Settings.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/AtomicFileWrite.hpp"
#include "../../../utils/BoundedFileRead.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/ThreadPool.hpp"

#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/PopupManager.hpp>

#include <algorithm>
#include <atomic>

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

std::mutex& poolMutex() {
    static auto* m = new std::mutex();
    return *m;
}

std::atomic<paimon::ThreadPool*>& poolPtr() {
    static auto* p = new std::atomic<paimon::ThreadPool*>(nullptr);
    return *p;
}

std::atomic<bool> s_levelThumbsInitialized{false};

// lazy heap pool: no atexit destructor, and never allocates after shutdown begins.
paimon::ThreadPool* ensurePool(bool createIfMissing, LevelThumbsClient const* client = nullptr) {
    if (paimon::isRuntimeShuttingDown() || (client && client->isShuttingDown())) {
        return poolPtr().load(std::memory_order_acquire);
    }
    if (!createIfMissing) {
        return poolPtr().load(std::memory_order_acquire);
    }
    std::lock_guard<std::mutex> lock(poolMutex());
    if (paimon::isRuntimeShuttingDown() || (client && client->isShuttingDown())) {
        return poolPtr().load(std::memory_order_acquire);
    }
    auto* p = poolPtr().load(std::memory_order_relaxed);
    if (!p) {
        p = new paimon::ThreadPool(2, "PaimonLevelThumbs");
        poolPtr().store(p, std::memory_order_release);
    }
    return p;
}

constexpr size_t kMaxDiskEntries = 400;
constexpr auto kMaxDiskAge = std::chrono::hours(24 * 21);

void pruneDiskCache(std::filesystem::path const& dir) {
    if (paimon::isRuntimeShuttingDown()) return;
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) return;

    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> entries;
    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (paimon::isRuntimeShuttingDown()) return;
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        auto written = entry.last_write_time(ec);
        if (ec) { ec.clear(); continue; }
        entries.emplace_back(written, entry.path());
    }

    if (paimon::isRuntimeShuttingDown()) return;
    auto const now = std::filesystem::file_time_type::clock::now();
    std::erase_if(entries, [&now](auto const& entry) {
        if (paimon::isRuntimeShuttingDown()) return false;
        if (now - entry.first < kMaxDiskAge) return false;
        std::error_code rmEc;
        std::filesystem::remove(entry.second, rmEc);
        return true;
    });

    if (paimon::isRuntimeShuttingDown() || entries.size() <= kMaxDiskEntries) return;

    std::sort(entries.begin(), entries.end(),
        [](auto const& a, auto const& b) { return a.first < b.first; });

    size_t const excess = entries.size() - kMaxDiskEntries;
    for (size_t i = 0; i < excess; i++) {
        if (paimon::isRuntimeShuttingDown()) return;
        std::error_code rmEc;
        std::filesystem::remove(entries[i].second, rmEc);
    }
    geode::log::info("[LevelThumbs] pruned {} cached thumbnails", excess);
}

} // namespace

bool LevelThumbsClient::isInitialized() {
    return s_levelThumbsInitialized.load(std::memory_order_acquire);
}

LevelThumbsClient& LevelThumbsClient::get() {
    // Heap-leaked: shutdown happens deterministically via waitShutdown().
    static auto* instance = []() {
        auto* client = new LevelThumbsClient();
        s_levelThumbsInitialized.store(true, std::memory_order_release);
        return client;
    }();
    return *instance;
}

LevelThumbsClient::LevelThumbsClient() {
    auto dir = cacheDir();
    if (auto* p = ensurePool(true, this); p && !p->isStopped()) {
        p->enqueue([dir]() { pruneDiskCache(dir); });
    }
}

void LevelThumbsClient::requestShutdown() {
    m_shuttingDown.store(true, std::memory_order_release);
    m_queue.clear();
    m_inflight.clear();
    if (auto* p = ensurePool(false, this)) {
        p->requestStop();
    }
}

void LevelThumbsClient::waitShutdown(std::chrono::steady_clock::time_point deadline) {
    requestShutdown();
    if (auto* p = ensurePool(false, this)) {
        p->waitShutdown(deadline);
    }
}

void LevelThumbsClient::shutdown() {
    waitShutdown(std::chrono::steady_clock::now() + std::chrono::milliseconds(1500));
}

std::string LevelThumbsClient::apiBaseUrl() const {
    auto* mod = Loader::get()->getLoadedMod(kLevelThumbsMod);
    if (mod && mod->hasSetting("level-thumbnails-api")) {
        auto url = mod->getSettingValue<std::string>("level-thumbnails-api");
        while (!url.empty() && url.back() == '/') url.pop_back();
        if (!url.empty()) {
            if (!HttpClient::isUrlSafe(url)) {
                log::warn("[LevelThumbs] Ignoring unsafe API URL from {}: {}", kLevelThumbsMod, url);
                return kDefaultApi;
            }
            return url;
        }
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
    return paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "levelthumbs"
        / fmt::to_string(std::hash<std::string>{}(apiBaseUrl()));
}

std::filesystem::path LevelThumbsClient::entryPath(int levelID, Quality quality) const {
    return cacheDir() / fmt::format("{}-{}.img", levelID, qualitySuffix(quality));
}

void LevelThumbsClient::fetchThumbnail(int levelID, Quality quality, DataCallback callback) {
    if (!callback) return;
    if (paimon::isRuntimeShuttingDown() || isShuttingDown()) return;
    if (levelID <= 0) {
        Loader::get()->queueInMainThread([callback = std::move(callback)]() mutable {
            if (paimon::isRuntimeShuttingDown()) return;
            callback(false, {});
        });
        return;
    }

    if (isNotFound(levelID) || !isLevelThumbsModActive()) {
        Loader::get()->queueInMainThread([callback = std::move(callback)]() mutable {
            if (paimon::isRuntimeShuttingDown()) return;
            callback(false, {});
        });
        return;
    }

    auto const key = (static_cast<int64_t>(levelID) << 4) | static_cast<int>(quality);
    auto it = m_inflight.find(key);
    if (it != m_inflight.end()) {
        it->second->callbacks.push_back(std::move(callback));
        return;
    }

    auto request = std::make_shared<Request>();
    request->levelID = levelID;
    request->quality = quality;
    request->callbacks.push_back(std::move(callback));

    m_inflight[key] = request;
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

    auto* p = ensurePool(true, this);
    if (!p || p->isStopped()) {
        finish(request, false, {});
        return;
    }

    p->enqueue([this, request, path, url]() {
        if (paimon::isRuntimeShuttingDown() || isShuttingDown()) {
            return;
        }

        auto cached = paimon::file::readBytes(path, 32ull * 1024 * 1024);
        bool const hit = !cached.empty();
        if (paimon::isRuntimeShuttingDown() || isShuttingDown()) return;

        Loader::get()->queueInMainThread([this, request, path, url, hit, cached = std::move(cached)]() mutable {
            if (paimon::isRuntimeShuttingDown() || isShuttingDown()) {
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
            if (paimon::isRuntimeShuttingDown() || isShuttingDown()) return;
            if (!success || data.empty()) {
                if (status == 404 || status == 410) markNotFound(request->levelID);
                finish(request, false, {});
                return;
            }

            if (!path.empty()) {
                if (auto* p = ensurePool(true, this); p && !p->isStopped()) {
                    p->enqueue([path, data]() {
                        if (paimon::isRuntimeShuttingDown()) return;
                        (void)paimon::file::writeAtomically(path, data);
                    });
                }
            }
            finish(request, true, data);
        });
}

void LevelThumbsClient::finish(std::shared_ptr<Request> const& request, bool success, std::vector<uint8_t> const& data) {
    auto const key = (static_cast<int64_t>(request->levelID) << 4) | static_cast<int>(request->quality);
    m_inflight.erase(key);
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
    auto root = paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "levelthumbs";
    if (auto* p = ensurePool(true, this); p && !p->isStopped()) {
        p->enqueue([root]() {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        });
    } else if (!paimon::isRuntimeShuttingDown() && !isShuttingDown()) {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
}

bool isLevelThumbsModInstalled() {
    auto* loader = Loader::get();
    return loader && loader->isModInstalled(kLevelThumbsMod);
}

bool isLevelThumbsModActive() {
    auto* loader = Loader::get();
    return loader && loader->isModLoaded(kLevelThumbsMod);
}

bool fallbackEnabled() {
    return paimon::modules::isEnabled(kModuleID) && isLevelThumbsModActive();
}

void maybePromptToInstallLevelThumbs() {
    // once per session: thumbnail misses can fire this from worker threads
    // for every visible cell, so the guard has to be atomic and cheap.
    static std::atomic<bool> s_promptQueued{false};
    if (paimon::isRuntimeShuttingDown() || !Loader::get()) return;
    if (s_promptQueued.exchange(true, std::memory_order_acq_rel)) return;

    Loader::get()->queueInMainThread([]() {
        if (paimon::isRuntimeShuttingDown()) return;
        // installed and enabled while the prompt was queued: nothing to ask.
        if (isLevelThumbsModActive()) {
            s_promptQueued.store(false, std::memory_order_release);
            return;
        }

        bool const installed = isLevelThumbsModInstalled();
        auto popup = PopupManager::get().quickPopup(
            paimon::loc::tr("levelthumbs.prompt.title", "Level Thumbnails"),
            installed
                ? paimon::loc::tr("levelthumbs.prompt.body_disabled",
                    "The thumbnail fallback uses the <cg>Level Thumbnails</c> mod, which is installed but disabled.\n\nEnable it and restart the game to show thumbnails for levels that have none in Paimbnails.")
                : paimon::loc::tr("levelthumbs.prompt.body_missing",
                    "The thumbnail fallback uses the <cg>Level Thumbnails</c> mod, which is not installed.\n\nDownload it to show thumbnails for levels that have none in Paimbnails."),
            paimon::loc::tr("levelthumbs.prompt.later", "Not Now"),
            installed ? paimon::loc::tr("levelthumbs.prompt.open", "Open Mod")
                      : paimon::loc::tr("levelthumbs.prompt.download", "Download"),
            [](FLAlertLayer*, bool btn2) {
                if (!btn2) return;
                if (auto* mod = Loader::get()->getInstalledMod(kLevelThumbsMod)) {
                    geode::openInfoPopup(mod);
                } else {
                    // not installed: Geode resolves the server page, which
                    // is where the download button lives.
                    (void)geode::openInfoPopup(std::string(kLevelThumbsMod));
                }
            },
            380.f
        );
        popup.showQueue();
    });
}

bool shouldFallback(int levelID) {
    if (levelID <= 0 || !paimon::modules::isEnabled(kModuleID)) return false;
    if (!isLevelThumbsModActive()) {
        maybePromptToInstallLevelThumbs();
        return false;
    }
    return !LevelThumbsClient::get().isNotFound(levelID);
}

Quality qualityForThumbnail(bool highQuality) {
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    return highQuality ? Quality::Medium : Quality::Small;
#else
    return highQuality ? Quality::High : Quality::Medium;
#endif
}

} // namespace paimon::levelthumbs
