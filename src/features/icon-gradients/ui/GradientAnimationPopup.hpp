#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/binding/ButtonSprite.hpp>

#include "../services/GradientAnimationManager.hpp"

namespace paimon::icon_gradients {

class GradientAnimationPopup : public PaimonPopup {
public:
    static GradientAnimationPopup* create(IconType initialType, bool secondPlayer);

private:
    bool init(IconType initialType, bool secondPlayer);

    void onEnabled(cocos2d::CCObject* sender);
    void onReverse(cocos2d::CCObject* sender);
    void onSpeed(cocos2d::CCObject* sender);
    void onIntensity(cocos2d::CCObject* sender);
    void onPreviousIcon(cocos2d::CCObject*);
    void onNextIcon(cocos2d::CCObject*);
    void onSwapPlayer(cocos2d::CCObject*);
    void onReset(cocos2d::CCObject*);
    void onRandomize(cocos2d::CCObject*);
    void onPlayPause(cocos2d::CCObject*);
    void onCustomize(cocos2d::CCObject*);

    void pickType(GradientAnimationType type);
    void openCustomEditor();

    void buildPreviewStage();
    void buildPresetGrid();
    void buildParameters();

    void rebuildPreview();
    void refreshControls();
    void refreshPresetSelection();
    void refreshLabels();
    void applyPreviewAnimation();

    cocos2d::CCNode* m_previewHost = nullptr;
    cocos2d::CCSprite* m_glow = nullptr;
    cocos2d::CCLabelBMFont* m_iconLabel = nullptr;
    cocos2d::CCLabelBMFont* m_typeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_descriptionLabel = nullptr;
    cocos2d::CCLabelBMFont* m_speedLabel = nullptr;
    cocos2d::CCLabelBMFont* m_intensityLabel = nullptr;
    ButtonSprite* m_playSprite = nullptr;

    CCMenuItemToggler* m_enabledToggle = nullptr;
    CCMenuItemToggler* m_reverseToggle = nullptr;
    Slider* m_speedSlider = nullptr;
    Slider* m_intensitySlider = nullptr;

    std::vector<std::pair<GradientAnimationType, cocos2d::CCNode*>> m_presetCards;
    size_t m_previewIndex = 0;
    bool m_secondPlayer = false;
    bool m_paused = false;
};

} // namespace paimon::icon_gradients
