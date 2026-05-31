#pragma once

// EditorContext.hpp — Fuente unica de verdad para detectar el editor.
//
// El mod debe AISLARSE POR COMPLETO del editor: ningun hook amplio (captura de
// posicion de botones, animaciones/blur de popups, skin de sliders, etc.) debe
// alterar comportamiento mientras el editor esta activo. Detectamos el editor
// por la escena en ejecucion (no por typeid de padres, que es fragil cuando
// otros mods hacen $modify de las clases del editor).

#include <Geode/Geode.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>

namespace paimon {

inline bool isEditorScene() {
    auto* director = cocos2d::CCDirector::get();
    if (!director) return false;
    auto* scene = director->getRunningScene();
    if (!scene) return false;
    return scene->getChildByType<LevelEditorLayer>(0) != nullptr ||
           scene->getChildByType<EditorUI>(0) != nullptr;
}

} // namespace paimon
