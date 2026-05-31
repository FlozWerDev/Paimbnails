#pragma once

// ButtonCarousel.hpp — Carrusel de botones con flechas de navegacion.
//
// Cuando un menu acumula muchos botones en una sola fila/columna (los de GD
// + los que añaden mods), este componente muestra solo N botones a la vez
// (2 o 3) dentro de una ventana recortada y añade flechas prev/next. Cada
// click desplaza exactamente 1 boton con una animacion suave (ease in-out),
// nunca de golpe.
//
// Uso tipico:
//   auto carousel = paimon::ui::ButtonCarousel::wrapMenu(
//       leftMenu, Orientation::Vertical, /*visible*/ 3,
//       /*itemSize*/ 30.f, /*crossSize*/ 30.f);
//   // `carousel` ya contiene los botones que estaban en leftMenu.
//   parent->addChild(carousel);
//
// Notas de compatibilidad:
//  - Los CCMenuItem deben seguir siendo hijos de un CCMenu para recibir
//    clicks; el carrusel los reparenta a su propio CCMenu interno.
//  - El recorte usa ScissorClipNode (GL scissor) para no romper el batching.
//  - Los botones fuera de la ventana visible se deshabilitan (sin touch) y
//    se ocultan, asi que no roban clicks aunque el scissor solo recorte el
//    render.

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <vector>

namespace paimon::ui {

class ButtonCarousel : public cocos2d::CCNode {
public:
    enum class Orientation { Horizontal, Vertical };

    // arrowThreshold: numero minimo de botones para que aparezcan las
    // flechas. Por defecto 4 (con <4 botones se muestran todos sin flechas).
    static ButtonCarousel* create(
        Orientation orientation,
        int visibleCount,
        float itemSize,
        float crossSize,
        float gap = 6.f,
        float arrowSize = 18.f,
        int arrowThreshold = 4
    );

    // Añade un boton (debe ser CCMenuItem para recibir clicks). Llamar a
    // rebuild() despues de añadir todos.
    void addButton(cocos2d::CCMenuItem* item);
    void addButtons(std::vector<cocos2d::CCMenuItem*> const& items);

    // Mueve todos los CCMenuItem hijos de `source` a este carrusel,
    // preservando su orden.
    void absorbMenuItems(cocos2d::CCMenu* source);

    // Recalcula posiciones, ventana de recorte y estado de flechas.
    void rebuild();

    int buttonCount() const { return static_cast<int>(m_items.size()); }
    int maxOffset() const;
    void scrollToIndex(int offset, bool animated = true);

    // Conveniencia: crea un carrusel ya poblado con los items de `source`.
    // El caller debe añadir el carrusel al arbol donde estaba el menu.
    static ButtonCarousel* wrapMenu(
        cocos2d::CCMenu* source,
        Orientation orientation,
        int visibleCount,
        float itemSize,
        float crossSize,
        float gap = 6.f,
        float arrowSize = 18.f,
        int arrowThreshold = 4
    );

protected:
    bool init(Orientation orientation, int visibleCount,
              float itemSize, float crossSize, float gap, float arrowSize,
              int arrowThreshold);

    void onPrev(cocos2d::CCObject*);
    void onNext(cocos2d::CCObject*);
    void onScrollComplete();

    void animateTo(int newOffset);
    void updateArrowState();
    void updateButtonVisibility(int offset, int margin);
    void relayout();          // recalcula ventana/flechas segun cantidad de botones
    int  effectiveSlots() const;  // botones visibles efectivos (todos si < umbral)
    bool needsArrows() const;     // true si hay >= umbral botones

    float strideLen() const { return m_itemSize + m_gap; }
    float windowMain() const;                       // largo de la ventana en el eje
    cocos2d::CCPoint innerPosForOffset(int offset) const;
    cocos2d::CCPoint itemLocalPos(int index) const; // pos del centro del item i

    static void scaleToFit(cocos2d::CCNode* node, float target);

    Orientation m_orientation = Orientation::Horizontal;
    int   m_visibleCount = 3;
    int   m_arrowThreshold = 4;
    float m_itemSize  = 30.f;
    float m_crossSize = 30.f;
    float m_gap       = 6.f;
    float m_arrowSize = 18.f;
    float m_arrowGap  = 4.f;
    int   m_offset    = 0;
    bool  m_animating = false;

    cocos2d::CCClippingNode*   m_clip      = nullptr; // ScissorClipNode
    cocos2d::CCMenu*           m_innerMenu = nullptr; // contiene los botones
    cocos2d::CCMenu*           m_arrowMenu = nullptr; // contiene las flechas
    CCMenuItemSpriteExtra*     m_prevArrow = nullptr; // global namespace (binding GD)
    CCMenuItemSpriteExtra*     m_nextArrow = nullptr;
    std::vector<cocos2d::CCMenuItem*> m_items;
};

} // namespace paimon::ui
