#pragma once
#include <Geode/Geode.hpp>

class LevelCellSettingsPopup : public geode::Popup {
protected:
    void onExit() override;

    cocos2d::CCLabelBMFont* m_bgTypeLabel = nullptr;
    std::vector<std::string> m_bgTypes;
    int m_bgTypeIndex = 0;

    Slider* m_thumbWidthSlider = nullptr;
    cocos2d::CCLabelBMFont* m_thumbWidthLabel = nullptr;

    Slider* m_blurSlider = nullptr;
    cocos2d::CCLabelBMFont* m_blurLabel = nullptr;

    Slider* m_darknessSlider = nullptr;
    cocos2d::CCLabelBMFont* m_darknessLabel = nullptr;

    Slider* m_edgeBlendSlider = nullptr;
    cocos2d::CCLabelBMFont* m_edgeBlendLabel = nullptr;

    CCMenuItemToggler* m_separatorToggle = nullptr;
    CCMenuItemToggler* m_viewButtonToggle = nullptr;
    CCMenuItemToggler* m_compactToggle = nullptr;
    CCMenuItemToggler* m_compactShowToggle = nullptr;
    CCMenuItemToggler* m_transparentToggle = nullptr;
    CCMenuItemToggler* m_hoverToggle = nullptr;
    CCMenuItemToggler* m_effectOnGradientToggle = nullptr;
    CCMenuItemToggler* m_mythicParticlesToggle = nullptr;
    CCMenuItemToggler* m_animatedGradientToggle = nullptr;

    cocos2d::CCLabelBMFont* m_animTypeLabel = nullptr;
    std::vector<std::string> m_animTypes;
    int m_animTypeIndex = 0;

    Slider* m_animSpeedSlider = nullptr;
    cocos2d::CCLabelBMFont* m_animSpeedLabel = nullptr;

    cocos2d::CCLabelBMFont* m_animEffectLabel = nullptr;
    std::vector<std::string> m_animEffects;
    int m_animEffectIndex = 0;

    std::string m_currentBgType;
    float m_currentThumbWidth = 0.5f;
    float m_currentBlur = 3.0f;
    float m_currentDarkness = 0.2f;
    float m_currentEdgeBlend = 0.65f;
    bool m_showSeparator = true;
    bool m_showViewButton = true;
    bool m_compactMode = false;
    bool m_compactShowQuickToggle = true;
    bool m_transparentMode = false;
    bool m_hoverEffects = true;
    std::string m_currentAnimType;
    float m_currentAnimSpeed = 1.0f;
    std::string m_currentAnimEffect;
    bool m_effectOnGradient = false;
    bool m_mythicParticles = true;
    bool m_animatedGradient = true;

    geode::ScrollLayer* m_scrollLayer = nullptr;
    cocos2d::CCSprite* m_scrollArrow = nullptr;
    cocos2d::CCPoint m_scrollArrowBasePos = {0.f, 0.f};
    bool m_scrollArrowBouncing = false;

    // While a slider is being dragged, every popup "chrome" node (background,
    // close button, decorations, section titles, toggles, selectors) hides
    // so the level list behind the popup becomes a live, uncluttered
    // preview. The slider being dragged stays exactly where it is and stays
    // interactive; a small floating caption shows its name + live value.
    // Everything returns the moment the slider is released.
    struct SliderRow {
        Slider* slider = nullptr;
        cocos2d::CCLabelBMFont* valueLabel = nullptr;
        std::string title;
    };
    std::vector<SliderRow> m_sliderRows;
    std::vector<cocos2d::CCNode*> m_hideOnDragNodes;
    bool m_dragHiding = false;
    Slider* m_activeDragSlider = nullptr;
    GLubyte m_dimOriginalOpacity = 0;
    // Saved BlurAPI (thesillydoggo.blur-api) user object while live-previewing.
    // That mod blurs independently of our Paiblur and would keep the list
    // unreadable unless we temporarily detach its marker.
    geode::Ref<cocos2d::CCObject> m_savedBlurApiOptions = nullptr;

    cocos2d::CCNodeRGBA* m_dragCaptionPill = nullptr;
    cocos2d::CCLabelBMFont* m_dragCaptionLabel = nullptr;

    geode::CopyableFunction<void()> m_onSettingsChanged;

    bool init() override;
    void loadSettings();
    void saveSettings();
    void checkScrollPosition(float dt);
    void checkDragState(float dt);
    void applyDragVisibility(Slider* active);
    void updateDragCaption(Slider* active);
    void onDragCaptionHidden();
    void registerSliderRow(Slider* slider, cocos2d::CCLabelBMFont* valueLabel, std::string title);

    void onBgTypePrev(cocos2d::CCObject*);
    void onBgTypeNext(cocos2d::CCObject*);
    void onThumbWidthChanged(cocos2d::CCObject*);
    void onBlurChanged(cocos2d::CCObject*);
    void onDarknessChanged(cocos2d::CCObject*);
    void onEdgeBlendChanged(cocos2d::CCObject*);
    void onSeparatorToggled(cocos2d::CCObject*);
    void onViewButtonToggled(cocos2d::CCObject*);
    void onCompactToggled(cocos2d::CCObject*);
    void onCompactShowToggleToggled(cocos2d::CCObject*);
    void onTransparentToggled(cocos2d::CCObject*);
    void onHoverToggled(cocos2d::CCObject*);
    void onAnimTypePrev(cocos2d::CCObject*);
    void onAnimTypeNext(cocos2d::CCObject*);
    void onAnimSpeedChanged(cocos2d::CCObject*);
    void onAnimEffectPrev(cocos2d::CCObject*);
    void onAnimEffectNext(cocos2d::CCObject*);
    void onEffectOnGradientToggled(cocos2d::CCObject*);
    void onMythicParticlesToggled(cocos2d::CCObject*);
    void onAnimatedGradientToggled(cocos2d::CCObject*);

    std::string getBgTypeDisplayName(std::string const& type);
    std::string getAnimTypeDisplayName(std::string const& type);
    std::string getAnimEffectDisplayName(std::string const& effect);

public:
    static LevelCellSettingsPopup* create();
    void setOnSettingsChanged(geode::CopyableFunction<void()> cb) { m_onSettingsChanged = std::move(cb); }

    // Incremented on every setting change; LevelCell::update() checks this to invalidate cache
    static inline int s_settingsVersion = 0;
};
