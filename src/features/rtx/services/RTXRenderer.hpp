#pragma once

// RTX postfx: copy frame, trace, filter, composite on top.

#include <Geode/cocos/platform/CCGL.h>

#include <chrono>

namespace paimon::rtx {

struct RTXConfig;

class RTXRenderer {
public:
    static RTXRenderer& get();

    void renderFrame();
    void onGLContextReload();

    float lastFrameMs() const { return m_frameMs; }
    float activeScale() const { return m_activeScale; }
    int traceWidth() const { return m_traceW; }
    int sceneWidth() const { return m_sceneW; }
    int traceHeight() const { return m_traceH; }
    bool isBroken() const { return m_broken; }

private:
    RTXRenderer() = default;
    RTXRenderer(RTXRenderer const&) = delete;
    RTXRenderer& operator=(RTXRenderer const&) = delete;

    struct Target {
        GLuint fbo = 0;
        GLuint tex = 0;
        int w = 0;
        int h = 0;
    };

    static constexpr int kBloomLevels = 5;

    bool ensurePrograms();
    bool ensureFullTargets(int srcW, int srcH);
    bool ensureTraceTargets(int srcW, int srcH, float scale);

    bool makeTarget(Target& t, int w, int h, bool hdr = false);
    void dropTarget(Target& t);
    void releaseAll();

    void drawInto(Target const& t);
    void updateAdaptiveScale(RTXConfig const& cfg);

    // Effectives without touching config; degrades scale>rays>atrous>bloom>cadence.
    void syncGovernorEffectives(RTXConfig const& cfg);
    void clampGovernorToConfig(RTXConfig const& cfg);
    bool governorStepDown(float budget);
    bool governorStepUp(RTXConfig const& cfg);

    void runTrace(RTXConfig const& cfg);
    void runFilter(RTXConfig const& cfg);
    void runBloom(RTXConfig const& cfg);
    void runAutoExposure(RTXConfig const& cfg);
    void runComposite(RTXConfig const& cfg, GLint const* viewport, GLuint prevFbo);

    struct TraceProgram {
        GLuint id = 0;
        GLint texel            = -1;
        GLint frame            = -1;
        GLint rayCount         = -1;
        GLint raySteps         = -1;
        GLint rayDistance      = -1;
        GLint stepGrowth       = -1;
        GLint lightThreshold   = -1;
        GLint lightRange       = -1;
        GLint bounceFalloff    = -1;
        GLint giSaturation     = -1;
        GLint normalStrength   = -1;
        GLint thickness        = -1;
        GLint aoRadius         = -1;
        GLint aoPower          = -1;
        GLint reflectStrength  = -1;
        GLint reflectRoughness = -1;
        GLint reflectFresnel   = -1;
        GLint reflectFade      = -1;
    };

    struct TemporalProgram {
        GLuint id = 0;
        GLint texel       = -1;
        GLint temporal    = -1;
        GLint clampSigma  = -1;
        GLint reprojNow   = -1;
        GLint reprojPrev  = -1;
        GLint reprojScale = -1;
        GLint histVar      = -1;
        GLint historyValid = -1;
        GLint outVariance  = -1;
    };

    struct AtrousProgram {
        GLuint id = 0;
        GLint texel  = -1;
        GLint stride = -1;
        GLint phi    = -1;
        GLint wide   = -1;
    };

    struct BloomProgram {
        GLuint id = 0;
        GLint texel      = -1;
        GLint mode       = -1;
        GLint threshold  = -1;
        GLint softKnee   = -1;
        GLint radius     = -1;
        GLint blend      = -1;
        GLint anamorphic = -1;
        GLint lightPos   = -1;
        GLint decay      = -1;
        GLint density    = -1;
        GLint tonemap    = -1;
        GLint hdrRange   = -1;
        GLint giMix      = -1;
        GLint adaptRate  = -1;
        GLint frame      = -1;
    };

    struct CompositeProgram {
        GLuint id = 0;
        GLint texel         = -1;
        GLint giTexel       = -1;
        GLint time          = -1;
        GLint mixAmount     = -1;
        GLint giStrength    = -1;
        GLint aoStrength    = -1;
        GLint bloomStrength = -1;
        GLint rayStrength   = -1;
        GLint tonemap       = -1;
        GLint exposure      = -1;
        GLint adaptKey      = -1;
        GLint contrast      = -1;
        GLint saturation    = -1;
        GLint temperature   = -1;
        GLint tint          = -1;
        GLint gammaV        = -1;
        GLint ca            = -1;
        GLint vignette      = -1;
        GLint grain         = -1;
        GLint sharpen       = -1;
    };

    TraceProgram     m_trace;
    TemporalProgram  m_temporalProg;
    AtrousProgram    m_atrousProg;
    BloomProgram     m_bloom;
    CompositeProgram m_composite;

    GLuint m_vbo = 0;
    GLuint m_sceneTex = 0;
    GLuint m_blackTex = 0;
    int m_sceneW = 0;
    int m_sceneH = 0;

    Target m_traceSrc;
    Target m_traceRT;
    Target m_history[2];
    Target m_variance[2];
    Target m_atrous[2];
    Target m_bloomDown[kBloomLevels];
    Target m_bloomUp[kBloomLevels];
    Target m_rays;
    Target m_exposure[2];

    int m_traceW = 0;
    int m_traceH = 0;
    int m_historyIndex = 0;
    int m_exposureIndex = 0;
    GLuint m_bloomResultTex = 0;
    GLuint m_giResultTex = 0;

    // No float FBO clips HDR: stays flat.
    bool m_hdr = true;
    bool m_hasExposure = false;

    // Previous camera for reprojection; updated only when tracing.
    float m_prevCamX = 0.f;
    float m_prevCamY = 0.f;
    float m_prevCamScale = 1.f;
    bool m_hasPrevCamera = false;

    unsigned m_frameCounter = 0;
    unsigned m_idleFrames = 0;
    int m_adaptTicks = 0;
    int m_upTicks = 0;
    float m_activeScale = 0.5f;
    int m_effRayCount = 3;
    int m_effRaySteps = 14;
    int m_effAtrous = 3;
    int m_effBloom = 4;
    int m_effSkip = 0;
    float m_frameMs = 0.f;
    float m_shaderTime = 0.f;
    bool m_wasActive = false;
    bool m_broken = false;

    std::chrono::steady_clock::time_point m_lastFrame{};
};

} // namespace paimon::rtx
