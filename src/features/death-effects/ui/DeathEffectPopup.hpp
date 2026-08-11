#pragma once

#include <Geode/Geode.hpp>

class Slider;

namespace paimon::death_effects {

class DeathEffectPopup : public geode::Popup {
public:
    static DeathEffectPopup* create();
    void onClose(cocos2d::CCObject* sender) override;

protected:
    bool init() override;

private:
    void rebuildList();
    void refreshControls();
    void refreshStatus();

    void onToggleSound(cocos2d::CCObject* sender);
    void onPreview(cocos2d::CCObject* sender);
    void onDelete(cocos2d::CCObject* sender);
    void onImportFile(cocos2d::CCObject* sender);
    void onImportFolder(cocos2d::CCObject* sender);
    void onOpenFolder(cocos2d::CCObject* sender);
    void onUseOriginal(cocos2d::CCObject* sender);
    void onCycleOrder(cocos2d::CCObject* sender);
    void onToggleAvoidRepeats(cocos2d::CCObject* sender);
    void onToggleStopOnReset(cocos2d::CCObject* sender);
    void onVolumeChanged(cocos2d::CCObject* sender);
    void onPitchChanged(cocos2d::CCObject* sender);
    void onInfo(cocos2d::CCObject* sender);

    static std::filesystem::path pathFromSender(cocos2d::CCObject* sender);

    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_volumeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_pitchLabel = nullptr;
    CCMenuItemSpriteExtra* m_orderButton = nullptr;
    CCMenuItemSpriteExtra* m_originalButton = nullptr;
    CCMenuItemToggler* m_avoidRepeatToggle = nullptr;
    CCMenuItemToggler* m_stopOnResetToggle = nullptr;
    Slider* m_volumeSlider = nullptr;
    Slider* m_pitchSlider = nullptr;
};

} // namespace paimon::death_effects
