#pragma once
#include <Geode/Geode.hpp>
#include <Geode/utils/function.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

class ReportInputPopup : public geode::Popup {
protected:
    int m_levelID = 0;
    geode::TextInput* m_textInput = nullptr;
    geode::CopyableFunction<void(std::string)> m_callback;

    bool init(int levelID, geode::CopyableFunction<void(std::string)> callback);
    void onSend(cocos2d::CCObject*);

public:
    static ReportInputPopup* create(int levelID, geode::CopyableFunction<void(std::string)> callback);
};
