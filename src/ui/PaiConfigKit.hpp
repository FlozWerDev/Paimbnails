#pragma once

// Kit compartido para los popups de configuracion "amigables".
// Filas con titulo + descripcion, valores siempre visibles y tarjetas por
// seccion, para que cualquier popup del mod se lea igual y sea facil de
// entender sin perder opciones.

#include <Geode/Geode.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <functional>
#include <string>
#include <vector>

namespace paimon::configkit {

// Paleta compartida
constexpr cocos2d::ccColor3B kCardColor  = {14, 18, 32};
constexpr GLubyte            kCardAlpha  = 145;
constexpr cocos2d::ccColor3B kTitleColor = {255, 255, 255};
constexpr cocos2d::ccColor3B kDescColor  = {166, 176, 198};
constexpr cocos2d::ccColor3B kValueColor = {255, 222, 120};
constexpr cocos2d::ccColor3B kOnColor    = {120, 255, 140};
constexpr cocos2d::ccColor3B kOffColor   = {150, 155, 170};

// Ancho util de las filas dentro de una tarjeta de ancho `cardWidth`.
constexpr float cardInnerWidth(float cardWidth) { return cardWidth - 20.f; }

// Todas las filas devuelven un CCNode con anchor {0,0} y contentSize
// {width, alto propio}; se apilan con makeCard() / makeScrollStack().

// Interruptor con titulo y descripcion opcional.
cocos2d::CCNode* makeToggleRow(
    float width,
    char const* title, char const* desc,
    bool value,
    std::function<void(bool)> onChange,
    CCMenuItemToggler** outToggle = nullptr);

// Slider con el valor siempre visible encima del control.
cocos2d::CCNode* makeSliderRow(
    float width,
    char const* title, char const* desc,
    double value, double minV, double maxV,
    std::function<std::string(double)> format,
    std::function<void(double)> onChange,
    Slider** outSlider = nullptr,
    cocos2d::CCLabelBMFont** outValue = nullptr);

// Selector `<  valor  >` que cicla entre opciones.
cocos2d::CCNode* makeSelectRow(
    float width,
    char const* title, char const* desc,
    std::vector<std::string> options, int index,
    std::function<void(int)> onChange,
    cocos2d::CCLabelBMFont** outLabel = nullptr);

// Boton de accion a la derecha, con titulo y descripcion a la izquierda.
cocos2d::CCNode* makeButtonRow(
    float width,
    char const* title, char const* desc,
    char const* buttonText,
    std::function<void()> onPress);

// Nota informativa (texto envuelto, gris).
cocos2d::CCNode* makeHint(float width, char const* text);

// Tarjeta: panel redondeado con titulo de color + filas apiladas.
// Las filas deben crearse con width = cardInnerWidth(width).
cocos2d::CCNode* makeCard(
    float width,
    char const* title, cocos2d::ccColor3B accent,
    std::vector<cocos2d::CCNode*> const& rows);

// Interruptor grande de encendido con estado en texto (Activado/Desactivado).
// `outStateLabel` permite sincronizar el texto de estado si el toggle se
// cambia por codigo (toggle() no dispara el callback).
cocos2d::CCNode* makeHeroToggle(
    float width,
    char const* title, char const* desc,
    bool value,
    std::function<void(bool)> onChange,
    CCMenuItemToggler** outToggle = nullptr,
    cocos2d::CCLabelBMFont** outStateLabel = nullptr);

// Actualiza el texto/color de una etiqueta de estado creada por makeHeroToggle.
void setHeroStateLabel(cocos2d::CCLabelBMFont* label, bool on);

// Apila tarjetas/filas (de arriba hacia abajo) en un ScrollLayer del tamano
// dado y hace scroll al inicio.
geode::ScrollLayer* makeScrollStack(
    cocos2d::CCSize size,
    std::vector<cocos2d::CCNode*> const& items,
    float gap = 8.f);

} // namespace paimon::configkit
