#include "PaiDrawIcon.hpp"

#include "../../utils/SpriteHelper.hpp"
#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace paidraw {

// El icono de PaiDraw usa exclusivamente assets de Geometry Dash:
// `GJ_paintBtn_001.png` (la cápsula de pintura del editor) sobre un
// `GJ_starsIcon_001.png` opcional como halo. Sin texturas custom,
// sin morados PaimonDraw — todo lo que GD ya tiene en GameSheet.
//
// Devuelve un CCNode listo para insertarse en cualquier menú; el
// llamador puede ajustar escala con setScale.
cocos2d::CCNode* createPaiDrawIcon(float targetSize) {
    auto* container = cocos2d::CCNode::create();
    container->setContentSize({targetSize, targetSize});
    container->setAnchorPoint({0.5f, 0.5f});

    // Halo: estrella GD en blanco para dar el "punch" visual sin
    // depender de colores custom. Si la estrella no carga, no pasa
    // nada — el botón de pintura por sí solo ya luce GD.
    if (auto* halo = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png")) {
        float maxDim = std::max(halo->getContentSize().width, halo->getContentSize().height);
        if (maxDim > 0.f) halo->setScale(targetSize / maxDim);
        halo->setOpacity(160);
        halo->setPosition({targetSize / 2.f, targetSize / 2.f});
        container->addChild(halo, 0);
    }

    // Icono principal: cápsula de pintura de GD. Es el frame canónico
    // para "dibujar/pintar" en el juego (lo usan ProfileBgPicker,
    // CommentBgSettings, ForYouPreferences, etc.).
    if (auto* paint = paimon::SpriteHelper::safeCreateWithFrameName("GJ_paintBtn_001.png")) {
        float maxDim = std::max(paint->getContentSize().width, paint->getContentSize().height);
        if (maxDim > 0.f) paint->setScale((targetSize * 0.85f) / maxDim);
        paint->setPosition({targetSize / 2.f, targetSize / 2.f});
        container->addChild(paint, 1);
    } else if (auto* fallback = paimon::SpriteHelper::safeCreateWithFrameName("GJ_colorBtn_001.png")) {
        // Fallback: el color picker de GD si no existe paintBtn.
        float maxDim = std::max(fallback->getContentSize().width, fallback->getContentSize().height);
        if (maxDim > 0.f) fallback->setScale((targetSize * 0.85f) / maxDim);
        fallback->setPosition({targetSize / 2.f, targetSize / 2.f});
        container->addChild(fallback, 1);
    } else {
        // Último recurso: una "P" en goldFont si no carga ningún frame.
        auto* label = cocos2d::CCLabelBMFont::create("P", "goldFont.fnt");
        label->setScale(targetSize / std::max(label->getContentSize().height, 1.f) * 0.7f);
        label->setPosition({targetSize / 2.f, targetSize / 2.f});
        container->addChild(label, 1);
    }
    return container;
}

} // namespace paidraw
