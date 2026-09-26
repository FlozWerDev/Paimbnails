#pragma once

// Single queue for realtime level search: shared cache, in-flight dedup, rate limit.

#include <Geode/Geode.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace paimon::levelsearch {

enum class SearchKind {
    Levels,
    Users,
    Lists,
};

class SearchRequestCoordinator : public cocos2d::CCNode, public LevelManagerDelegate {
public:
    using Token = std::uint64_t;

    // ok=false means failed or dropped. items may be null.
    using Callback = std::function<void(bool ok, cocos2d::CCArray* items, std::string const& pageInfo)>;

    static SearchRequestCoordinator& get();

    // Queues a request or serves it from cache. Returns 0 on sync cache hit.
    Token request(SearchKind kind, GJSearchObject* object, Callback callback);

    // Drops a waiter; the result is still cached for the next caller.
    void cancel(Token token);

    // A prefix that returned nothing poisons longer queries, answered locally.
    bool isKnownEmpty(SearchKind kind, std::string const& query) const;
    void noteQueryOutcome(SearchKind kind, std::string const& query, int resultCount);

    // Drops cached pages and prefix knowledge on layer exit.
    void reset();

    // LevelManagerDelegate
    void loadLevelsFinished(cocos2d::CCArray* levels, char const* key) override;
    void loadLevelsFailed(char const* key) override;
    void loadLevelsFinished(cocos2d::CCArray* levels, char const* key, int) override;
    void loadLevelsFailed(char const* key, int) override;
    void setupPageInfo(gd::string info, char const* key) override;

private:
    struct Waiter {
        Token token = 0;
        Callback callback;
    };

    struct Request {
        SearchKind kind = SearchKind::Levels;
        std::string key;
        geode::Ref<GJSearchObject> object;
        std::vector<Waiter> waiters;
    };

    struct CacheEntry {
        geode::Ref<cocos2d::CCArray> items;
        std::string pageInfo;
        double timestamp = 0.0;
    };

    // 48 pages of <=10 rows: survives back-and-forth paging without pinning memory.
    static constexpr std::size_t kMaxCacheEntries = 48;
    static constexpr double kCacheTtlSeconds = 90.0;
    static constexpr double kMinDispatchInterval = 0.40;
    static constexpr double kRequestTimeout = 15.0;
    static constexpr std::size_t kMinPrefixLength = 3;
    static constexpr double kEmptyPrefixTtlSeconds = 120.0;

    Token m_nextToken = 1;
    std::deque<Request> m_queue;
    Request m_current;
    bool m_inFlight = false;
    bool m_pumpScheduled = false;
    double m_lastDispatch = 0.0;
    std::string m_currentPageInfo;
    LevelManagerDelegate* m_previousDelegate = nullptr;

    std::unordered_map<std::string, CacheEntry> m_cache;
    std::deque<std::string> m_cacheOrder;
    std::unordered_map<std::string, double> m_emptyQueries;

    static double nowSeconds();
    static std::string cacheKey(SearchKind kind, std::string const& searchKey);
    static std::string emptyKey(SearchKind kind, std::string const& query);

    void schedulePump(double delay);
    void pump(float);
    void onTimeout(float);

    void dispatch(GameLevelManager* manager, Request const& request);
    void restoreDelegate();
    void finishCurrent(bool ok, cocos2d::CCArray* items);
    void store(SearchKind kind, std::string const& searchKey, cocos2d::CCArray* items, std::string const& pageInfo);
    CacheEntry const* lookup(SearchKind kind, std::string const& searchKey);
    void trimCache();
    bool isCurrentKey(char const* key) const;
};

} // namespace paimon::levelsearch
