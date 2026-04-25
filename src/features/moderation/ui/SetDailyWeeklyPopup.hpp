#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

class SetDailyWeeklyPopup : public geode::Popup {
protected:
    int m_levelID;

    bool init(int value);
    
    void onSetDaily(cocos2d::CCObject* sender);
    void onSetWeekly(cocos2d::CCObject* sender);
    void onUnset(cocos2d::CCObject* sender);

public:
    static SetDailyWeeklyPopup* create(int levelID);
};
