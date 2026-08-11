#pragma once
// Barra de opciones de la tienda: botones del juego verdes que pasan a
// celeste al elegirlos.
//
// Misma API que gdkit::makeTabBar, pero con caras de boton (GJ_button_01 /
// GJ_button_04) en vez de las pestanas del juego.

#include <Geode/Geode.hpp>

#include <functional>
#include <string>
#include <vector>

namespace paimon::icon_gallery::ui {

constexpr float kSegmentedBarHeight = 30.f;

// Devuelve un CCNode con anchor {0,0} y contentSize {width, kSegmentedBarHeight}.
cocos2d::CCNode* makeSegmentedBar(
    float width,
    std::vector<std::string> const& labels,
    int selected,
    std::function<void(int)> onSelect);

}  // namespace paimon::icon_gallery::ui
