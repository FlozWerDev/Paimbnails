#pragma once

// optional history.geometrydash.eu enrichment; nothing depends on it.
// cached in infostore, one fetch per id.

#include <functional>
#include <matjson.hpp>
#include <string>
#include <vector>

namespace paimon::info::gdhistory {

inline constexpr char const* kModuleId = "paimbnails.gdhistory.info";

// the opt-in setting is on.
bool enabled();

// not a private server, where official ids mean nothing.
bool reachable();

// the setting is on and we are not talking to a private server.
bool available();

struct UserHistory {
    // yyyy-mm-dd. gd never exposes the creation date, so this is estimated from the user id.
    std::string createdApprox;
    // earliest date history saw this user under any name.
    std::string firstSeen;
    // every username history recorded for the user, newest first.
    std::vector<std::string> names;
};

// calls back on the main thread; ok is false when history knows nothing about the user.
// userInitiated skips the opt-in check, for one lookup the user asked for by hand.
void requestUserHistory(int userID, bool userInitiated,
                        std::function<void(UserHistory const&, bool ok)> callback);

// calls back on the main thread with the formatted date (empty when unknown);
// answers from cache immediately when possible.
void requestLevelDate(int levelID, std::function<void(std::string const&)> callback);

// empty json means the request failed or gdhistory is unavailable.
void requestLevelHistory(int levelID, std::function<void(matjson::Value)> callback);

// result lands in infostore for gamelevelmanager::usernameforuserid. fire and forget.
void requestUsername(int userID);

} // namespace paimon::info::gdhistory
