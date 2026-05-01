#pragma once
#include <Geode/Geode.hpp>
#include <Geode/binding/Slider.hpp>
#include "../services/ProfilePicCustomizer.hpp"

class ProfilePicIconsDetailPopup : public geode::Popup {
protected:
    ProfilePicConfig* m_cfg = nullptr;
    std::function<void()> m_onChange;
    cocos2d::CCNode* m_contentNode = nullptr;
    cocos2d::CCNode* m_previewNode = nullptr;

    // Sliders
    Slider* m_scaleSlider = nullptr;
    cocos2d::CCLabelBMFont* m_scaleLabel = nullptr;
    Slider* m_speedSlider = nullptr;
    cocos2d::CCLabelBMFont* m_speedLabel = nullptr;

    bool init(ProfilePicConfig* cfg, std::function<void()> onChange);

    void rebuild();
    void rebuildPreview();

    void onClose(cocos2d::CCObject* sender) override;

    // Handlers
    void onColor1Source(cocos2d::CCObject* sender);
    void onPickColor1(cocos2d::CCObject* sender);
    void onColor2Source(cocos2d::CCObject* sender);
    void onPickColor2(cocos2d::CCObject* sender);
    void onGlowToggle(cocos2d::CCObject* sender);
    void onGlowColorSource(cocos2d::CCObject* sender);
    void onPickGlowColor(cocos2d::CCObject* sender);
    void onScaleChanged(cocos2d::CCObject* sender);
    void onAddCustomIcon(cocos2d::CCObject* sender);
    void onRemoveCustomIcon(cocos2d::CCObject* sender);
    void onCustomIconSelect(cocos2d::CCObject* sender);
    void onAnimTypeSelect(cocos2d::CCObject* sender);
    void onAnimSpeedChanged(cocos2d::CCObject* sender);
    void onIconImageToggle(cocos2d::CCObject* sender);

public:
    static ProfilePicIconsDetailPopup* create(ProfilePicConfig* cfg, std::function<void()> onChange);
};
