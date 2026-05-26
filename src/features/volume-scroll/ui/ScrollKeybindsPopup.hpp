#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>

#include <string>
#include <vector>

namespace paimon::volscroll {

// ─────────────────────────────────────────────────────────────────────────────
// ScrollKeybindsPopup
//
// Hub central de atajos de teclado del mod. Reune en un solo popup todos los
// keybinds del mod (volume scroll, captura, pause zoom, panel de settings,
// layout editor, level search) usando filas custom que delegan en
// `ExtendedKeybindEditPopup` para la edicion.
//
// El popup custom soporta NO solo teclas del teclado y combinaciones (Ctrl+1)
// sino tambien botones del mouse (left/right/middle/side) y, para keybinds
// que no son hold-while-scroll, tambien scroll wheel up/down.
//
// Para los keybinds del scroll de volumen el binding extendido es de tipo
// "hold mouse button while scrolling" — el scroll mismo es la accion, el
// botoncito del mouse hace de modificador. allowScroll=false en la creacion
// del popup oculta la opcion de scroll bind.
//
// Se abre desde el overlay de "Capturar Pantalla" (CaptureOverlay) mediante
// el menu contextual disponible al hacer click derecho en el juego.
// ─────────────────────────────────────────────────────────────────────────────

class ScrollKeybindsPopup : public geode::Popup {
public:
    static ScrollKeybindsPopup* create();

protected:
    bool init() override;
    void onExit() override;

    // ScrollLayer que contiene todas las filas (headers + keybinds).
    geode::ScrollLayer* m_scrollLayer = nullptr;

    // Lista de SettingNodeV3 que renderizamos para los keybinds nativos —
    // hoy en dia esta lista queda vacia porque construimos filas propias,
    // pero se mantiene por si en el futuro queremos volver a mezclar UI
    // nativa con UI custom.
    std::vector<cocos2d::CCNode*> m_keybindNodes;

    // Builders
    cocos2d::CCNode* makeSectionHeader(char const* title, float width);

    // Versiones de la fila — la primera mantiene compatibilidad con el codigo
    // viejo que pasaba solo settingKey, la segunda es la nueva con
    // displayName + flag para permitir scroll wheel como bind.
    cocos2d::CCNode* makeKeybindRow(char const* settingKey, float width);
    cocos2d::CCNode* makeKeybindRow(
        char const* settingKey,
        char const* displayName,
        float width,
        bool allowScroll
    );

    // Abre el ExtendedKeybindEditPopup para editar el binding asociado a
    // `settingKey`. Cuando el usuario guarda, se actualiza el label de la
    // fila pasado como `labelToRefresh`.
    void openEditPopup(
        std::string settingKey,
        std::string displayName,
        bool allowScroll,
        cocos2d::CCLabelBMFont* labelToRefresh
    );

    // Reset de los 4 keybinds del scroll de volumen a sus defaults +
    // limpieza de los extended binds asociados.
    void onResetVolumeDefaults(cocos2d::CCObject*);

    // Helper interno para reabrir el popup tras un reset (refresca toda la UI).
    void reopenAfterReset(float);
};

} // namespace paimon::volscroll
