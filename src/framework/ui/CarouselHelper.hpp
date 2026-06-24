#pragma once

// CarouselHelper.hpp — Convert an existing CCMenu with too many buttons into a
// ButtonCarousel with arrows, in place. Aimed at GD menus (ProfilePage's
// left/socials menus, LevelInfoLayer's left-side menu, etc.) where GD and other
// mods pile many buttons into one row/column.

#include <Geode/Geode.hpp>
#include "ButtonCarousel.hpp"

namespace paimon::ui {

class CarouselHelper {
public:
    // Replace `menu` with a ButtonCarousel holding its CCMenuItems.
    //  - Only acts if the menu has more buttons than fit (else returns nullptr).
    //  - The carousel inherits the menu's position/anchor/zOrder.
    //  - The original menu is hidden, not destroyed, so ID lookups still work.
    // Returns the carousel, or nullptr if not needed / on failure.
    static ButtonCarousel* wrapInPlace(
        cocos2d::CCMenu* menu,
        ButtonCarousel::Orientation orientation,
        int visibleCount = 3,
        float itemSize = 30.f,
        float crossSize = 30.f,
        float gap = 6.f,
        float arrowSize = 16.f,
        int arrowThreshold = 4
    ) {
        if (!menu) return nullptr;

        // Count actual CCMenuItems.
        int itemCount = 0;
        if (auto children = menu->getChildren()) {
            for (auto* node : geode::cocos::CCArrayExt<cocos2d::CCNode*>(children)) {
                if (geode::cast::typeinfo_cast<cocos2d::CCMenuItem*>(node)) ++itemCount;
            }
        }
        // Below the threshold no carousel is needed (all fit).
        if (itemCount < arrowThreshold) return nullptr;

        auto* parent = menu->getParent();
        if (!parent) return nullptr;

        // Avoid double-wrap: bail if a sibling carousel with our ID already exists.
        std::string carouselID = std::string(menu->getID()) + "-carousel";
        if (parent->getChildByID(carouselID)) return nullptr;

        auto pos    = menu->getPosition();
        auto anchor = menu->getAnchorPoint();
        int  zorder = menu->getZOrder();

        auto* carousel = ButtonCarousel::create(
            orientation, visibleCount, itemSize, crossSize, gap, arrowSize, arrowThreshold);
        if (!carousel) return nullptr;

        carousel->absorbMenuItems(menu);
        carousel->rebuild();

        carousel->setID(carouselID);
        carousel->setPosition(pos);
        carousel->setAnchorPoint(anchor);
        parent->addChild(carousel, zorder);

        // Original menu is now empty and hidden; ID lookups still find it, just without buttons.
        menu->setVisible(false);

        return carousel;
    }
};

} // namespace paimon::ui
