#pragma once

#include "../services/VersusStore.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

#include <string>

namespace paimon::versus {

// a player's two ladders side by side. opened from the chip in the username
// row, on your own profile and on anybody else's.
class VersusProfilePopup : public geode::Popup {
public:
    static VersusProfilePopup* create(int accountId, std::string const& username,
                                      ModeProfile const& classic, ModeProfile const& platformer,
                                      bool own);

protected:
    bool init(int accountId, std::string const& username,
              ModeProfile const& classic, ModeProfile const& platformer, bool own);

    void buildColumn(Mode mode, ModeProfile const& profile, float centerX);
    void onHistory(cocos2d::CCObject* sender);
    void onChallenge(cocos2d::CCObject* sender);

    int m_accountId = 0;
    std::string m_username;
    bool m_own = false;
};

} // namespace paimon::versus
