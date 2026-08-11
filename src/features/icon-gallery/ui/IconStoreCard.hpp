#pragma once
// Tarjeta de la rejilla: vista previa, nombre, autor y sello de instalado.
//
// La tarjeta nace "vacia" (solo sabe su slug) y se rellena sola cuando el
// GalleryStore termina de bajar el .gdicon de ese icono. Asi la rejilla
// aparece entera al instante y se va poblando.

#include <Geode/Geode.hpp>

#include <functional>
#include <string>

namespace paimon::icon_gallery {

class IconStoreCard final : public cocos2d::CCNodeRGBA {
public:
    static IconStoreCard* create(std::string slug, float width, float height,
                                 std::function<void(std::string const&)> onPress);

    // Relee el estado del store y repinta. Barato: no reconstruye la tarjeta.
    void refresh();

    std::string const& slug() const { return m_slug; }

private:
    bool init(std::string slug, float width, float height,
              std::function<void(std::string const&)> onPress);

    void buildFrame();
    void showPreview(cocos2d::CCTexture2D* texture);
    void setLoading(bool loading);

    // Centro de m_previewBox en coordenadas de sus hijos. Los hijos de un
    // CCNode se sitúan desde su esquina inferior izquierda (el anchorPoint
    // mueve el nodo, no su sistema de coordenadas), asi que {0,0} caeria
    // fuera de la tarjeta.
    cocos2d::CCPoint boxCenter() const;

    std::string m_slug;
    float m_width = 0.f;
    float m_height = 0.f;
    bool m_metaShown = false;
    bool m_previewShown = false;

    cocos2d::CCNode* m_previewBox = nullptr;   // caja donde va la vista previa
    cocos2d::CCSprite* m_preview = nullptr;
    cocos2d::CCNode* m_spinner = nullptr;
    cocos2d::CCLabelBMFont* m_name = nullptr;
    cocos2d::CCLabelBMFont* m_author = nullptr;
    cocos2d::CCLabelBMFont* m_typeBadge = nullptr;
    cocos2d::CCNode* m_typeBadgeBg = nullptr;
    cocos2d::CCSprite* m_installedMark = nullptr;
};

}  // namespace paimon::icon_gallery
