// Fix Green Usernames.
//
// GD forgets the name behind a user id as soon as it restarts, and then renders
// scores, comments and level cells with a blank or "-" name. Every name the
// game does resolve is cached here, and handed back when it later comes up
// empty.

#include "../InfoModule.hpp"
#include "../services/GDHistoryClient.hpp"
#include "../services/InfoStore.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/modify/GameLevelManager.hpp>
#include <string>

using namespace geode::prelude;

namespace {

bool greenUsersEnabled() {
    return paimon::info::moduleEnabled("info-mod-green-users");
}

bool isBlankName(std::string const& name) {
    return name.empty() || name == "-";
}

} // namespace

class $modify(PaimonInfoSuiteNames, GameLevelManager) {
    void storeUserName(int userID, int accountID, gd::string userName) {
        GameLevelManager::storeUserName(userID, accountID, userName);
        if (!greenUsersEnabled()) return;

        std::string name(userName);
        if (isBlankName(name)) return;
        paimon::info::InfoStore::get().rememberUsername(userID, name);
    }

    gd::string userNameForUserID(int id) {
        auto result = GameLevelManager::userNameForUserID(id);
        if (!greenUsersEnabled()) return result;

        std::string name(result);
        if (!isBlankName(name)) return result;

        auto cached = paimon::info::InfoStore::get().username(id);
        if (cached.empty()) {
            // Nothing local. If the user opted into GDHistory, ask so the next
            // time this id shows up it already has a name.
            paimon::info::gdhistory::requestUsername(id);
            return result;
        }
        return gd::string(cached);
    }
};
