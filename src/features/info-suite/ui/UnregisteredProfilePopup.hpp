#pragma once

// Green players have a user id but no account, so GD refuses to open a profile
// for them at all. Everything the servers still expose about them — their name,
// their ids, and their levels — is gathered here instead.

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::info {

class UnregisteredProfilePopup : public geode::Popup {
public:
    static UnregisteredProfilePopup* create(int userID, std::string userName);

protected:
    bool init(int userID, std::string userName);

    void onLevels(cocos2d::CCObject*);
    void onCopyID(cocos2d::CCObject*);

    int m_userID = 0;
    std::string m_userName;
};

} // namespace paimon::info
