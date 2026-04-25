#pragma once
#include <Geode/DefaultInclude.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <fmod.hpp>

class PaimonLoadingOverlay;

class LeaderboardHistoryLayer : public cocos2d::CCLayer, public LevelManagerDelegate {
public:
    static LeaderboardHistoryLayer* create();
    static cocos2d::CCScene* scene();

protected:
    bool init() override;
    void keyBackClicked() override;
    void onEnterTransitionDidFinish() override;
    void onExitTransitionDidStart() override;
    void update(float dt) override;
    
    // CCMouseDelegate
    bool ccMouseScroll(float x, float y);

    void onBack(cocos2d::CCObject* sender);
    void onTab(cocos2d::CCObject* sender);
    void onCellClick(cocos2d::CCObject* sender);
    void onNextPage(cocos2d::CCObject* sender);
    void onPrevPage(cocos2d::CCObject* sender);

    void loadHistory(std::string type);
    void createList();
    void updatePageButtons();

    // LevelManagerDelegate
    void loadLevelsFinished(cocos2d::CCArray* levels, char const* key) override;
    void loadLevelsFailed(char const* key) override;
    void setupPageInfo(gd::string, char const*) override;

    PaimonLoadingOverlay* m_loadingSpinner = nullptr;
    cocos2d::CCMenu* m_tabsMenu = nullptr;
    std::vector<CCMenuItemToggler*> m_tabs;
    std::string m_currentType = "daily";

    // datos del historial
    struct HistoryEntry {
        int levelID = 0;
        long long setAt = 0;
        std::string setBy;
        std::string levelName;
        std::string creatorName;
    };
    std::vector<HistoryEntry> m_entries;

    // paginacion
    static constexpr int ITEMS_PER_PAGE = 4;
    int m_currentPage = 0;
    int m_totalServerItems = 0;
    cocos2d::CCMenu* m_pageMenu = nullptr;
    CCMenuItemSpriteExtra* m_prevBtn = nullptr;
    CCMenuItemSpriteExtra* m_nextBtn = nullptr;
    cocos2d::CCLabelBMFont* m_pageLbl = nullptr;

    cocos2d::CCNode* m_listContainer = nullptr;
    geode::ScrollLayer* m_scrollView = nullptr;

    // musica cueva (efecto sobre musica de menu)
    FMOD::DSP* m_lowpassDSP = nullptr;
    FMOD::DSP* m_reverbDSP = nullptr;
    float m_savedBgVolume = 1.0f;
    bool m_caveApplied = false;

    void applyCaveEffect();
    void removeCaveEffect();
    void delaySilenceBg(float dt);

public:
    ~LeaderboardHistoryLayer();
};
