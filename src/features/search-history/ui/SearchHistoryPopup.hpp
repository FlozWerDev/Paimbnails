#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/General.hpp>
#include <functional>

// Popup que lista el historial de busqueda. Cada entrada puede usarse para
// rellenar de nuevo el formulario de busqueda (callback con el indice) o
// eliminarse. Boton "Clear" para vaciar todo.
class SearchHistoryPopup : public geode::Popup {
public:
    // El callback recibe el indice de la entrada elegida en
    // paimon::searchhistory::history.
    static SearchHistoryPopup* create(std::function<void(int)> callback);

protected:
    std::function<void(int)> m_callback;
    geode::ScrollLayer* m_scroll = nullptr;

    bool init(std::function<void(int)> callback);
    void rebuild();
    void onSearchEntry(cocos2d::CCObject*);
    void onRemoveEntry(cocos2d::CCObject*);
    void onClear(cocos2d::CCObject*);
};
