#pragma once

#include "../services/ProgressBarManager.hpp"
#include <Geode/Geode.hpp>
#include <string>

// Self-contained procedural + texture-clipped progress bar renderer used by
// both live PlayLayer HUD replacement and the interactive popup preview.
class CustomProgressBarNode : public cocos2d::CCNode {
public:
    static CustomProgressBarNode* create();

    void renderState(
        ProgressBarConfig const& cfg,
        ProgressBarTelemetry const& tel,
        float animTime,
        cocos2d::CCTexture2D* fillTex,
        cocos2d::CCTexture2D* bgTex
    );

    cocos2d::CCLabelBMFont* getLabelNode() const { return m_label; }
    cocos2d::CCRect getTrackLocalRect() const;

protected:
    bool init() override;

private:
    cocos2d::CCDrawNode* m_shadowDraw = nullptr;
    cocos2d::CCDrawNode* m_trackDraw = nullptr;
    cocos2d::CCDrawNode* m_fillDraw = nullptr;
    cocos2d::CCDrawNode* m_markerDraw = nullptr;
    cocos2d::CCDrawNode* m_borderDraw = nullptr;

    cocos2d::CCClippingNode* m_bgClip = nullptr;
    cocos2d::CCDrawNode* m_bgStencil = nullptr;
    cocos2d::CCSprite* m_bgTexSprite = nullptr;

    cocos2d::CCClippingNode* m_fillClip = nullptr;
    cocos2d::CCDrawNode* m_fillStencil = nullptr;
    cocos2d::CCSprite* m_fillTexSprite = nullptr;

    cocos2d::extension::CCScale9Sprite* m_gdFrame = nullptr;
    cocos2d::CCSprite* m_gdFill = nullptr;

    cocos2d::CCLabelBMFont* m_label = nullptr;

    std::string m_lastLabelText;
    float m_cachedW = 220.f;
    float m_cachedH = 12.f;
    float m_gdFrameVisualH = 12.f;
    float m_gdFrameTexH = 16.f;
    float m_gdFrameCapX = 10.f;

    void updateLabel(ProgressBarConfig const& cfg, ProgressBarTelemetry const& tel,
                     float animTime, float w, float h, float fillW);
};
