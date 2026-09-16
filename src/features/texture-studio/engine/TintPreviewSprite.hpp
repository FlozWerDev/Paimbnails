#pragma once

#include "LuminanceTinter.hpp"

#include <Geode/Geode.hpp>

namespace paimon::texture_studio {

// CCSprite that re-tints its base texture on the GPU on every draw from a
// packed role-mask texture (R=C1 G=C2 B=detail A=glow, the same bytes the CPU
// kernel blends with). Color and grade changes only push uniforms: no pixel
// re-upload, no worker thread. Segmentation still runs on the CPU once per
// mask-param change, and the export bake stays on the CPU path, so shipped
// packs remain bit-identical while the editor preview follows every slider
// tick. Layer-owned like the preview sprites it replaces; it dies with the
// editor layer, so no shutdown wiring is needed.
class TintPreviewSprite : public cocos2d::CCSprite {
public:
    static TintPreviewSprite* create(cocos2d::CCTexture2D* base,
                                     cocos2d::CCTexture2D* mask);
    bool init(cocos2d::CCTexture2D* base, cocos2d::CCTexture2D* mask);

    ~TintPreviewSprite() override;

    void setMaskTexture(cocos2d::CCTexture2D* mask);

    // Takes effect on the next draw; main thread only.
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
