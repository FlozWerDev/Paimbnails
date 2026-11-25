#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include "ColorToggle.hpp"

namespace paimon::icon_gradients {

class GradientLayer;

class LoadLayer : public PaimonPopup {

private:

    GradientLayer* m_layer = nullptr;
    ScrollLayer* m_scrollLayer = nullptr;

    ColorToggle* m_selected = nullptr;
    std::vector<ColorToggle*> m_toggles;
    std::unordered_map<ColorToggle*, GradientConfig> m_toggleGradients;

    // first entry still waiting for its lazy paint.
    std::size_t m_updatedIndex = 0;

    bool init() override;

    // bottom-bar button shared by the load/delete actions.
    CCMenuItemSpriteExtra* makeActionButton(const char*, SEL_MenuHandler, const CCPoint&, bool,
        const char* texture = "GJ_button_01.png");

    void updateGradient(float);
    void updateUI();

    void onSelect(CCObject*);
    void onLoad(CCObject*);
    void onDelete(CCObject*);

public:

    static LoadLayer* create(GradientLayer*);

};

} // namespace paimon::icon_gradients
