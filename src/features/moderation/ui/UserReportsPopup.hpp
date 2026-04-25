#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include "../../moderation/services/PendingQueue.hpp"

class UserReportsPopup : public geode::Popup {
protected:
    std::vector<ReportEntry> m_reports;
    std::string m_reportedUsername;
    int m_currentIndex = 0;
    cocos2d::CCLabelBMFont* m_reporterLabel = nullptr;
    cocos2d::CCLabelBMFont* m_noteLabel = nullptr;
    cocos2d::CCLabelBMFont* m_counterLabel = nullptr;
    CCMenuItemSpriteExtra* m_prevBtn = nullptr;
    CCMenuItemSpriteExtra* m_nextBtn = nullptr;

    bool init(std::string const& reportedUsername, std::vector<ReportEntry> const& reports);
    void updateDisplay();
    void onPrev(cocos2d::CCObject*);
    void onNext(cocos2d::CCObject*);

public:
    static UserReportsPopup* create(std::string const& reportedUsername, std::vector<ReportEntry> const& reports);
};
