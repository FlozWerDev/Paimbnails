#pragma once

// The level info popup. It replaces the vanilla "Level Info" alert of the
// comments layer, so the description GD used to show is here too, on the first
// tab.
//
// Tab 0 is a visual dashboard (thumbnail banner, difficulty face, rating badge,
// coins, stat tiles, like bar, song). The remaining tabs are the raw field
// lists, where tapping a row copies it. Your own attempts and jumps are not
// here: those live in LevelStatsPopup.

#include <Geode/Geode.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>
#include "../services/LevelFacts.hpp"
#include <string>
#include <vector>

namespace paimon::info {

class ExtendedInfoPopup : public geode::Popup {
public:
    static ExtendedInfoPopup* create(GJGameLevel* level);

protected:
    bool init(GJGameLevel* level);
    void onClose(cocos2d::CCObject* sender) override;

    void buildTabBar(float centerX, float y);
    void refreshTabButtons();
    void buildActionRow(float centerX);

    // Geometry of the list, which depends on the filter and on the drawer.
    bool filterActive() const;
    float listTop() const;
    float listBottom() const;
    float viewHeight() const;
    float contentFloor() const;
    void applyLayout();

    // Rebuilds the scroll contents for the selected tab.
    void rebuildContent();
    void buildSummary(cocos2d::CCNode* content);
    void buildFactRows(cocos2d::CCNode* content);

    // Summary blocks, each returning a node of the given width.
    cocos2d::CCNode* makeHeaderBlock(float width);
    cocos2d::CCNode* makeDescriptionBlock(float width);
    cocos2d::CCNode* makeStatGrid(float width);
    cocos2d::CCNode* makeLikeBar(float width);
    cocos2d::CCNode* makeSongBlock(float width);

    void addThumbnailBanner(cocos2d::CCNode* block, float width, float height);
    void requestThumbnail();
    void applyBackdrop(cocos2d::CCTexture2D* texture);
    void installBackdrop(cocos2d::CCSprite* blurred, cocos2d::CCSize const& area, bool fadeIn);

    void onTab(cocos2d::CCObject* sender);
    void onRow(cocos2d::CCObject* sender);
    void onCopyAll(cocos2d::CCObject*);
    void onLevelStats(cocos2d::CCObject*);
    void onDrawer(cocos2d::CCObject*);
    void tickDrawer(float dt);
    void onFilterChanged(std::string const& text);

    void runAction(Fact const& fact);
    void requestExactDate();
    void applyExactDate(std::string const& date);

    geode::Ref<GJGameLevel> m_level;
    geode::Ref<cocos2d::CCTexture2D> m_thumbnail;
    bool m_hasBackdrop = false;
    std::vector<Fact> m_facts;
    std::vector<int> m_shown;   // indices into m_facts currently listed

    geode::ScrollLayer* m_scroll = nullptr;
    geode::TextInput* m_filter = nullptr;
    cocos2d::CCLabelBMFont* m_emptyLabel = nullptr;
    cocos2d::extension::CCScale9Sprite* m_listBg = nullptr;
    cocos2d::CCMenu* m_tabMenu = nullptr;
    cocos2d::CCMenu* m_actionRow = nullptr;
    CCMenuItemSpriteExtra* m_drawerBtn = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_tabButtons;

    // 0 = tabs and buttons in place, 1 = both tucked away and the list grown
    // over the whole popup.
    float m_drawer = 0.f;
    float m_drawerTarget = 0.f;

    // -1 is the visual summary; 0.. map onto FactTab.
    int m_tab = -1;
    std::string m_query;
};

} // namespace paimon::info
