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

    std::vector<std::string> m_styles;
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
    void onOpenExtraEffects(cocos2d::CCObject*);
    void onTogglePeek(cocos2d::CCObject*);
    void onPopupTransitionPrev(cocos2d::CCObject*);
    void onPopupTransitionNext(cocos2d::CCObject*);
    void onPopupTransitionDurationChanged(cocos2d::CCObject*);
    void onBgTransitionPrev(cocos2d::CCObject*);
    void onBgTransitionNext(cocos2d::CCObject*);
    void onBgTransitionDurationChanged(cocos2d::CCObject*);
    void onClose(cocos2d::CCObject*) override;

    void updateStyleLabel();
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
