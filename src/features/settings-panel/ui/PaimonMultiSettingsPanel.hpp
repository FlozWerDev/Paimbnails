#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>

class PaimonMultiSettingsPanel : public cocos2d::CCLayer {
protected:
    // fondo
    cocos2d::CCSprite* m_blurBg = nullptr;
    cocos2d::CCLayerColor* m_darkOverlay = nullptr;

    // panel principal (se arrastra entero)
    cocos2d::CCNode* m_panelContainer = nullptr;
    cocos2d::CCDrawNode* m_panelBg = nullptr;
    cocos2d::CCDrawNode* m_titleBarBg = nullptr;
    cocos2d::CCLabelBMFont* m_titleLabel = nullptr;

    // sidebar
    cocos2d::CCMenu* m_sidebarMenu = nullptr;
    cocos2d::CCDrawNode* m_sidebarBg = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_sidebarButtons;
    cocos2d::CCDrawNode* m_sidebarAccent = nullptr;
    int m_selectedCategory = 0;

    // content area
    geode::ScrollLayer* m_scrollLayer = nullptr;

    // search
    geode::TextInput* m_searchInput = nullptr;
    std::string m_searchQuery;
    bool m_isSearchActive = false;

    // drag state
    bool m_isDragging = false;
    cocos2d::CCPoint m_dragOffset;

    // animacion
    bool m_isClosing = false;

    // Touch priority computada dinamicamente al abrir (force priority aware)
    int m_touchPrio = -600;
    int m_childTouchPrio = -601;

    // dimensiones del panel
    static constexpr float PANEL_W = 480.f;
    static constexpr float PANEL_H = 280.f;
    static constexpr float TITLE_BAR_H = 28.f;
    static constexpr float SIDEBAR_W = 44.f;
    static constexpr float CORNER_RADIUS = 8.f;
    static constexpr float CONTENT_W = PANEL_W - SIDEBAR_W;
    static constexpr float CONTENT_H = PANEL_H - TITLE_BAR_H;

    bool init(cocos2d::CCSprite* blurBg, int initialCategory);

    void buildTitleBar();
    void buildSidebar();
    void buildContentArea();
    void selectCategory(int index);
    void rebuildContent();
    void relayoutContent();
    void updateSidebarAccent();

    // search
    void onSearchChanged(std::string const& query);
    void buildSearchResults(std::string const& query);

    void runEntryAnimation();

    // touch handling
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

    bool isTouchInTitleBar(cocos2d::CCPoint const& worldPos);
    bool isTouchInPanel(cocos2d::CCPoint const& worldPos);
    bool isTouchInSearchInput(cocos2d::CCPoint const& worldPos) const;

    void keyBackClicked() override;
    void onClose(cocos2d::CCObject*);
    void onExit() override;

public:
    static PaimonMultiSettingsPanel* create(cocos2d::CCSprite* blurBg, int initialCategory = 0);
    void animateClose();
    void onCloseFinished();
    void relayoutScrollContent();
    void setSelectedCategory(int index);
};
