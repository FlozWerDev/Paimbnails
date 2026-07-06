#pragma once
#include <Geode/Geode.hpp>

class ProfileBgGradientPopup : public geode::Popup {
public:
    using ApplyCallback = geode::CopyableFunction<void(std::string const& effect, float speed)>;

    static ProfileBgGradientPopup* create(int accountID,
                                          std::string const& initialEffect,
                                          float initialSpeed,
                                          ApplyCallback onApply);

protected:
    int           m_accountID = 0;
    std::string   m_effect    = "none";
    float         m_speed     = 1.0f;
    ApplyCallback m_onApply;

    cocos2d::CCNode*           m_previewContainer = nullptr;

    Slider*                    m_speedSlider = nullptr;
    cocos2d::CCLabelBMFont*    m_speedLabel  = nullptr;

    std::vector<std::pair<std::string, CCMenuItemSpriteExtra*>> m_effectButtons;

    bool init(int accountID, std::string const& initialEffect, float initialSpeed,
              ApplyCallback onApply);

    void onSelectEffect(cocos2d::CCObject* sender);
    void onSpeedChanged(cocos2d::CCObject* sender);
    void onApplyBtn(cocos2d::CCObject*);
    void onInfo(cocos2d::CCObject*);

    void refreshSpeedLabel();
    void refreshEffectSelection();
    void rebuildPreview();

    static cocos2d::ccColor3B currentPlayerColor1();
    static cocos2d::ccColor3B currentPlayerColor2();
};
