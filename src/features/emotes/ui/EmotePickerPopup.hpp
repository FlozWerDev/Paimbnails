#pragma once

#include <Geode/Geode.hpp>
#include "../models/EmoteModels.hpp"
#include <string>

namespace paimon::emotes {

class EmotePickerPopup : public geode::Popup {
public:
    enum class Tab { All, Stickers, GIFs };
    enum class LayoutSize { Normal, Large };

protected:
    geode::CopyableFunction<std::string()> m_getText;
    geode::CopyableFunction<void(std::string const&)> m_onTextChanged;
    int m_charLimit = 140;
    LayoutSize m_layoutSize = LayoutSize::Normal;

    float m_popupW = 380.f;
    float m_popupH = 192.f;

    geode::TextInput* m_textInput = nullptr;

    cocos2d::CCNode* m_renderPreview = nullptr;
    cocos2d::CCNode* m_renderPreviewBg = nullptr;

    CCMenuItemSpriteExtra* m_searchBtn = nullptr;
    geode::TextInput* m_searchInput = nullptr;
    cocos2d::CCNode* m_searchInputBg = nullptr;
    bool m_searchActive = false;
    std::string m_searchQuery;

    cocos2d::CCMenu* m_typeMenu = nullptr;
    CCMenuItemSpriteExtra* m_refreshBtn = nullptr;
    bool m_isRefreshingCatalog = false;
    CCMenuItemSpriteExtra* m_btnAll = nullptr;
    CCMenuItemSpriteExtra* m_btnGif = nullptr;
    CCMenuItemSpriteExtra* m_btnStatic = nullptr;
    geode::ScrollLayer* m_catScroll = nullptr;
    cocos2d::CCMenu* m_catMenu = nullptr;
    Tab m_activeTab = Tab::All;
    std::string m_activeCategory;

    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCNode* m_contentNode = nullptr;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;

    struct HoverCell {
        cocos2d::CCNode* btn = nullptr;
        cocos2d::CCLayerColor* hoverLayer = nullptr;
        cocos2d::CCNode* container = nullptr;
        EmoteInfo info;
        bool loadRequested = false;
        bool loaded = false;
        cocos2d::CCNode* placeholder = nullptr;
    };
    std::vector<HoverCell> m_hoverCells;
    int m_hoverFrameSkip = 0;

    int m_lazyLoadFrameSkip = 0;

    // Bumped on grid rebuild so stale thumbnail callbacks drop themselves.
    uint32_t m_gridGeneration = 0;

    float m_gridX = 0.f;
    float m_gridW = 0.f;
    float m_gridH = 0.f;
    float m_botY = 0.f;

    bool m_touchHitOutside = false;

    bool ccTouchBegan(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void ccTouchEnded(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void update(float dt) override;
    bool isInsideVisibleScroll(cocos2d::CCNode* item);

    bool init(
        geode::CopyableFunction<std::string()> getText,
        geode::CopyableFunction<void(std::string const&)> onTextChanged,
        int charLimit,
        LayoutSize size);
    void switchTab(Tab tab);
    void rebuildCategorySidebar();
    void selectCategory(std::string const& cat);
    void buildEmoteGrid(std::vector<EmoteInfo> const& emotes);
    void buildAllEmotesGrid();
    void buildSearchResultsGrid();
    void onEmoteClicked(cocos2d::CCObject* sender);
    void onTabAll(cocos2d::CCObject*);
    void onTabGif(cocos2d::CCObject*);
    void onTabStatic(cocos2d::CCObject*);
    void onCategoryClicked(cocos2d::CCObject* sender);
    void refreshGrid();
    void updateTabHighlights();
    void updateRefreshButtonState();
    void onRefreshCatalog(cocos2d::CCObject*);
    void onSearchToggle(cocos2d::CCObject*);
    void onSearchTextChanged(std::string const& text);
    void onInputTextChanged(std::string const& text);
    void updateRenderPreview();
    void insertEmoteAtCursor(std::string const& emoteName);
    void rebuildScrollArea();

    void onExit() override;

    void onClose(cocos2d::CCObject*) override;
    void finishClose();
    bool m_closing = false;
    unsigned char m_dimOpacity = 0;

    void requestVisibleThumbnails();
    void requestAllThumbnails();
    void loadCellThumbnail(size_t cellIdx);

    void attachLoadedThumbnail(size_t cellIdx,
                               cocos2d::CCTexture2D* tex,
                               bool isGif,
                               std::vector<uint8_t> gifData);

public:
    static EmotePickerPopup* create(
        geode::CopyableFunction<std::string()> getText,
        geode::CopyableFunction<void(std::string const&)> onTextChanged,
        int charLimit = 140,
        LayoutSize size = LayoutSize::Normal);
    void show() override;
    void positionNearBottom(cocos2d::CCNode* anchor, float bottomPadding = 0.f);
    void positionCentered();
    void closeAnimated();
};

} // namespace paimon::emotes
