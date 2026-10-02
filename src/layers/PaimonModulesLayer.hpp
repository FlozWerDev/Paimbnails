#pragma once
#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/ui/TextInput.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
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
        cocos2d::CCLabelBMFont* desc = nullptr;
        cocos2d::ccColor3B accentColor{255, 255, 255};
        float descWidth = 0.f;
        bool ceded = false;  // switched on, but held back by another mod
    };

    enum Filter { kFilterAll = 0, kFilterOn, kFilterOff, kFilterEdited };

    using Snapshot = std::vector<std::pair<paimon::modules::Module const*, bool>>;

    bool init() override;
    void keyBackClicked() override;

    cocos2d::CCMenu* m_menu = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_titleLabel = nullptr;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;
    cocos2d::CCLabelBMFont* m_totalLabel = nullptr;
    geode::TextInput* m_searchInput = nullptr;
    CCMenuItemSpriteExtra* m_undoBtn = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_sectionBtns;

    std::vector<Row> m_rows;
    std::vector<paimon::modules::Module const*> m_visible;
    std::unordered_map<std::string_view, int> m_childCount;
    std::vector<Snapshot> m_undo;
    int m_sectionIndex = 0;  // 0 = every section
    int m_filter = kFilterAll;
    bool m_showIds = false;
    std::string m_query;
    float m_contentX = 0.f;
    float m_innerW = 0.f;

    void buildSidebar(float panelTop, float panelBot);
    void buildHeader(float headerY);
    void buildFilterBar(float y);
    void collectVisible();
    void buildList();
    void refreshCount();
    void refreshRow(int index, bool updateToggler = true);
    void refreshAllRows(int skipTogglerIndex = -1);
    void refreshSidebar();
    void refreshUndo();
    void rebuild();

    void pushUndo(Snapshot snapshot);
    void applyToVisible(bool useDefaults, bool value);
    void copyActiveIds();
    void setCompatForced(bool forced);
    void openActions();
    void openHelp();
    void showDetails(paimon::modules::Module const* mod);

    void onToggle(cocos2d::CCObject* sender);
    void onToggleCompatForce(cocos2d::CCObject*);
    void onUndo(cocos2d::CCObject*);
    void onBack(cocos2d::CCObject*);

public:
    static PaimonModulesLayer* create();
    static cocos2d::CCScene* scene();
};
