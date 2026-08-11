#include "GlobalIconClient.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/JsonHelper.hpp"
#include "../../../utils/Debug.hpp"

#include <ctime>

using namespace geode::prelude;

namespace paimon::globalicon {

namespace {
    std::string jStr(matjson::Value const& v, std::string const& def = "") {
        return v.isString() ? v.asString().unwrapOr(def) : def;
    }
    int64_t jInt(matjson::Value const& v, int64_t def = 0) {
        return v.isNumber() ? static_cast<int64_t>(v.asDouble().unwrapOr(static_cast<double>(def))) : def;
    }
    bool jBool(matjson::Value const& v, bool def = false) {
        return v.isBool() ? v.asBool().unwrapOr(def) : def;
    }

    int64_t nowSeconds() {
        return static_cast<int64_t>(std::time(nullptr));
    }

    // HttpClient reports non-2xx as success=false with the status baked into
    // the body, so "not sharing" has to be recovered from the message.
    bool isNotFound(std::string const& response) {
        return response.rfind("HTTP 404", 0) == 0;
    }
}

GlobalIconMeta parseMetaJson(matjson::Value const& v) {
    GlobalIconMeta meta;
    meta.accountID = static_cast<int>(jInt(v["accountID"]));
    meta.username  = jStr(v["username"]);
    meta.enabled   = jBool(v["enabled"]);
    meta.updatedAt = jStr(v["updatedAt"]);

    auto const& icons = v["icons"];
    if (icons.isObject()) {
        // Iterate known types; const operator[] returns null if missing.
        for (auto const& typeName : {
            "cube", "ship", "ball", "ufo", "wave", "robot",
            "spider", "swing", "jetpack", "death", "trail", "fire"
        }) {
            if (!icons.contains(typeName)) continue;
            auto const& s = icons[typeName];
            if (!s.isObject()) continue;
            GlobalIconSlot slot;
            slot.type      = jStr(s["type"], typeName);
            slot.name      = jStr(s["name"]);
            slot.packID    = jStr(s["packID"]);
            slot.packName  = jStr(s["packName"]);
            slot.quality   = static_cast<int>(jInt(s["quality"], 3));
            slot.specialID = static_cast<int>(jInt(s["specialID"]));
            slot.fireCount = static_cast<int>(jInt(s["fireCount"]));
            slot.pngFile   = jStr(s["pngFile"]);
            slot.pngUrl    = jStr(s["pngUrl"]);
            slot.plistFile = jStr(s["plistFile"]);
            slot.plistUrl  = jStr(s["plistUrl"]);
            slot.jsonFile  = jStr(s["jsonFile"]);
            slot.jsonUrl   = jStr(s["jsonUrl"]);
            slot.bytes     = jInt(s["bytes"]);
            if (slot.name.empty() || slot.pngUrl.empty()) continue; // unusable slot
            meta.icons[typeName] = std::move(slot);
        }
    }
    return meta;
}

GlobalIconClient& GlobalIconClient::get() {
    static GlobalIconClient instance;
    return instance;
}

std::string GlobalIconClient::baseUrl() const {
    // Prefer the configured server URL; fall back to the default constant.
    std::string base = Mod::get()->getSettingValue<std::string>("global-icon-server-url");
    if (base.empty()) base = std::string(GLOBAL_ICON_BASE);
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base;
}

bool GlobalIconClient::lookup(int accountID, CacheEntry& out) const {
    auto it = m_cache.find(accountID);
    if (it == m_cache.end()) return false;
    int64_t ttl = it->second.found ? kPositiveTtlSeconds : kNegativeTtlSeconds;
    if (nowSeconds() - it->second.fetchedAt > ttl) return false;
    out = it->second;
    return true;
}

void GlobalIconClient::store(int accountID, GlobalIconMeta const& meta, bool found) {
    if (m_cache.size() >= kMaxCacheEntries && !m_cache.count(accountID)) {
        // Cheap bound: drop everything already past its TTL, and if that frees
        // nothing, clear outright rather than grow without limit.
        int64_t now = nowSeconds();
        for (auto it = m_cache.begin(); it != m_cache.end();) {
            int64_t ttl = it->second.found ? kPositiveTtlSeconds : kNegativeTtlSeconds;
            it = (now - it->second.fetchedAt > ttl) ? m_cache.erase(it) : std::next(it);
        }
        if (m_cache.size() >= kMaxCacheEntries) m_cache.clear();
    }
    m_cache[accountID] = CacheEntry{meta, found, nowSeconds()};
}

void GlobalIconClient::invalidate(int accountID) {
    m_cache.erase(accountID);
}

void GlobalIconClient::invalidateAll() {
    m_cache.clear();
}

void GlobalIconClient::getMetadata(int accountID, MetaCallback cb) {
    if (accountID <= 0) {
        if (cb) cb(false, false, GlobalIconMeta{});
        return;
    }

    CacheEntry cached;
    if (lookup(accountID, cached)) {
        if (cb) cb(true, cached.found, cached.meta);
        return;
    }

    // A request is already in flight for this account: ride along with it.
    auto inflightIt = m_inflight.find(accountID);
    if (inflightIt != m_inflight.end()) {
        if (cb) inflightIt->second.push_back(std::move(cb));
        return;
    }

    auto& waiters = m_inflight[accountID];
    if (cb) waiters.push_back(std::move(cb));

    std::string url = baseUrl() + "/api/icons/" + std::to_string(accountID);
    HttpClient::get().get(url, [accountID](bool success, std::string const& resp) {
        auto& self = GlobalIconClient::get();

        bool found = false;
        GlobalIconMeta meta;
        if (success) {
            auto parsed = matjson::parse(resp);
            if (parsed.isOk()) {
                meta = parseMetaJson(parsed.unwrap());
                found = meta.enabled && !meta.icons.empty();
            } else {
                success = false;
            }
        } else if (isNotFound(resp)) {
            // Definitive "not sharing": cache it so we stop asking.
            success = true;
        }

        if (success) self.store(accountID, meta, found);

        auto node = self.m_inflight.extract(accountID);
        if (node.empty()) return;
        for (auto& waiter : node.mapped()) {
            if (waiter) waiter(success, found, meta);
        }
    });
}

void GlobalIconClient::getMetadataBatch(std::vector<int> const& accountIDs, BatchCallback cb) {
    std::unordered_map<int, GlobalIconMeta> result;
    if (accountIDs.empty()) {
        if (cb) cb(true, result);
        return;
    }

    // Only ask for what isn't cached; a page of comments from familiar players
    // usually resolves without any request at all.
    matjson::Value ids = matjson::Value::array();
    int count = 0;
    for (int id : accountIDs) {
        if (id <= 0) continue;
        CacheEntry cached;
        if (lookup(id, cached)) {
            if (cached.found) result[id] = cached.meta;
            continue;
        }
        ids.push(id);
        if (++count >= 64) break; // server cap
    }

    if (count == 0) {
        if (cb) cb(true, result);
        return;
    }

    matjson::Value body = matjson::makeObject({ {"accountIDs", ids} });
    std::string url = baseUrl() + "/api/icons/batch";

    HttpClient::get().post(url, body.dump(matjson::NO_INDENTATION),
        [cb = std::move(cb), result = std::move(result)](bool success, std::string const& resp) mutable {
            if (!success) {
                if (cb) cb(false, result);
                return;
            }
            auto parsed = matjson::parse(resp);
            if (!parsed.isOk()) {
                if (cb) cb(false, result);
                return;
            }
            auto& self = GlobalIconClient::get();
            auto root = parsed.unwrap();
            auto const& metaObj = root["metadata"];

            paimon::json::forEachInArray(root["found"], [&](matjson::Value const& idVal) {
                int id = static_cast<int>(idVal.isNumber() ? idVal.asDouble().unwrapOr(0.0) : 0.0);
                if (id <= 0) return;
                auto const& mv = metaObj[std::to_string(id)];
                if (!mv.isObject()) return;
                auto meta = parseMetaJson(mv);
                bool found = meta.enabled && !meta.icons.empty();
                self.store(id, meta, found);
                if (found) result[id] = std::move(meta);
            });

            // Remember the misses too, so the next page doesn't re-ask for them.
            paimon::json::forEachInArray(root["missing"], [&](matjson::Value const& idVal) {
                int id = static_cast<int>(idVal.isNumber() ? idVal.asDouble().unwrapOr(0.0) : 0.0);
                if (id > 0) self.store(id, GlobalIconMeta{}, false);
            });

            if (cb) cb(true, result);
        });
}

void GlobalIconClient::downloadFile(std::string const& url, FileCallback cb) {
    if (url.empty()) {
        if (cb) cb(false, {});
        return;
    }
    // downloadFromUrlRaw validates the URL (anti-SSRF) and never sends X-API-Key to external hosts; blobs are public.
    HttpClient::get().downloadFromUrlRaw(url,
        [cb = std::move(cb)](bool success, std::vector<uint8_t> const& data, int, int) {
            if (cb) cb(success, data);
        });
}

void GlobalIconClient::syncIcons(std::string const& jsonBody, SyncCallback cb) {
    std::string url = baseUrl() + "/api/icons/sync";
    // post() already includes the X-API-Key header the server requires.
    HttpClient::get().post(url, jsonBody, [cb = std::move(cb)](bool success, std::string const& resp) {
        if (cb) cb(success, resp);
    });
}

void GlobalIconClient::clearIcons(int accountID, std::string const& username, SyncCallback cb) {
    matjson::Value body = matjson::makeObject({
        {"accountID", accountID},
        {"username", username},
        {"enabled", false},
        {"icons", matjson::Value::array()},
    });
    syncIcons(body.dump(matjson::NO_INDENTATION), std::move(cb));
}

} // namespace paimon::globalicon
