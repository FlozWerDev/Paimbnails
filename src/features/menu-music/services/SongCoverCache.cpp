#include "SongCoverCache.hpp"
#include "MenuMusicCoverLog.hpp"

#include "../../thumbnails/services/ThumbnailLoader.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/MainThreadDelay.hpp"

#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GJSearchObject.hpp>
#include <Geode/binding/LevelManagerDelegate.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>

#include <matjson.hpp>
#include <fmt/format.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <system_error>

using namespace geode::prelude;

namespace paimon::menumusic {

namespace {

constexpr int kMaxSearchAttempts = 2;
constexpr float kDebounceSec = 2.5f;
constexpr float kMinSearchGapSec = 4.0f;
constexpr float kRateLimitCooldownSec = 300.0f;
// GD level browser: 0=relevance, 1=downloads, 2=most liked (approx).
constexpr int kSearchModeMostLiked = 2;

GJSearchObject* makeSongSearchObject(int songID, bool customSong) {
    auto obj = GJSearchObject::create(SearchType::Search, "");
    if (!obj) return nullptr;

    obj->m_difficulty = "-1";
    obj->m_length = "-1";
    obj->m_page = 0;
    obj->m_starFilter = false;
    obj->m_uncompletedFilter = false;
    obj->m_featuredFilter = false;
    obj->m_songID = songID;
    obj->m_originalFilter = false;
    obj->m_twoPlayerFilter = false;
    obj->m_customSongFilter = customSong;
    obj->m_songFilter = true;
    obj->m_noStarFilter = false;
    obj->m_coinsFilter = false;
    obj->m_epicFilter = false;
    obj->m_legendaryFilter = false;
    obj->m_mythicFilter = false;
    obj->m_completedFilter = false;
    obj->m_demonFilter = static_cast<GJDifficulty>(0);
    obj->m_folder = 0;
    obj->m_searchMode = kSearchModeMostLiked;
    return obj;
}

bool shouldUseCustomSongFilter(int songID, int searchAttempt) {
    if (searchAttempt != 0) return false;
    if (auto* mdm = MusicDownloadManager::sharedState()) {
        if (mdm->isResourceSong(songID)) return false;
    }
    return true;
}

bool saveTextureToPngFile(CCTexture2D* tex, std::filesystem::path const& dst) {
    if (!tex || tex->getPixelsWide() <= 0 || tex->getPixelsHigh() <= 0) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(dst.parent_path(), ec);

    auto const size = CCSize(
        static_cast<float>(tex->getPixelsWide()),
        static_cast<float>(tex->getPixelsHigh())
    );

    auto* sprite = CCSprite::createWithTexture(tex);
    if (!sprite) return false;
    sprite->setAnchorPoint({0.f, 0.f});
    sprite->setPosition({0.f, 0.f});

    auto* rt = CCRenderTexture::create(size.width, size.height);
    if (!rt) return false;

    rt->beginWithClear(0.f, 0.f, 0.f, 0.f);
    sprite->visit();
    rt->end();

    CCImage* img = rt->newCCImage(false);
    if (!img) return false;

    auto const pathStr = geode::utils::string::pathToString(dst);
    bool const ok = img->saveToFile(pathStr.c_str(), false);
    img->release();

    return ok && std::filesystem::exists(dst, ec) && !ec;
}

bool copyThumbnailFileToSongCover(int songID, int levelID) {
    std::error_code ec;
    auto dstDir = SongCoverCache::get().getSongDir(songID);
    std::filesystem::create_directories(dstDir, ec);

    bool isGif = ThumbnailLoader::get().hasGIFData(levelID);
    auto src = ThumbnailLoader::get().getCachePath(levelID, isGif);
    if (!std::filesystem::exists(src, ec) || ec) {
        isGif = false;
        src = ThumbnailLoader::get().getCachePath(levelID, false);
        if (!std::filesystem::exists(src, ec) || ec) {
            coverlog::warn("[SongCoverCache] no thumbnail file for levelID={} songID={} (gifPath={}, pngPath={})",
                levelID, songID,
                geode::utils::string::pathToString(ThumbnailLoader::get().getCachePath(levelID, true)),
                geode::utils::string::pathToString(ThumbnailLoader::get().getCachePath(levelID, false)));
            return false;
        }
    }

    auto ext = isGif ? ".gif" : ".png";
    auto dst = dstDir / fmt::format("{}{}", levelID, ext);
    std::filesystem::copy_file(
        src, dst,
        std::filesystem::copy_options::overwrite_existing,
        ec
    );
    if (ec) {
        coverlog::warn("[SongCoverCache] copy failed levelID={} songID={}: {} -> {} ({})",
            levelID, songID,
            geode::utils::string::pathToString(src),
            geode::utils::string::pathToString(dst),
            ec.message());
        return false;
    }
    return std::filesystem::exists(dst, ec) && !ec;
}

bool persistLevelCover(int songID, int levelID, CCTexture2D* tex) {
    if (copyThumbnailFileToSongCover(songID, levelID)) {
        return true;
    }
    if (!tex) return false;
    auto dst = SongCoverCache::get().getSongDir(songID) / fmt::format("{}.png", levelID);
    return saveTextureToPngFile(tex, dst);
}

std::vector<int> collectTopLevelIds(cocos2d::CCArray* levels, int songID, int maxCount) {
    std::vector<GJGameLevel*> matched;
    std::vector<GJGameLevel*> fallback;
    matched.reserve(maxCount);
    fallback.reserve(maxCount);

    if (levels) {
        for (auto* level : CCArrayExt<GJGameLevel*>(levels)) {
            if (!level || level->m_levelID <= 0) continue;
            fallback.push_back(level);
            if (level->m_songID == songID) {
                matched.push_back(level);
            }
        }
    }

    auto const sortByLikes = [](GJGameLevel* a, GJGameLevel* b) {
        return a->m_likes > b->m_likes;
    };

    auto& source = !matched.empty() ? matched : fallback;
    std::sort(source.begin(), source.end(), sortByLikes);

    std::vector<int> out;
    out.reserve(maxCount);
    for (auto* level : source) {
        if (static_cast<int>(out.size()) >= maxCount) break;
        out.push_back(level->m_levelID);
    }
    return out;
}

class SongCoverSearchNode : public CCNode, public LevelManagerDelegate {
public:
    static SongCoverSearchNode* create() {
        auto ret = new SongCoverSearchNode();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    bool init() override {
        if (!CCNode::init()) return false;
        this->retain();
        return true;
    }

    ~SongCoverSearchNode() override {
        clearDelegate();
    }

    void beginSearch(int songID, bool customSong, std::string const& key) {
        m_songID = songID;
        m_customSong = customSong;
        m_pendingKey = key;
    }

    void clearDelegate() {
        if (auto manager = GameLevelManager::get()) {
            if (manager->m_levelManagerDelegate == this) {
                manager->m_levelManagerDelegate = nullptr;
            }
        }
    }

    bool isActiveDelegate() const {
        auto manager = GameLevelManager::get();
        return manager && manager->m_levelManagerDelegate == this;
    }

    void loadLevelsFinished(CCArray* levels, char const* key) override {
        handleLevels(levels, key);
    }

    void loadLevelsFailed(char const* key) override {
        handleFailure(key);
    }

    void loadLevelsFinished(CCArray* levels, char const* key, int) override {
        handleLevels(levels, key);
    }

    void loadLevelsFailed(char const* key, int) override {
        handleFailure(key);
    }

private:
    bool isCurrentKey(char const* key) const {
        if (!key) return false;
        if (m_pendingKey.empty()) return isActiveDelegate();
        return m_pendingKey == key;
    }

    void handleLevels(CCArray* levels, char const* key) {
        if (paimon::isRuntimeShuttingDown()) return;
        if (!isCurrentKey(key) && !isActiveDelegate()) {
            coverlog::warn("[SongCoverCache] ignoring stale search result songID={} key={} pendingKey={}",
                m_songID, key ? key : "(null)", m_pendingKey);
            return;
        }
        clearDelegate();

        coverlog::info("[SongCoverCache] search finished songID={} customSong={} levels={} key={}",
            m_songID, m_customSong, levels ? levels->count() : 0, key ? key : "(null)");

        SongCoverCache::get().handleLevelSearchResult(m_songID, m_customSong, levels);
    }

    void handleFailure(char const* key) {
        if (paimon::isRuntimeShuttingDown()) return;
        if (!isCurrentKey(key) && !isActiveDelegate()) return;
        clearDelegate();
        coverlog::warn("[SongCoverCache] search failed songID={} key={}",
            m_songID, key ? key : "(null)");
        SongCoverCache::get().handleLevelSearchFailed(m_songID);
    }

    int m_songID = 0;
    bool m_customSong = true;
    std::string m_pendingKey;
};

struct ThumbnailBatchState {
    int songID = 0;
    std::vector<int> levelIds;
    std::vector<int> savedLevelIds;
    std::vector<std::string> coverPaths;
    std::shared_ptr<int> completed;
    int pendingCount = 0;
};

} // namespace

SongCoverCache& SongCoverCache::get() {
    static SongCoverCache instance;
    return instance;
}

double SongCoverCache::nowSeconds() const {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

bool SongCoverCache::isCooldownActive() const {
    return nowSeconds() < m_cooldownUntil;
}

void SongCoverCache::noteServerRateLimit() {
    m_cooldownUntil = nowSeconds() + kRateLimitCooldownSec;
    coverlog::warn("[SongCoverCache] pausing searches for {:.0f}s (rate limit)",
        kRateLimitCooldownSec);
}

std::filesystem::path SongCoverCache::getCoversDir() const {
    return Mod::get()->getSaveDir() / "menu-music" / "song-covers";
}

std::filesystem::path SongCoverCache::getSongDir(int songID) const {
    return getCoversDir() / std::to_string(songID);
}

bool SongCoverCache::loadManifest(int songID, SongManifest& out) const {
    out = {};
    auto manifestPath = getSongDir(songID) / "manifest.json";
    std::error_code ec;
    if (!std::filesystem::exists(manifestPath, ec) || ec) {
        if (m_loggedMissingManifests.insert(songID).second) {
            coverlog::info("[SongCoverCache] manifest missing songID={} path={}",
                songID, geode::utils::string::pathToString(manifestPath));
        }
        return false;
    }

    auto data = geode::utils::file::readString(manifestPath);
    if (!data) {
        coverlog::warn("[SongCoverCache] manifest unreadable songID={} path={}",
            songID, geode::utils::string::pathToString(manifestPath));
        return false;
    }

    auto parsed = matjson::parse(data.unwrap());
    if (!parsed.isOk()) {
        coverlog::warn("[SongCoverCache] manifest parse error songID={}", songID);
        return false;
    }

    auto root = parsed.unwrap();
    if (root.contains("levelIds") && root["levelIds"].isArray()) {
        for (auto const& item : root["levelIds"].asArray().unwrap()) {
            if (auto id = item.asInt()) {
                out.levelIds.push_back(static_cast<int>(id.unwrap()));
            }
        }
    }

    if (root.contains("files") && root["files"].isArray()) {
        for (auto const& item : root["files"].asArray().unwrap()) {
            if (auto file = item.asString()) {
                auto path = getSongDir(songID) / file.unwrap();
                if (std::filesystem::exists(path, ec) && !ec) {
                    out.coverPaths.push_back(geode::utils::string::pathToString(path));
                }
            }
        }
    }

    if (out.coverPaths.empty()) {
        coverlog::warn("[SongCoverCache] manifest has no valid files songID={} levelIds={}",
            songID, out.levelIds.size());
    }
    return !out.coverPaths.empty();
}

void SongCoverCache::saveManifest(
    int songID,
    std::vector<int> const& levelIds,
    std::vector<std::string> const& coverPaths
) const {
    std::error_code ec;
    std::filesystem::create_directories(getSongDir(songID), ec);

    matjson::Value root = matjson::Value::object();
    auto levelArr = matjson::Value::array();
    for (int id : levelIds) levelArr.push(id);
    root["levelIds"] = levelArr;

    auto fileArr = matjson::Value::array();
    for (auto const& fullPath : coverPaths) {
        fileArr.push(geode::utils::string::pathToString(
            std::filesystem::path(fullPath).filename()));
    }
    root["files"] = fileArr;

    std::ofstream file(getSongDir(songID) / "manifest.json", std::ios::trunc);
    if (file) file << root.dump();
}

bool SongCoverCache::hasCachedCovers(int songID) const {
    return !getCachedCoverPaths(songID).empty();
}

std::vector<std::string> SongCoverCache::getCachedCoverPaths(int songID) const {
    if (songID <= 0) return {};

    SongManifest manifest;
    if (loadManifest(songID, manifest)) {
        return manifest.coverPaths;
    }

    std::vector<std::string> legacy;
    std::error_code ec;
    for (bool isGif : {false, true}) {
        auto ext = isGif ? ".gif" : ".png";
        auto path = getCoversDir() / fmt::format("{}{}", songID, ext);
        if (std::filesystem::exists(path, ec) && !ec) {
            legacy.push_back(geode::utils::string::pathToString(path));
            return legacy;
        }
    }
    return {};
}

void SongCoverCache::ensureSearchNode() {
    if (m_searchNode) return;
    auto* node = SongCoverSearchNode::create();
    if (!node) return;
    m_searchNode = node;
}

void SongCoverCache::cancelDebounce() {
    m_debounceGeneration.fetch_add(1, std::memory_order_acq_rel);
    m_debouncePending = false;
}

void SongCoverCache::scheduleDebouncedFlush(float delaySec) {
    m_debouncePending = true;
    auto const generation = m_debounceGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    auto const targetSong = m_targetSongID;
    coverlog::info("[SongCoverCache] debounce scheduled {:.1f}s gen={} songID={}",
        delaySec, generation, targetSong);

    paimon::scheduleMainThreadDelay(delaySec, [generation, targetSong]() {
        auto& cache = SongCoverCache::get();
        if (generation != cache.m_debounceGeneration.load(std::memory_order_acquire)) {
            coverlog::info("[SongCoverCache] debounce cancelled gen={} (current={})",
                generation, cache.m_debounceGeneration.load(std::memory_order_acquire));
            return;
        }
        cache.m_debouncePending = false;
        coverlog::info("[SongCoverCache] debounce fired gen={} songID={}", generation, targetSong);
        cache.flushDeferredRequests();
    });
}

void SongCoverCache::abortActiveWork() {
    if (auto* node = static_cast<SongCoverSearchNode*>(m_searchNode)) {
        node->clearDelegate();
    }
    m_searchInFlight = false;
}

void SongCoverCache::requestCovers(int songID, CoversCallback callback) {
    if (songID <= 0) {
        coverlog::warn("[SongCoverCache] requestCovers rejected: invalid songID={}", songID);
        if (callback) callback({}, false);
        return;
    }

    if (auto cached = getCachedCoverPaths(songID); !cached.empty()) {
        coverlog::info("[SongCoverCache] disk cache hit songID={} covers={}", songID, cached.size());
        if (callback) callback(cached, true);
        return;
    }

    if (paimon::isRuntimeShuttingDown()) {
        coverlog::warn("[SongCoverCache] requestCovers skipped songID={}: runtime shutting down", songID);
        return;
    }
    if (isCooldownActive()) {
        coverlog::warn("[SongCoverCache] requestCovers skipped songID={}: rate-limit cooldown ({:.0f}s left)",
            songID, m_cooldownUntil - nowSeconds());
        return;
    }

    int const previousSongID = m_targetSongID;
    bool const songChanged = previousSongID != songID;
    if (callback) {
        m_deferredCallbacks[songID].push_back(std::move(callback));
    }
    m_targetSongID = songID;

    if (songChanged) {
        coverlog::info("[SongCoverCache] song changed {} -> {}, debouncing {:.1f}s",
            previousSongID, songID, kDebounceSec);
        for (auto it = m_deferredCallbacks.begin(); it != m_deferredCallbacks.end(); ) {
            if (it->first != songID) it = m_deferredCallbacks.erase(it);
            else ++it;
        }
        abortActiveWork();
        m_queue.clear();
        scheduleDebouncedFlush(kDebounceSec);
        return;
    }

    if (!m_debouncePending) {
        coverlog::info("[SongCoverCache] same songID={}, scheduling debounce", songID);
        scheduleDebouncedFlush(kDebounceSec);
    }
}

void SongCoverCache::flushDeferredRequests() {
    m_debouncePending = false;

    if (paimon::isRuntimeShuttingDown()) {
        coverlog::warn("[SongCoverCache] flushDeferredRequests aborted: shutting down");
        return;
    }
    if (isCooldownActive()) {
        coverlog::warn("[SongCoverCache] flushDeferredRequests aborted: cooldown ({:.0f}s left)",
            m_cooldownUntil - nowSeconds());
        return;
    }

    double const now = nowSeconds();
    if (now < m_nextSearchAllowedAt) {
        coverlog::info("[SongCoverCache] waiting search gap {:.1f}s before songID={}",
            m_nextSearchAllowedAt - now, m_targetSongID);
        scheduleDebouncedFlush(static_cast<float>(m_nextSearchAllowedAt - now));
        return;
    }

    int const songID = m_targetSongID;
    if (songID <= 0) return;

    if (auto cached = getCachedCoverPaths(songID); !cached.empty()) {
        std::vector<CoversCallback> cbs;
        if (auto it = m_deferredCallbacks.find(songID); it != m_deferredCallbacks.end()) {
            cbs = std::move(it->second);
            m_deferredCallbacks.erase(it);
        }
        for (auto& cb : cbs) {
            if (cb) cb(cached, true);
        }
        return;
    }

    auto deferredIt = m_deferredCallbacks.find(songID);
    if (deferredIt == m_deferredCallbacks.end() || deferredIt->second.empty()) {
        for (auto& batch : m_queue) {
            if (batch.songID == songID) {
                coverlog::info(
                    "[SongCoverCache] flushDeferredRequests: pumping queued batch songID={}",
                    songID
                );
                pumpQueue();
                return;
            }
        }
        coverlog::info("[SongCoverCache] flushDeferredRequests: no callbacks for songID={}", songID);
        return;
    }

    for (auto& batch : m_queue) {
        if (batch.songID == songID) {
            auto& dst = batch.callbacks;
            auto& src = deferredIt->second;
            dst.insert(dst.end(),
                std::make_move_iterator(src.begin()),
                std::make_move_iterator(src.end()));
            deferredIt->second.clear();
            pumpQueue();
            return;
        }
    }

    PendingBatch batch;
    batch.songID = songID;
    batch.callbacks = std::move(deferredIt->second);
    m_deferredCallbacks.erase(deferredIt);

    m_queue.clear();
    m_queue.push_back(std::move(batch));

    coverlog::info("[SongCoverCache] debounced level search for songID={}", songID);
    pumpQueue();
}

void SongCoverCache::cancelPending(int songID) {
    m_deferredCallbacks.erase(songID);
    if (m_targetSongID == songID) {
        m_targetSongID = 0;
        cancelDebounce();
    }
    m_queue.erase(
        std::remove_if(m_queue.begin(), m_queue.end(),
            [&](PendingBatch const& b) { return b.songID == songID; }),
        m_queue.end()
    );
}

void SongCoverCache::cancelAllPending() {
    cancelDebounce();
    if (auto* node = static_cast<SongCoverSearchNode*>(m_searchNode)) {
        node->clearDelegate();
    }
    m_targetSongID = 0;
    m_debouncePending = false;
    m_deferredCallbacks.clear();
    m_queue.clear();
    m_searchInFlight = false;
}

void SongCoverCache::cleanup() {
    cancelAllPending();
}

void SongCoverCache::dispatchCallbacks(
    PendingBatch& batch,
    std::vector<std::string> const& coverPaths,
    bool success
) {
    auto callbacks = std::move(batch.callbacks);
    for (auto& cb : callbacks) {
        if (cb) cb(coverPaths, success);
    }
}

void SongCoverCache::finishRequest(
    int songID,
    std::vector<std::string> const& coverPaths,
    bool success
) {
    m_searchInFlight = false;

    if (success) {
        coverlog::info("[SongCoverCache] finishRequest OK songID={} covers={}", songID, coverPaths.size());
    } else {
        coverlog::warn("[SongCoverCache] finishRequest FAILED songID={}", songID);
    }

    auto it = std::find_if(m_queue.begin(), m_queue.end(),
        [&](PendingBatch const& b) { return b.songID == songID; });
    if (it != m_queue.end()) {
        dispatchCallbacks(*it, coverPaths, success);
        m_queue.erase(it);
    } else {
        coverlog::warn("[SongCoverCache] finishRequest: no queue batch for songID={}", songID);
    }

    if (!isCooldownActive() && !m_queue.empty()) {
        double const now = nowSeconds();
        if (now < m_nextSearchAllowedAt) {
            scheduleDebouncedFlush(static_cast<float>(m_nextSearchAllowedAt - now));
        } else {
            pumpQueue();
        }
    }
}

void SongCoverCache::handleLevelSearchFailed(int songID) {
    noteServerRateLimit();
    abortActiveWork();
    m_deferredCallbacks.clear();
    cancelDebounce();
    finishRequest(songID, {}, false);
}

void SongCoverCache::pumpQueue() {
    if (m_searchInFlight) {
        coverlog::info("[SongCoverCache] pumpQueue blocked: search in flight");
        return;
    }
    if (m_queue.empty()) return;
    if (paimon::isRuntimeShuttingDown()) return;
    if (isCooldownActive()) {
        coverlog::warn("[SongCoverCache] pumpQueue blocked: cooldown");
        return;
    }

    double const now = nowSeconds();
    if (now < m_nextSearchAllowedAt) {
        scheduleDebouncedFlush(static_cast<float>(m_nextSearchAllowedAt - now));
        return;
    }

    ensureSearchNode();
    auto* searchNode = static_cast<SongCoverSearchNode*>(m_searchNode);
    if (!searchNode) {
        coverlog::warn("[SongCoverCache] pumpQueue: search node unavailable");
        auto failed = m_queue.front();
        finishRequest(failed.songID, {}, false);
        return;
    }

    auto& batch = m_queue.front();
    if (batch.songID != m_targetSongID) {
        coverlog::warn("[SongCoverCache] pumpQueue: batch songID={} != target={}",
            batch.songID, m_targetSongID);
        finishRequest(batch.songID, {}, false);
        return;
    }

    if (auto cached = getCachedCoverPaths(batch.songID); !cached.empty()) {
        coverlog::info("[SongCoverCache] pumpQueue disk hit songID={}", batch.songID);
        finishRequest(batch.songID, cached, true);
        return;
    }

    if (batch.searchAttempt >= kMaxSearchAttempts) {
        coverlog::warn("[SongCoverCache] max search attempts reached songID={}", batch.songID);
        finishRequest(batch.songID, {}, false);
        return;
    }

    const bool customSong = shouldUseCustomSongFilter(batch.songID, batch.searchAttempt);
    auto* searchObj = makeSongSearchObject(batch.songID, customSong);
    if (!searchObj) {
        coverlog::warn("[SongCoverCache] makeSongSearchObject failed songID={}", batch.songID);
        finishRequest(batch.songID, {}, false);
        return;
    }

    auto key = searchObj->getKey();
    batch.searchKey = key ? key : "";
    if (batch.searchKey.empty()) {
        coverlog::warn("[SongCoverCache] empty search key songID={} attempt={}",
            batch.songID, batch.searchAttempt);
        finishRequest(batch.songID, {}, false);
        return;
    }

    auto* manager = GameLevelManager::get();
    if (!manager) {
        coverlog::warn("[SongCoverCache] GameLevelManager unavailable songID={}", batch.songID);
        finishRequest(batch.songID, {}, false);
        return;
    }

    coverlog::info("[SongCoverCache] RobTop search songID={} attempt={} customSong={} key={}",
        batch.songID, batch.searchAttempt, customSong, batch.searchKey);

    if (auto* cached = manager->getStoredOnlineLevels(searchObj->getKey())) {
        auto levelIds = collectTopLevelIds(cached, batch.songID, kMaxLevelsPerSong);
        coverlog::info("[SongCoverCache] GLM session cache hit songID={} levels={} matchedIds={}",
            batch.songID, cached->count(), levelIds.size());
        if (!levelIds.empty()) {
            m_searchInFlight = true;
            loadThumbnailsForLevels(batch.songID, levelIds);
            return;
        }
        coverlog::warn("[SongCoverCache] GLM session cache empty after filter songID={}", batch.songID);
    }

    m_searchInFlight = true;
    m_nextSearchAllowedAt = now + kMinSearchGapSec;
    searchNode->beginSearch(batch.songID, customSong, batch.searchKey);
    searchNode->clearDelegate();
    manager->m_levelManagerDelegate = searchNode;
    manager->getOnlineLevels(searchObj);
}

void SongCoverCache::handleLevelSearchResult(
    int songID,
    bool customSong,
    cocos2d::CCArray* levels
) {
    auto it = std::find_if(m_queue.begin(), m_queue.end(),
        [&](PendingBatch const& b) { return b.songID == songID; });
    if (it == m_queue.end() || songID != m_targetSongID) {
        coverlog::warn("[SongCoverCache] stale search result songID={} target={} inQueue={}",
            songID, m_targetSongID, it != m_queue.end());
        m_searchInFlight = false;
        return;
    }

    auto levelIds = collectTopLevelIds(levels, songID, kMaxLevelsPerSong);
    coverlog::info("[SongCoverCache] collectTopLevelIds songID={} rawLevels={} selected={}",
        songID, levels ? levels->count() : 0, levelIds.size());
    if (levelIds.empty()) {
        it->searchAttempt++;
        coverlog::warn("[SongCoverCache] no levels for songID={} attempt={}/{} customSong={}",
            songID, it->searchAttempt, kMaxSearchAttempts, customSong);
        if (it->searchAttempt < kMaxSearchAttempts && !isCooldownActive()) {
            m_searchInFlight = false;
            m_nextSearchAllowedAt = nowSeconds() + kMinSearchGapSec;
            scheduleDebouncedFlush(kMinSearchGapSec);
            return;
        }
        finishRequest(songID, {}, false);
        return;
    }

    loadThumbnailsForLevels(songID, levelIds);
}

void SongCoverCache::loadThumbnailsForLevels(int songID, std::vector<int> const& levelIds) {
    if (songID != m_targetSongID || levelIds.empty()) {
        finishRequest(songID, {}, false);
        return;
    }

    ensureSearchNode();
    Ref<CCNode> keepAlive = m_searchNode;

    auto state = std::make_shared<ThumbnailBatchState>();
    state->songID = songID;
    state->levelIds = levelIds;
    state->completed = std::make_shared<int>(0);
    state->pendingCount = static_cast<int>(levelIds.size());

    coverlog::info("[SongCoverCache] fetching {} thumbnails by level ID for songID={}",
        state->pendingCount, songID);

    ThumbnailLoader::get().prefetchLevels(levelIds, ThumbnailLoader::PriorityHero);

    for (int levelID : levelIds) {
        ThumbnailLoader::get().requestLoad(
            levelID,
            std::to_string(levelID),
            [this, state, levelID, keepAlive](CCTexture2D* tex, bool success) {
                if (paimon::isRuntimeShuttingDown() || state->songID != m_targetSongID) {
                    if (++(*state->completed) >= state->pendingCount) {
                        finishRequest(state->songID, state->coverPaths, !state->coverPaths.empty());
                    }
                    return;
                }

                if (success) {
                    if (persistLevelCover(state->songID, levelID, tex)) {
                        auto path = geode::utils::string::pathToString(
                            getSongDir(state->songID) / fmt::format("{}.png", levelID));
                        std::error_code ec;
                        if (!std::filesystem::exists(path, ec) || ec) {
                            auto gifPath = getSongDir(state->songID) / fmt::format("{}.gif", levelID);
                            if (std::filesystem::exists(gifPath, ec) && !ec) {
                                path = geode::utils::string::pathToString(gifPath);
                            }
                        }
                        if (std::filesystem::exists(path, ec) && !ec) {
                            state->savedLevelIds.push_back(levelID);
                            state->coverPaths.push_back(path);
                            coverlog::info("[SongCoverCache] persisted cover levelID={} songID={}",
                                levelID, state->songID);
                        } else {
                            coverlog::warn("[SongCoverCache] persist ok but file missing levelID={} songID={}",
                                levelID, state->songID);
                        }
                    } else {
                        coverlog::warn("[SongCoverCache] persist failed levelID={} songID={} tex={}",
                            levelID, state->songID, tex ? "ok" : "null");
                    }
                } else {
                    coverlog::warn("[SongCoverCache] thumbnail load failed levelID={} songID={}",
                        levelID, state->songID);
                }

                if (++(*state->completed) >= state->pendingCount) {
                    if (!state->coverPaths.empty()) {
                        saveManifest(state->songID, state->savedLevelIds, state->coverPaths);
                    }
                    coverlog::info("[SongCoverCache] saved {} covers for songID={}",
                        state->coverPaths.size(), state->songID);
                    finishRequest(state->songID, state->coverPaths, !state->coverPaths.empty());
                }
            },
            ThumbnailLoader::PriorityHero,
            false,
            ThumbnailLoader::Quality::Small
        );
    }
}

} // namespace paimon::menumusic