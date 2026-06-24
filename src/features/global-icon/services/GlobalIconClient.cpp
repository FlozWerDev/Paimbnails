#include "GlobalIconClient.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/JsonHelper.hpp"
#include "../../../utils/Debug.hpp"

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

void GlobalIconClient::getMetadata(int accountID, MetaCallback cb) {
    if (accountID <= 0) {
        if (cb) cb(false, false, GlobalIconMeta{});
        return;
    }
    std::string url = baseUrl() + "/api/icons/" + std::to_string(accountID);
    HttpClient::get().get(url, [cb = std::move(cb)](bool success, std::string const& resp) {
        if (!success) {
    if (cb) cb(false, false, GlobalIconMeta{});
            return;
        }
        auto parsed = matjson::parse(resp);
        if (!parsed.isOk()) {
            if (cb) cb(false, false, GlobalIconMeta{});
            return;
        }
        auto meta = parseMetaJson(parsed.unwrap());
        if (cb) cb(true, true, meta);
    });
}

void GlobalIconClient::getMetadataBatch(std::vector<int> const& accountIDs, BatchCallback cb) {
    std::unordered_map<int, GlobalIconMeta> empty;
    if (accountIDs.empty()) {
        if (cb) cb(true, empty);
        return;
    }
    matjson::Value ids = matjson::Value::array();
    int count = 0;
    for (int id : accountIDs) {
        if (id <= 0) continue;
        ids.push(id);
        if (++count >= 64) break; // server cap
    }
    matjson::Value body = matjson::makeObject({ {"accountIDs", ids} });

    std::string url = baseUrl() + "/api/icons/batch";
    HttpClient::get().post(url, body.dump(matjson::NO_INDENTATION),
        [cb = std::move(cb)](bool success, std::string const& resp) {
            std::unordered_map<int, GlobalIconMeta> result;
            if (!success) {
                if (cb) cb(false, result);
                return;
            }
            auto parsed = matjson::parse(resp);
            if (!parsed.isOk()) {
                if (cb) cb(false, result);
                return;
            }
            auto root = parsed.unwrap();
            auto const& metaObj = root["metadata"];
            paimon::json::forEachInArray(root["found"], [&](matjson::Value const& idVal) {
                int id = static_cast<int>(idVal.isNumber() ? idVal.asDouble().unwrapOr(0.0) : 0.0);
                if (id <= 0) return;
                auto const& mv = metaObj[std::to_string(id)];
                if (mv.isObject()) result[id] = parseMetaJson(mv);
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
