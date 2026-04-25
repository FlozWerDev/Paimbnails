#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/function.hpp>
#include <string>
#include <vector>

namespace paimon::fonts {

class FontPickerPopup : public geode::Popup {
public:
    enum class Tab { GDFonts, Custom };

    static FontPickerPopup* create(geode::CopyableFunction<void(std::string const&)> onSelect);
    void positionBelow(cocos2d::CCNode* anchor, float gap = 4.f);

protected:
    geode::CopyableFunction<void(std::string const&)> m_onSelect;

    // ── Preview section (top) ──
    cocos2d::CCNode* m_previewContainer = nullptr;
    cocos2d::CCNode* m_previewFontSprite = nullptr;
    cocos2d::CCLabelBMFont* m_previewLabel = nullptr;

    // ── Sidebar (bottom-left) ──
    cocos2d::CCMenu* m_sideMenu = nullptr;
    CCMenuItemSpriteExtra* m_tabGD = nullptr;
    CCMenuItemSpriteExtra* m_tabCustom = nullptr;
    CCMenuItemSpriteExtra* m_qpBig = nullptr;
    CCMenuItemSpriteExtra* m_qpChat = nullptr;
    CCMenuItemSpriteExtra* m_qpGold = nullptr;
    Tab m_activeTab = Tab::GDFonts;

    // ── Font grid (bottom-right) ──
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCNode* m_contentNode = nullptr;

    // ── Custom tab (bottom-right, swapped in) ──
    cocos2d::CCNode* m_customContainer = nullptr;
    geode::TextInput* m_customInput = nullptr;

    bool m_touchHitOutside = false;

    bool ccTouchBegan(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void ccTouchEnded(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    bool isInsideVisibleScroll(cocos2d::CCNode* item);

    bool init(geode::CopyableFunction<void(std::string const&)> onSelect);
    void switchTab(Tab tab);
    void updateTabHighlights();
    void buildGDFontGrid();
    void showPreview(std::string const& fontId, std::string const& fontFile);
    void onTabGD(cocos2d::CCObject*);
    void onTabCustom(cocos2d::CCObject*);
    void onQuickPick(cocos2d::CCObject* sender);
    void onFontClicked(cocos2d::CCObject* sender);
    void onCustomApply(cocos2d::CCObject*);
    void onRemoveFont(cocos2d::CCObject*);
};

} // namespace paimon::fonts
