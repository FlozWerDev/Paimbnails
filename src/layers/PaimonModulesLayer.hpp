#pragma once
#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <string>
#include <vector>

class PaimonModulesLayer : public cocos2d::CCLayer {
protected:
    bool init() override;
    void keyBackClicked() override;

    cocos2d::CCMenu* m_menu = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;

    std::vector<std::string> m_keys;
    std::vector<CCMenuItemToggler*> m_togglers;
    std::vector<cocos2d::CCLayerColor*> m_accents;
    std::vector<cocos2d::CCNodeRGBA*> m_cards;
    std::vector<cocos2d::CCLabelBMFont*> m_stateLabels;
    std::vector<cocos2d::ccColor3B> m_accentColors;

    void buildList();
    void refreshCount();
    void refreshRowVisual(int index, bool enabled);

    void onToggle(cocos2d::CCObject* sender);
    void onAllOn(cocos2d::CCObject*);
    void onAllOff(cocos2d::CCObject*);
    void onBack(cocos2d::CCObject*);

public:
    static PaimonModulesLayer* create();
    static cocos2d::CCScene* scene();
};
