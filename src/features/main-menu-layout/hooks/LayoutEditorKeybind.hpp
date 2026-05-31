#pragma once

#include "../services/MainMenuLayoutManager.hpp"
#include "../ui/MainMenuLayoutEditor.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>

namespace paimon::menu_layout {

/// Devuelve true si `layer` es el CCLayer interactivo mas arriba en la
/// escena actual. Se usa para evitar abrir el editor de layout cuando hay
/// popups o capas modales encima del layer hookeado.
inline bool isTopInteractiveLayer(cocos2d::CCLayer* layer) {
    if (!layer) return false;
    auto scene = cocos2d::CCDirector::get()->getRunningScene();
    if (!scene) return false;
    auto children = scene->getChildren();
    if (!children) return false;
    for (int i = static_cast<int>(children->count()) - 1; i >= 0; --i) {
        auto* node = geode::cast::typeinfo_cast<cocos2d::CCNode*>(children->objectAtIndex(i));
        if (!node || !node->isVisible()) continue;
        auto* topLayer = geode::cast::typeinfo_cast<cocos2d::CCLayer*>(node);
        if (!topLayer) continue;
        return topLayer == layer;
    }
    return false;
}

/// Registra el keybind del editor de layout en `layer`. Solo debe llamarse
/// desde escenas soportadas (main menu y pause menu).
inline void registerLayoutEditorKeybind(cocos2d::CCLayer* layer) {
    if (!layer) return;

    layer->addEventListener(
        geode::KeybindSettingPressedEventV3(geode::Mod::get(), "main-menu-layout-keybind"),
        [layer](geode::Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat || !layer->isRunning()) return;
            if (!layer->getParent()) return;

            if (auto* active = MainMenuLayoutEditor::getActive()) {
                if (active->getTargetRoot() == layer) {
                    active->saveAndClose();
                }
                return;
            }

            if (!isTopInteractiveLayer(layer)) return;

            auto scene = cocos2d::CCDirector::get()->getRunningScene();
            if (!scene) return;

            MainMenuLayoutManager::get().captureDefaultsAndApply(layer);
            MainMenuLayoutEditor::open(layer);
        }
    );
}

} // namespace paimon::menu_layout
