#pragma once

// ButtonCarousel.hpp — Button carousel with navigation arrows.
//
// When a menu accumulates many buttons in one row/column (GD's plus those added
// by mods), this shows only N at a time inside a clipped window with prev/next
// arrows. Each click scrolls exactly one button with an ease in-out animation.
//
// Usage:
//   auto carousel = paimon::ui::ButtonCarousel::wrapMenu(
//       leftMenu, Orientation::Vertical, /*visible*/ 3,
//       /*itemSize*/ 30.f, /*crossSize*/ 30.f);
//   parent->addChild(carousel);
//
// Compatibility notes:
//  - CCMenuItems must stay children of a CCMenu to receive clicks; the carousel
//    reparents them to its own inner CCMenu.
//  - Clipping uses ScissorClipNode (GL scissor) to avoid breaking batching.
//  - Buttons outside the visible window are disabled and hidden so they don't
//    steal clicks (scissor only clips render).

#include <Geode/Geode.hpp>
#include <vector>

namespace paimon::ui {

class ButtonCarousel : public cocos2d::CCNode {
public:
    enum class Orientation { Horizontal, Vertical };

    // arrowThreshold: minimum buttons before arrows appear (default 4; below that, all show without arrows).
    static ButtonCarousel* create(
        Orientation orientation,
        int visibleCount,
        float itemSize,
        float crossSize,
        float gap = 6.f,
        float arrowSize = 18.f,
        int arrowThreshold = 4
    );

    // Add a button (must be a CCMenuItem to receive clicks). Call rebuild() after adding all.
    void addButton(cocos2d::CCMenuItem* item);
    void addButtons(std::vector<cocos2d::CCMenuItem*> const& items);

    // Move all CCMenuItem children of `source` into this carousel, preserving order.
    void absorbMenuItems(cocos2d::CCMenu* source);

    // Recompute positions, clip window, and arrow state.
    void rebuild();

    int buttonCount() const { return static_cast<int>(m_items.size()); }
    int maxOffset() const;
    void scrollToIndex(int offset, bool animated = true);

    // Convenience: create a carousel pre-populated with `source`'s items.
    // Caller must add the carousel where the menu was.
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
    void relayout();          // recompute window/arrows from button count
    int  effectiveSlots() const;  // effective visible buttons (all if below threshold)
    bool needsArrows() const;     // true if there are >= threshold buttons

    float strideLen() const { return m_itemSize + m_gap; }
    float windowMain() const;                       // window length along the axis
    cocos2d::CCPoint innerPosForOffset(int offset) const;
    cocos2d::CCPoint itemLocalPos(int index) const; // center pos of item i

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
    cocos2d::CCMenu*           m_innerMenu = nullptr; // holds the buttons
    cocos2d::CCMenu*           m_arrowMenu = nullptr; // holds the arrows
    CCMenuItemSpriteExtra*     m_prevArrow = nullptr; // global namespace (binding GD)
    CCMenuItemSpriteExtra*     m_nextArrow = nullptr;
    std::vector<cocos2d::CCMenuItem*> m_items;
};

} // namespace paimon::ui
