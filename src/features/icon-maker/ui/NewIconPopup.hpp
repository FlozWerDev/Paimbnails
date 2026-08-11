#pragma once
// Crear un icono en una sola pantalla: gamemode, nombre y de que se parte
// (lienzo vacio o la forma de un icono oficial). Devuelve el id creado.

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#include <functional>
#include <string>
#include <vector>

class SimplePlayer;

namespace paimon::icon_maker {

class NewIconPopup : public geode::Popup {
public:
    using CreatedCallback = std::function<void(std::string const& slotId)>;

    static NewIconPopup* create(CreatedCallback onCreated);

protected:
    bool init(CreatedCallback onCreated);

    void selectType(IconType type);
    void setStartFromTemplate(bool fromTemplate);
    void pickTemplate();
    void refreshStartRow();
    void onCreate();

    CreatedCallback m_onCreated;
    geode::TextInput* m_nameInput = nullptr;
    IconType m_selectedType = IconType::Cube;

    bool m_fromTemplate = true;
    int m_templateIcon = 1;

    struct TypeButton {
        IconType type;
        cocos2d::CCPoint position;
    };
    std::vector<TypeButton> m_typeButtons;

    geode::Ref<cocos2d::CCNode> m_typeSelector = nullptr;
    cocos2d::CCLabelBMFont* m_typeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_startLabel = nullptr;
    cocos2d::CCNode* m_templateRow = nullptr;
    SimplePlayer* m_templatePreview = nullptr;
};

}  // namespace paimon::icon_maker
