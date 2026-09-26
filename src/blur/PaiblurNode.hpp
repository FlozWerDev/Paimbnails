#pragma once

#include <Geode/cocos/cocoa/CCGeometry.h>
#include <Geode/cocos/base_nodes/CCNode.h>
#include <Geode/cocos/platform/CCGL.h>

namespace paimon::paiblur {

class PaiblurNode : public cocos2d::CCNodeRGBA {
public:
    static PaiblurNode* create(cocos2d::CCSize const& winSize, float intensity, float darkness);

    void fadeIn(float duration);

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
    int m_blurW = 0;
    int m_blurH = 0;
    int m_lastSrcW = 0;
    int m_lastSrcH = 0;

    float m_intensity = 4.0f;
    float m_darkness = 0.28f;

    // set on unrecoverable GL failure; visit() no-ops
    bool m_broken = false;

    // steady-state throttle: reuse m_texB, full pass every Nth frame
    bool m_hasCachedBlur = false;
    float m_lastRadius = -1.f;
    int m_steadyFrames = 0;
};

} // namespace paimon::paiblur
