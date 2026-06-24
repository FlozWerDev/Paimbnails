#pragma once
// Garage gear popup for the Paimon Icons feature (tabs + IconConfigStore::update).

#include <Geode/ui/Popup.hpp>

namespace cocos2d { class CCMenu; class CCNode; }

namespace paimon::icons::ui {

class PaimonIconsConfigPopup : public geode::Popup {
public:
    static PaimonIconsConfigPopup* open();

protected:
    bool init();

    enum class Tab {
        General      = 0,
        LockedIcons  = 1,
        Animations   = 2,
        ApplyTo      = 3,
        PerGamemode  = 4,
        Presets      = 5,
    };

    void buildHeader();
    void buildTabs();
    void buildBody();
    void switchTo(Tab tab);
    cocos2d::CCNode* buildGeneralTab();
    cocos2d::CCNode* buildLockedTab();
    cocos2d::CCNode* buildAnimationsTab();
    cocos2d::CCNode* buildApplyToTab();
    cocos2d::CCNode* buildPerGamemodeTab();
    cocos2d::CCNode* buildPresetsTab();

    cocos2d::CCMenu* m_tabMenu = nullptr;
    cocos2d::CCNode* m_body    = nullptr;
    Tab m_activeTab = Tab::General;
};

}  // namespace paimon::icons::ui
