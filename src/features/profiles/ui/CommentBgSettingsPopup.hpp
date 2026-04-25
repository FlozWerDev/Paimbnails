#pragma once
#include <Geode/Geode.hpp>

struct ProfileConfig;
struct ThumbnailInfo;

class CommentBgSettingsPopup : public geode::Popup {
protected:
    int m_accountID = 0;
    ProfileConfig* m_configPtr = nullptr;

    // Type selection buttons
    CCMenuItemSpriteExtra* m_btnNone = nullptr;
    CCMenuItemSpriteExtra* m_btnThumbnail = nullptr;
    CCMenuItemSpriteExtra* m_btnBanner = nullptr;
    CCMenuItemSpriteExtra* m_btnSolid = nullptr;

    // Thumbnail ID input + position
    cocos2d::CCNode* m_thumbnailIdRow = nullptr;
    geode::TextInput* m_inputField = nullptr;
    cocos2d::CCLabelBMFont* m_thumbPosLabel = nullptr;
    CCMenuItemSpriteExtra* m_btnThumbPrev = nullptr;
    CCMenuItemSpriteExtra* m_btnThumbNext = nullptr;
    cocos2d::CCSprite* m_thumbPreviewSprite = nullptr;
    cocos2d::CCNode* m_thumbPreviewClip = nullptr;
    std::vector<ThumbnailInfo> m_cachedThumbnails;
    int m_thumbRequestToken = 0;

    // Banner sub-options
    cocos2d::CCNode* m_bannerRow = nullptr;
    CCMenuItemToggler* m_toggleBannerBg = nullptr;
    CCMenuItemToggler* m_toggleBannerImg = nullptr;

    // Solid color
    cocos2d::CCNode* m_solidColorRow = nullptr;
    cocos2d::CCSprite* m_colorPreviewSprite = nullptr;

    // Sliders
    Slider* m_blurSlider = nullptr;
    Slider* m_darknessSlider = nullptr;

    // Blur type toggle
    CCMenuItemToggler* m_toggleBlurGaussian = nullptr;
    CCMenuItemToggler* m_toggleBlurPaimon = nullptr;

    // Labels
    cocos2d::CCLabelBMFont* m_blurLabel = nullptr;
    cocos2d::CCLabelBMFont* m_darknessLabel = nullptr;
    cocos2d::CCLabelBMFont* m_typeLabel = nullptr;

    // Real-time preview (comment-like)
    cocos2d::CCNode* m_previewNode = nullptr;
    cocos2d::CCClippingNode* m_previewClip = nullptr;
    cocos2d::CCNode* m_previewBgContainer = nullptr;
    cocos2d::CCSprite* m_previewIcon = nullptr;
    cocos2d::CCLabelBMFont* m_previewUsername = nullptr;
    cocos2d::CCLabelBMFont* m_previewComment = nullptr;
    int m_previewToken = 0;

    ~CommentBgSettingsPopup();
    bool init(int accountID, ProfileConfig const& config);
    void onSelectType(cocos2d::CCObject* sender);
    void onSave(cocos2d::CCObject*);
    void updateTypeSelection();
    void updateConditionalRows();
    void refreshSliderLabels();
    void refreshPreview();

    // Slider callbacks
    void onBlurChanged(cocos2d::CCObject* sender);
    void onDarknessChanged(cocos2d::CCObject* sender);

    // Thumbnail position navigation
    void onThumbPrev(cocos2d::CCObject*);
    void onThumbNext(cocos2d::CCObject*);
    void fetchThumbnailsForLevel();
    void updateThumbnailPreview();

    // Color picker
    void onPickColor(cocos2d::CCObject*);

    // Banner toggles
    void onToggleBannerBg(cocos2d::CCObject*);
    void onToggleBannerImg(cocos2d::CCObject*);

    // Blur type toggles
    void onToggleBlurGaussian(cocos2d::CCObject*);
    void onToggleBlurPaimon(cocos2d::CCObject*);

public:
    static CommentBgSettingsPopup* create(int accountID, ProfileConfig const& config);
    void onColorSelected(cocos2d::ccColor3B color);
};
