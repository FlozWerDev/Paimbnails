#pragma once

// Green players have a user id but no account, so GD refuses them a profile.
// Everything the servers still expose about them (name, ids, levels) is gathered here.

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
