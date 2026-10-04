#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include <Geode/Geode.hpp>
#include "../models/EmoteModels.hpp"
#include <string>
#include <vector>
#include <unordered_set>

namespace paimon::emotes {

class EmotePickerPopup : public PaimonPopup {
public:
    enum class Tab { All, Stickers, GIFs };
    enum class LayoutSize { Normal, Large };

protected:
    float m_restingScale = 1.f;
    geode::CopyableFunction<std::string()> m_getText;
    geode::CopyableFunction<void(std::string const&)> m_onTextChanged;
    int m_charLimit = 140;
    LayoutSize m_layoutSize = LayoutSize::Normal;

    float m_popupW = 380.f;
    float m_popupH = 220.f;

    geode::TextInput* m_searchInput = nullptr;
    std::string m_searchQuery;

    cocos2d::CCNode* m_renderPreviewBg = nullptr;

    CCMenuItemToggler* m_keepOpenToggle = nullptr;
    bool m_keepOpen = false;

    Slider* m_sizeSlider = nullptr;
    float m_cellSize = 34.f;

    CCMenuItemSpriteExtra* m_refreshBtn = nullptr;
    bool m_isRefreshingCatalog = false;

    // horizontal tab strip: Favorites, Recents, All, then each category.
    geode::ScrollLayer* m_tabStrip = nullptr;
    cocos2d::CCMenu* m_tabMenu = nullptr;

    enum class View { Favorites, Recents, All, Category };
    View m_view = View::All;
    std::string m_activeCategory;

    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCNode* m_contentNode = nullptr;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;

    cocos2d::CCNode* m_titleBar = nullptr;
    cocos2d::CCNode* m_resizeHandle = nullptr;
    cocos2d::CCLabelBMFont* m_previewName = nullptr;
    cocos2d::CCLabelBMFont* m_previewCode = nullptr;

    struct HoverCell {
        cocos2d::CCNode* btn = nullptr;
        cocos2d::CCLayerColor* hoverLayer = nullptr;
        cocos2d::CCNode* container = nullptr;
        cocos2d::CCNode* sprite = nullptr;
        CCMenuItemSpriteExtra* starBtn = nullptr;
        EmoteInfo info;
        bool loadRequested = false;
        bool loaded = false;
        bool isGifSprite = false;
        cocos2d::CCNode* placeholder = nullptr;
    };
    std::vector<HoverCell> m_hoverCells;
    int m_hoverFrameSkip = 0;
    int m_lazyLoadFrameSkip = 0;

    // bumped on grid rebuild so stale thumbnail callbacks drop themselves.
    uint32_t m_gridGeneration = 0;

    float m_gridX = 0.f;
    float m_gridW = 0.f;
    float m_gridH = 0.f;
    float m_botY = 0.f;

    std::unordered_set<std::string> m_favorites;
    std::vector<std::string> m_recents;

    bool m_touchHitOutside = false;
    bool m_draggingTitle = false;
    bool m_draggingResize = false;
    cocos2d::CCPoint m_dragStartTouch;
    cocos2d::CCPoint m_dragStartPos;
    cocos2d::CCSize m_dragStartSize;
    float m_lastGridW = 0.f;
    float m_lastGridH = 0.f;

    bool ccTouchBegan(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void ccTouchMoved(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void ccTouchEnded(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void keyDown(cocos2d::enumKeyCodes key, double) override;
    void update(float dt) override;
    bool isInsideVisibleScroll(cocos2d::CCNode* item);

    bool init(
        geode::CopyableFunction<std::string()> getText,
        geode::CopyableFunction<void(std::string const&)> onTextChanged,
        int charLimit,
        LayoutSize size);

    void buildChrome();
    void buildBody();
    void relayout();
    void applyWindowBg();

    void rebuildTabStrip();
    void switchView(View view, std::string const& cat = "");
    void updateTabHighlights();

    std::vector<EmoteInfo> currentEmotes() const;
    void buildEmoteGrid(std::vector<EmoteInfo> const& emotes);
    void refreshGrid();

    void onEmoteClicked(cocos2d::CCObject* sender);
    void onStarClicked(cocos2d::CCObject* sender);
    void onTabClicked(cocos2d::CCObject* sender);
    void onSizeSlider(cocos2d::CCObject*);
    void onResetLayout(cocos2d::CCObject*);
    void onRefreshCatalog(cocos2d::CCObject*);
    void onSearchTextChanged(std::string const& text);
    void updateRefreshButtonState();
    void insertEmoteAtCursor(std::string const& emoteName);
    void insertFirstResult();

    bool isFavorite(std::string const& name) const;
    void toggleFavorite(std::string const& name);
    void pushRecent(std::string const& name);
    void loadFavoritesAndRecents();
    void saveFavorites();
    void saveRecents();
    void saveLayout();
    void loadLayout();

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
