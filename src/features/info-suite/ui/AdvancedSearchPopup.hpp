#pragma once

// The Advanced Search builder. Two tabs so the popup stays readable at GD's
// popup size: "Servidor" holds the filters RobTop's API understands, "Refinar"
// holds the ones we apply to each page ourselves.

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/ui/TextInput.hpp>
#include "../services/AdvancedSearch.hpp"
#include <string>
#include <vector>

namespace paimon::info {

class AdvancedSearchPopup : public geode::Popup {
public:
    static AdvancedSearchPopup* create();

protected:
    bool init();
    void onClose(cocos2d::CCObject* sender) override;

    void buildTabs(float centerX, float y);
    void buildServerTab(cocos2d::CCNode* parent, float width);
    void buildRefineTab(cocos2d::CCNode* parent, float width);
    void refreshTabs();

    void onTab(cocos2d::CCObject* sender);
    void onDifficulty(cocos2d::CCObject* sender);
    void onLength(cocos2d::CCObject* sender);
    void onDemonCycle(cocos2d::CCObject* sender);
    void onFlag(cocos2d::CCObject* sender);
    void onSearch(cocos2d::CCObject*);
    void onSavePreset(cocos2d::CCObject*);
    void onPresets(cocos2d::CCObject*);

    void collectInputs();
    void applyQuery(AdvancedQuery const& query);
    void refreshDifficultyButtons();
    void refreshLengthButtons();
    void refreshDemonLabel();

    AdvancedQuery m_query;
    int m_tab = 0;

    cocos2d::CCNode* m_serverTab = nullptr;
    cocos2d::CCNode* m_refineTab = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_tabButtons;
    std::vector<CCMenuItemSpriteExtra*> m_difficultyButtons;
    std::vector<CCMenuItemSpriteExtra*> m_lengthButtons;
    cocos2d::CCLabelBMFont* m_demonLabel = nullptr;

    geode::TextInput* m_queryInput = nullptr;
    geode::TextInput* m_songInput = nullptr;
    geode::TextInput* m_minIDInput = nullptr;
    geode::TextInput* m_maxIDInput = nullptr;
    geode::TextInput* m_minVerInput = nullptr;
    geode::TextInput* m_maxVerInput = nullptr;
    geode::TextInput* m_minObjInput = nullptr;
    geode::TextInput* m_maxObjInput = nullptr;
};

} // namespace paimon::info
