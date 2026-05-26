#pragma once
#include <Geode/Geode.hpp>
#include <Geode/utils/function.hpp>
#include <Geode/binding/Slider.hpp>

class ThumbnailSettingsPopup : public geode::Popup {
protected:
    Slider* m_intensitySlider = nullptr;
    cocos2d::CCLabelBMFont* m_intensityLabel = nullptr;
    Slider* m_darknessSlider = nullptr;
    cocos2d::CCLabelBMFont* m_darknessLabel = nullptr;
    cocos2d::CCLabelBMFont* m_styleValueLabel = nullptr;
    CCMenuItemToggler* m_dynamicSongToggle = nullptr;
    CCMenuItemToggler* m_dynamicShadersToggle = nullptr;
    Slider* m_dynamicShadersDelaySlider = nullptr;
    cocos2d::CCLabelBMFont* m_dynamicShadersDelayLabel = nullptr;

    // Global Popup animations
    CCMenuItemToggler* m_dynamicPopupToggle = nullptr;
    std::vector<std::string> m_popupStyles;
    int m_popupStyleIndex = 0;
    std::string m_currentPopupStyle;
    cocos2d::CCLabelBMFont* m_popupStyleValueLabel = nullptr;
    Slider* m_popupSpeedSlider = nullptr;
    cocos2d::CCLabelBMFont* m_popupSpeedLabel = nullptr;

    // Dynamic exit
    CCMenuItemToggler* m_dynamicExitToggle = nullptr;
    Slider* m_dynamicExitSpeedSlider = nullptr;
    cocos2d::CCLabelBMFont* m_dynamicExitSpeedLabel = nullptr;
    bool m_dynamicExit = true;
    double m_currentPopupSpeed = 1.0;
    double m_currentExitSpeed = 1.0;

    // Popup gallery transition
    std::vector<std::string> m_popupTransitions;
    int m_popupTransitionIndex = 0;
    std::string m_currentPopupTransition;
    cocos2d::CCLabelBMFont* m_popupTransitionLabel = nullptr;
    Slider* m_popupTransitionDurationSlider = nullptr;
    cocos2d::CCLabelBMFont* m_popupTransitionDurationLabel = nullptr;

    // Background transition
    std::vector<std::string> m_bgTransitions;
    int m_bgTransitionIndex = 0;
    std::string m_currentBgTransition;
    cocos2d::CCLabelBMFont* m_bgTransitionLabel = nullptr;
    Slider* m_bgTransitionDurationSlider = nullptr;
    cocos2d::CCLabelBMFont* m_bgTransitionDurationLabel = nullptr;

    std::string m_currentStyle;
    int m_currentIntensity = 5;
    int m_currentDarkness = 27;
    bool m_dynamicSong = false;
    bool m_dynamicShaders = false;
    float m_dynamicShadersDelay = 0.0f;
    bool m_dynamicPopup = true;

    std::vector<std::string> m_styles;
    std::vector<std::string> m_allStyles; // lista completa sin filtrar
    int m_styleIndex = 0;

    geode::CopyableFunction<void()> m_onSettingsChanged;

    // peek mode: oculta popups para ver el fondo
    bool m_peekMode = false;
    cocos2d::CCMenu* m_peekMenu = nullptr;

    bool init() override;

    void onStylePrev(cocos2d::CCObject*);
    void onStyleNext(cocos2d::CCObject*);
    void onIntensityChanged(cocos2d::CCObject*);
    void onDarknessChanged(cocos2d::CCObject*);
    void onDynamicSongToggled(cocos2d::CCObject*);
    void onDynamicShadersToggled(cocos2d::CCObject*);
    void onDynamicShadersDelayChanged(cocos2d::CCObject*);
    void onOpenExtraEffects(cocos2d::CCObject*);
    void onDynamicPopupToggled(cocos2d::CCObject*);
    void onPopupStylePrev(cocos2d::CCObject*);
    void onPopupStyleNext(cocos2d::CCObject*);
    void onPopupSpeedChanged(cocos2d::CCObject*);
    void onDynamicExitToggled(cocos2d::CCObject*);
    void onDynamicExitSpeedChanged(cocos2d::CCObject*);
    void updatePopupStyleLabel();
    std::string getPopupStyleDisplayName(std::string const& style);
    void onTogglePeek(cocos2d::CCObject*);
    void onPopupTransitionPrev(cocos2d::CCObject*);
    void onPopupTransitionNext(cocos2d::CCObject*);
    void onPopupTransitionDurationChanged(cocos2d::CCObject*);
    void onBgTransitionPrev(cocos2d::CCObject*);
    void onBgTransitionNext(cocos2d::CCObject*);
    void onBgTransitionDurationChanged(cocos2d::CCObject*);
    void onClose(cocos2d::CCObject*) override;

    void updateStyleLabel();
    void updateStylesForDynamicShaders();
    void updatePopupTransitionLabel();
    void updateBgTransitionLabel();
    void saveSettings();
    std::string getStyleDisplayName(std::string const& style);
    std::string getPopupTransitionDisplayName(std::string const& transition);
    std::string getBgTransitionDisplayName(std::string const& transition);

public:
    static ThumbnailSettingsPopup* create();
    void setOnSettingsChanged(geode::CopyableFunction<void()> cb) { m_onSettingsChanged = std::move(cb); }
    void togglePeek();
};
