// Intercepts RobTop user-search lookups via GameLevelManager and serves
// disk-cached responses for instant results. Online level/list browse results
// are deliberately left uncached so dynamic search filters stay live.

#include <Geode/Geode.hpp>
#include <Geode/modify/GameLevelManager.hpp>

#include "../utils/GDRobTopCache.hpp"
#include "../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;

namespace {

bool shouldUseCache() {
    return paimon::gd::GDRobTopCache::get().isEnabled() && !paimon::isRuntimeShuttingDown();
}

void deliverCachedResponse(
    GameLevelManager* self,
    std::string const& response,
    std::string const& tag,
    void (GameLevelManager::*handler)(gd::string, gd::string)
) {
    Loader::get()->queueInMainThread([self, response, tag, handler]() {
        if (paimon::isRuntimeShuttingDown() || !self) return;
        (self->*handler)(response, tag);
    });
}

void maybeStore(std::string const& category, std::string const& key, gd::string const& response, std::time_t ttl) {
    if (!shouldUseCache() || key.empty()) return;
    auto body = static_cast<std::string>(response);
    if (body.empty() || body == "-1") return;
    paimon::gd::GDRobTopCache::get().store(category, key, body, ttl);
}

// Serves a cached response for `key` if present. Returns true when the cached
// response was queued.
bool tryServeCached(
    GameLevelManager* self,
    char const* key,
    std::string const& category,
    void (GameLevelManager::*handler)(gd::string, gd::string)
) {
    if (!key || !*key) return false;
    auto cached = paimon::gd::GDRobTopCache::get().lookup(category, key);
    if (!cached) return false;
    log::debug("[GDRobTopCache] {} hit: {}", category, key);
    deliverCachedResponse(self, *cached, key, handler);
    return true;
}

} // namespace

// NOTE: Online level/list *browse* results (getOnlineLevels / getLevelLists)
// are intentionally NOT disk-cached.
//
// Those endpoints back the dynamic search filters (Awarded, Recent, Trending,
// Most Liked, etc.), whose contents rotate on the server constantly. Serving a
// disk-cached snapshot (TTL was 7 days) made the filter buttons show old levels
// instead of the latest ones. Geometry Dash already keeps online levels in
// memory (m_onlineLevels) for the duration of a session, so paging back and
// forth within a session stays fast without our disk cache; the disk layer only
// added cross-session persistence, which is exactly what caused the staleness.
//
// User *search* keeps a hard cache (list results change slowly; rate-limit win).
//
// User *info* (profiles) is deliberately NOT cached here anymore: the
// getGJUserInfo response carries relationship state (friendstate, incoming
// request, blocked) that changes server-side. Serving a disk-cached copy made
// ProfilePage show stale "not friends" states even after a request was
// accepted, and the SWR replay interfered with GD's active-download
// bookkeeping. GD already keeps profiles in memory for the session, so we let
// the native cache handle it.
class $modify(PaimonRobTopCacheGameLevelManager, GameLevelManager) {
    $override
    void getUsers(GJSearchObject* object) {
        if (shouldUseCache() && object &&
            tryServeCached(this, object->getKey(), "users",
                           &GameLevelManager::onGetUsersCompleted)) {
            return;
        }
        GameLevelManager::getUsers(object);
    }

    $override
    void onGetUsersCompleted(gd::string response, gd::string tag) {
        maybeStore("users", static_cast<std::string>(tag), response, paimon::gd::kCacheTTLWeek);
        GameLevelManager::onGetUsersCompleted(response, tag);
    }
};
