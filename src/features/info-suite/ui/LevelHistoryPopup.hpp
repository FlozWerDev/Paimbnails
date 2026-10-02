#pragma once
#include "../../../ui/PaimonPopup.hpp"

// linea de tiempo del nivel segun history.geometrydash.eu: estado actual, rate/feature y snapshots.
// cada fila abre su ficha completa.

#include "../services/LevelHistoryModel.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <matjson.hpp>
#include <string>

namespace paimon::info {

class LevelHistoryPopup : public PaimonPopup {
public:
    static LevelHistoryPopup* create(GJGameLevel* level);

protected:
    bool init(GJGameLevel* level);

    void loadHistory();
    void applyHistory(matjson::Value root);
    void rebuildList();
    void refreshToolButtons();

    cocos2d::CCNode* makeStateBlock(float width);
    cocos2d::CCNode* makeMilestoneBlock(float width);
    cocos2d::CCNode* makeEntryCell(HistoryEntry const& entry, int index, float width);

    void onEntry(cocos2d::CCObject* sender);
    void onOrder(cocos2d::CCObject*);
    void onFilter(cocos2d::CCObject*);

    geode::Ref<GJGameLevel> m_level;
    geode::Ref<geode::ScrollLayer> m_scroll;
    geode::Ref<cocos2d::CCNode> m_content;
    geode::Ref<cocos2d::CCLabelBMFont> m_statusLabel;
    geode::Ref<cocos2d::CCMenu> m_toolMenu;
    ButtonSprite* m_orderSprite = nullptr;
    ButtonSprite* m_filterSprite = nullptr;

    LevelHistory m_history;
    std::string m_uploadDate;   // estimacion de gdhistory, mejor que el primer snapshot
    bool m_loaded = false;
    bool m_newestFirst = false;
    bool m_onlyMilestones = false;
};

} // namespace paimon::info
