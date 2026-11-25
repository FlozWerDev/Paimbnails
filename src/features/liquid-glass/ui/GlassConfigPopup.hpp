#pragma once

#include "../../../ui/PaimonPopup.hpp"
#include "../services/GlassQuality.hpp"

class Slider;

namespace paimon::glass {

class GlassConfigPopup : public PaimonPopup {
public:
    static GlassConfigPopup* create();
    void onClose(cocos2d::CCObject* sender) override;

protected:
    bool init() override;

private:
    void refreshLabels();
    // `redecorate` attaches plates to what is already on screen; only
    // needed when the material or one of its scopes turns on.
    void applyAndSave(bool redecorate = false);

    void onToggleEnabled(cocos2d::CCObject* sender);
    void onTogglePopups(cocos2d::CCObject* sender);
    void onToggleButtons(cocos2d::CCObject* sender);
    void onToggleColors(cocos2d::CCObject* sender);
    void onCycleTier(cocos2d::CCObject* sender);
    void onPickTint(cocos2d::CCObject* sender);
    void onBlurChanged(cocos2d::CCObject* sender);
    void onRefractionChanged(cocos2d::CCObject* sender);
    void onTintOpacityChanged(cocos2d::CCObject* sender);
    void onRimChanged(cocos2d::CCObject* sender);
    void onReset(cocos2d::CCObject* sender);

    GlassSettings m_work;

    CCMenuItemToggler* m_enableToggle = nullptr;
    CCMenuItemToggler* m_popupsToggle = nullptr;
    CCMenuItemToggler* m_buttonsToggle = nullptr;
    CCMenuItemToggler* m_colorsToggle = nullptr;
    CCMenuItemSpriteExtra* m_tierButton = nullptr;
    CCMenuItemSpriteExtra* m_tintButton = nullptr;
    Slider* m_blurSlider = nullptr;
    Slider* m_refractionSlider = nullptr;
    Slider* m_tintOpacitySlider = nullptr;
    Slider* m_rimSlider = nullptr;
    cocos2d::CCLabelBMFont* m_blurLabel = nullptr;
    cocos2d::CCLabelBMFont* m_refractionLabel = nullptr;
    cocos2d::CCLabelBMFont* m_tintOpacityLabel = nullptr;
    cocos2d::CCLabelBMFont* m_rimLabel = nullptr;
};

} // namespace paimon::glass
