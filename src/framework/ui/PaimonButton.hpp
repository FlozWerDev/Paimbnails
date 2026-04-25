#pragma once

// PaimonButton.hpp — Fabrica de botones del mod con auto-registro.
// Evita que cada hook repita el patron de crear CCMenuItemSpriteExtra
// + registrar en PaimonButtonHighlighter + guardar escala original.

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include "../../utils/SpriteHelper.hpp"

using namespace geode::prelude;

namespace paimon::ui {

class PaimonButton {
public:
    // Crea un boton registrado como boton del mod.
    // Almacena la escala original en el tag del sprite (CCMenuItemSpriteExtra
    // no tiene campo de usuario para esto, asi que usamos el ID con prefijo).
    static CCMenuItemSpriteExtra* create(
        cocos2d::CCNode* sprite,
        cocos2d::CCObject* target,
        cocos2d::SEL_MenuHandler callback
    ) {
        auto btn = CCMenuItemSpriteExtra::create(sprite, target, callback);
        if (!btn) return nullptr;

        registerAsPaimon(btn);
        return btn;
    }

    // Crea un boton a partir de un sprite frame name.
    static CCMenuItemSpriteExtra* createFromSprite(
        char const* spriteName,
        cocos2d::CCObject* target,
        cocos2d::SEL_MenuHandler callback,
        float scale = 1.0f
    ) {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName(spriteName);
        if (!spr) {
            spr = paimon::SpriteHelper::safeCreate(spriteName);
        }
        if (!spr) return nullptr;

        spr->setScale(scale);
        return create(spr, target, callback);
    }

    // Registra un boton ya existente como boton de Paimon (para retrocompatibilidad).
    static void registerAsPaimon(CCMenuItemSpriteExtra* btn) {
        if (!btn) return;
        std::string currentID = btn->getID();
        if (!currentID.starts_with("paimon-mod-btn")) {
            btn->setID(currentID.empty()
                ? "paimon-mod-btn"
                : ("paimon-mod-btn-" + currentID));
        }
    }

    // Comprueba si un boton es un boton de Paimon.
    static bool isPaimonButton(CCMenuItemSpriteExtra* btn) {
        if (!btn) return false;
        return btn->getID().starts_with("paimon-mod-btn");
    }
};

} // namespace paimon::ui
