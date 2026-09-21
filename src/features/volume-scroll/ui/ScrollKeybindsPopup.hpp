#pragma once

#include <Geode/Geode.hpp>

#include <string>

namespace paimon::volscroll {

// Volume rows use a mouse hold as the modifier for scrolling.

class ScrollKeybindsPopup : public geode::Popup {
public:
    static ScrollKeybindsPopup* create();

protected:
    bool init() override;

    // Scroll content containing headers and keybinds.
    geode::ScrollLayer* m_scrollLayer = nullptr;

    cocos2d::CCNode* makeSectionHeader(char const* title, float width);

    cocos2d::CCNode* makeKeybindRow(
        char const* settingKey,
        char const* displayName,
        float width,
        bool allowScroll
    );

    // Open the editor and refresh labelToRefresh after saving.
    void openEditPopup(
        std::string settingKey,
        std::string displayName,
        bool allowScroll,
        cocos2d::CCLabelBMFont* labelToRefresh
    );

    // Restore volume defaults and clear their extended binds.
    void onResetVolumeDefaults(cocos2d::CCObject*);
};

}
