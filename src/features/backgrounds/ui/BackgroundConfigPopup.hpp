#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/binding/Slider.hpp>

class BackgroundConfigPopup : public geode::Popup {
protected:
    geode::TextInput* m_idInput;
    cocos2d::CCLayer* m_menuLayer;
    cocos2d::CCLayer* m_profileLayer;
    cocos2d::CCLayer* m_petLayer = nullptr;
    cocos2d::CCLayer* m_layerBgLayer = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_tabs;
    int m_selectedTab = 0;
    Slider* m_slider = nullptr;

    // layer bg tab
    std::string m_selectedLayerKey = "creator";
    geode::TextInput* m_layerIdInput = nullptr;
    Slider* m_layerDarkSlider = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_layerSelectBtns;
    CCMenuItemToggler* m_layerDarkToggle = nullptr;

    bool init();
    
    // ayudantes de ui
    void createTabs();
    void onTab(cocos2d::CCObject* sender);
    void updateTabs();
    cocos2d::CCNode* createMenuTab();
    cocos2d::CCNode* createProfileTab();
    cocos2d::CCNode* createPetTab();
    cocos2d::CCNode* createLayerBgTab();

    // acciones menu
    void onCustomImage(cocos2d::CCObject* sender);
    void onCustomVideo(cocos2d::CCObject* sender);
    void onDownloadedThumbnails(cocos2d::CCObject* sender);
    void onSetID(cocos2d::CCObject* sender);
    void onApply(cocos2d::CCObject* sender);
    void onDarkMode(cocos2d::CCObject* sender);
    void onIntensityChanged(cocos2d::CCObject* sender);

    // acciones profile
    void onProfileCustomImage(cocos2d::CCObject* sender);
    void onProfileClear(cocos2d::CCObject* sender);
    void onCustomizePhoto(cocos2d::CCObject* sender);

    // acciones pet
    void onOpenPetConfig(cocos2d::CCObject* sender);

    // features
    void onDefaultMenu(cocos2d::CCObject* sender);
    void onAdaptiveColors(cocos2d::CCObject* sender);

    // layer bg actions
    void onLayerSelect(cocos2d::CCObject* sender);
    void onLayerCustomImage(cocos2d::CCObject* sender);
    void onLayerCustomVideo(cocos2d::CCObject* sender);
    void onLayerRandom(cocos2d::CCObject* sender);
    void onLayerSameAs(cocos2d::CCObject* sender);
    void onLayerDefault(cocos2d::CCObject* sender);
    void onLayerSetID(cocos2d::CCObject* sender);
    void onLayerDarkMode(cocos2d::CCObject* sender);
    void onLayerDarkIntensity(cocos2d::CCObject* sender);
    void updateLayerSelectButtons();

    // helper
    CCMenuItemSpriteExtra* createBtn(char const* text, cocos2d::CCPoint pos, cocos2d::SEL_MenuHandler handler, cocos2d::CCNode* parent);

public:
    static BackgroundConfigPopup* create();
};
