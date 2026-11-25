#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

namespace geode { class Scrollbar; }

namespace paimon::fonts {

class FontPickerPopup : public PaimonPopup {
public:
    enum class Tab { GDFonts, Custom };

    static FontPickerPopup* create(geode::CopyableFunction<void(std::string const&)> onSelect);
    void closeAnimated();

protected:
    geode::CopyableFunction<void(std::string const&)> m_onSelect;

    cocos2d::CCNode* m_previewContainer = nullptr;
    cocos2d::CCNode* m_previewFontSprite = nullptr;
    cocos2d::CCLabelBMFont* m_previewLabel = nullptr;

    CCMenuItemSpriteExtra* m_tabGD = nullptr;
    CCMenuItemSpriteExtra* m_tabCustom = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_quickButtons;
    Tab m_activeTab = Tab::GDFonts;

    geode::ScrollLayer* m_scroll = nullptr;
    geode::Scrollbar* m_scrollbar = nullptr;
    cocos2d::CCMenu* m_gridMenu = nullptr;
    cocos2d::CCNode* m_contentNode = nullptr;
    struct FontPreview {
        cocos2d::CCNode* cell;
        ButtonSprite* background;
        std::string file;
        bool loaded = false;
    };
    std::vector<FontPreview> m_fontPreviews;
    float m_lastPreviewScrollY = 0.f;
    bool m_fontPreviewsPending = true;
    std::string m_selectedFontFile;

    cocos2d::CCNode* m_customContainer = nullptr;
    geode::TextInput* m_customInput = nullptr;

    bool m_touchHitOutside = false;

    bool ccTouchBegan(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void ccTouchEnded(cocos2d::CCTouch*, cocos2d::CCEvent*) override;

    bool init(geode::CopyableFunction<void(std::string const&)> onSelect);
    void switchTab(Tab tab);
    void updateTabHighlights();
    void buildGDFontGrid();
    void update(float dt) override;
    void showPreview(std::string const& fontId, std::string const& fontFile);
    void onQuickPick(cocos2d::CCObject* sender);
    void onFontClicked(cocos2d::CCObject* sender);
    void onCustomApply(cocos2d::CCObject*);
    void onRemoveFont(cocos2d::CCObject*);
};

} // namespace paimon::fonts
