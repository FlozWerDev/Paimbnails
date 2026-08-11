// Disk-cache user searches; browse filters and profiles stay native and fresh.

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

// Queue a cached response for key when available.
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

// Browse filters change constantly, so disk snapshots went stale; GD already
// keeps them in memory. Profiles include relationship state, so native caching
// handles those as well.
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
