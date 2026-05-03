#pragma once

#include "../services/MainMenuLayoutManager.hpp"

#include <Geode/ui/Popup.hpp>
#include <Geode/binding/Slider.hpp>

namespace paimon::menu_layout {

class MainMenuDrawShapePopup : public geode::Popup {
public:
    using ApplyCallback = geode::CopyableFunction<void(DrawShapeLayout const& layout)>;

    static MainMenuDrawShapePopup* create(DrawShapeLayout const& layout, ApplyCallback onApply);

protected:
    bool init(DrawShapeLayout const& layout, ApplyCallback onApply);

private:
    void buildUI();
    void refreshUI();
    void applyPreview();

    void onKind(CCObject* sender);
    void onColor(CCObject*);
    void onWidth(CCObject*);
    void onHeight(CCObject*);
    void onRadius(CCObject*);
    void onOpacity(CCObject*);

    DrawShapeLayout m_layout;
    ApplyCallback m_onApply;

    cocos2d::CCMenu* m_menu = nullptr;
    cocos2d::CCLabelBMFont* m_kindValue = nullptr;
    cocos2d::CCLabelBMFont* m_widthValue = nullptr;
    cocos2d::CCLabelBMFont* m_heightValue = nullptr;
    cocos2d::CCLabelBMFont* m_radiusValue = nullptr;
    cocos2d::CCLabelBMFont* m_opacityValue = nullptr;
    cocos2d::CCLayerColor* m_colorPreview = nullptr;
    Slider* m_widthSlider = nullptr;
    Slider* m_heightSlider = nullptr;
    Slider* m_radiusSlider = nullptr;
    Slider* m_opacitySlider = nullptr;
};

} // namespace paimon::menu_layout
