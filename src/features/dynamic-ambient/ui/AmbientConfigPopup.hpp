#pragma once

#include "../services/AmbientMath.hpp"
#include "../../../ui/PaimonPopup.hpp"
#include <string>

class CCMenuItemSpriteExtra;
class CCMenuItemToggler;
class Slider;

namespace cocos2d {
class CCLabelBMFont;
class CCLayerColor;
}

namespace paimon::ambient {

class AmbientConfigPopup : public PaimonPopup {
public:
    static AmbientConfigPopup* create();

protected:
    bool init() override;
    void update(float dt) override;
    void onClose(cocos2d::CCObject*) override;

private:
    void applyAndSave(bool redecorate = false);
    void refreshLabels();
    void refreshLiveStatus();

    void onToggleEnabled(cocos2d::CCObject*);
    void onToggleButtons(cocos2d::CCObject*);
    void onTogglePanels(cocos2d::CCObject*);
    void onToggleAssets(cocos2d::CCObject*);
    void onIntensityChanged(cocos2d::CCObject*);
    void onRimChanged(cocos2d::CCObject*);
    void onShadowChanged(cocos2d::CCObject*);
    void onTintChanged(cocos2d::CCObject*);
    void onPaletteChanged(cocos2d::CCObject*);
    void onReset(cocos2d::CCObject*);

    AmbientSettings m_work;

    CCMenuItemToggler* m_enableToggle = nullptr;
    CCMenuItemToggler* m_buttonsToggle = nullptr;
    CCMenuItemToggler* m_panelsToggle = nullptr;
    CCMenuItemToggler* m_assetsToggle = nullptr;

    Slider* m_intensitySlider = nullptr;
    Slider* m_rimSlider = nullptr;
    Slider* m_shadowSlider = nullptr;
    Slider* m_tintSlider = nullptr;
    Slider* m_paletteSlider = nullptr;

    cocos2d::CCLabelBMFont* m_intensityLabel = nullptr;
    cocos2d::CCLabelBMFont* m_rimLabel = nullptr;
    cocos2d::CCLabelBMFont* m_shadowLabel = nullptr;
    cocos2d::CCLabelBMFont* m_tintLabel = nullptr;
    cocos2d::CCLabelBMFont* m_paletteLabel = nullptr;
    cocos2d::CCLabelBMFont* m_glassNoticeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_liveLightLabel = nullptr;
    cocos2d::CCLayerColor* m_keySwatch = nullptr;
    cocos2d::CCLayerColor* m_shadowSwatch = nullptr;
    cocos2d::CCLayerColor* m_primarySwatch = nullptr;
    cocos2d::CCLayerColor* m_secondarySwatch = nullptr;
    std::string m_lastLightText;
};

} // namespace paimon::ambient
