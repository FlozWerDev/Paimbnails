#pragma once
#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/ui/TextInput.hpp>
#include <string>
#include <vector>
#include "../core/modules/ModuleRegistry.hpp"

class PaimonModulesLayer : public cocos2d::CCLayer {
protected:
    struct Row {
        paimon::modules::Module const* mod = nullptr;
        CCMenuItemToggler* toggler = nullptr;
        cocos2d::CCLayerColor* accent = nullptr;
        cocos2d::CCNodeRGBA* card = nullptr;
        cocos2d::CCLabelBMFont* state = nullptr;
        cocos2d::ccColor3B accentColor{255, 255, 255};
        bool ceded = false;  // switched on, but held back by another mod
    };

    bool init() override;
    void keyBackClicked() override;

    cocos2d::CCMenu* m_menu = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;
    cocos2d::CCLabelBMFont* m_sectionLabel = nullptr;
    geode::TextInput* m_searchInput = nullptr;

    std::vector<Row> m_rows;
    std::vector<paimon::modules::Module const*> m_visible;
    int m_sectionIndex = 0;  // 0 = every section
    std::string m_query;

    void collectVisible();
    void buildList();
    void refreshCount();
    void refreshRow(int index, bool updateToggler = true);
    void refreshAllRows(int skipTogglerIndex = -1);
    void updateSectionLabel();

    void onToggle(cocos2d::CCObject* sender);
    void onToggleCompatForce(cocos2d::CCObject*);
    void onAllOn(cocos2d::CCObject*);
    void onAllOff(cocos2d::CCObject*);
    void onPrevSection(cocos2d::CCObject*);
    void onNextSection(cocos2d::CCObject*);
    void onBack(cocos2d::CCObject*);

public:
    static PaimonModulesLayer* create();
    static cocos2d::CCScene* scene();
};
