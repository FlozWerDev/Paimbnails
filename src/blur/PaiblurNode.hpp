#pragma once
// Dynamic per-frame popup background blur (EclipseMenu-style raw-GL pipeline).
// Falls back to static blur if shader/FBO setup fails.

#include <Geode/cocos/cocoa/CCGeometry.h>
#include <Geode/cocos/base_nodes/CCNode.h>
#include <Geode/cocos/platform/CCGL.h>

namespace paimon::paiblur {

class PaiblurNode : public cocos2d::CCNodeRGBA {
public:
    static PaiblurNode* create(cocos2d::CCSize const& winSize, float intensity, float darkness);

    void fadeIn(float duration);
    void fadeOutAndRemove(float duration);

    void setBlurIntensity(float intensity);
    void setDarkness(float darkness);

    void visit() override;

    ~PaiblurNode() override;

protected:
    bool initWithWinSize(cocos2d::CCSize const& winSize, float intensity, float darkness);

    bool ensureRenderTargets(int srcW, int srcH);
    void releaseRenderTargets();

    GLuint m_fboA = 0;
    GLuint m_texA = 0;
    GLuint m_fboB = 0;
    GLuint m_texB = 0;
    GLuint m_vbo  = 0;
    int m_blurW = 0;            // blur FBO size in real pixels
    int m_blurH = 0;
    int m_lastSrcW = 0;         // viewport size the FBOs were sized from
    int m_lastSrcH = 0;

    float m_intensity = 4.0f;
    float m_darkness = 0.28f;

    // Set when a GL step failed unrecoverably (blit unsupported, FBO lost).
    // visit() becomes a no-op so the popup stays usable over the sharp scene.
    bool m_broken = false;
};

} // namespace paimon::paiblur
