#pragma once
// Visual picker for built-in procedural shapes (primitives) and GD-style decals/accessories.

#include "../data/IconProject.hpp"
#include "../../../ui/PaimonPopup.hpp"

#include <functional>
#include <string>

namespace geode { class ScrollLayer; }

namespace paimon::icon_maker {

class ShapePickerPopup : public PaimonPopup {
public:
    using PickedCallback = std::function<void(PieceShape const& shape, std::string const& defaultName)>;

    static ShapePickerPopup* create(PickedCallback onPicked, bool startOnDecals = false);

protected:
    bool init(PickedCallback onPicked, bool startOnDecals);
    void rebuildGrid();

    PickedCallback m_onPicked;
    bool m_showDecals = false;
    bool m_hollowPrimitives = false;

    cocos2d::CCNode* m_tabsHost = nullptr;
    cocos2d::CCNode* m_gridHost = nullptr;
    cocos2d::CCNode* m_modeHost = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
};

}  // namespace paimon::icon_maker
