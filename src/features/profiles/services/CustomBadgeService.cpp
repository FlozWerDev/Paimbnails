#include "CustomBadgeService.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include <matjson.hpp>
#include <Geode/loader/Log.hpp>

using namespace geode::prelude;

void CustomBadgeService::fetchBadge(int accountID, BadgeCallback callback) {
    {
        std::lock_guard lock(m_mutex);
        auto it = m_cache.find(accountID);
        if (it != m_cache.end()) {
            auto age = std::chrono::steady_clock::now() - it->second.cachedAt;
            if (age < CACHE_TTL) {
                auto name = it->second.emoteName;
                Loader::get()->queueInMainThread([callback, name]() mutable {
                    if (paimon::isRuntimeShuttingDown()) return;
                    callback(true, name);
                });
                return;
            }
            m_cache.erase(it);
        }

        m_pendingRequests.push_back({ accountID, std::move(callback) });

        if (!m_flushScheduled) {
            m_flushScheduled = true;
            Loader::get()->queueInMainThread([this]() {
                if (paimon::isRuntimeShuttingDown()) return;
                flushPendingRequests();
            });
        }
    }
}

void CustomBadgeService::flushPendingRequests() {
    if (paimon::isRuntimeShuttingDown()) return;
    std::vector<PendingBadgeRequest> pending;
    {
        std::lock_guard lock(m_mutex);
        m_flushScheduled = false;
        pending.swap(m_pendingRequests);
    }

    if (pending.empty()) return;

    std::vector<int> uniqueIDs;
    for (auto& req : pending) {
        bool found = false;
        for (auto id : uniqueIDs) {
            if (id == req.accountID) { found = true; break; }
        }
        if (!found) uniqueIDs.push_back(req.accountID);
    }

    if (uniqueIDs.size() == 1) {
        int accountID = uniqueIDs[0];
        HttpClient::get().downloadCustomBadge(accountID,
            [this, accountID, pending = std::move(pending)](bool success, std::string const& response) mutable {
                std::string emoteName;
                if (success && !response.empty()) {
                    auto res = matjson::parse(response);
                    if (res.isOk()) {
                        auto json = res.unwrap();
                        if (json.contains("emote")) {
                            emoteName = json["emote"].asString().unwrapOr("");
                        }
                    }
                }
                {
                    std::lock_guard lock(m_mutex);
                    m_cache[accountID] = { emoteName, std::chrono::steady_clock::now() };
                }
                Loader::get()->queueInMainThread([pending = std::move(pending), success, emoteName]() mutable {
                    if (paimon::isRuntimeShuttingDown()) return;
                    for (auto& req : pending) {
                        if (req.callback) req.callback(success, emoteName);
                    }
                });
            });
        return;
    }

    HttpClient::get().downloadCustomBadgeBatch(uniqueIDs,
        [this, pending = std::move(pending)](bool success, std::string const& response) mutable {
            std::unordered_map<int, std::string> results;
            if (success && !response.empty()) {
                auto res = matjson::parse(response);
                if (res.isOk()) {
                    auto json = res.unwrap();
                    if (json.contains("badges") && json["badges"].isObject()) {
                        auto& badges = json["badges"];
                        for (auto& [key, val] : badges) {
                            int id = 0;
                            auto idRes = geode::utils::numFromString<int>(key);
                            if (!idRes) continue;
                            id = idRes.unwrap();
                            std::string emote;
                            if (val.isObject() && val.contains("emote")) {
                                emote = val["emote"].asString().unwrapOr("");
                            }
                            results[id] = emote;
                        }
                    }
                } else {
                    log::warn("[CustomBadge] Failed to parse batch response");
                }
            }

            {
                std::lock_guard lock(m_mutex);
                for (auto& [id, emote] : results) {
                    m_cache[id] = { emote, std::chrono::steady_clock::now() };
                }
            }

            Loader::get()->queueInMainThread([pending = std::move(pending), success, results = std::move(results)]() mutable {
                if (paimon::isRuntimeShuttingDown()) return;
                for (auto& req : pending) {
                    auto it = results.find(req.accountID);
                    std::string emote = (it != results.end()) ? it->second : "";
                    if (req.callback) req.callback(success, emote);
                }
            });
        });
}

void CustomBadgeService::setBadge(int accountID, std::string const& emoteName, ActionCallback callback) {
    HttpClient::get().uploadCustomBadge(accountID, emoteName,
        [this, accountID, emoteName, callback](bool success, std::string const& msg) mutable {
            if (success) {
                std::lock_guard lock(m_mutex);
                m_cache[accountID] = { emoteName, std::chrono::steady_clock::now() };
            } else {
                log::warn("[CustomBadge] Failed to set badge '{}' for accountID {}: {}", emoteName, accountID, msg);
            }
            Loader::get()->queueInMainThread([callback, success, msg]() mutable {
                if (paimon::isRuntimeShuttingDown()) return;
                callback(success, msg);
            });
        });
}

void CustomBadgeService::clearBadge(int accountID, ActionCallback callback) {
    HttpClient::get().deleteCustomBadge(accountID,
        [this, accountID, callback](bool success, std::string const& msg) mutable {
            if (success) {
                std::lock_guard lock(m_mutex);
                m_cache[accountID] = { "", std::chrono::steady_clock::now() };
            } else {
                log::warn("[CustomBadge] Failed to clear badge for accountID {}: {}", accountID, msg);
            }
            Loader::get()->queueInMainThread([callback, success, msg]() mutable {
                if (paimon::isRuntimeShuttingDown()) return;
                callback(success, msg);
            });
        });
}

void CustomBadgeService::invalidateCache(int accountID) {
    std::lock_guard lock(m_mutex);
    m_cache.erase(accountID);
}

void CustomBadgeService::updateCacheFromBundle(int accountID, std::string const& emoteName) {
    std::lock_guard lock(m_mutex);
    m_cache[accountID] = { emoteName, std::chrono::steady_clock::now() };
}