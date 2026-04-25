#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include "../../../utils/HttpClient.hpp"

class ReportUserPopup : public geode::Popup {
protected:
    int m_reportedAccountID = 0;
    std::string m_reportedUsername;
    geode::TextInput* m_textInput = nullptr;

    bool init(int accountID, std::string const& username);
    void onSend(cocos2d::CCObject*);

public:
    static ReportUserPopup* create(int accountID, std::string const& username);
};
