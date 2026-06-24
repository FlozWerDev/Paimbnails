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
    // Sync callbacks
    geode::CopyableFunction<std::string()> m_getText;
    geode::CopyableFunction<void(std::string const&)> m_onTextChanged;
    int m_charLimit = 140;
    LayoutSize m_layoutSize = LayoutSize::Normal;

    // Computed dimensions based on layout size
    float m_popupW = 380.f;
    float m_popupH = 192.f;

    // Input section (top)
    geode::TextInput* m_textInput = nullptr;

    // Live preview section
    cocos2d::CCNode* m_renderPreview = nullptr;
    cocos2d::CCNode* m_renderPreviewBg = nullptr;

    // Search
    CCMenuItemSpriteExtra* m_searchBtn = nullptr;
    geode::TextInput* m_searchInput = nullptr;
    cocos2d::CCNode* m_searchInputBg = nullptr;
    bool m_searchActive = false;
    std::string m_searchQuery;

    // Category sidebar (bottom-left)
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

    // Emote grid (bottom-right)
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCNode* m_contentNode = nullptr;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;

    // Hover tracking for emote cells
    struct HoverCell {
        cocos2d::CCNode* btn = nullptr;          // CCMenuItemSpriteExtra*
        cocos2d::CCLayerColor* hoverLayer = nullptr;
        cocos2d::CCNode* container = nullptr;    // for content size / world-rect
        EmoteInfo info;                          // emote data for lazy loading
        bool loadRequested = false;              // true once thumbnail load was queued
        bool loaded = false;                     // true once thumbnail was attached
        cocos2d::CCNode* placeholder = nullptr;  // placeholder label/spinner
    };
    std::vector<HoverCell> m_hoverCells;
    int m_hoverFrameSkip = 0;

    // Walk the cell list every few frames to load newly visible thumbnails.
    int m_lazyLoadFrameSkip = 0;

    // Bumped on grid rebuild so stale thumbnail callbacks drop themselves.
    uint32_t m_gridGeneration = 0;

    // Grid width cache (changes depending on sidebar visibility)
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

    // Dispatch thumbnail loads for cells near the visible viewport; already-requested cells are skipped.
    void requestVisibleThumbnails();

    // Attach the loaded thumbnail (texture or GIF data) to a cell.
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
    void positionNearBottom(cocos2d::CCNode* anchor, float bottomPadding = 0.f);
    void positionCentered();
};

} // namespace paimon::emotes
