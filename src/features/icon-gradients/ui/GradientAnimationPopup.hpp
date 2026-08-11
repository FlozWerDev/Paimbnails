#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include "../services/GradientAnimationManager.hpp"

namespace paimon::icon_gradients {

class GradientAnimationPopup : public geode::Popup {
public:
    static GradientAnimationPopup* create(IconType initialType, bool secondPlayer);

private:
    bool init(IconType initialType, bool secondPlayer);

    void onEffect(cocos2d::CCObject* sender);
    void onEnabled(cocos2d::CCObject* sender);
    void onReverse(cocos2d::CCObject* sender);
    void onSpeed(cocos2d::CCObject* sender);
    void onIntensity(cocos2d::CCObject* sender);
    void onPreviousIcon(cocos2d::CCObject*);
    void onNextIcon(cocos2d::CCObject*);
    void onReset(cocos2d::CCObject*);
    void onCustomize(cocos2d::CCObject*);

    void openCustomEditor();
    void rebuildPreview();
    void refreshControls();
    void refreshEffectSelection();
    void refreshLabels();

    cocos2d::CCNode* m_previewHost = nullptr;
    cocos2d::CCLabelBMFont* m_iconLabel = nullptr;
    cocos2d::CCLabelBMFont* m_descriptionLabel = nullptr;
    cocos2d::CCLabelBMFont* m_speedLabel = nullptr;
    cocos2d::CCLabelBMFont* m_intensityLabel = nullptr;

    CCMenuItemToggler* m_enabledToggle = nullptr;
    CCMenuItemToggler* m_reverseToggle = nullptr;
    Slider* m_speedSlider = nullptr;
    Slider* m_intensitySlider = nullptr;

    std::vector<std::pair<GradientAnimationType, CCMenuItemSpriteExtra*>> m_effectButtons;
    size_t m_previewIndex = 0;
    bool m_secondPlayer = false;
};

} // namespace paimon::icon_gradients
