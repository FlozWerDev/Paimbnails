#pragma once
#include <Geode/Geode.hpp>
#include <Geode/binding/Slider.hpp>

class PetConfigPopup : public geode::Popup {
protected:
    void onExit() override;
    void scrollWheel(float x, float y) override;

    // gallery
    cocos2d::CCNode* m_galleryContainer = nullptr;
    cocos2d::CCMenu* m_galleryMenu = nullptr;
    cocos2d::CCSprite* m_previewSprite = nullptr;
    cocos2d::CCLabelBMFont* m_selectedLabel = nullptr;

    // scroll for settings
    geode::ScrollLayer* m_scrollLayer = nullptr;
    cocos2d::CCSprite* m_scrollArrow = nullptr;
    cocos2d::CCPoint m_scrollArrowBasePos = {0, 0};

    // sliders
    Slider* m_scaleSlider = nullptr;
    cocos2d::CCLabelBMFont* m_scaleLabel = nullptr;
    Slider* m_sensitivitySlider = nullptr;
    cocos2d::CCLabelBMFont* m_sensitivityLabel = nullptr;
    Slider* m_opacitySlider = nullptr;
    cocos2d::CCLabelBMFont* m_opacityLabel = nullptr;
    Slider* m_bounceHeightSlider = nullptr;
    cocos2d::CCLabelBMFont* m_bounceHeightLabel = nullptr;
    Slider* m_bounceSpeedSlider = nullptr;
    cocos2d::CCLabelBMFont* m_bounceSpeedLabel = nullptr;
    Slider* m_rotDampSlider = nullptr;
    cocos2d::CCLabelBMFont* m_rotDampLabel = nullptr;
    Slider* m_maxTiltSlider = nullptr;
    cocos2d::CCLabelBMFont* m_maxTiltLabel = nullptr;
    Slider* m_trailLengthSlider = nullptr;
    cocos2d::CCLabelBMFont* m_trailLengthLabel = nullptr;
    Slider* m_trailWidthSlider = nullptr;
    cocos2d::CCLabelBMFont* m_trailWidthLabel = nullptr;
    Slider* m_breathScaleSlider = nullptr;
    cocos2d::CCLabelBMFont* m_breathScaleLabel = nullptr;
    Slider* m_breathSpeedSlider = nullptr;
    cocos2d::CCLabelBMFont* m_breathSpeedLabel = nullptr;
    Slider* m_squishSlider = nullptr;
    cocos2d::CCLabelBMFont* m_squishLabel = nullptr;
    Slider* m_offsetXSlider = nullptr;
    cocos2d::CCLabelBMFont* m_offsetXLabel = nullptr;
    Slider* m_offsetYSlider = nullptr;
    cocos2d::CCLabelBMFont* m_offsetYLabel = nullptr;

    // toggles
    CCMenuItemToggler* m_enableToggle = nullptr;
    CCMenuItemToggler* m_flipToggle = nullptr;
    CCMenuItemToggler* m_trailToggle = nullptr;
    CCMenuItemToggler* m_idleToggle = nullptr;
    CCMenuItemToggler* m_bounceToggle = nullptr;
    CCMenuItemToggler* m_squishToggle = nullptr;
    CCMenuItemToggler* m_allLayersToggle = nullptr;
    CCMenuItemToggler* m_showInGameplayToggle = nullptr;
    std::vector<CCMenuItemToggler*> m_layerToggles;

    // tabs
    int m_currentTab = 0; // 0 = gallery, 1 = settings, 2 = advanced
    cocos2d::CCNode* m_galleryTab = nullptr;
    cocos2d::CCNode* m_settingsTab = nullptr;
    cocos2d::CCNode* m_advancedTab = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_tabs;

    // advanced tab scroll
    geode::ScrollLayer* m_advancedScroll = nullptr;
    cocos2d::CCSprite* m_advancedArrow = nullptr;
    cocos2d::CCPoint m_advancedArrowBasePos = {0, 0};

    // advanced sliders
    Slider* m_shadowOffXSlider = nullptr;
    cocos2d::CCLabelBMFont* m_shadowOffXLabel = nullptr;
    Slider* m_shadowOffYSlider = nullptr;
    cocos2d::CCLabelBMFont* m_shadowOffYLabel = nullptr;
    Slider* m_shadowOpacitySlider = nullptr;
    cocos2d::CCLabelBMFont* m_shadowOpacityLabel = nullptr;
    Slider* m_shadowScaleSlider = nullptr;
    cocos2d::CCLabelBMFont* m_shadowScaleLabel = nullptr;
    Slider* m_particleRateSlider = nullptr;
    cocos2d::CCLabelBMFont* m_particleRateLabel = nullptr;
    Slider* m_particleSizeSlider = nullptr;
    cocos2d::CCLabelBMFont* m_particleSizeLabel = nullptr;
    Slider* m_particleGravitySlider = nullptr;
    cocos2d::CCLabelBMFont* m_particleGravityLabel = nullptr;
    Slider* m_particleLifetimeSlider = nullptr;
    cocos2d::CCLabelBMFont* m_particleLifetimeLabel = nullptr;
    Slider* m_speechIntervalSlider = nullptr;
    cocos2d::CCLabelBMFont* m_speechIntervalLabel = nullptr;
    Slider* m_speechDurationSlider = nullptr;
    cocos2d::CCLabelBMFont* m_speechDurationLabel = nullptr;
    Slider* m_speechScaleSlider = nullptr;
    cocos2d::CCLabelBMFont* m_speechScaleLabel = nullptr;
    Slider* m_sleepAfterSlider = nullptr;
    cocos2d::CCLabelBMFont* m_sleepAfterLabel = nullptr;
    Slider* m_sleepBobSlider = nullptr;
    cocos2d::CCLabelBMFont* m_sleepBobLabel = nullptr;
    Slider* m_clickDurationSlider = nullptr;
    cocos2d::CCLabelBMFont* m_clickDurationLabel = nullptr;
    Slider* m_clickJumpSlider = nullptr;
    cocos2d::CCLabelBMFont* m_clickJumpLabel = nullptr;
    Slider* m_reactionDurationSlider = nullptr;
    cocos2d::CCLabelBMFont* m_reactionDurationLabel = nullptr;
    Slider* m_reactionJumpSlider = nullptr;
    cocos2d::CCLabelBMFont* m_reactionJumpLabel = nullptr;
    Slider* m_reactionSpinSlider = nullptr;
    cocos2d::CCLabelBMFont* m_reactionSpinLabel = nullptr;

    // advanced toggles
    CCMenuItemToggler* m_shadowToggle = nullptr;
    CCMenuItemToggler* m_particleToggle = nullptr;
    CCMenuItemToggler* m_speechToggle = nullptr;
    CCMenuItemToggler* m_sleepToggle = nullptr;
    CCMenuItemToggler* m_clickToggle = nullptr;
    CCMenuItemToggler* m_reactCompleteToggle = nullptr;
    CCMenuItemToggler* m_reactDeathToggle = nullptr;
    CCMenuItemToggler* m_reactPracticeToggle = nullptr;


    bool init() override;
    void createTabButtons();
    void onTabSwitch(cocos2d::CCObject* sender);

    // gallery
    void buildGalleryTab();
    void refreshGallery();
    void onAddImage(cocos2d::CCObject*);
    void onDeleteImage(cocos2d::CCObject*);
    void onDeleteAllImages(cocos2d::CCObject*);
    void onSelectImage(cocos2d::CCObject*);

    // settings
    void buildSettingsTab();
    void checkScrollPosition(float dt);

    // advanced
    void buildAdvancedTab();
    void checkAdvancedScrollPosition(float dt);

    // slider callbacks
    void onScaleChanged(cocos2d::CCObject*);
    void onSensitivityChanged(cocos2d::CCObject*);
    void onOpacityChanged(cocos2d::CCObject*);
    void onBounceHeightChanged(cocos2d::CCObject*);
    void onBounceSpeedChanged(cocos2d::CCObject*);
    void onRotDampChanged(cocos2d::CCObject*);
    void onMaxTiltChanged(cocos2d::CCObject*);
    void onTrailLengthChanged(cocos2d::CCObject*);
    void onTrailWidthChanged(cocos2d::CCObject*);
    void onBreathScaleChanged(cocos2d::CCObject*);
    void onBreathSpeedChanged(cocos2d::CCObject*);
    void onSquishChanged(cocos2d::CCObject*);
    void onOffsetXChanged(cocos2d::CCObject*);
    void onOffsetYChanged(cocos2d::CCObject*);

    // advanced slider callbacks
    void onShadowOffXChanged(cocos2d::CCObject*);
    void onShadowOffYChanged(cocos2d::CCObject*);
    void onShadowOpacityChanged(cocos2d::CCObject*);
    void onShadowScaleChanged(cocos2d::CCObject*);
    void onParticleRateChanged(cocos2d::CCObject*);
    void onParticleSizeChanged(cocos2d::CCObject*);
    void onParticleGravityChanged(cocos2d::CCObject*);
    void onParticleLifetimeChanged(cocos2d::CCObject*);
    void onSpeechIntervalChanged(cocos2d::CCObject*);
    void onSpeechDurationChanged(cocos2d::CCObject*);
    void onSpeechScaleChanged(cocos2d::CCObject*);
    void onSleepAfterChanged(cocos2d::CCObject*);
    void onSleepBobChanged(cocos2d::CCObject*);
    void onClickDurationChanged(cocos2d::CCObject*);
    void onClickJumpChanged(cocos2d::CCObject*);
    void onReactionDurationChanged(cocos2d::CCObject*);
    void onReactionJumpChanged(cocos2d::CCObject*);
    void onReactionSpinChanged(cocos2d::CCObject*);

    // toggle callbacks
    void onEnableToggled(cocos2d::CCObject*);
    void onFlipToggled(cocos2d::CCObject*);
    void onTrailToggled(cocos2d::CCObject*);
    void onIdleToggled(cocos2d::CCObject*);
    void onBounceToggled(cocos2d::CCObject*);
    void onSquishToggled(cocos2d::CCObject*);
    void onLayerToggled(cocos2d::CCObject*);
    void onAllLayersToggled(cocos2d::CCObject*);
    void onShowInGameplayToggled(cocos2d::CCObject*);
    void onOpenLayerPicker(cocos2d::CCObject*);
    void onOpenShop(cocos2d::CCObject*);

    // advanced toggle callbacks
    void onShadowToggled(cocos2d::CCObject*);
    void onParticleToggled(cocos2d::CCObject*);
    void onParticleTypeChanged(cocos2d::CCObject*);
    void onParticleColorPicked(cocos2d::CCObject*);
    void onSpeechToggled(cocos2d::CCObject*);
    void onSleepToggled(cocos2d::CCObject*);
    void onClickToggled(cocos2d::CCObject*);
    void onReactCompleteToggled(cocos2d::CCObject*);
    void onReactDeathToggled(cocos2d::CCObject*);
    void onReactPracticeToggled(cocos2d::CCObject*);
    void onSetIconStateImage(cocos2d::CCObject*);

public:
    void applyLive();
    void refreshVisibleLayerControls();
    void refreshIconStateLabels();

    static PetConfigPopup* create();
};
