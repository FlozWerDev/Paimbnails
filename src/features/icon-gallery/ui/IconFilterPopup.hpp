#pragma once
// Filtros de la tienda: gamemodes (multiseleccion) y orden.
//
// Van en un popup y no en la cabecera porque nueve gamemodes mas el orden no
// caben en una fila en 4:3 sin dejar la rejilla en un pasillo.

#include "../IconGalleryTypes.hpp"
#include "../services/GalleryStore.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include <functional>

namespace paimon::icon_gallery {

class IconFilterPopup : public geode::Popup {
public:
    using ApplyCallback = std::function<void(GalleryStore::Query const&)>;

    static IconFilterPopup* create(GalleryStore::Query current, ApplyCallback onApply);

protected:
    bool init(GalleryStore::Query current, ApplyCallback onApply);

    void buildTypes(float width, float top);
    void buildSort(float width, float top);
    void apply();

    GalleryStore::Query m_query;
    ApplyCallback m_onApply;
    cocos2d::CCNode* m_sortHost = nullptr;
};

}  // namespace paimon::icon_gallery
