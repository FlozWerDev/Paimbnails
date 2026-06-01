#pragma once

#include <Geode/ui/Popup.hpp>
#include <Geode/ui/LazySprite.hpp>
#include <Geode/utils/cocos.hpp>
#include <map>
#include <string>

// ModPreviewGalleryPopup — visor a pantalla completa de las previews de un mod,
// con navegación anterior/siguiente y contador. Port de ImagePopup (Mod-Previews
// de Alphalaneous) a la API de geode::Popup de Geode v5.

namespace paimon::mod_previews {

class ModPreviewGalleryPopup : public geode::Popup {
public:
    // page: imagen inicial (1-based). count: total de previews. urlBase: prefijo sin "{n}.png".
    static ModPreviewGalleryPopup* create(int page, int count, std::string urlBase);

protected:
    int m_page = 1;
    int m_count = 1;
    std::string m_urlBase;
    geode::LazySprite* m_current = nullptr;
    cocos2d::CCLabelBMFont* m_label = nullptr;
    std::map<int, geode::Ref<geode::LazySprite>> m_cache;

    bool init(int page, int count, std::string urlBase);
    void showImage(int page);
    void onLoad(geode::LazySprite* spr);
    void onPrev(cocos2d::CCObject*);
    void onNext(cocos2d::CCObject*);
};

} // namespace paimon::mod_previews
