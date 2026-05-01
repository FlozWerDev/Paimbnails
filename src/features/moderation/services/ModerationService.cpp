#include "ModerationService.hpp"
#include "../../../utils/HttpClient.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/GameManager.hpp>
#include <fstream>

using namespace geode::prelude;

// ── cache helpers ───────────────────────────────────────────────────

bool ModerationService::tryModCache(ModeratorCallback& callback) {
    if (!m_modCache.has_value()) return false;
    auto elapsed = std::chrono::steady_clock::now() - m_modCache->timestamp;
    if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() < MOD_CACHE_TTL_SECONDS) {
        callback(m_modCache->isMod, m_modCache->isAdmin);
        return true;
    }
    m_modCache.reset();
    return false;
}

void ModerationService::updateModCache(bool isMod, bool isAdmin) {
    m_modCache = ModCacheEntry{isMod || isAdmin, isAdmin, std::chrono::steady_clock::now()};
}

// ── moderator checks ───────────────────────────────────────────────

void ModerationService::checkModerator(std::string const& username, ModeratorCallback callback) {
    log::info("[ModService] checkModerator: user={}", username);
    if (!m_serverEnabled) { callback(false, false); return; }
    if (tryModCache(callback)) return;

    auto* accountManager = GJAccountManager::get();
    if (!accountManager) {
        log::warn("[ModService] GJAccountManager no disponible, chequeo denegado");
        callback(false, false);
        return;
    }

    int currentAccountID = accountManager->m_accountID;
    if (currentAccountID <= 0) {
        log::warn("[ModService] usuario '{}' no logueado, chequeo denegado", username);
        callback(false, false);
        return;
    }

    HttpClient::get().checkModeratorAccount(username, currentAccountID,
        [this, callback, username](bool isMod, bool isAdmin) {
            bool effectiveMod = isMod || isAdmin;
            updateModCache(effectiveMod, isAdmin);
            if (isAdmin) {
                Mod::get()->setSavedValue<bool>("is-verified-admin", true);
                auto path = Mod::get()->getSaveDir() / "admin_verification.dat";
                std::ofstream f(path, std::ios::binary | std::ios::trunc);
                if (f) {
                    time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                    f.write(reinterpret_cast<char const*>(&now), sizeof(now));
                }
            }
            if (effectiveMod) {
                Mod::get()->setSavedValue<bool>("is-verified-moderator", true);
                auto path = Mod::get()->getSaveDir() / "moderator_verification.dat";
                std::ofstream f(path, std::ios::binary | std::ios::trunc);
                if (f) {
                    time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                    f.write(reinterpret_cast<char const*>(&now), sizeof(now));
                }
            }
            callback(effectiveMod, isAdmin);
        });
}

void ModerationService::checkModeratorAccount(std::string const& username, int accountID, ModeratorCallback callback) {
    if (!m_serverEnabled) { callback(false, false); return; }
    if (tryModCache(callback)) return;

    auto* accountManager = GJAccountManager::get();
    if (!accountManager) {
        log::warn("[ModService] GJAccountManager no disponible, chequeo denegado");
        callback(false, false);
        return;
    }

    int currentAccountID = accountManager->m_accountID;
    if (currentAccountID <= 0) {
        log::warn("[ModService] usuario '{}' no logueado, chequeo denegado", username);
        callback(false, false);
        return;
    }

    HttpClient::get().checkModeratorAccount(username, currentAccountID,
        [this, callback](bool isMod, bool isAdmin) {
            bool effectiveMod = isMod || isAdmin;
            updateModCache(effectiveMod, isAdmin);
            if (isAdmin) Mod::get()->setSavedValue<bool>("is-verified-admin", true);
            if (effectiveMod) Mod::get()->setSavedValue<bool>("is-verified-moderator", true);
            callback(effectiveMod, isAdmin);
        });
}

// ── per-username public status cache ────────────────────────────────

bool ModerationService::tryUserStatusCache(std::string const& username, ModeratorCallback& callback) {
    std::string key = geode::utils::string::toLower(username);
    std::lock_guard<std::mutex> lock(m_userStatusMutex);
    auto it = m_userStatusCache.find(key);
    if (it == m_userStatusCache.end()) return false;
    auto elapsed = std::chrono::steady_clock::now() - it->second.cachedAt;
    if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() < USER_STATUS_CACHE_TTL_SECONDS) {
        callback(it->second.isMod, it->second.isAdmin);
        return true;
    }
    m_userStatusCache.erase(it);
    return false;
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

    // Need a valid accountID to check moderator status — without it the server
    // will always return 401, wasting a request and spamming error logs.
    auto* accountManager = GJAccountManager::get();
    if (!accountManager || accountManager->m_accountID <= 0) {
        log::debug("[ModService] checkUserStatus: no logged-in account, skipping server check for user={}", username);
        callback(false, false);
        return;
    }

    HttpClient::get().checkModeratorAccount(username, accountManager->m_accountID,
        [this, username, callback](bool isMod, bool isAdmin) {
            updateUserStatusCache(username, isMod, isAdmin);
            callback(isMod, isAdmin);
        });
}

// ── admin ops ───────────────────────────────────────────────────────

void ModerationService::addModerator(std::string const& username, std::string const& adminUser, ActionCallback callback) {
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

    HttpClient::get().postWithAuth("/api/admin/add-moderator", json.dump(),
        [callback](bool success, std::string const& response) {
            callback(success, success ? "moderador anadido con exito" : response);
        });
}

void ModerationService::removeModerator(std::string const& username, std::string const& adminUser, ActionCallback callback) {
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

    HttpClient::get().postWithAuth("/api/admin/remove-moderator", json.dump(),
        [callback](bool success, std::string const& response) {
            callback(success, success ? "moderador eliminado con exito" : response);
        });
}

// ── queue operations ────────────────────────────────────────────────

void ModerationService::syncVerificationQueue(PendingCategory category, QueueCallback callback) {
    log::debug("[ModService] syncVerificationQueue: category={}", static_cast<int>(category));
    if (!m_serverEnabled) {
        callback(true, PendingQueue::get().list(category));
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
        log::info("[ModService] syncVerificationQueue: server response - success={}, response={}", success, response.substr(0, 500));
        if (!success) { callback(true, PendingQueue::get().list(category)); return; }

        auto jsonRes = matjson::parse(response);
        if (!jsonRes.isOk()) { callback(true, PendingQueue::get().list(category)); return; }
        auto json = jsonRes.unwrap();

        if (!json.contains("items") || !json["items"].isArray()) {
            callback(true, PendingQueue::get().list(category));
            return;
        }
        auto itemsRes = json["items"].asArray();
        if (!itemsRes) { callback(true, PendingQueue::get().list(category)); return; }

        std::vector<PendingItem> items;
        log::info("[ModService] syncVerificationQueue: parsing {} items from server", itemsRes.unwrap().size());
        for (auto const& item : itemsRes.unwrap()) {
            PendingItem it{};

            // levelId
            if (item["levelId"].isString())
                it.levelID = geode::utils::numFromString<int>(item["levelId"].asString().unwrapOr("0")).unwrapOr(0);
            else if (item["levelId"].isNumber())
                it.levelID = item["levelId"].asInt().unwrapOr(0);
            if (it.levelID == 0) {
                // Try multiple field names for account ID
                static const char* accountIdFields[] = {"accountID", "accountId", "account_id", "userID", "userId", "user_id"};
                for (const char* fieldName : accountIdFields) {
                    if (item.contains(fieldName)) {
                        if (item[fieldName].isString()) {
                            auto parsed = geode::utils::numFromString<int>(item[fieldName].asString().unwrapOr("0"));
                            if (parsed.isOk() && parsed.unwrap() != 0) {
                                it.levelID = parsed.unwrap();
                                break;
                            }
                        } else if (item[fieldName].isNumber()) {
                            int val = item[fieldName].asInt().unwrapOr(0);
                            if (val != 0) {
                                it.levelID = val;
                                break;
                            }
                        }
                    }
                }
            }
            
            log::debug("[ModService] Parsed item: levelID={}, category={}", it.levelID, static_cast<int>(category));

            it.category = category;

            // timestamp ms → s
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

            it.status    = PendingStatus::Open;
            it.isCreator = false;

            // suggestions array
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
                // Use server-provided filename if available (e.g., pending_thumbnails/ for captured uploads)
                std::string serverFilename = item["filename"].asString().unwrapOr("");
                if (!serverFilename.empty()) {
                    s.filename = serverFilename;
                } else {
                    s.filename = fmt::format("suggestions/{}.webp", it.levelID);
                }
                s.submittedBy = it.submittedBy;
                s.timestamp   = it.timestamp;
                it.suggestions.push_back(s);
            } else if (it.category == PendingCategory::ProfileBackground ||
                       it.category == PendingCategory::ProfileImg) {
                Suggestion s;
                s.filename    = item["filename"].asString().unwrapOr("");
                s.submittedBy = it.submittedBy;
                s.timestamp   = it.timestamp;
                if (!s.filename.empty()) it.suggestions.push_back(s);
            }

            // user report fields
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

void ModerationService::claimQueueItem(int levelId, PendingCategory category,
                                       std::string const& username, ActionCallback callback,
                                       std::string const& type) {
    if (!m_serverEnabled) { callback(false, "servidor desactivado"); return; }

    int accountID = 0;
    if (auto* am = GJAccountManager::get()) accountID = am->m_accountID;
    if (username.empty() || accountID <= 0) { callback(false, "Account ID required"); return; }

    std::string endpoint = fmt::format("/api/queue/claim/{}", levelId);
    matjson::Value json = matjson::makeObject({
        {"levelId", levelId},
        {"category", PendingQueue::catToStr(category)},
        {"username", username},
        {"accountID", accountID}
    });
    if (!type.empty()) json["type"] = type;
    std::string postData = json.dump();

    HttpClient::get().checkModeratorAccount(username, accountID,
        [this, callback, levelId, username, accountID, endpoint, postData](bool isMod, bool isAdmin) {
            if (!(isMod || isAdmin)) { callback(false, "No tienes permisos de moderador"); return; }

            HttpClient::get().postWithAuth(endpoint, postData,
                [this, callback, levelId, username, accountID, endpoint, postData](bool success, std::string const& response) {
                    if (success) { callback(true, response); return; }

                    bool authFailed = response.find("403") != std::string::npos ||
                                     response.find("needsModCode") != std::string::npos ||
                                     response.find("invalidCode") != std::string::npos ||
                                     response.find("Moderator auth required") != std::string::npos;
                    if (!authFailed) { callback(false, response); return; }

                    // retry: refresh mod code and try once more
                    m_modCache.reset();
                    HttpClient::get().checkModeratorAccount(username, accountID,
                        [callback, endpoint, postData](bool isMod2, bool isAdmin2) {
                            if (!(isMod2 || isAdmin2)) { callback(false, "Mod Code invalido. Genera uno nuevo en ajustes."); return; }

                            HttpClient::get().postWithAuth(endpoint, postData,
                                [callback](bool retryOk, std::string const& retryResp) {
                                    if (retryOk) { callback(true, retryResp); return; }
                                    if (retryResp.find("needsModCode") != std::string::npos)
                                        callback(false, "Configura tu Mod Code en ajustes de Paimbnails");
                                    else if (retryResp.find("invalidCode") != std::string::npos)
                                        callback(false, "Mod Code invalido o expirado. Actualiza en ajustes.");
                                    else
                                        callback(false, retryResp);
                                });
                        });
                });
        });
}

void ModerationService::acceptQueueItem(int levelId, PendingCategory category,
                                        std::string const& username, ActionCallback callback,
                                        std::string const& targetFilename,
                                        std::string const& type) {
    if (!m_serverEnabled) {
        PendingQueue::get().accept(levelId, category);
        callback(true, "aceptado localmente");
        return;
    }

    auto* accountManager = GJAccountManager::get();
    if (!accountManager) {
        callback(false, "account manager unavailable");
        return;
    }

    int accountID = accountManager->m_accountID;
    std::string endpoint = fmt::format("/api/queue/accept/{}", levelId);

    matjson::Value json = matjson::makeObject({
        {"levelId", levelId},
        {"category", PendingQueue::catToStr(category)},
        {"username", username},
        {"accountID", accountID}
    });
    if (!targetFilename.empty()) json["targetFilename"] = targetFilename;
    if (!type.empty()) json["type"] = type;
    std::string postData = json.dump();

    HttpClient::get().checkModeratorAccount(username, accountID,
        [this, callback, levelId, category, username, accountID, endpoint, postData](bool isMod, bool isAdmin) {
            if (!(isMod || isAdmin)) { callback(false, "No tienes permisos de moderador"); return; }

            HttpClient::get().postWithAuth(endpoint, postData,
                [this, callback, levelId, category, username, accountID, endpoint, postData](bool success, std::string const& response) {
                    if (success) {
                        PendingQueue::get().accept(levelId, category);
                        callback(true, response);
                        return;
                    }

                    bool authFailed = response.find("403") != std::string::npos ||
                                     response.find("needsModCode") != std::string::npos ||
                                     response.find("invalidCode") != std::string::npos ||
                                     response.find("Moderator auth required") != std::string::npos;
                    if (!authFailed) { callback(false, response); return; }

                    m_modCache.reset();
                    HttpClient::get().checkModeratorAccount(username, accountID,
                        [callback, levelId, category, endpoint, postData](bool isMod2, bool isAdmin2) {
                            if (!(isMod2 || isAdmin2)) { callback(false, "Mod Code invalido. Genera uno nuevo en ajustes."); return; }

                            HttpClient::get().postWithAuth(endpoint, postData,
                                [callback, levelId, category](bool retryOk, std::string const& retryResp) {
                                    if (retryOk) {
                                        PendingQueue::get().accept(levelId, category);
                                        callback(true, retryResp);
                                    } else {
                                        if (retryResp.find("needsModCode") != std::string::npos)
                                            callback(false, "Configura tu Mod Code en ajustes de Paimbnails");
                                        else if (retryResp.find("invalidCode") != std::string::npos)
                                            callback(false, "Mod Code invalido o expirado. Actualiza en ajustes.");
                                        else
                                            callback(false, retryResp);
                                    }
                                });
                        });
                });
        });
}

void ModerationService::rejectQueueItem(int levelId, PendingCategory category,
                                        std::string const& username, std::string const& reason,
                                        ActionCallback callback,
                                        std::string const& type) {
    if (!m_serverEnabled) {
        PendingQueue::get().reject(levelId, category, reason);
        callback(true, "rechazado localmente");
        return;
    }

    auto* accountManager = GJAccountManager::get();
    if (!accountManager) {
        callback(false, "account manager unavailable");
        return;
    }

    int accountID = accountManager->m_accountID;
    std::string endpoint = fmt::format("/api/queue/reject/{}", levelId);

    matjson::Value json = matjson::makeObject({
        {"levelId", levelId},
        {"category", PendingQueue::catToStr(category)},
        {"username", username},
        {"reason", reason},
        {"accountID", accountID}
    });
    if (!type.empty()) json["type"] = type;
    std::string postData = json.dump();

    HttpClient::get().checkModeratorAccount(username, accountID,
        [this, callback, levelId, category, reason, username, accountID, endpoint, postData](bool isMod, bool isAdmin) {
            if (!(isMod || isAdmin)) { callback(false, "No tienes permisos de moderador"); return; }

            HttpClient::get().postWithAuth(endpoint, postData,
                [this, callback, levelId, category, reason, username, accountID, endpoint, postData](bool success, std::string const& response) {
                    if (success) {
                        PendingQueue::get().reject(levelId, category, reason);
                        callback(true, response);
                        return;
                    }

                    bool authFailed = response.find("403") != std::string::npos ||
                                     response.find("needsModCode") != std::string::npos ||
                                     response.find("invalidCode") != std::string::npos ||
                                     response.find("Moderator auth required") != std::string::npos;
                    if (!authFailed) { callback(false, response); return; }

                    m_modCache.reset();
                    HttpClient::get().checkModeratorAccount(username, accountID,
                        [callback, levelId, category, reason, endpoint, postData](bool isMod2, bool isAdmin2) {
                            if (!(isMod2 || isAdmin2)) { callback(false, "Mod Code invalido. Genera uno nuevo en ajustes."); return; }

                            HttpClient::get().postWithAuth(endpoint, postData,
                                [callback, levelId, category, reason](bool retryOk, std::string const& retryResp) {
                                    if (retryOk) {
                                        PendingQueue::get().reject(levelId, category, reason);
                                        callback(true, retryResp);
                                    } else {
                                        if (retryResp.find("needsModCode") != std::string::npos)
                                            callback(false, "Configura tu Mod Code en ajustes de Paimbnails");
                                        else if (retryResp.find("invalidCode") != std::string::npos)
                                            callback(false, "Mod Code invalido o expirado. Actualiza en ajustes.");
                                        else
                                            callback(false, retryResp);
                                    }
                                });
                        });
                });
        });
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
