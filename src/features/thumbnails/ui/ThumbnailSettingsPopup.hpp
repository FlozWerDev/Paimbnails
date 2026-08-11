#pragma once

#include <Geode/Geode.hpp>

class ThumbnailSettingsPopup : public geode::Popup {
protected:
    geode::ScrollLayer* m_scroll = nullptr;
    int m_tab = 0;

    std::vector<std::string> m_styles;
    std::vector<std::string> m_allStyles;
    std::vector<std::string> m_popupStyles;
    std::vector<std::string> m_popupTransitions;
    std::vector<std::string> m_bgTransitions;

    int m_styleIndex = 0;
    int m_popupStyleIndex = 0;
    int m_popupTransitionIndex = 0;
    int m_bgTransitionIndex = 0;

    std::string m_currentStyle;
    std::string m_currentPopupStyle;
    std::string m_currentPopupTransition;
    std::string m_currentBgTransition;
    int m_currentIntensity = 4;
    int m_currentDarkness = 27;
    bool m_dynamicSong = false;
    bool m_dynamicShaders = false;
    bool m_dynamicPopup = true;
    bool m_dynamicExit = true;
    float m_dynamicShadersDelay = 0.f;
    double m_currentPopupSpeed = 1.0;
    double m_currentExitSpeed = 1.0;

    geode::CopyableFunction<void()> m_onSettingsChanged;
    bool m_peekMode = false;
    cocos2d::CCMenu* m_peekMenu = nullptr;

    bool init() override;
    void rebuild();
    void scheduleRebuild();
    void addPeekButton();
    void openExtraEffects();
    void updateStylesForDynamicShaders();
    void saveSettings();
    void onClose(cocos2d::CCObject*) override;
    std::string getStyleDisplayName(std::string const& style) const;
    std::string getPopupStyleDisplayName(std::string const& style) const;
    std::string getTransitionDisplayName(std::string const& transition) const;

public:
    static ThumbnailSettingsPopup* create();
    void setOnSettingsChanged(geode::CopyableFunction<void()> cb) {
        m_onSettingsChanged = std::move(cb);
    }
    void togglePeek();
};