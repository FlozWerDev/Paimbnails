#pragma once
// La explicacion del modelo mental del creador, en un solo sitio: que es una
// zona, que es una capa y que hace cada ajuste raro. El editor enlaza aqui
// desde el boton "?" en vez de llenar la interfaz de texto.

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

namespace paimon::icon_maker {

class IconHelpPopup : public geode::Popup {
public:
    enum class Topic { Basics = 0, Paint = 1, Export = 2 };

    static IconHelpPopup* create(Topic topic = Topic::Basics);

protected:
    bool init(Topic topic);
    void showTopic(Topic topic);

    cocos2d::CCNode* m_body = nullptr;
};

}  // namespace paimon::icon_maker
