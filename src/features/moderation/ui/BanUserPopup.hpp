#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

class BanUserPopup : public geode::Popup {
protected:
    std::string m_username;
    geode::TextInput* m_input;

    bool init(std::string const& username);
    void onBan(cocos2d::CCObject*);

public:
    static BanUserPopup* create(std::string const& username);
};
