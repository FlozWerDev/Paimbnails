#pragma once

// UIHelpers.hpp — Funciones de fabrica para componentes UI reutilizables.
// Reduce la repeticion de codigo de creacion de UI entre popups y capas.

#include <Geode/Geode.hpp>
#include "../../utils/SpriteHelper.hpp"

using namespace geode::prelude;

namespace paimon::ui {

// Crea un menu horizontal de pie de pagina (fila inferior de popup).
inline cocos2d::CCMenu* makeFooterMenu(
    std::initializer_list<cocos2d::CCNode*> items,
    float gap = 10.f
) {
    auto menu = cocos2d::CCMenu::create();
    menu->setContentSize({0, 0});
    menu->setLayout(
        RowLayout::create()
            ->setGap(gap)
            ->setAxisAlignment(AxisAlignment::Center)
    );

    for (auto* item : items) {
        if (item) menu->addChild(item);
    }

    menu->updateLayout();
    return menu;
}

// Crea un boton circular de icono (estilo GJ: circulo con sprite adentro).
inline CCMenuItemSpriteExtra* makeCircleIconButton(
    char const* spriteName,
    cocos2d::CCObject* target,
    cocos2d::SEL_MenuHandler callback,
    float scale = 1.0f
) {
    auto spr = paimon::SpriteHelper::safeCreateWithFrameName(spriteName);
    if (!spr) spr = paimon::SpriteHelper::safeCreate(spriteName);
    if (!spr) return nullptr;

    spr->setScale(scale);

    auto btn = CCMenuItemSpriteExtra::create(spr, target, callback);
    return btn;
}

// Agrega un hijo a un menu y actualiza el layout.
inline void addToMenuAndUpdate(cocos2d::CCMenu* menu, cocos2d::CCNode* child) {
    if (!menu || !child) return;
    menu->addChild(child);
    menu->updateLayout();
}

// Aspect-fill: calcula la escala para que un sprite llene un area sin deformar.
inline float aspectFillScale(cocos2d::CCSize spriteSize, cocos2d::CCSize targetSize) {
    if (spriteSize.width <= 0 || spriteSize.height <= 0) return 1.f;
    float sx = targetSize.width  / spriteSize.width;
    float sy = targetSize.height / spriteSize.height;
    return std::max(sx, sy);
}

// Aspect-fit: calcula la escala para que un sprite quepa en un area sin deformar.
inline float aspectFitScale(cocos2d::CCSize spriteSize, cocos2d::CCSize targetSize) {
    if (spriteSize.width <= 0 || spriteSize.height <= 0) return 1.f;
    float sx = targetSize.width  / spriteSize.width;
    float sy = targetSize.height / spriteSize.height;
    return std::min(sx, sy);
}

// Normaliza un icono sprite para encajar en targetSize x targetSize.
inline void normalizeIconSprite(cocos2d::CCSprite* spr, float targetSize) {
    if (!spr) return;
    auto cs = spr->getContentSize();
    float maxDim = std::max(cs.width, cs.height);
    if (maxDim > 0.f) spr->setScale(targetSize / maxDim);
}

// Crea un titulo de seccion (goldFont, centrado).
inline cocos2d::CCLabelBMFont* makeSectionTitle(
    char const* text, float scale = 0.45f
) {
    auto label = cocos2d::CCLabelBMFont::create(text, "goldFont.fnt");
    if (label) label->setScale(scale);
    return label;
}

// Crea un panel oscuro usando SpriteHelper.
inline cocos2d::CCNode* makeDarkPanel(float width, float height, unsigned char alpha = 80) {
    return paimon::SpriteHelper::createDarkPanel(width, height, alpha);
}

} // namespace paimon::ui
