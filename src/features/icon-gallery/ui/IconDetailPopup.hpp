#pragma once
// Ficha de un icono de la tienda: vista previa grande, datos del autor y los
// botones de descargar / equipar / quitar.

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include <functional>
#include <string>

namespace paimon::icon_gallery {

class IconDetailPopup : public geode::Popup {
public:
    // `onChanged` avisa a la rejilla para que repinte el sello de instalado.
    static IconDetailPopup* create(std::string slug, std::function<void()> onChanged);

protected:
    bool init(std::string slug, std::function<void()> onChanged);

    void buildStatic();
    void refresh();
    void rebuildActions();

    void onDownload();
    void onEquip();
    void onRemove();

    void setBusy(bool busy, char const* message = nullptr);
    void notifyChanged();

    // Espera a que el store termine de bajar el icon.json de este icono. La
    // ficha puede abrirse antes de que llegue (toque rapido, o descarga que
    // fallo), y sin esto se quedaria en "Cargando..." para siempre.
    void awaitMeta(float dt);

    std::string m_slug;
    std::function<void()> m_onChanged;
    bool m_busy = false;

    cocos2d::CCNode* m_previewBox = nullptr;
    cocos2d::CCSprite* m_preview = nullptr;
    cocos2d::CCNode* m_spinner = nullptr;
    cocos2d::CCNode* m_infoHost = nullptr;
    cocos2d::CCNode* m_actionHost = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
};

}  // namespace paimon::icon_gallery
