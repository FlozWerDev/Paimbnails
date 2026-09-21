#pragma once
// One hidden rail in the icon kit lends buttons to the hub popup, instead of
// stacking them in the garage column.

#include <Geode/Geode.hpp>
#include <Geode/binding/GJGarageLayer.hpp>

#include <string>
#include <vector>

namespace paimon::garage_hub {

// Guarda el boton en el carril del layer. `label` es el texto que sale bajo el
// icono en el popup y `order` decide el sitio en la fila (menor va antes).
void addButton(GJGarageLayer* layer, cocos2d::CCMenuItem* btn, std::string const& label, int order);

// El carril oculto del layer, o null si nadie ha registrado nada todavia.
cocos2d::CCMenu* rail(GJGarageLayer* layer);

// Los botones registrados, ya ordenados como los pinta el popup.
std::vector<cocos2d::CCMenuItem*> entries(GJGarageLayer* layer);

std::string labelOf(cocos2d::CCNode* btn);

// Pone el unico boton visible, el que abre el popup del hub.
void installHubButton(GJGarageLayer* layer);

}  // namespace paimon::garage_hub
