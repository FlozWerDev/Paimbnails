#pragma once

#include "../services/MainMenuLayoutManager.hpp"

#include <Geode/ui/Popup.hpp>
#include <Geode/binding/Slider.hpp>

namespace paimon::menu_layout {

class MainMenuContextEditPopup : public geode::Popup {
public:
    using ApplyCallback = geode::CopyableFunction<void(MenuButtonLayout const& layout)>;

    static MainMenuContextEditPopup* create(std::string title, MenuButtonLayout const& layout, bool allowColor, bool allowFont, ApplyCallback onApply);

protected:
    bool init(std::string title, MenuButtonLayout const& layout, bool allowColor, bool allowFont, ApplyCallback onApply);

private:
    void buildUI(std::string const& title);
    void refreshUI();
    void emit();

    void onOpacity(CCObject*);
    void onColor(CCObject*);
    void onFont(CCObject*);

    MenuButtonLayout m_layout;
    bool m_allowColor = false;
    bool m_allowFont = false;
    ApplyCallback m_onApply;

    cocos2d::CCMenu* m_menu = nullptr;
    cocos2d::CCLayerColor* m_colorPreview = nullptr;
    cocos2d::CCLabelBMFont* m_opacityValue = nullptr;
    cocos2d::CCLabelBMFont* m_fontValue = nullptr;
    Slider* m_opacitySlider = nullptr;
};

} // namespace paimon::menu_layout
