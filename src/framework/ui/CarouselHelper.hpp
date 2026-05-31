#pragma once

// CarouselHelper.hpp — Utilidades para convertir un CCMenu existente con
// demasiados botones en un ButtonCarousel con flechas, en su sitio.
//
// Pensado para los menus de GD (left-menu / socials-menu de ProfilePage,
// left-side-menu de LevelInfoLayer, etc.) donde GD + otros mods acumulan
// muchos botones en una sola fila/columna.

#include <Geode/Geode.hpp>
#include "ButtonCarousel.hpp"

namespace paimon::ui {

class CarouselHelper {
public:
    // Reemplaza `menu` por un ButtonCarousel que contiene sus CCMenuItem.
    //
    //  - Solo actua si el menu tiene MAS de `visibleCount` botones (si caben
    //    todos, no hay nada que paginar y devuelve nullptr).
    //  - El carrusel hereda posicion / anchor / zOrder del menu original.
    //  - El menu original se oculta (setVisible(false)) en vez de destruirse,
    //    para no romper codigo que aun tenga referencias por ID.
    //
    // Devuelve el carrusel creado, o nullptr si no hizo falta / fallo.
    static ButtonCarousel* wrapInPlace(
        cocos2d::CCMenu* menu,
        ButtonCarousel::Orientation orientation,
        int visibleCount = 3,
        float itemSize = 30.f,
        float crossSize = 30.f,
        float gap = 6.f,
        float arrowSize = 16.f,
        int arrowThreshold = 4
    ) {
        if (!menu) return nullptr;

        // Contar CCMenuItem reales.
        int itemCount = 0;
        if (auto children = menu->getChildren()) {
            for (auto* node : geode::cocos::CCArrayExt<cocos2d::CCNode*>(children)) {
                if (geode::cast::typeinfo_cast<cocos2d::CCMenuItem*>(node)) ++itemCount;
            }
        }
        // Por debajo del umbral no hace falta carrusel (caben todos).
        if (itemCount < arrowThreshold) return nullptr;

        auto* parent = menu->getParent();
        if (!parent) return nullptr;

        // Evita doble-wrap: si ya hay un carrusel hermano con nuestro ID, salir.
        std::string carouselID = std::string(menu->getID()) + "-carousel";
        if (parent->getChildByID(carouselID)) return nullptr;

        auto pos    = menu->getPosition();
        auto anchor = menu->getAnchorPoint();
        int  zorder = menu->getZOrder();

        auto* carousel = ButtonCarousel::create(
            orientation, visibleCount, itemSize, crossSize, gap, arrowSize, arrowThreshold);
        if (!carousel) return nullptr;

        carousel->absorbMenuItems(menu);
        carousel->rebuild();

        carousel->setID(carouselID);
        carousel->setPosition(pos);
        carousel->setAnchorPoint(anchor);
        parent->addChild(carousel, zorder);

        // El menu original queda vacio y oculto; el codigo que lo referencie
        // por ID seguira encontrandolo (sin romper), solo que sin botones.
        menu->setVisible(false);

        return carousel;
    }
};

} // namespace paimon::ui
