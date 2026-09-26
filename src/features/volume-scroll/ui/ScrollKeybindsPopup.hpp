#pragma once

#include <Geode/Geode.hpp>

#include <string>

namespace paimon::volscroll {

// volume rows take a mouse hold as the scroll modifier.

class ScrollKeybindsPopup : public geode::Popup {
public:
    static ScrollKeybindsPopup* create();

protected:
    bool init() override;

    geode::ScrollLayer* m_scrollLayer = nullptr;

    cocos2d::CCNode* makeSectionHeader(char const* title, float width);

    cocos2d::CCNode* makeKeybindRow(
        char const* settingKey,
        char const* displayName,
        float width,
        bool allowScroll
    );

    void openEditPopup(
        std::string settingKey,
        std::string displayName,
        bool allowScroll,
        cocos2d::CCLabelBMFont* labelToRefresh
    );

    void onResetVolumeDefaults(cocos2d::CCObject*);
};

}
