#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>

class BanUserPopup : public PaimonPopup {
protected:
    std::string m_username;
    geode::TextInput* m_input;

    bool init(std::string const& username);
    void onBan(cocos2d::CCObject*);

public:
    static BanUserPopup* create(std::string const& username);
};
