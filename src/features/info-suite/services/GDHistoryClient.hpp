#pragma once

// Optional enrichment from history.geometrydash.eu.
//
// Off by default, and nothing in the Info Suite depends on it: the daily and
// weekly history comes from RobTop's own servers, and comment dates are
// interpolated locally. This only sharpens two things the game genuinely does
// not know — a level's real upload date, and the name behind a user id we have
// never seen.
//
// Every result is cached in InfoStore, so an id is fetched at most once.

#include <functional>
#include <matjson.hpp>
#include <string>

namespace paimon::info::gdhistory {

inline constexpr char const* kModuleId = "paimbnails.gdhistory.info";

// The setting is on and we are not talking to a private server.
bool available();

// Asks for a level's real upload date. Calls back on the main thread with the
// formatted date, or an empty string when it is unknown. Answers from cache
// immediately when possible.
void requestLevelDate(int levelID, std::function<void(std::string const&)> callback);

// Asks for the recorded upload/version history of a level. An empty JSON value
// means the request failed or GDHistory is unavailable.
void requestLevelHistory(int levelID, std::function<void(matjson::Value)> callback);

// Asks for the name behind a user id. Result lands in InfoStore for
// GameLevelManager::userNameForUserID to pick up. Fire and forget.
void requestUsername(int userID);

} // namespace paimon::info::gdhistory
