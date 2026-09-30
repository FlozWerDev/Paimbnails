#pragma once

#include "LuminanceTinter.hpp"

#include <Geode/Geode.hpp>

namespace paimon::texture_studio {

// gpu re-tint per draw from the role mask (uniforms only); layer-owned, dies with the editor.
class TintPreviewSprite : public cocos2d::CCSprite {
public:
    static TintPreviewSprite* create(cocos2d::CCTexture2D* base,
                                     cocos2d::CCTexture2D* mask);
    bool init(cocos2d::CCTexture2D* base, cocos2d::CCTexture2D* mask);

    ~TintPreviewSprite() override;

    void setMaskTexture(cocos2d::CCTexture2D* mask);

    // takes effect on the next draw; main thread only.
    void setTint(TintColors const& colors, int brightness, float saturation,
                 float contrast, bool glowReplace, bool applyDetail,
                 int darkThreshold);

    void draw() override;

private:
    void pushUniforms(cocos2d::CCGLProgram* program);

    cocos2d::CCTexture2D* m_maskTex = nullptr;
    bool m_usesTintProgram = false;
    TintColors m_colors{};
    float m_brightness = 160.f;
    float m_saturation = 1.f;
    float m_contrast = 0.f;
    bool  m_glowReplace = false;
    bool  m_applyDetail = false;
    float m_darkThreshold = 0.f;
};

}  // namespace paimon::texture_studio
