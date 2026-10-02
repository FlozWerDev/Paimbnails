#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>
#include "../../../utils/HttpClient.hpp"

class ReportUserPopup : public PaimonPopup {
protected:
    int m_reportedAccountID = 0;
    std::string m_reportedUsername;
    geode::TextInput* m_textInput = nullptr;

    bool init(int accountID, std::string const& username);
    void onSend(cocos2d::CCObject*);

public:
    static ReportUserPopup* create(int accountID, std::string const& username);
};
