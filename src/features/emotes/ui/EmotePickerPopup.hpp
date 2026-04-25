#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/function.hpp>
#include "../models/EmoteModels.hpp"
#include <string>

namespace paimon::emotes {

class EmotePickerPopup : public geode::Popup {
public:
    enum class Tab { All, Stickers, GIFs };

protected:
    // ── Sync callbacks ──
    geode::CopyableFunction<std::string()> m_getText;
    geode::CopyableFunction<void(std::string const&)> m_onTextChanged;
    int m_charLimit = 140;

    // ── Input section (top) ──
    geode::TextInput* m_textInput = nullptr;

    // ── Live preview section ──
    cocos2d::CCNode* m_renderPreview = nullptr;
    cocos2d::CCNode* m_renderPreviewBg = nullptr;

    // ── Category sidebar (bottom-left) ──
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

    // ── Emote grid (bottom-right) ──
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCNode* m_contentNode = nullptr;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;

    // ── Grid width cache (changes depending on sidebar visibility) ──
    float m_gridX = 0.f;
    float m_gridW = 0.f;
    float m_gridH = 0.f;
    float m_botY = 0.f;

    bool m_touchHitOutside = false;

    bool ccTouchBegan(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void ccTouchEnded(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    bool isInsideVisibleScroll(cocos2d::CCNode* item);

    bool init(
        geode::CopyableFunction<std::string()> getText,
        geode::CopyableFunction<void(std::string const&)> onTextChanged,
        int charLimit);
    void switchTab(Tab tab);
    void rebuildCategorySidebar();
    void selectCategory(std::string const& cat);
    void buildEmoteGrid(std::vector<EmoteInfo> const& emotes);
    void buildAllEmotesGrid();
    void onEmoteClicked(cocos2d::CCObject* sender);
    void onTabAll(cocos2d::CCObject*);
    void onTabGif(cocos2d::CCObject*);
    void onTabStatic(cocos2d::CCObject*);
    void onCategoryClicked(cocos2d::CCObject* sender);
    void refreshGrid();
    void updateTabHighlights();
    void updateRefreshButtonState();
    void onRefreshCatalog(cocos2d::CCObject*);
    void onInputTextChanged(std::string const& text);
    void updateRenderPreview();
    void insertEmoteAtCursor(std::string const& emoteName);
    void rebuildScrollArea();

public:
    static EmotePickerPopup* create(
        geode::CopyableFunction<std::string()> getText,
        geode::CopyableFunction<void(std::string const&)> onTextChanged,
        int charLimit = 140);
    void positionNearBottom(cocos2d::CCNode* anchor, float bottomPadding = 0.f);
};

} // namespace paimon::emotes
