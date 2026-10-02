#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include "ColorToggle.hpp"

namespace paimon::icon_gradients {

class GradientLayer;

class LoadLayer : public PaimonPopup {

private:

    // owning editor and scroll content.
    GradientLayer* m_layer = nullptr;
    ScrollLayer* m_scrollLayer = nullptr;

    // selection and saved entries.
    ColorToggle* m_selected = nullptr;
    std::vector<ColorToggle*> m_toggles;
    std::unordered_map<ColorToggle*, GradientConfig> m_toggleGradients;

    // first entry still waiting for its lazy paint.
    int m_updatedIndex = 100;

    bool init() override;

    // bottom-bar button shared by the load/delete actions.
    CCMenuItemSpriteExtra* makeActionButton(const char*, SEL_MenuHandler, const CCPoint&, bool);

    // lazy painter.
    void updateGradient(float);
    void updateUI();

    // selection and actions.
    void onSelect(CCObject*);
    void onLoad(CCObject*);
    void onDelete(CCObject*);

    // factory.
public:

    static LoadLayer* create(GradientLayer*);

};

} // namespace paimon::icon_gradients
