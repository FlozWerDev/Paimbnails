#pragma once

// Clips a CCMenu to N visible items and moves one item per arrow click.
// Items remain in a CCMenu for input; scissor clipping handles rendering, while
// hidden/disabled off-screen items cannot steal clicks.

#include <Geode/Geode.hpp>
#include <vector>

namespace paimon::ui {

class ButtonCarousel : public cocos2d::CCNode {
public:
    enum class Orientation { Horizontal, Vertical };

    static ButtonCarousel* create(
        Orientation orientation,
        int visibleCount,
        float itemSize,
        float crossSize,
        float gap = 6.f,
        float arrowSize = 18.f,
        int arrowThreshold = 4
    );

    // Add CCMenuItems, then call rebuild().
    void addButton(cocos2d::CCMenuItem* item);
    void addButtons(std::vector<cocos2d::CCMenuItem*> const& items);

    // Move source items into this carousel, preserving order.
    void absorbMenuItems(cocos2d::CCMenu* source);

    void rebuild();

    int buttonCount() const { return static_cast<int>(m_items.size()); }
    int maxOffset() const;
    void scrollToIndex(int offset, bool animated = true);

    // Create a carousel pre-populated from source.
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
    void relayout();
    int  effectiveSlots() const;
    bool needsArrows() const;

    float strideLen() const { return m_itemSize + m_gap; }
    float windowMain() const;
    cocos2d::CCPoint innerPosForOffset(int offset) const;
    cocos2d::CCPoint itemLocalPos(int index) const;

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

    cocos2d::CCClippingNode*   m_clip      = nullptr;
    cocos2d::CCMenu*           m_innerMenu = nullptr;
    cocos2d::CCMenu*           m_arrowMenu = nullptr;
    CCMenuItemSpriteExtra*     m_prevArrow = nullptr;
    CCMenuItemSpriteExtra*     m_nextArrow = nullptr;
    std::vector<cocos2d::CCMenuItem*> m_items;
};

}
