#pragma once
// La rejilla del hub del icon kit. Los botones no se duplican: se los pide
// prestados al carril oculto del garage y se los devuelve al cerrar.

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include <vector>

class GJGarageLayer;

namespace paimon::garage_hub::ui {

class GarageHubPopup : public geode::Popup {
public:
    static GarageHubPopup* create(GJGarageLayer* garage);

protected:
    // Un boton prestado, con el destino original que hay que devolverle: el
    // popup se pone de intermediario para poder cerrarse antes de disparar.
    struct Borrowed {
        geode::Ref<cocos2d::CCMenuItem> button;
        cocos2d::CCObject* listener = nullptr;
        cocos2d::SEL_MenuHandler selector = nullptr;
    };

    ~GarageHubPopup() override;

    bool init(GJGarageLayer* garage);
    void onClose(cocos2d::CCObject* sender) override;

    void borrow(cocos2d::CCMenuItem* btn, cocos2d::CCPoint const& spot);
    void giveButtonsBack();
    void onEntry(cocos2d::CCObject* sender);

    geode::Ref<GJGarageLayer> m_garage;
    std::vector<Borrowed> m_borrowed;
};

}  // namespace paimon::garage_hub::ui
