#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <vector>
#include <string>

namespace paimon::quickhub {

// ─────────────────────────────────────────────────────────────────────────────
// RadialConfigPopup
// Popup para configurar que opciones aparecen en el Quick Hub Radial y en que
// orden. Interfaz con preview del circulo + lista reordenable.
//
// Layout:
//   ┌──────────────────────────────────────────────────────────┐
//   │  Configurar Quick Hub                             [Reset] │
//   ├────────────────────────┬─────────────────────────────────┤
//   │                        │                                 │
//   │   PREVIEW CIRCULAR     │   LISTA DE OPCIONES             │
//   │   (muestra como se     │   [↑] [Icono] Nombre [↓] [✕]   │
//   │    vera el radial)     │   [↑] [Icono] Nombre [↓] [✕]   │
//   │                        │   ...                           │
//   │                        │                                 │
//   │                        │   ── Disponibles ──             │
//   │                        │   [+] Icono Nombre              │
//   │                        │   [+] Icono Nombre              │
//   │                        │                                 │
//   ├────────────────────────┴─────────────────────────────────┤
//   │                    [Guardar]  [Cancelar]                  │
//   └──────────────────────────────────────────────────────────┘
// ─────────────────────────────────────────────────────────────────────────────

class RadialConfigPopup : public geode::Popup {
public:
    static RadialConfigPopup* create();

protected:
    bool init() override;

    // Datos de trabajo (copia editable)
    std::vector<std::string> m_activeIds;

    // Nodos UI
    cocos2d::CCNode* m_previewNode = nullptr;
    geode::ScrollLayer* m_scrollLayer = nullptr;

    // Reconstruye la lista de opciones activas + disponibles
    void rebuildList();

    // Reconstruye el preview circular
    void rebuildPreview();

    // Acciones
    void onMoveUp(int idx);
    void onMoveDown(int idx);
    void onRemoveOption(int idx);
    void onAddOption(int availIdx);
    void onSave(cocos2d::CCObject*);
    void onReset(cocos2d::CCObject*);
};

} // namespace paimon::quickhub
