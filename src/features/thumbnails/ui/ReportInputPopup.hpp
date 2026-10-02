#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>

class ReportInputPopup : public PaimonPopup {
protected:
    int m_levelID = 0;
    geode::TextInput* m_textInput = nullptr;
    geode::CopyableFunction<void(std::string)> m_callback;

    bool init(int levelID, geode::CopyableFunction<void(std::string)> callback);
    void onSend(cocos2d::CCObject*);

public:
    static ReportInputPopup* create(int levelID, geode::CopyableFunction<void(std::string)> callback);
};
