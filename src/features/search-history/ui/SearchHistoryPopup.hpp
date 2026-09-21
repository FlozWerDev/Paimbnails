#pragma once

#include <Geode/Geode.hpp>
#include <functional>

// Historial de busqueda: tocar relanza, X elimina, "Clear" vacia.
class SearchHistoryPopup : public geode::Popup {
public:
    // El callback recibe el indice de la entrada elegida en
    // paimon::searchhistory::history.
    static SearchHistoryPopup* create(std::function<void(int)> callback);

protected:
    std::function<void(int)> m_callback;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;

    bool init(std::function<void(int)> callback);
    void rebuild();
    void onSearchEntry(cocos2d::CCObject*);
    void onRemoveEntry(cocos2d::CCObject*);
    void onClear(cocos2d::CCObject*);
};
