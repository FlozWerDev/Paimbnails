#pragma once
#include <Geode/Geode.hpp>

class VideoSettingsPopup : public geode::Popup {
protected:
    // ── Playback ──
    CCMenuItemToggler* m_audioToggle = nullptr;
    cocos2d::CCLabelBMFont* m_fpsLabel = nullptr;
    cocos2d::CCLabelBMFont* m_qualityLabel = nullptr;

    // ── Filters ──
    cocos2d::CCLabelBMFont* m_blurTypeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_blurIntensityLabel = nullptr;
    cocos2d::CCMenu*        m_blurIntensityMenu = nullptr;
    cocos2d::CCLabelBMFont* m_blurIntensityTitleLabel = nullptr;

    // ── Rotation ──
    cocos2d::CCLabelBMFont* m_rotationLabel = nullptr;

    // ── Cache ──
    cocos2d::CCLabelBMFont* m_ramLabel = nullptr;

    // ── Indices ──
    int m_fpsIndex = 0;
    int m_qualityIndex = 0;
    int m_blurTypeIndex = 0;
    int m_blurIntensityIndex = 4;
    int m_rotationIndex = 0;

    bool init() override;

    // ── Playback handlers ──
    void onFpsPrev(cocos2d::CCObject*);
    void onFpsNext(cocos2d::CCObject*);
    void onQualityPrev(cocos2d::CCObject*);
    void onQualityNext(cocos2d::CCObject*);
    void onAudioToggle(cocos2d::CCObject*);

    // ── Filter handlers ──
    void onBlurTypePrev(cocos2d::CCObject*);
    void onBlurTypeNext(cocos2d::CCObject*);
    void onBlurIntensityPrev(cocos2d::CCObject*);
    void onBlurIntensityNext(cocos2d::CCObject*);

    // ── Rotation handlers ──
    void onRotationPrev(cocos2d::CCObject*);
    void onRotationNext(cocos2d::CCObject*);

    // ── Cache handlers ──
    void onClearRAM(cocos2d::CCObject*);
    void onClearDiskCache(cocos2d::CCObject*);

    // ── Label updaters ──
    void updateFpsLabel();
    void updateQualityLabel();
    void updateBlurTypeLabel();
    void updateBlurIntensityLabel();
    void updateBlurIntensityVisibility();
    void updateRotationLabel();
    void updateRAMLabel();

    // ── Option lists ──
    static inline std::vector<int> FPS_OPTIONS     = {15, 24, 30, 60, 90, 120};
    static inline std::vector<int> QUALITY_OPTIONS = {0, 50, 75, 100};
    static inline std::vector<const char*> QUALITY_NAMES = {"Auto", "Low", "Medium", "High"};

    static inline std::vector<std::string>    BLUR_TYPE_OPTIONS = {"none", "blur", "paimonblur"};
    static inline std::vector<const char*>    BLUR_TYPE_NAMES   = {"None", "Blur", "Paimon Blur"};

    static inline std::vector<float>       BLUR_INTENSITY_OPTIONS = {0.1f,0.2f,0.3f,0.4f,0.5f,0.6f,0.7f,0.8f,0.9f,1.0f};
    static inline std::vector<const char*> BLUR_INTENSITY_NAMES   = {"10%","20%","30%","40%","50%","60%","70%","80%","90%","100%"};

    static inline std::vector<int>         ROTATION_OPTIONS = {0, 90, 180, 270};
    static inline std::vector<const char*> ROTATION_NAMES   = {"0", "90", "180", "270"};

public:
    static VideoSettingsPopup* create();
};