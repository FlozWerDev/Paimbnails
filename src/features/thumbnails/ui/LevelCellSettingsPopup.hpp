#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/Slider.hpp>
#include <string>
#include <vector>

class LevelCellSettingsPopup : public PaimonPopup {
protected:
    void onExit() override;

    std::vector<std::string> m_bgTypes;
    std::vector<std::string> m_animTypes;
    std::vector<std::string> m_animEffects;
    int m_bgTypeIndex = 0;
    int m_animTypeIndex = 0;
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

    int m_tab = 0;
    geode::ScrollLayer* m_scrollLayer = nullptr;
    cocos2d::CCNode* m_scrollbar = nullptr;

    // during drag everything but the dragged slider hides so the list behind is a live preview.
    struct SliderRow {
        Slider* slider = nullptr;
        cocos2d::CCLabelBMFont* valueLabel = nullptr;
        std::string title;
    };
    std::vector<SliderRow> m_sliderRows;
    std::vector<geode::Ref<cocos2d::CCNode>> m_dragHidden;
    bool m_dragHiding = false;
    Slider* m_activeDragSlider = nullptr;
    GLubyte m_dimOriginalOpacity = 0;
    // blurapi blurs independently of paiblur: drop its marker or the list goes unreadable.
    geode::Ref<cocos2d::CCObject> m_savedBlurApiOptions = nullptr;

    cocos2d::CCNodeRGBA* m_dragCaptionPill = nullptr;
    cocos2d::CCLabelBMFont* m_dragCaptionLabel = nullptr;

    geode::CopyableFunction<void()> m_onSettingsChanged;

    bool init() override;
    void loadSettings();
    void saveSettings();
    void rebuild(bool keepScroll = false);
    void scheduleRebuild(bool keepScroll = false);
    void resetToDefaults();
    void applyPreset(float blur, float darkness, float edgeBlend);
    std::vector<cocos2d::CCNode*> buildBackgroundTab(float width);
    std::vector<cocos2d::CCNode*> buildDisplayTab(float width);
    std::vector<cocos2d::CCNode*> buildHoverTab(float width);
    cocos2d::CCNode* trackedSlider(float width, char const* title, char const* desc,
        double value, double minV, double maxV, int precision, std::function<void(double)> onChange);

    void checkDragState(float dt);
    void applyDragVisibility(Slider* active);
    void restoreDragVisibility();
    void updateDragCaption(Slider* active);
    void onDragCaptionHidden();

    std::string getBgTypeDisplayName(std::string const& type);
    std::string getAnimTypeDisplayName(std::string const& type);
    std::string getAnimEffectDisplayName(std::string const& effect);

public:
    static LevelCellSettingsPopup* create();
    void setOnSettingsChanged(geode::CopyableFunction<void()> cb) { m_onSettingsChanged = std::move(cb); }

    // incremented on every setting change; levelcell::update() checks this to invalidate cache
    static inline int s_settingsVersion = 0;
};
