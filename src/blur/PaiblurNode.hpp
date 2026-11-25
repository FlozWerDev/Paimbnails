#pragma once

#include <Geode/cocos/cocoa/CCGeometry.h>
#include <Geode/cocos/base_nodes/CCNode.h>
#include <Geode/cocos/platform/CCGL.h>

namespace paimon::paiblur {

class PaiblurNode : public cocos2d::CCNodeRGBA {
public:
    static PaiblurNode* create(cocos2d::CCSize const& winSize, float intensity, float darkness);

    PaiblurNode();
    ~PaiblurNode() override;

    void fadeIn(float duration);

    void visit() override;

    // called while the old gl context is still current (GameManager::reloadAll)
    // or with deleteHandles=false after an Android EGL context loss:
    // releases/resets fbos/textures/vbo handles and drops the shared program so
    // visit() rebuilds everything lazily under the new context.
    void onGLContextReload(bool deleteHandles = true);
    static void onGLContextReloadAll(bool deleteHandles = true);

protected:
    bool initWithWinSize(cocos2d::CCSize const& winSize, float intensity, float darkness);

    bool ensureRenderTargets(int srcW, int srcH);
    void releaseRenderTargets(bool deleteHandles = true);

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

    // set on unrecoverable gl failure; visit() no-ops
    bool m_broken = false;

    // both blur axes are cached in m_texa; full pass every nth steady frame.
    bool m_hasCachedBlur = false;
    float m_lastRadius = -1.f;
    int m_steadyFrames = 0;
    void const* m_contextToken = nullptr;
};

} // namespace paimon::paiblur
