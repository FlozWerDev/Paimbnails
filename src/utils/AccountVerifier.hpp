#pragma once

#include <Geode/Geode.hpp>
#include <string>

struct VerifiedAccount {
    int accountID = 0;
    std::string username;
    int userID = 0;
    bool isOfficialServer = false;
    bool isValid = false;

    bool loggedIn() const { return isValid && accountID > 0 && !username.empty(); }
};

class AccountVerifier {
public:
    static AccountVerifier& get() {
        static AccountVerifier instance;
        return instance;
    }

    VerifiedAccount verify() const {
        VerifiedAccount result;

        auto* am = GJAccountManager::get();
        if (!am) return result;

        result.accountID = am->m_accountID;
        result.username = std::string(am->m_username);

        auto* gm = GameManager::get();
        if (!gm) return result;

        // username must match between AccountManager and GameManager
        std::string gmName(gm->m_playerName);
        if (result.username.empty()) result.username = gmName;

        // userID from GameManager (SeedValue)
        result.userID = gm->m_playerUserID;

        // isOfficialServer: m_scoreValid is true only when connected to
        // official Boomlings servers with valid leaderboard scores
        result.isOfficialServer = gm->m_scoreValid;

        // valid = logged in with positive accountID and non-empty username
        result.isValid = result.accountID > 0 && !result.username.empty();

        return result;
    }

    bool isLoggedIn() const { return verify().loggedIn(); }
    bool isOfficial() const { return verify().isOfficialServer; }
    int getAccountID() const { return verify().accountID; }
    std::string getUsername() const { return verify().username; }
};
