#pragma once

#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <string>
#include <vector>

namespace paimon::keybinds {

// Unified Keybinds & Gestures manager opened from Paimon Hub > General > "Manager Cut".
class ManagerCutPopup : public PaimonPopup {
public:
    enum class Mode {
        Keys = 0,
        Gestures = 1,
    };

    static ManagerCutPopup* create(Mode initialMode = Mode::Keys);

protected:
    bool init(Mode initialMode);

private:
    Mode m_mode = Mode::Keys;
    int m_categoryFilter = 0; // 0 = All

    cocos2d::CCNode* m_tabBar = nullptr;
    cocos2d::CCNode* m_catBar = nullptr;
    cocos2d::CCMenu* m_footerMenu = nullptr;
    CCMenuItemSpriteExtra* m_switchModeBtn = nullptr;
    CCMenuItemSpriteExtra* m_resetBtn = nullptr;
    cocos2d::CCLabelBMFont* m_subtitleLbl = nullptr;
    geode::ScrollLayer* m_scrollLayer = nullptr;
    float m_listW = 390.f;
    float m_listH = 175.f;

    void setMode(Mode mode);
    void setCategoryFilter(int index);
    void rebuildHeaderControls();
    void rebuildList();
    void rebuildFooterControls();

    cocos2d::CCNode* makeSectionHeader(char const* title, float width);
    cocos2d::CCNode* makeKeyRow(
        std::string const& settingKey,
        std::string const& displayName,
        bool isVolume,
        bool isQuickHubHold,
        float width
    );
    cocos2d::CCNode* makeGestureRow(int actionIndex, float width);
    std::vector<cocos2d::CCNode*> buildGestureComfortCards(float width);

    void onResetAll();
    void openKeyEditor(
        std::string const& settingKey,
        std::string const& displayName,
        bool isVolume,
        bool isQuickHubHold
    );
    void openGestureEditor(int actionIndex);
};

} // namespace paimon::keybinds
