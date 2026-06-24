// Intercepts RobTop user/profile lookups via GameLevelManager and serves
// disk-cached responses (7 days) before hitting the network.
// Online level/list browse results are deliberately left uncached so the
// dynamic search filters always reflect the live server (see the class note).

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

void maybeStore(std::string const& category, std::string const& key, gd::string const& response) {
    if (!shouldUseCache() || key.empty()) return;
    auto body = static_cast<std::string>(response);
    if (body.empty() || body == "-1") return;
    paimon::gd::GDRobTopCache::get().store(category, key, body, paimon::gd::kCacheTTLWeek);
}

// Serves a cached response for `key` if present. Returns true when the cached
// response was queued (caller must then skip the original network call).
// Callers guard `shouldUseCache()` first to preserve the original short-circuit
// ordering (no key derivation when caching is disabled).
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
// Caching IS still applied to user searches and user info (profiles), where the
// data is effectively static and rate-limit reduction is the real win.
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
    void getGJUserInfo(int id) {
        if (shouldUseCache() && id > 0 &&
            tryServeCached(this, GameLevelManager::getUserInfoKey(id), "userinfo",
                           &GameLevelManager::onGetGJUserInfoCompleted)) {
            return;
        }
        GameLevelManager::getGJUserInfo(id);
    }

    $override
    void onGetUsersCompleted(gd::string response, gd::string tag) {
        maybeStore("users", static_cast<std::string>(tag), response);
        GameLevelManager::onGetUsersCompleted(response, tag);
    }

    $override
    void onGetGJUserInfoCompleted(gd::string response, gd::string tag) {
        maybeStore("userinfo", static_cast<std::string>(tag), response);
        GameLevelManager::onGetGJUserInfoCompleted(response, tag);
    }
};