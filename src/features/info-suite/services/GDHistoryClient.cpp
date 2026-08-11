#include "GDHistoryClient.hpp"
#include "InfoStore.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/WebHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <unordered_set>

using namespace geode::prelude;

namespace paimon::info::gdhistory {

namespace {

constexpr char const* kBase = "https://history.geometrydash.eu/api/v1";
constexpr int kTimeoutSeconds = 8;

// One request per id per session, whatever the answer was.
std::unordered_set<int>& levelsAsked() {
    static std::unordered_set<int> set;
    return set;
}

std::unordered_set<int>& usersAsked() {
    static std::unordered_set<int> set;
    return set;
}

// GDPS setups answer on a different host, where these ids mean nothing. There
// is no binding that exposes the active server, so we go by the switcher mods
// people actually use.
bool onPrivateServer() {
    auto* loader = Loader::get();
    if (!loader) return false;
    return loader->isModLoaded("km7dev.gdps_switcher")
        || loader->isModLoaded("km7dev.gdps-switcher")
        || loader->isModLoaded("weebify.separate_dashes")
        || loader->isModLoaded("itzkiba.gdps_switcher");
}

std::string asString(matjson::Value const& value) {
    auto res = value.asString();
    return res.isOk() ? res.unwrap() : std::string{};
}

// The API returns an ISO timestamp; only the date part is worth showing.
std::string dateOnly(std::string const& iso) {
    if (iso.size() < 10) return iso;
    return iso.substr(0, 10);
}

} // namespace

bool available() {
    if (!paimon::modules::isEnabled(kModuleId)) return false;
    return !onPrivateServer();
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

    auto req = web::WebRequest();
    req.timeout(std::chrono::seconds(kTimeoutSeconds));

    WebHelper::dispatch(std::move(req), "GET",
        fmt::format("{}/date/level/{}/", kBase, levelID),
        [levelID, callback](web::WebResponse res) {
            if (!res.ok()) return;

            auto json = res.json();
            if (!json.isOk()) return;

            auto body = json.unwrap();
            auto date = dateOnly(asString(body["approx"]["estimation"]));
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

    auto req = web::WebRequest();
    req.timeout(std::chrono::seconds(kTimeoutSeconds));

    WebHelper::dispatch(std::move(req), "GET",
        fmt::format("{}/level/{}/", kBase, levelID),
        [callback = std::move(callback)](web::WebResponse res) mutable {
            if (!res.ok()) {
                callback(matjson::Value());
                return;
            }

            auto json = res.json();
            if (!json.isOk()) {
                callback(matjson::Value());
                return;
            }

            callback(json.unwrap());
        });
}

void requestUsername(int userID) {
    if (userID <= 0) return;
    if (!InfoStore::get().username(userID).empty()) return;
    if (!available()) return;
    if (!usersAsked().insert(userID).second) return;

    auto req = web::WebRequest();
    req.timeout(std::chrono::seconds(kTimeoutSeconds));

    WebHelper::dispatch(std::move(req), "GET",
        fmt::format("{}/user/{}/brief/", kBase, userID),
        [userID](web::WebResponse res) {
            if (!res.ok()) return;

            auto json = res.json();
            if (!json.isOk()) return;

            auto body = json.unwrap();
            auto name = asString(body["username"]);
            if (name.empty()) return;

            InfoStore::get().rememberUsername(userID, name);
            InfoStore::get().save();
        });
}

} // namespace paimon::info::gdhistory
