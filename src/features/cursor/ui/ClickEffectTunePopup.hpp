#pragma once

// Ajustes finos de UN efecto de click concreto.
//
// Cada estallido y cada efecto de mantener guarda su propio tamano y su propia
// velocidad, porque no todos se ven bien con los mismos numeros. Este popup es
// lo que abre el engranaje que hay al lado de cada selector.

#include <Geode/Geode.hpp>
#include <Geode/binding/Slider.hpp>
#include <functional>
#include <string>

class ClickEffectTunePopup : public geode::Popup {
public:
    // `onChange` recibe (tamano, velocidad) en cada movimiento del slider.
    // `onTest` dispara la vista previa del popup de atras.
    static ClickEffectTunePopup* create(
        std::string title, std::string desc,
        float size, float speed,
        std::function<void(float, float)> onChange,
        std::function<void()> onTest);

private:
    std::function<void(float, float)> m_onChange;
    std::function<void()> m_onTest;
    std::string m_titleText;
    std::string m_descText;
    float m_size  = 1.f;
    float m_speed = 1.f;

    Slider* m_sizeSlider = nullptr;
    Slider* m_speedSlider = nullptr;
    cocos2d::CCLabelBMFont* m_sizeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_speedLabel = nullptr;

    bool init() override;
    void resetToDefault();
};
