#pragma once
#include <Geode/Geode.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/utils/function.hpp>

// ProfileBgGradientPopup
// Sub-popup que aparece tras pulsar "Degradado Iconos" en
// ProfileBgPickerPopup.  Permite elegir un efecto animado y la velocidad,
// y previsualiza el degradado con los colores actuales del icono del jugador.
class ProfileBgGradientPopup : public geode::Popup {
public:
    using ApplyCallback = geode::CopyableFunction<void(std::string const& effect, float speed)>;

    static ProfileBgGradientPopup* create(int accountID,
                                          std::string const& initialEffect,
                                          float initialSpeed,
                                          ApplyCallback onApply);

protected:
    int           m_accountID = 0;
    std::string   m_effect    = "none";
    float         m_speed     = 1.0f;
    ApplyCallback m_onApply;

    // Preview
    cocos2d::CCNode*           m_previewContainer = nullptr;

    // Speed slider
    Slider*                    m_speedSlider = nullptr;
    cocos2d::CCLabelBMFont*    m_speedLabel  = nullptr;

    // Effect buttons (en orden: none, rotate, pulse, shift, slide)
    std::vector<std::pair<std::string, CCMenuItemSpriteExtra*>> m_effectButtons;

    bool init(int accountID, std::string const& initialEffect, float initialSpeed,
              ApplyCallback onApply);

    void onSelectEffect(cocos2d::CCObject* sender);
    void onSpeedChanged(cocos2d::CCObject* sender);
    void onApplyBtn(cocos2d::CCObject*);
    void onInfo(cocos2d::CCObject*);

    void refreshSpeedLabel();
    void refreshEffectSelection();
    void rebuildPreview();

    static cocos2d::ccColor3B currentPlayerColor1();
    static cocos2d::ccColor3B currentPlayerColor2();
};
