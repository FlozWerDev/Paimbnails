#include "ClickEffectTunePopup.hpp"
#include "../services/CursorClickFX.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <fmt/format.h>
#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
namespace kit = paimon::configkit;
namespace fx = paimon::cursorfx;

std::string formatTune(double value) {
    return fmt::format("x{:.2f}", value);
}

// Slider::setValue trabaja en 0..1.
float normTune(float value) {
    return std::clamp((value - fx::kClickTuneMin) / (fx::kClickTuneMax - fx::kClickTuneMin),
                      0.f, 1.f);
}
} // namespace

ClickEffectTunePopup* ClickEffectTunePopup::create(
    std::string title, std::string desc,
    float size, float speed,
    std::function<void(float, float)> onChange,
    std::function<void()> onTest
) {
    auto* ret = new ClickEffectTunePopup();
    ret->m_titleText = std::move(title);
    ret->m_descText = std::move(desc);
    ret->m_size = std::clamp(size, fx::kClickTuneMin, fx::kClickTuneMax);
    ret->m_speed = std::clamp(speed, fx::kClickTuneMin, fx::kClickTuneMax);
    ret->m_onChange = std::move(onChange);
    ret->m_onTest = std::move(onTest);
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ClickEffectTunePopup::init() {
    if (!Popup::init(320.f, 226.f)) return false;

    this->setTitle(m_titleText);

    auto content = m_mainLayer->getContentSize();
    float cardW = content.width - 30.f;
    float innerW = kit::cardInnerWidth(cardW);

    auto push = [this] {
        if (m_onChange) m_onChange(m_size, m_speed);
    };

    auto* card = kit::makeCard(cardW, "Este efecto", {150, 220, 255}, {
        kit::makeSliderRow(innerW,
            "Tamano", "Multiplica el tamano solo de este efecto.",
            m_size, fx::kClickTuneMin, fx::kClickTuneMax,
            formatTune,
            [this, push](double value) {
                m_size = static_cast<float>(value);
                push();
            },
            &m_sizeSlider, &m_sizeLabel),
        kit::makeSliderRow(innerW,
            "Velocidad", "Que tan rapido y lejos sale este efecto.",
            m_speed, fx::kClickTuneMin, fx::kClickTuneMax,
            formatTune,
            [this, push](double value) {
                m_speed = static_cast<float>(value);
                push();
            },
            &m_speedSlider, &m_speedLabel),
        kit::makeButtonRow(innerW,
            "Restablecer", "Vuelve a x1.00 en los dos.", "Reset",
            [this] { resetToDefault(); }),
    });

    // El pie deja sitio al boton de probar, por eso el scroll no llega abajo.
    auto* stack = kit::makeScrollStack({cardW, content.height - 92.f}, {
        kit::makeHint(cardW, m_descText.c_str()),
        card,
    });
    stack->setPosition({15.f, 42.f});
    m_mainLayer->addChild(stack, 5);

    // Boton de probar en el pie: se ve el cambio sin cerrar el popup.
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    auto* testSpr = ButtonSprite::create("Probar");
    testSpr->setScale(0.6f);
    auto* testBtn = CCMenuItemExt::createSpriteExtra(
        testSpr, [this](CCMenuItemSpriteExtra*) { if (m_onTest) m_onTest(); });
    testBtn->setPosition({content.width / 2.f, 22.f});
    menu->addChild(testBtn);
    m_mainLayer->addChild(menu, 10);

    paimon::markDynamicPopup(this);
    return true;
}

void ClickEffectTunePopup::resetToDefault() {
    m_size = 1.f;
    m_speed = 1.f;

    if (m_sizeSlider)  m_sizeSlider->setValue(normTune(m_size));
    if (m_speedSlider) m_speedSlider->setValue(normTune(m_speed));
    if (m_sizeLabel)   m_sizeLabel->setString(formatTune(m_size).c_str());
    if (m_speedLabel)  m_speedLabel->setString(formatTune(m_speed).c_str());

    if (m_onChange) m_onChange(m_size, m_speed);
    if (m_onTest) m_onTest();
}
