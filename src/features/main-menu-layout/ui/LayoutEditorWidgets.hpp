#pragma once

#include "../services/MainMenuLayoutManager.hpp"
#include "../../../ui/PaimonPopup.hpp"
#include "../../../ui/PaimonUI.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <functional>
#include <string>

namespace paimon::menu_layout {

enum class ElementCategory {
    Button,
    Label,
    Sprite,
    Shape,
};

struct FriendlyElementInfo {
    std::string displayName;
    std::string groupTag;
    ElementCategory category = ElementCategory::Button;
};

struct CanvasSettings {
    bool gridOn = false;
    bool snapEnabled = false;
    bool snapToGrid = true;
    bool smartGuides = true;
    bool safeZone = false;
    bool showHiddenOutlines = true;
    bool autoHideHud = true;
    float gridStep = 24.f;
    float snapDistance = 10.f;

    void load();
    void save() const;
};

namespace studio_ui {
    inline constexpr cocos2d::ccColor3B kTextPrimary{ 255, 255, 255 };
    inline constexpr cocos2d::ccColor3B kTextMuted{ 190, 200, 215 };
    inline constexpr cocos2d::ccColor3B kAccentGold{ 255, 210, 90 };
    inline constexpr cocos2d::ccColor3B kAccentCyan{ 110, 220, 255 };
    inline constexpr cocos2d::ccColor3B kAccentMint{ 120, 255, 165 };
    inline constexpr cocos2d::ccColor3B kAccentOrange{ 255, 175, 75 };
    inline constexpr cocos2d::ccColor3B kAccentPurple{ 205, 160, 255 };

    bool isSpanish();

    FriendlyElementInfo describeButtonElement(EditableMenuButton const& eb);
    FriendlyElementInfo describeShapeElement(DrawShapeLayout const& shape, int index);
    cocos2d::ccColor3B categoryColor(ElementCategory cat);

    // Clipped menu for ScrollLayer rows so touches outside the viewport do not fire.
    class ClippedScrollMenu : public cocos2d::CCMenu {
    public:
        static ClippedScrollMenu* create(geode::ScrollLayer* scroll);
        bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
        void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

    private:
        bool containsTouch(cocos2d::CCTouch* touch) const;
        geode::ScrollLayer* m_scroll = nullptr;
    };

    // Native GD window frame (GJ_square01..07) with optional inner dark inset.
    cocos2d::CCNode* makeGdWindow(
        cocos2d::CCSize size,
        paimon::ui::Bg bg = paimon::ui::Bg::Brown,
        GLubyte insetOpacity = 90);

    // Native GD ButtonSprite button wrapped for clean compact sizing.
    CCMenuItemSpriteExtra* makeGdButton(
        std::string const& text,
        float width,
        float height,
        paimon::ui::Btn skin,
        std::function<void()> onPress,
        char const* font = "bigFont.fnt");

    void setGdButtonState(
        CCMenuItemSpriteExtra* btn,
        paimon::ui::Btn skin,
        std::string const& newText = "");

    CCMenuItemSpriteExtra* makeDockTool(
        char const* primaryFrame,
        char const* fallbackFrame,
        std::string const& caption,
        float width,
        float height,
        cocos2d::CCObject* target,
        cocos2d::SEL_MenuHandler handler);

    void setDockToolState(CCMenuItemSpriteExtra* tool, bool active, bool enabled = true);

    cocos2d::CCSprite* createBestSprite(
        std::initializer_list<char const*> frames,
        float maxExtent,
        cocos2d::ccColor3B tint = { 255, 255, 255 });

    void openNumericInputPopup(
        std::string const& title,
        float currentValue,
        float minValue,
        float maxValue,
        bool integerOnly,
        std::function<void(float)> onApply);

    void openCanvasSettingsPopup(
        CanvasSettings const& current,
        std::function<void(CanvasSettings const&)> onChanged);
} // namespace studio_ui

} // namespace paimon::menu_layout
