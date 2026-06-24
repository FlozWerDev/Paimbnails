#pragma once
#include <Geode/Geode.hpp>

namespace paimon::smoothscroll {

// Config popup for smooth-scroll: toggle, sensitivity, smoothness.
class SmoothScrollConfigPopup : public geode::Popup {
public:
    static SmoothScrollConfigPopup* create();

protected:
    bool init() override;

    Slider* m_sensitivitySlider = nullptr;
    cocos2d::CCLabelBMFont* m_sensitivityLabel = nullptr;
    Slider* m_smoothnessSlider = nullptr;
    cocos2d::CCLabelBMFont* m_smoothnessLabel = nullptr;
    CCMenuItemToggler* m_enableToggle = nullptr;

    void onEnableToggled(cocos2d::CCObject*);
    void onSensitivityChanged(cocos2d::CCObject*);
    void onSmoothnessChanged(cocos2d::CCObject*);
    void onReset(cocos2d::CCObject*);

    void refreshLabels();
};

} // namespace paimon::smoothscroll
