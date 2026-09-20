#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include "ColorToggle.hpp"

namespace paimon::icon_gradients {

class GradientLayer;

class LoadLayer : public Popup {

private:

    // Owning editor and scroll content.
    GradientLayer* m_layer = nullptr;
    ScrollLayer* m_scrollLayer = nullptr;

    // Selection and saved entries.
    ColorToggle* m_selected = nullptr;
    std::vector<ColorToggle*> m_toggles;
    std::unordered_map<ColorToggle*, GradientConfig> m_toggleGradients;

    // First entry still waiting for its lazy paint.
    int m_updatedIndex = 100;

    bool init() override;

    // Bottom-bar button shared by the Load/Delete actions.
    CCMenuItemSpriteExtra* makeActionButton(const char*, SEL_MenuHandler, const CCPoint&, bool);

    // Lazy painter.
    void updateGradient(float);
    void updateUI();

    // Selection and actions.
    void onSelect(CCObject*);
    void onLoad(CCObject*);
    void onDelete(CCObject*);

    // Factory.
public:

    static LoadLayer* create(GradientLayer*);

};

} // namespace paimon::icon_gradients
