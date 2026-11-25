#include "GDHistoryClient.hpp"
#include "InfoStore.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/JsonHelper.hpp"
#include "../../../utils/WebHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <unordered_set>

using namespace geode::prelude;

namespace paimon::info::gdhistory {

namespace {

constexpr char const* kBase = "https://history.geometrydash.eu/api/v1";
constexpr int kTimeoutSeconds = 8;

// one request per id per session, whatever the answer was.
std::unordered_set<int>& levelsAsked() {
    static std::unordered_set<int> set;
    return set;
}

std::unordered_set<int>& usersAsked() {
    static std::unordered_set<int> set;
    return set;
}

std::unordered_map<int, UserHistory>& userHistoryCache() {
    static std::unordered_map<int, UserHistory> map;
    return map;
}

// the two requests behind one user lookup report here; the callback fires once both are done.
struct UserLookup {
    UserHistory result;
    std::function<void(UserHistory const&, bool)> done;
    int pending = 2;
    bool ok = false;
};

void finishUserLookup(int userID, std::shared_ptr<UserLookup> const& lookup) {
    if (--lookup->pending > 0) return;
    if (lookup->ok) userHistoryCache()[userID] = lookup->result;
    lookup->done(lookup->result, lookup->ok);
}

// official ids do not apply on private servers; detect known switchers because bindings expose no server.
bool onPrivateServer() {
    auto* loader = Loader::get();
    if (!loader) return false;
    return loader->isModLoaded("km7dev.gdps_switcher")
        || loader->isModLoaded("km7dev.gdps-switcher")
        || loader->isModLoaded("weebify.separate_dashes")
        || loader->isModLoaded("itzkiba.gdps_switcher");
}

// the api returns an iso timestamp; only the date part is worth showing.
std::string dateOnly(std::string const& iso) {
    if (iso.size() < 10) return iso;
    return iso.substr(0, 10);
}

// shared GET + json guards: onOk runs only for a 2xx with parseable json,
// onFail (if given) covers both failure cases.
void dispatchJson(std::string url, std::function<void(matjson::Value const&)> onOk,
                  std::function<void()> onFail = nullptr) {
    auto req = web::WebRequest();
    req.timeout(asp::Duration::fromSecs(kTimeoutSeconds));

    WebHelper::dispatch(std::move(req), "GET", std::move(url),
        [onOk = std::move(onOk), onFail = std::move(onFail)](web::WebResponse res) mutable {
            if (!res.ok()) {
                if (onFail) onFail();
                return;
            }

            auto json = res.json();
            if (!json.isOk()) {
                if (onFail) onFail();
                return;
            }

            if (onOk) onOk(json.unwrap());
        });
}

} // namespace

bool enabled() {
    return paimon::modules::isEnabled(kModuleId);
}

bool reachable() {
    return !onPrivateServer();
}

bool available() {
    return enabled() && reachable();
}

void requestLevelDate(int levelID, std::function<void(std::string const&)> callback) {
    if (levelID <= 0) return;

    auto cached = InfoStore::get().levelDate(levelID);
    if (!cached.empty()) {
        if (callback) callback(cached);
        return;
    }

    if (!available()) return;
    if (!levelsAsked().insert(levelID).second) return;

    dispatchJson(fmt::format("{}/date/level/{}/", kBase, levelID),
        [levelID, callback](matjson::Value const& body) {
            auto date = dateOnly(paimon::json::stringOr(body["approx"]["estimation"]));
            if (date.empty()) return;

            InfoStore::get().setLevelDate(levelID, date);
            InfoStore::get().save();
            if (callback) callback(date);
        });
}

void requestLevelHistory(int levelID, std::function<void(matjson::Value)> callback) {
    if (!callback) return;
    if (levelID <= 0 || !available()) {
        callback(matjson::Value());
        return;
    }

    dispatchJson(fmt::format("{}/level/{}/", kBase, levelID),
        [callback = std::move(callback)](matjson::Value const& body) mutable {
            callback(body);
        },
        [callback]() mutable {
            callback(matjson::Value());
        });
}

void requestUserHistory(int userID, bool userInitiated,
                        std::function<void(UserHistory const&, bool)> callback) {
    if (!callback) return;
    if (userID <= 0 || !reachable() || (!userInitiated && !enabled())) {
        callback(UserHistory{}, false);
        return;
    }

    auto cached = userHistoryCache().find(userID);
    if (cached != userHistoryCache().end()) {
        callback(cached->second, true);
        return;
    }

    auto lookup = std::make_shared<UserLookup>();
    lookup->done = std::move(callback);

    // every record is one sighting of the user under one name.
    dispatchJson(fmt::format("{}/user/{}/", kBase, userID),
        [userID, lookup](matjson::Value const& body) {
            paimon::json::forEachInArray(body["records"], [&](matjson::Value const& record) {
                auto name = paimon::json::stringOr(record["username"]);
                if (!name.empty()
                        && std::find(lookup->result.names.begin(), lookup->result.names.end(), name)
                            == lookup->result.names.end()) {
                    lookup->result.names.push_back(name);
                }

                auto seen = dateOnly(paimon::json::stringOr(record["cache_created"]));
                if (!seen.empty() && (lookup->result.firstSeen.empty() || seen < lookup->result.firstSeen)) {
                    lookup->result.firstSeen = seen;
                }
            });
            if (!lookup->result.names.empty() || !lookup->result.firstSeen.empty()) lookup->ok = true;
            finishUserLookup(userID, lookup);
        },
        [userID, lookup]() { finishUserLookup(userID, lookup); });

    // history dates levels, not users: a user id maps to the level uploaded around then.
    dispatchJson(fmt::format("{}/date/user/{}/", kBase, userID),
        [userID, lookup](matjson::Value const& body) {
            auto levelID = paimon::json::integerOr<int64_t>(body["approx"]);
            if (levelID <= 0) {
                finishUserLookup(userID, lookup);
                return;
            }

            dispatchJson(fmt::format("{}/date/level/{}/", kBase, levelID),
                [userID, lookup](matjson::Value const& level) {
                    lookup->result.createdApprox = dateOnly(
                        paimon::json::stringOr(level["approx"]["estimation"]));
                    if (!lookup->result.createdApprox.empty()) lookup->ok = true;
                    finishUserLookup(userID, lookup);
                },
                [userID, lookup]() { finishUserLookup(userID, lookup); });
        },
        [userID, lookup]() { finishUserLookup(userID, lookup); });
}

void requestUsername(int userID) {
    if (userID <= 0) return;
    if (!InfoStore::get().username(userID).empty()) return;
    if (!available()) return;
    if (!usersAsked().insert(userID).second) return;

    dispatchJson(fmt::format("{}/user/{}/brief/", kBase, userID),
        [userID](matjson::Value const& body) {
            auto name = paimon::json::stringOr(body["username"]);
            if (name.empty()) return;

            InfoStore::get().rememberUsername(userID, name);
            InfoStore::get().save();
        });
}

} // namespace paimon::info::gdhistory
