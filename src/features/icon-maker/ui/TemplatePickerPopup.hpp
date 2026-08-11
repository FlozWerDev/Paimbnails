#pragma once
// Selector de icono oficial como plantilla: grid paginado de SimplePlayers
// del gamemode dado; devuelve el id elegido.

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include <functional>

namespace paimon::icon_maker {

class TemplatePickerPopup : public geode::Popup {
public:
    using PickedCallback = std::function<void(int iconId)>;

    static TemplatePickerPopup* create(IconType type, PickedCallback onPicked);

protected:
    bool init(IconType type, PickedCallback onPicked);

    void rebuildPage();
    int iconCount() const;

    IconType m_type = IconType::Cube;
    PickedCallback m_onPicked;
    cocos2d::CCNode* m_gridArea = nullptr;
    cocos2d::CCLabelBMFont* m_pageLabel = nullptr;
    int m_page = 0;
};

}  // namespace paimon::icon_maker
