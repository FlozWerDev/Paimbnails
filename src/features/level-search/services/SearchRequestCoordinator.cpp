#include "SearchRequestCoordinator.hpp"

#include "../../../core/RuntimeLifecycle.hpp"

#include <algorithm>
#include <chrono>

using namespace geode::prelude;

namespace paimon::levelsearch {

SearchRequestCoordinator& SearchRequestCoordinator::get() {
    // Intentionally never released: it outlives every LevelSearchLayer and
    // holds the cache that makes re-entering the layer free.
    static SearchRequestCoordinator* instance = nullptr;
    if (!instance) {
        instance = new SearchRequestCoordinator();
        instance->init();
        instance->retain();
    }
    return *instance;
}

double SearchRequestCoordinator::nowSeconds() {
    using clock = std::chrono::steady_clock;
    static auto const start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}

std::string SearchRequestCoordinator::cacheKey(SearchKind kind, std::string const& searchKey) {
    return fmt::format("{}|{}", static_cast<int>(kind), searchKey);
}

std::string SearchRequestCoordinator::emptyKey(SearchKind kind, std::string const& query) {
    std::string lowered = query;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return fmt::format("{}|{}", static_cast<int>(kind), lowered);
}

SearchRequestCoordinator::Token SearchRequestCoordinator::request(
    SearchKind kind,
    GJSearchObject* object,
    Callback callback
) {
    if (!object || !callback) {
        if (callback) callback(false, nullptr, {});
        return 0;
    }

    auto const* rawKey = object->getKey();
    std::string key = rawKey ? rawKey : "";
    if (key.empty()) {
        callback(false, nullptr, {});
        return 0;
    }

    // Copy the entry out first: the callback may re-enter and rehash m_cache, dangling a reference into it.
    if (auto const* entry = lookup(kind, key)) {
        auto items = entry->items;
        auto pageInfo = entry->pageInfo;
        callback(true, items.data(), pageInfo);
        return 0;
    }

    // GD's own store already holds anything fetched this session. Levels only.
    if (kind == SearchKind::Levels) {
        if (auto* manager = GameLevelManager::get()) {
            if (auto* stored = manager->getStoredOnlineLevels(key.c_str())) {
                std::string pageInfo;
                if (auto info = manager->getPageInfo(key.c_str())) {
                    pageInfo = info;
                }
                store(kind, key, stored, pageInfo);
                callback(true, stored, pageInfo);
                return 0;
            }
        }
    }

    auto token = m_nextToken++;

    // Join an identical request already running or queued.
    if (m_inFlight && m_current.key == key && m_current.kind == kind) {
        m_current.waiters.push_back({token, std::move(callback)});
        return token;
    }
    for (auto& queued : m_queue) {
        if (queued.key == key && queued.kind == kind) {
            queued.waiters.push_back({token, std::move(callback)});
            return token;
        }
    }

    Request entry;
    entry.kind = kind;
    entry.key = key;
    entry.object = object;
    entry.waiters.push_back({token, std::move(callback)});
    m_queue.push_back(std::move(entry));

    schedulePump(0.0);
    return token;
}

void SearchRequestCoordinator::cancel(Token token) {
    if (token == 0) return;

    auto drop = [token](std::vector<Waiter>& waiters) {
        waiters.erase(
            std::remove_if(waiters.begin(), waiters.end(), [token](Waiter const& waiter) {
                return waiter.token == token;
            }),
            waiters.end()
        );
    };

    drop(m_current.waiters);
    for (auto& queued : m_queue) {
        drop(queued.waiters);
    }
}

bool SearchRequestCoordinator::isKnownEmpty(SearchKind kind, std::string const& query) const {
    if (query.size() < kMinPrefixLength) return false;

    auto const now = nowSeconds();
    for (std::size_t len = kMinPrefixLength; len <= query.size(); ++len) {
        auto it = m_emptyQueries.find(emptyKey(kind, query.substr(0, len)));
        if (it == m_emptyQueries.end()) continue;
        if (now - it->second <= kEmptyPrefixTtlSeconds) return true;
    }
    return false;
}

void SearchRequestCoordinator::noteQueryOutcome(SearchKind kind, std::string const& query, int resultCount) {
    if (query.size() < kMinPrefixLength) return;

    auto key = emptyKey(kind, query);
    if (resultCount > 0) {
        m_emptyQueries.erase(key);
        return;
    }
    m_emptyQueries[key] = nowSeconds();

    // Keep the map small; it is only a hint.
    if (m_emptyQueries.size() > 256) {
        m_emptyQueries.clear();
    }
}

void SearchRequestCoordinator::reset() {
    m_queue.clear();
    m_current.waiters.clear();
    m_cache.clear();
    m_cacheOrder.clear();
    m_emptyQueries.clear();
}

void SearchRequestCoordinator::schedulePump(double delay) {
    if (m_pumpScheduled) return;
    auto* scheduler = CCDirector::get() ? CCDirector::get()->getScheduler() : nullptr;
    if (!scheduler) return;

    m_pumpScheduled = true;
    scheduler->scheduleSelector(
        schedule_selector(SearchRequestCoordinator::pump),
        this,
        0.f,
        0,
        static_cast<float>(std::max(0.0, delay)),
        false
    );
}

void SearchRequestCoordinator::pump(float) {
    m_pumpScheduled = false;
    if (auto* scheduler = CCDirector::get() ? CCDirector::get()->getScheduler() : nullptr) {
        scheduler->unscheduleSelector(schedule_selector(SearchRequestCoordinator::pump), this);
    }

    if (paimon::isRuntimeShuttingDown()) return;
    if (m_inFlight) return;

    // Drop entries whose only waiters cancelled while queued.
    while (!m_queue.empty() && m_queue.front().waiters.empty()) {
        m_queue.pop_front();
    }
    if (m_queue.empty()) return;

    auto const now = nowSeconds();
    auto const wait = kMinDispatchInterval - (now - m_lastDispatch);
    if (wait > 0.0) {
        schedulePump(wait);
        return;
    }

    auto* manager = GameLevelManager::get();
    if (!manager) {
        m_current = std::move(m_queue.front());
        m_queue.pop_front();
        finishCurrent(false, nullptr);
        return;
    }

    m_current = std::move(m_queue.front());
    m_queue.pop_front();
    m_currentPageInfo.clear();

    // Remember whoever owned the delegate so the layer we interrupted keeps
    // receiving its own callbacks once we are done.
    m_previousDelegate = manager->m_levelManagerDelegate;
    if (m_previousDelegate == this) m_previousDelegate = nullptr;
    manager->m_levelManagerDelegate = this;

    m_inFlight = true;
    m_lastDispatch = now;
    dispatch(manager, m_current);

    if (auto* scheduler = CCDirector::get() ? CCDirector::get()->getScheduler() : nullptr) {
        scheduler->scheduleSelector(
            schedule_selector(SearchRequestCoordinator::onTimeout),
            this,
            0.f,
            0,
            static_cast<float>(kRequestTimeout),
            false
        );
    }
}

void SearchRequestCoordinator::onTimeout(float) {
    if (!m_inFlight) return;
    // A dropped callback would otherwise wedge the queue forever.
    log::warn("[realtime-search] request timed out: {}", m_current.key);
    finishCurrent(false, nullptr);
}

void SearchRequestCoordinator::dispatch(GameLevelManager* manager, Request const& request) {
    if (!manager || !request.object) return;

    switch (request.kind) {
        case SearchKind::Users:
            manager->getUsers(request.object.data());
            return;
        case SearchKind::Lists:
            manager->getLevelLists(request.object.data());
            return;
        case SearchKind::Levels:
        default:
            manager->getOnlineLevels(request.object.data());
            return;
    }
}

void SearchRequestCoordinator::restoreDelegate() {
    if (auto* manager = GameLevelManager::get()) {
        if (manager->m_levelManagerDelegate == this) {
            manager->m_levelManagerDelegate = m_previousDelegate;
        }
    }
    m_previousDelegate = nullptr;
}

void SearchRequestCoordinator::finishCurrent(bool ok, CCArray* items) {
    if (auto* scheduler = CCDirector::get() ? CCDirector::get()->getScheduler() : nullptr) {
        scheduler->unscheduleSelector(schedule_selector(SearchRequestCoordinator::onTimeout), this);
    }

    restoreDelegate();
    m_inFlight = false;

    auto request = std::move(m_current);
    m_current = Request{};
    auto pageInfo = m_currentPageInfo;
    m_currentPageInfo.clear();

    if (ok) {
        store(request.kind, request.key, items, pageInfo);
        noteQueryOutcome(request.kind, request.object ? std::string(request.object->m_searchQuery) : std::string(),
            items ? static_cast<int>(items->count()) : 0);
    }

    for (auto& waiter : request.waiters) {
        if (waiter.callback) waiter.callback(ok, items, pageInfo);
    }

    schedulePump(0.0);
}

void SearchRequestCoordinator::store(
    SearchKind kind,
    std::string const& searchKey,
    CCArray* items,
    std::string const& pageInfo
) {
    if (searchKey.empty()) return;

    // Copy so a later GD refresh of the same array cannot mutate our snapshot.
    auto snapshot = CCArray::create();
    if (items) {
        for (auto* object : CCArrayExt<CCObject*>(items)) {
            if (object) snapshot->addObject(object);
        }
    }

    auto key = cacheKey(kind, searchKey);
    if (m_cache.find(key) == m_cache.end()) {
        m_cacheOrder.push_back(key);
    }
    m_cache[key] = CacheEntry{snapshot, pageInfo, nowSeconds()};
    trimCache();
}

SearchRequestCoordinator::CacheEntry const* SearchRequestCoordinator::lookup(
    SearchKind kind,
    std::string const& searchKey
) {
    auto key = cacheKey(kind, searchKey);
    auto it = m_cache.find(key);
    if (it == m_cache.end()) return nullptr;

    if (nowSeconds() - it->second.timestamp > kCacheTtlSeconds) {
        m_cache.erase(it);
        m_cacheOrder.erase(std::remove(m_cacheOrder.begin(), m_cacheOrder.end(), key), m_cacheOrder.end());
        return nullptr;
    }

    // Touch for LRU.
    m_cacheOrder.erase(std::remove(m_cacheOrder.begin(), m_cacheOrder.end(), key), m_cacheOrder.end());
    m_cacheOrder.push_back(key);
    return &it->second;
}

void SearchRequestCoordinator::trimCache() {
    while (m_cacheOrder.size() > kMaxCacheEntries) {
        auto oldest = m_cacheOrder.front();
        m_cacheOrder.pop_front();
        m_cache.erase(oldest);
    }
}

bool SearchRequestCoordinator::isCurrentKey(char const* key) const {
    return m_inFlight && key && !m_current.key.empty() && m_current.key == key;
}

void SearchRequestCoordinator::loadLevelsFinished(CCArray* levels, char const* key) {
    if (paimon::isRuntimeShuttingDown()) return;
    if (!isCurrentKey(key)) return;

    if (m_currentPageInfo.empty()) {
        if (auto* manager = GameLevelManager::get()) {
            if (auto info = manager->getPageInfo(key)) {
                m_currentPageInfo = info;
            }
        }
    }

    finishCurrent(true, levels);
}

void SearchRequestCoordinator::loadLevelsFailed(char const* key) {
    if (paimon::isRuntimeShuttingDown()) return;
    if (!isCurrentKey(key)) return;
    finishCurrent(false, nullptr);
}

void SearchRequestCoordinator::loadLevelsFinished(CCArray* levels, char const* key, int) {
    loadLevelsFinished(levels, key);
}

void SearchRequestCoordinator::loadLevelsFailed(char const* key, int) {
    loadLevelsFailed(key);
}

void SearchRequestCoordinator::setupPageInfo(gd::string info, char const* key) {
    if (paimon::isRuntimeShuttingDown()) return;
    if (!isCurrentKey(key)) return;
    m_currentPageInfo = info;
}

} // namespace paimon::levelsearch
