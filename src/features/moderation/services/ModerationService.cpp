#include "ModerationService.hpp"
#include "../../../utils/HttpClient.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/GameManager.hpp>
#include "../../../core/ModAuthFlow.hpp"

using namespace geode::prelude;

namespace {

// server sends ids as JSON numbers or numeric strings depending on the endpoint.
int intFieldLoose(matjson::Value const& item, char const* key) {
    if (item[key].isString())
        return geode::utils::numFromString<int>(item[key].asString().unwrapOr("0")).unwrapOr(0);
    if (item[key].isNumber())
        return item[key].asInt().unwrapOr(0);
    return 0;
}

} // namespace

void ModerationService::checkModerator(std::string const& username, ModeratorCallback callback) {
    auto* account = GJAccountManager::get();
    checkModeratorAccount(username, account ? account->m_accountID : 0, std::move(callback));
}

void ModerationService::checkModeratorAccount(std::string const& username, int accountID, ModeratorCallback callback) {
    if (!m_serverEnabled) { callback(false, false); return; }
    HttpClient::get().checkModeratorAccount(username, accountID, std::move(callback));
}

bool ModerationService::tryUserStatusCache(std::string const& username, ModeratorCallback& callback) {
    std::string key = geode::utils::string::toLower(username);
    std::optional<UserStatusCacheEntry> cached;
    {
        std::lock_guard<std::mutex> lock(m_userStatusMutex);
        auto it = m_userStatusCache.find(key);
        if (it == m_userStatusCache.end()) return false;
        if (std::chrono::steady_clock::now() - it->second.cachedAt >=
            std::chrono::seconds(USER_STATUS_CACHE_TTL_SECONDS)) {
            m_userStatusCache.erase(it);
            return false;
        }
        cached = it->second;
    }
    callback(cached->isMod, cached->isAdmin);
    return true;
}

void ModerationService::updateUserStatusCache(std::string const& username, bool isMod, bool isAdmin) {
    std::string key = geode::utils::string::toLower(username);
    std::lock_guard<std::mutex> lock(m_userStatusMutex);
    m_userStatusCache[key] = { isMod, isAdmin, std::chrono::steady_clock::now() };
}

void ModerationService::resetUserStatusCache() {
    std::lock_guard<std::mutex> lock(m_userStatusMutex);
    m_userStatusCache.clear();
}

void ModerationService::resetUserStatusCache(std::string const& username) {
    std::string key = geode::utils::string::toLower(username);
    std::lock_guard<std::mutex> lock(m_userStatusMutex);
    m_userStatusCache.erase(key);
}

void ModerationService::checkUserStatus(std::string const& username, ModeratorCallback callback) {
    if (!m_serverEnabled) { callback(false, false); return; }
    if (tryUserStatusCache(username, callback)) {
        log::debug("[ModService] checkUserStatus cache hit: user={}", username);
        return;
    }

    HttpClient::get().get("/api/moderator/check?username=" + HttpClient::encodeQueryParam(username),
        [this, username, callback](bool success, std::string const& response) {
            auto parsed = matjson::parse(response);
            if (!success || !parsed.isOk() || !parsed.unwrap().isObject()) {
                callback(false, false);
                return;
            }
            auto const& json = parsed.unwrap();
            bool isAdmin = json["isAdmin"].asBool().unwrapOr(false);
            bool isMod = isAdmin || json["isModerator"].asBool().unwrapOr(false);
            updateUserStatusCache(username, isMod, isAdmin);
            callback(isMod, isAdmin);
        });
}

void ModerationService::postModeratorChange(char const* endpoint, std::string const& username,
                                            std::string const& adminUser, char const* successMessage,
                                            ActionCallback callback) {
    if (!m_serverEnabled) { callback(false, "server disabled"); return; }

    auto* accountManager = GJAccountManager::get();
    if (!accountManager) {
        callback(false, "account manager unavailable");
        return;
    }

    matjson::Value json = matjson::makeObject({
        {"username", username},
        {"adminUser", adminUser},
        {"accountID", accountManager->m_accountID}
    });

    HttpClient::get().postWithAuth(endpoint, json.dump(),
        [callback = std::move(callback), successMessage](bool success, std::string const& response) {
            callback(success, success ? std::string(successMessage) : response);
        });
}

void ModerationService::postQueueAction(std::string const& endpoint, matjson::Value const& json,
                                        ActionCallback callback, geode::CopyableFunction<void()> onSuccess) {
    HttpClient::get().postWithAuth(endpoint, json.dump(),
        [callback = std::move(callback), onSuccess = std::move(onSuccess)](bool success, std::string const& response) {
            if (!success && (response.starts_with("HTTP 401") || response.starts_with("HTTP 403") ||
                response.find("MOD_") != std::string::npos ||
                response.find("SESSION_") != std::string::npos ||
                response.find("AUTH_") != std::string::npos)) {
                paimon::modauth::clearVerifiedSession();
            }
            if (success && onSuccess) onSuccess();
            callback(success, response);
        });
}

void ModerationService::addModerator(std::string const& username, std::string const& adminUser, ActionCallback callback) {
    postModeratorChange("/api/admin/add-moderator", username, adminUser,
                        "moderador anadido con exito", std::move(callback));
}

void ModerationService::removeModerator(std::string const& username, std::string const& adminUser, ActionCallback callback) {
    postModeratorChange("/api/admin/remove-moderator", username, adminUser,
                        "moderador eliminado con exito", std::move(callback));
}

void ModerationService::syncVerificationQueue(PendingCategory category, QueueCallback callback) {
    log::debug("[ModService] syncVerificationQueue: category={}", static_cast<int>(category));
    if (!m_serverEnabled) {
        callback(false, {});
        return;
    }

    std::string endpoint = "/api/queue/";
    switch (category) {
        case PendingCategory::Verify:            endpoint += "verify";            break;
        case PendingCategory::Update:            endpoint += "update";            break;
        case PendingCategory::Report:            endpoint += "report";            break;
        case PendingCategory::ProfileBackground: endpoint += "profilebackground"; break;
        case PendingCategory::ProfileImg:        endpoint += "profileimgs";       break;
    }

    std::string username;
    int accountID = 0;
    if (auto* gm = GameManager::get()) username = gm->m_playerName;
    if (auto* am = GJAccountManager::get()) accountID = am->m_accountID;
    if (!username.empty() && accountID > 0) {
        endpoint += "?username=" + HttpClient::encodeQueryParam(username)
                  + "&accountID=" + std::to_string(accountID);
    }

    HttpClient::get().get(endpoint, [callback, category, endpoint](bool success, std::string const& response) {

        if (!success) { callback(false, {}); return; }

        auto jsonRes = matjson::parse(response);
        if (!jsonRes.isOk()) { callback(false, {}); return; }
        auto json = jsonRes.unwrap();

        if (!json.contains("items") || !json["items"].isArray()) {
            callback(false, {});
            return;
        }
        auto itemsRes = json["items"].asArray();
        if (!itemsRes) { callback(false, {}); return; }

        std::vector<PendingItem> items;
        log::info("[ModService] syncVerificationQueue: parsing {} items from server", itemsRes.unwrap().size());
        for (auto const& item : itemsRes.unwrap()) {
            PendingItem it{};

            it.levelID = intFieldLoose(item, "levelId");
            if (it.levelID == 0) {
                static const char* accountIdFields[] = {"accountID", "accountId", "account_id", "userID", "userId", "user_id"};
                for (const char* fieldName : accountIdFields) {
                    int const val = intFieldLoose(item, fieldName);
                    if (val != 0) {
                        it.levelID = val;
                        break;
                    }
                }
            }

            log::debug("[ModService] Parsed item: levelID={}, category={}", it.levelID, static_cast<int>(category));

            it.category = category;

            {
                long long ms = 0;
                if (item["timestamp"].isString())
                    ms = geode::utils::numFromString<long long>(item["timestamp"].asString().unwrapOr("0")).unwrapOr(0);
                else if (item["timestamp"].isNumber())
                    ms = (long long)item["timestamp"].asDouble().unwrapOr(0.0);
                it.timestamp = (int64_t)(ms > 0 ? (ms / 1000) : 0);
            }

            it.submittedBy = item["submittedBy"].asString().unwrapOr("");
            it.note = item["note"].asString().unwrapOr("");
            it.claimedBy = item["claimedBy"].asString().unwrapOr("");
            it.filename = item["filename"].asString().unwrapOr("");
            if (item["claimedAt"].isNumber()) {
                it.claimedAt = (int64_t)item["claimedAt"].asDouble().unwrapOr(0.0);
            }

            it.status    = PendingStatus::Open;
            it.isCreator = false;

            auto sugArr = item["suggestions"].asArray();
            if (sugArr.isOk()) {
                for (auto const& sug : sugArr.unwrap()) {
                    Suggestion s;
                    s.filename = sug["filename"].asString().unwrapOr("");
                    s.submittedBy = sug["submittedBy"].asString().unwrapOr("");
                    if (sug["timestamp"].isNumber()) {
                        long long ms = (long long)sug["timestamp"].asDouble().unwrapOr(0.0);
                        s.timestamp = (int64_t)(ms > 0 ? (ms / 1000) : 0);
                    }
                    s.accountID = sug["accountID"].asInt().unwrapOr(0);
                    it.suggestions.push_back(s);
                }
            } else if (it.category == PendingCategory::Verify) {
                Suggestion s;
                std::string serverFilename = it.filename;
                if (!serverFilename.empty()) {
                    s.filename = serverFilename;
                } else {
                    s.filename = fmt::format("suggestions/{}.webp", it.levelID);
                }
                s.submittedBy = it.submittedBy;
                s.timestamp   = it.timestamp;
                it.suggestions.push_back(s);
            } else if ((it.category == PendingCategory::Update ||
                        it.category == PendingCategory::ProfileBackground ||
                        it.category == PendingCategory::ProfileImg) &&
                       !it.filename.empty()) {
                Suggestion s;
                s.filename    = it.filename;
                s.submittedBy = it.submittedBy;
                s.timestamp   = it.timestamp;
                it.suggestions.push_back(s);
            }

            it.type = item["type"].asString().unwrapOr("");
            it.reportedUsername = item["reportedUsername"].asString().unwrapOr("");
            auto repArr = item["reports"].asArray();
            if (repArr.isOk()) {
                for (auto const& rpt : repArr.unwrap()) {
                    ReportEntry re;
                    re.reporter = rpt["reporter"].asString().unwrapOr("");
                    re.reporterAccountID = rpt["reporterAccountID"].asInt().unwrapOr(0);
                    re.note = rpt["note"].asString().unwrapOr("");
                    if (rpt["timestamp"].isNumber()) {
                        long long ms = (long long)rpt["timestamp"].asDouble().unwrapOr(0.0);
                        re.timestamp = (int64_t)(ms > 0 ? (ms / 1000) : 0);
                    }
                    it.reports.push_back(re);
                }
            }

            if (it.levelID != 0) {
                items.push_back(std::move(it));
            } else {
                log::warn("[ModService] Filtered out item with invalid levelID=0, category={}", static_cast<int>(category));
            }
        }
        log::info("[ModService] syncVerificationQueue: returning {} items (filtered from server response)", items.size());
        callback(true, items);
    });
}

namespace {

// base payload shared by every queue action; callers add their own fields.
matjson::Value makeQueueJson(int levelId, PendingCategory category,
                             std::string const& username, int accountID) {
    return matjson::makeObject({
        {"levelId", levelId},
        {"category", PendingQueue::catToStr(category)},
        {"username", username},
        {"accountID", accountID}
    });
}

} // namespace

void ModerationService::claimQueueItem(int levelId, PendingCategory category,
                                       std::string const& username, ActionCallback callback,
                                       std::string const& type) {
    if (!m_serverEnabled) { callback(false, "servidor desactivado"); return; }

    int accountID = 0;
    if (auto* am = GJAccountManager::get()) accountID = am->m_accountID;
    if (username.empty() || accountID <= 0) { callback(false, "Account ID required"); return; }

    matjson::Value json = makeQueueJson(levelId, category, username, accountID);
    if (!type.empty()) json["type"] = type;

    postQueueAction(fmt::format("/api/queue/claim/{}", levelId), json, std::move(callback));
}

void ModerationService::unclaimQueueItem(int levelId, PendingCategory category,
                                         std::string const& username, ActionCallback callback,
                                         std::string const& type, bool force) {
    if (!m_serverEnabled) { callback(false, "servidor desactivado"); return; }

    int accountID = 0;
    if (auto* am = GJAccountManager::get()) accountID = am->m_accountID;
    if (username.empty() || accountID <= 0) { callback(false, "Account ID required"); return; }

    matjson::Value json = makeQueueJson(levelId, category, username, accountID);
    json["action"] = "unclaim";
    if (force) json["force"] = true;
    if (!type.empty()) json["type"] = type;

    postQueueAction(fmt::format("/api/queue/unclaim/{}", levelId), json, std::move(callback));
}

void ModerationService::takeoverQueueItem(int levelId, PendingCategory category,
                                          std::string const& username, ActionCallback callback,
                                          std::string const& type) {
    if (!m_serverEnabled) { callback(false, "servidor desactivado"); return; }

    int accountID = 0;
    if (auto* am = GJAccountManager::get()) accountID = am->m_accountID;
    if (username.empty() || accountID <= 0) { callback(false, "Account ID required"); return; }

    matjson::Value json = makeQueueJson(levelId, category, username, accountID);
    json["action"] = "takeover";
    json["force"] = true;
    if (!type.empty()) json["type"] = type;

    postQueueAction(fmt::format("/api/queue/claim/{}", levelId), json, std::move(callback));
}

void ModerationService::acceptQueueItem(int levelId, PendingCategory category,
                                        std::string const& username, ActionCallback callback,
                                        std::string const& targetFilename,
                                        std::string const& type,
                                        bool acceptAll) {
    if (!m_serverEnabled) {
        callback(false, "servidor desactivado");
        return;
    }

    auto* accountManager = GJAccountManager::get();
    if (!accountManager) {
        callback(false, "account manager unavailable");
        return;
    }

    int accountID = accountManager->m_accountID;

    matjson::Value json = makeQueueJson(levelId, category, username, accountID);
    if (acceptAll) json["acceptAll"] = true;
    else if (!targetFilename.empty()) json["targetFilename"] = targetFilename;
    if (!type.empty()) json["type"] = type;

    postQueueAction(fmt::format("/api/queue/accept/{}", levelId), json, std::move(callback),
        [levelId, category]() { PendingQueue::get().accept(levelId, category); });
}

void ModerationService::rejectQueueItem(int levelId, PendingCategory category,
                                        std::string const& username, std::string const& reason,
                                        ActionCallback callback,
                                        std::string const& type,
                                        std::string const& targetFilename) {
    if (!m_serverEnabled) {
        callback(false, "servidor desactivado");
        return;
    }

    auto* accountManager = GJAccountManager::get();
    if (!accountManager) {
        callback(false, "account manager unavailable");
        return;
    }

    int accountID = accountManager->m_accountID;

    matjson::Value json = makeQueueJson(levelId, category, username, accountID);
    json["reason"] = reason;
    if (!type.empty()) json["type"] = type;
    if (!targetFilename.empty()) json["targetFilename"] = targetFilename;

    postQueueAction(fmt::format("/api/queue/reject/{}", levelId), json, std::move(callback),
        [levelId, category, reason]() { PendingQueue::get().reject(levelId, category, reason); });
}

void ModerationService::submitReport(int levelId, std::string const& username,
                                     std::string const& note, ActionCallback callback) {
    if (!m_serverEnabled) {
        PendingQueue::get().addOrBump(levelId, PendingCategory::Report, username, note);
        callback(true, "reportado localmente");
        return;
    }

    HttpClient::get().submitReport(levelId, username, note,
        [callback, levelId, username, note](bool success, std::string const& response) {
            if (success) PendingQueue::get().addOrBump(levelId, PendingCategory::Report, username, note);
            callback(success, response);
        });
}

void ModerationService::resetModCache() {
    paimon::modauth::clearVerifiedSession();
}
