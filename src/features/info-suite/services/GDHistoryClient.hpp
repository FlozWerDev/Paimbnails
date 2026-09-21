#pragma once

// Optional history.geometrydash.eu enrichment; nothing depends on it.
// Cached in InfoStore, one fetch per id.

#include <functional>
#include <matjson.hpp>
#include <string>

namespace paimon::info::gdhistory {

inline constexpr char const* kModuleId = "paimbnails.gdhistory.info";

// The setting is on and we are not talking to a private server.
bool available();

// Calls back on the main thread with the formatted date (empty when unknown);
// answers from cache immediately when possible.
void requestLevelDate(int levelID, std::function<void(std::string const&)> callback);

// Empty JSON means the request failed or GDHistory is unavailable.
void requestLevelHistory(int levelID, std::function<void(matjson::Value)> callback);

// Result lands in InfoStore for GameLevelManager::userNameForUserID. Fire and forget.
void requestUsername(int userID);

} // namespace paimon::info::gdhistory
