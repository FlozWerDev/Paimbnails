#pragma once
#include <Geode/Geode.hpp>
#include <vector>
#include <string>

namespace paimon::quickhub {

// Popup para configurar que opciones aparecen en el Quick Hub Radial y en que
// orden.
//
// Layout:
// Configurar Quick Hub
// [ vista previa de la rueda ]   [ Activos | Anadir ]
// [ abrir con Ctrl           ]   [ lista reordenable ]
//                    [Reset] [Guardar]

class RadialConfigPopup : public geode::Popup {
public:
    static RadialConfigPopup* create();

protected:
    bool init() override;

    // Datos de trabajo (copia editable)
    std::vector<std::string> m_activeIds;
    int m_tab = 0;

    cocos2d::CCNode* m_previewNode = nullptr;
    geode::ScrollLayer* m_scrollLayer = nullptr;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;

    // Reconstruye la lista segun la pestana activa
    void rebuildList();

    // Reconstruye el preview circular
    void rebuildPreview();

    void setTab(int tab);

    void onMoveUp(int idx);
    void onMoveDown(int idx);
    void onRemoveOption(int idx);
    void onAddOption(std::string const& id);
    void onEditCustom(std::string const& id);
    void onDeleteCustom(std::string const& id);
    void onSave(cocos2d::CCObject*);
    void onReset(cocos2d::CCObject*);
};

} // namespace paimon::quickhub
