#pragma once

#include "AmbientMath.hpp"

#include <Geode/Geode.hpp>
#include <array>
#include <cstdint>
#include <unordered_map>

namespace paimon::ambient {

// Captures a lightweight 24x14 downsample of the scene background behind UI
// and tracks the dominant light source position, key color, and shadow mood.
// Uses async double-buffered PBO readback on desktop GL so sampling never
// stalls the render pipeline. Main thread only.
class AmbientAnalyzer {
public:
    static AmbientAnalyzer& get();

    static constexpr int kGridW = 24;
    static constexpr int kGridH = 14;
    static constexpr int kBlitW = 96;
    static constexpr int kBlitH = 56;

    void onSceneChanged(cocos2d::CCScene* scene);
    void requestImmediateSample(cocos2d::CCScene const* scene = nullptr);
    void advanceFrame(float dt);

    // Called from non-popup AmbientNode::draw() or AmbientBackdropProbe::draw()
    // before foreground UI obscures the backdrop.
    void maybeSampleFramebuffer(
        cocos2d::CCScene const* ownerScene,
        GLint framebuffer,
        int vpX, int vpY, int vpW, int vpH
    );

    // Pre-analyzes a static background texture into the palette cache without
    // modifying any scene's active state.
    void cacheTexturePalette(cocos2d::CCTexture2D* texture);

    // Seeds either an attached owning scene (if live) or a pending layer seed
    // (if the layer is still in init() before being added to its scene).
    bool seedLayerFromTexture(cocos2d::CCLayer* layer, cocos2d::CCTexture2D* texture, bool snapIfLive = true);

    // Seeds a specific scene directly from a backdrop texture.
    bool seedSceneFromTexture(cocos2d::CCScene const* scene, cocos2d::CCTexture2D* texture, bool snapIfLive = false);

    // Claims any pending layer seeds belonging to `scene`'s children.
    void bindSceneSeeds(cocos2d::CCScene* scene);

    // Compatibility wrapper: seeds the active scene (if any) and caches the texture palette.
    bool sampleTexture(cocos2d::CCTexture2D* texture);

    bool hasSceneCapture(cocos2d::CCScene const* scene) const;
    AmbientLightState const& stateForScene(cocos2d::CCScene const* scene) const;
    AmbientLightState const& currentState() const;
    AmbientLightState const& targetState() const;
    float timeSeconds() const { return m_time; }

    void releaseGL(bool deleteHandles);

private:
    struct SceneAmbientEntry {
        AmbientLightState target;
        AmbientLightState smoothed;
        std::uint64_t generation = 1;
        unsigned lastSampledFrame = 0;
        double lastSampleTime = -1.0;
        int consecutiveDarkSamples = 0;
    };

    AmbientAnalyzer() = default;
    bool ensurePrograms();
    bool ensureGridTarget();
    bool ensureCopyTarget(int w, int h);
#if defined(GEODE_IS_WINDOWS)
    bool ensureCopyFbo(int w, int h);
#endif
    bool ensureBlitTarget();
    bool analyzeTextureToState(cocos2d::CCTexture2D* texture, AmbientLightState& outState);
    SceneAmbientEntry& ensureSceneEntry(cocos2d::CCScene const* scene);
    void pruneStaleScenes();
    void enqueuePboOrReadImmediate(unsigned frame, cocos2d::CCScene const* scene, SceneAmbientEntry& entry);
    void collectPendingPbo(unsigned frame);
    double nowSeconds() const;

    AmbientLightState m_defaultState;
    std::unordered_map<cocos2d::CCScene const*, SceneAmbientEntry> m_scenes;
    std::unordered_map<cocos2d::CCLayer const*, AmbientLightState> m_pendingLayerSeeds;
    std::unordered_map<cocos2d::CCTexture2D const*, AmbientLightState> m_texturePaletteCache;

    void const* m_contextToken = nullptr;
    cocos2d::CCScene const* m_activeScene = nullptr;
    cocos2d::CCScene const* m_pboScene = nullptr;
    std::uint64_t m_nextGeneration = 1;
    std::uint64_t m_pboSceneGen = 0;
    float m_time = 0.f;
    int m_validationFailures = 0;
    bool m_validated = false;
    bool m_broken = false;

    GLuint m_copyTex = 0;
    GLuint m_copyFbo = 0;
    int m_copyW = 0;
    int m_copyH = 0;
    GLuint m_blitTex = 0;
    GLuint m_blitFbo = 0;
    GLuint m_gridTex = 0;
    GLuint m_gridFbo = 0;
    GLuint m_pbo = 0;
    bool m_pboPending = false;
    unsigned m_pboFrame = 0;
    geode::Ref<cocos2d::CCGLProgram> m_downsampleProgram;
    std::array<std::uint8_t, static_cast<std::size_t>(kGridW) * kGridH * 4> m_readback{};
};

} // namespace paimon::ambient
