#pragma once

// optional history.geometrydash.eu enrichment; nothing depends on it.
// cached in infostore, one fetch per id.

#include <functional>
#include <matjson.hpp>
#include <string>

namespace paimon::info::gdhistory {

inline constexpr char const* kModuleId = "paimbnails.gdhistory.info";

// the setting is on and we are not talking to a private server.
bool available();

// calls back on the main thread with the formatted date (empty when unknown);
// answers from cache immediately when possible.
void requestLevelDate(int levelID, std::function<void(std::string const&)> callback);

// empty json means the request failed or gdhistory is unavailable.
void requestLevelHistory(int levelID, std::function<void(matjson::Value)> callback);

// result lands in infostore for gamelevelmanager::usernameforuserid. fire and forget.
void requestUsername(int userID);

} // namespace paimon::info::gdhistory
