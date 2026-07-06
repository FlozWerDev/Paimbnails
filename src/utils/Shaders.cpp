#include "Shaders.hpp"
#include "GLSLLoader.hpp"
#include "../features/audio/services/PaimonAudio.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include "../utils/MainThreadDelay.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#include <atomic>

using namespace geode::prelude;
using namespace cocos2d;

namespace Shaders {

// All procedural shaders moved to resources/shaders/*.glsl; inline literals
// removed. GLSLLoader loads from disk with fail-fast (nullptr if the file is missing).

CCGLProgram* getOrCreateShader(char const* key, char const* vertexSrc, char const* fragmentSrc) {
    auto shaderCache = CCShaderCache::sharedShaderCache();
    if (auto program = shaderCache->programForKey(key)) {
        return program;
    }

    if (!vertexSrc || !fragmentSrc) {
        geode::log::error("shader source is null for key: {}", key);
        return nullptr;
    }

    auto program = new CCGLProgram();
    program->initWithVertexShaderByteArray(vertexSrc, fragmentSrc);
    program->addAttribute("a_position", kCCVertexAttrib_Position);
    program->addAttribute("a_color", kCCVertexAttrib_Color);
    program->addAttribute("a_texCoord", kCCVertexAttrib_TexCoords);

    if (!program->link()) {
        geode::log::error("failed to link shader: {}", key);
        program->release();
        return nullptr;
    }

    program->updateUniforms();
    shaderCache->addProgram(program, key);
    program->release();
    return program;
}

void applyBlurPass(CCSprite* input, CCRenderTexture* output, CCGLProgram* program, CCSize const& size, float radius) {
    input->setShaderProgram(program);
    input->setPosition(size * 0.5f);

    program->use();
    program->setUniformsForBuiltins();
    GLint locScreen = program->getUniformLocationForName("u_screenSize");
    if (locScreen != -1) {
        program->setUniformLocationWith2f(locScreen, size.width, size.height);
    }
    GLint locRadius = program->getUniformLocationForName("u_radius");
    if (locRadius != -1) {
        program->setUniformLocationWith1f(locRadius, radius);
    }

    // IMPORTANT: CCRenderTexture::begin() doesn't clear the FBO. When the source
    // texture has pixels with alpha < 1 (custom thumbnails, transparent images),
    // alpha blending mixes against uninitialized FBO memory — some drivers return
    // white, others garbage, causing random white LevelCells.
    output->beginWithClear(0.f, 0.f, 0.f, 0.f);
    input->visit();
    output->end();
}

float intensityToBlurRadius(float intensity) {
    float normalized = std::clamp((intensity - 1.0f) / 9.0f, 0.0f, 1.0f);
    // Smooth ease-in curve for more gradual low-end, stronger high-end
    float curved = normalized * normalized * (3.0f - 2.0f * normalized);
    return 0.03f + (curved * 0.27f);
}

CCSprite* createBlurredSprite(CCTexture2D* texture, CCSize const& targetSize, float intensity, bool useDirectRadius) {
    if (!texture) return nullptr;
    if (targetSize.width <= 0.f || targetSize.height <= 0.f ||
        targetSize.width > 4096.f || targetSize.height > 4096.f) return nullptr;

    auto srcSprite = CCSprite::createWithTexture(texture);
    if (!srcSprite) return nullptr;

    ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    texture->setTexParameters(&params);

    // -- Optimization: downscale before blurring --
    // Blurring at full resolution (1920x1080) wastes GPU cycles.
    // Scale down to at most 640px on the longest side — blur is a low-freq
    // operation so the result is visually identical, but much faster.
    constexpr float kMaxBlurDim = 1024.f;
    CCSize blurSize = targetSize;
    float downFactor = 1.f;
    if (blurSize.width > kMaxBlurDim || blurSize.height > kMaxBlurDim) {
        downFactor = kMaxBlurDim / std::max(blurSize.width, blurSize.height);
        blurSize.width  = std::max(4.f, std::round(blurSize.width  * downFactor));
        blurSize.height = std::max(4.f, std::round(blurSize.height * downFactor));
    }

    float scaleX = blurSize.width / texture->getContentSize().width;
    float scaleY = blurSize.height / texture->getContentSize().height;
    float scale = std::max(scaleX, scaleY);

    srcSprite->setScale(scale);
    srcSprite->setAnchorPoint({0.5f, 0.5f});
    srcSprite->setPosition(blurSize * 0.5f);
    srcSprite->setFlipY(true);

    auto blurH = paimon::shaders::getBlurHorizontalShader();
    auto blurV = paimon::shaders::getBlurVerticalShader();

    if (!blurH || !blurV) {
        srcSprite->setScale(std::max(targetSize.width / texture->getContentSize().width,
                                     targetSize.height / texture->getContentSize().height));
        srcSprite->setPosition(targetSize * 0.5f);
        return srcSprite;
    }

    auto rtA = CCRenderTexture::create(static_cast<int>(blurSize.width), static_cast<int>(blurSize.height));
    auto rtB = CCRenderTexture::create(static_cast<int>(blurSize.width), static_cast<int>(blurSize.height));

    if (!rtA || !rtB) {
        srcSprite->setScale(std::max(targetSize.width / texture->getContentSize().width,
                                     targetSize.height / texture->getContentSize().height));
        srcSprite->setPosition(targetSize * 0.5f);
        return srcSprite;
    }

    // Boost radius slightly to compensate for the smaller blur buffer
    float radius = useDirectRadius ? intensity : intensityToBlurRadius(intensity);
    if (downFactor < 1.f) radius *= 1.f / downFactor * 0.6f;

    // Pass 1: horizontal + vertical
    applyBlurPass(srcSprite, rtA, blurH, blurSize, radius);

    auto midSprite = CCSprite::createWithTexture(rtA->getSprite()->getTexture());
    midSprite->setFlipY(true);
    midSprite->setAnchorPoint({0.5f, 0.5f});
    midSprite->setPosition(blurSize * 0.5f);
    midSprite->getTexture()->setTexParameters(&params);

    applyBlurPass(midSprite, rtB, blurV, blurSize, radius);

    // For low intensity (< 4.0), a single H+V pass is visually sufficient
    // and cuts GPU work by 50%. High intensity benefits from the second pass
    // to avoid visible banding / square artifacts.
    if (!useDirectRadius && intensity >= 4.0f) {
        // Pass 2: second H+V pass for smoother blur
        auto mid2 = CCSprite::createWithTexture(rtB->getSprite()->getTexture());
        mid2->setFlipY(true);
        mid2->setAnchorPoint({0.5f, 0.5f});
        mid2->setPosition(blurSize * 0.5f);
        mid2->getTexture()->setTexParameters(&params);

        applyBlurPass(mid2, rtA, blurH, blurSize, radius * 0.8f);

        auto mid3 = CCSprite::createWithTexture(rtA->getSprite()->getTexture());
        mid3->setFlipY(true);
        mid3->setAnchorPoint({0.5f, 0.5f});
        mid3->setPosition(blurSize * 0.5f);
        mid3->getTexture()->setTexParameters(&params);

        applyBlurPass(mid3, rtB, blurV, blurSize, radius * 0.8f);
    }

    auto finalSprite = CCSprite::createWithTexture(rtB->getSprite()->getTexture());
    finalSprite->setAnchorPoint({0.5f, 0.5f});
    finalSprite->setFlipY(true);
    finalSprite->getTexture()->setTexParameters(&params);

    return finalSprite;
}

CCSprite* createPopupBlurredSprite(CCTexture2D* texture, CCSize const& targetSize, float intensity) {
    // PERF: blur internally at half-res, upsample at the end. Blur removes high
    // frequencies, so blurring at full winSize is wasteful — the final bilinear
    // upsample looks identical. Cuts blur fragments ~75% (key at 360 Hz). The
    // result sprite still has contentSize = targetSize via the upsample FBO.
    if (!texture) return nullptr;
    if (targetSize.width <= 0.f || targetSize.height <= 0.f ||
        targetSize.width > 4096.f || targetSize.height > 4096.f) return nullptr;

    auto srcSprite = CCSprite::createWithTexture(texture);
    if (!srcSprite) return nullptr;

    ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    texture->setTexParameters(&params);

    // Internal blur dim cap. 960 keeps good quality even with aggressive
    // downsample; the subsequent blur hides any downsample aliasing.
    constexpr float kInternalBlurDim = 960.f;
    CCSize blurSize = targetSize;
    if (blurSize.width > kInternalBlurDim || blurSize.height > kInternalBlurDim) {
        float scale = std::min(kInternalBlurDim / blurSize.width,
                                kInternalBlurDim / blurSize.height);
        blurSize.width = std::round(blurSize.width * scale);
        blurSize.height = std::round(blurSize.height * scale);
        if (blurSize.width < 16.f) blurSize.width = 16.f;
        if (blurSize.height < 16.f) blurSize.height = 16.f;
    }

    // Stretch the source sprite to blurSize via independent setScaleX/Y
    // ("stretch", not "cover", to avoid clipping edges).
    {
        float texW = std::max(1.0f, srcSprite->getContentSize().width);
        float texH = std::max(1.0f, srcSprite->getContentSize().height);
        srcSprite->setScaleX(blurSize.width / texW);
        srcSprite->setScaleY(blurSize.height / texH);
    }
    srcSprite->setAnchorPoint({0.5f, 0.5f});
    srcSprite->setPosition(blurSize * 0.5f);
    srcSprite->setFlipY(true);

    auto blurH = paimon::shaders::getBlurHorizontalShader();
    auto blurV = paimon::shaders::getBlurVerticalShader();
    if (!blurH || !blurV) {
        srcSprite->setScaleX(targetSize.width / std::max(1.0f, srcSprite->getContentSize().width));
        srcSprite->setScaleY(targetSize.height / std::max(1.0f, srcSprite->getContentSize().height));
        srcSprite->setPosition(targetSize * 0.5f);
        return srcSprite;
    }

    auto rtA = CCRenderTexture::create(static_cast<int>(blurSize.width), static_cast<int>(blurSize.height));
    auto rtB = CCRenderTexture::create(static_cast<int>(blurSize.width), static_cast<int>(blurSize.height));
    if (!rtA || !rtB) {
        srcSprite->setScaleX(targetSize.width / std::max(1.0f, srcSprite->getContentSize().width));
        srcSprite->setScaleY(targetSize.height / std::max(1.0f, srcSprite->getContentSize().height));
        srcSprite->setPosition(targetSize * 0.5f);
        return srcSprite;
    }

    rtA->getSprite()->getTexture()->setTexParameters(&params);
    rtB->getSprite()->getTexture()->setTexParameters(&params);

    float radius = intensityToBlurRadius(std::clamp(intensity, 1.0f, 10.0f));
    radius = std::min(radius, 0.55f);

    applyBlurPass(srcSprite, rtA, blurH, blurSize, radius);

    auto mid1 = CCSprite::createWithTexture(rtA->getSprite()->getTexture());
    if (!mid1) return nullptr;
    {
        float texW = std::max(1.0f, mid1->getContentSize().width);
        float texH = std::max(1.0f, mid1->getContentSize().height);
        mid1->setScaleX(blurSize.width / texW);
        mid1->setScaleY(blurSize.height / texH);
    }
    mid1->setFlipY(true);
    mid1->setAnchorPoint({0.5f, 0.5f});
    mid1->setPosition(blurSize * 0.5f);
    mid1->getTexture()->setTexParameters(&params);
    applyBlurPass(mid1, rtB, blurV, blurSize, radius);

    if (intensity >= 4.0f) {
        // PERF: glFlush() between passes was pure overhead on desktop OpenGL (a
        // CPU-blocking driver round-trip). Keep it only on mobile (deferred
        // renderers need an explicit barrier).
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
        glFlush();
#endif

        auto mid2 = CCSprite::createWithTexture(rtB->getSprite()->getTexture());
        if (!mid2) return nullptr;
        {
            float texW = std::max(1.0f, mid2->getContentSize().width);
            float texH = std::max(1.0f, mid2->getContentSize().height);
            mid2->setScaleX(blurSize.width / texW);
            mid2->setScaleY(blurSize.height / texH);
        }
        mid2->setFlipY(true);
        mid2->setAnchorPoint({0.5f, 0.5f});
        mid2->setPosition(blurSize * 0.5f);
        mid2->getTexture()->setTexParameters(&params);
        applyBlurPass(mid2, rtA, blurH, blurSize, radius * 0.8f);

        auto mid3 = CCSprite::createWithTexture(rtA->getSprite()->getTexture());
        if (!mid3) return nullptr;
        {
            float texW = std::max(1.0f, mid3->getContentSize().width);
            float texH = std::max(1.0f, mid3->getContentSize().height);
            mid3->setScaleX(blurSize.width / texW);
            mid3->setScaleY(blurSize.height / texH);
        }
        mid3->setFlipY(true);
        mid3->setAnchorPoint({0.5f, 0.5f});
        mid3->setPosition(blurSize * 0.5f);
        mid3->getTexture()->setTexParameters(&params);
        applyBlurPass(mid3, rtB, blurV, blurSize, radius * 0.8f);
    }

    auto finalSprite = CCSprite::createWithTexture(rtB->getSprite()->getTexture());
    if (!finalSprite) return nullptr;
    finalSprite->setAnchorPoint({0.5f, 0.5f});
    finalSprite->setFlipY(true);
    finalSprite->getTexture()->setTexParameters(&params);
    return finalSprite;
}

CCSprite* createPopupPaimonBlurredSprite(CCTexture2D* texture, CCSize const& targetSize, float intensity) {
    // For popups the final sprite must have EXACTLY winSize dimensions, so always
    // upsample-blit to a targetSize FBO regardless of the base's contentSize. This
    // guarantees the final sprite has contentSize == targetSize and flipY=true,
    // avoiding the contentSize/orientation inconsistencies of returning base directly.
    auto* base = createPaimonBlurSprite(texture, targetSize, intensity);
    if (!base) return nullptr;

    auto baseSize = base->getContentSize();

    // upsample to a targetSize FBO — a single bilinear pass
    auto rt = CCRenderTexture::create(
        static_cast<int>(std::round(targetSize.width)),
        static_cast<int>(std::round(targetSize.height)));
    if (!rt) {
        // Fallback: if CCRenderTexture::create fails, return base with setFlipY applied.
        base->setFlipY(true);
        return base;
    }

    ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    rt->getSprite()->getTexture()->setTexParameters(&params);

    // Center and scale base to targetSize.
    //
    // Y-orientation: createPaimonBlurSprite does NOT apply setFlipY(true) to its
    // intermediate sprites (unlike createBlurredSprite/Gauss which does). The total
    // pass count is even (N down + N up), so the final base sprite still renders
    // Y-inverted vs the screen because the original `tex` came from a captured FBO
    // and was never compensated. We force flipY=true here so base draws Y-correct
    // into rt; the rt storage then matches screen orientation, and finalSprite's
    // setFlipY(true) reads it back correctly. Without this, the blur shows
    // upside-down (regression seen after removing normalizeBlurSpriteToWinSize).
    base->setFlipY(true);
    base->setAnchorPoint({0.5f, 0.5f});
    base->setPosition(targetSize * 0.5f);
    {
        float bw = std::max(1.0f, baseSize.width);
        float bh = std::max(1.0f, baseSize.height);
        base->setScaleX(targetSize.width / bw);
        base->setScaleY(targetSize.height / bh);
    }

    rt->beginWithClear(0.f, 0.f, 0.f, 0.f);
    base->visit();
    rt->end();

    auto finalSprite = CCSprite::createWithTexture(rt->getSprite()->getTexture());
    if (!finalSprite) {
        base->setFlipY(true);
        return base;
    }
    finalSprite->setAnchorPoint({0.5f, 0.5f});
    finalSprite->setFlipY(true);  // FBO inverts Y, so we need flipY
    // force contentSize to targetSize explicitly (defensive)
    finalSprite->setContentSize(targetSize);
    finalSprite->getTexture()->setTexParameters(&params);
    return finalSprite;
}

CCGLProgram* getBlurCellShader() {
    return paimon::shaders::getBlurCellShader();
}

CCGLProgram* getBlurSinglePassShader() {
    return paimon::shaders::getBlurSinglePassShader();
}

CCGLProgram* getPaimonBlurShader() {
    return paimon::shaders::getKawaseRealtimeShader();
}

CCSprite* createPaimonBlurSprite(CCTexture2D* texture, CCSize const& targetSize, float intensity) {
    // CCRenderTexture-based Dual Kawase
    if (!texture) return nullptr;
    if (targetSize.width <= 0.f || targetSize.height <= 0.f ||
        targetSize.width > 4096.f || targetSize.height > 4096.f) return nullptr;

    auto srcSprite = CCSprite::createWithTexture(texture);
    if (!srcSprite) return nullptr;

    ccTexParams linearParams{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    texture->setTexParameters(&linearParams);

    // -- Optimization: downscale before blurring --
    // Blurring at full resolution wastes GPU cycles. Scale down to at most
    // 1024px on the longest side — blur is a low-freq operation so the result
    // is visually identical but much faster (matching createBlurredSprite).
    constexpr float kMaxBlurDim = 1024.f;
    CCSize blurSize = targetSize;
    float downFactor = 1.f;
    if (blurSize.width > kMaxBlurDim || blurSize.height > kMaxBlurDim) {
        downFactor = kMaxBlurDim / std::max(blurSize.width, blurSize.height);
        blurSize.width  = std::max(8.f, std::round(blurSize.width  * downFactor));
        blurSize.height = std::max(8.f, std::round(blurSize.height * downFactor));
    } else {
        blurSize.width  = std::round(blurSize.width);
        blurSize.height = std::round(blurSize.height);
    }

    float scaleX = blurSize.width / texture->getContentSize().width;
    float scaleY = blurSize.height / texture->getContentSize().height;
    float scale = std::max(scaleX, scaleY);
    srcSprite->setScale(scale);
    srcSprite->setAnchorPoint({0.5f, 0.5f});
    srcSprite->setPosition(blurSize * 0.5f);

    auto blurDown = paimon::shaders::getKawaseDownShader();
    auto blurUp   = paimon::shaders::getKawaseUpShader();
    if (!blurDown || !blurUp) {
        srcSprite->setScale(std::max(
            targetSize.width / texture->getContentSize().width,
            targetSize.height / texture->getContentSize().height));
        srcSprite->setPosition(targetSize * 0.5f);
        return srcSprite;
    }

    int passes = std::clamp(static_cast<int>(intensity * 0.8f), 3, 7);

    struct MipLevel {
        CCRenderTexture* rt;
        CCSize size;
    };
    std::vector<MipLevel> mips;
    mips.reserve(passes);

    CCSize currentSize = blurSize;
    CCSprite* currentSprite = srcSprite;

    for (int i = 0; i < passes; ++i) {
        CCSize nextSize = {
            std::max(std::round(currentSize.width * 0.7f), 32.f),
            std::max(std::round(currentSize.height * 0.7f), 32.f)
        };

        auto rt = CCRenderTexture::create(
            static_cast<int>(nextSize.width),
            static_cast<int>(nextSize.height)
        );
        if (!rt) break;

        currentSprite->setShaderProgram(blurDown);
        blurDown->use();
        blurDown->setUniformsForBuiltins();
        blurDown->setUniformLocationWith2f(
            blurDown->getUniformLocationForName("u_halfpixel"),
            0.5f / currentSize.width,
            0.5f / currentSize.height
        );

        if (i == 0) {
            float downScale = std::max(
                nextSize.width / texture->getContentSize().width,
                nextSize.height / texture->getContentSize().height
            );
            currentSprite->setScale(downScale);
            currentSprite->setPosition(nextSize * 0.5f);
        } else {
            currentSprite->setPosition(nextSize * 0.5f);
            float prevW = currentSprite->getContentSize().width;
            float prevH = currentSprite->getContentSize().height;
            float sx = nextSize.width / prevW;
            float sy = nextSize.height / prevH;
            currentSprite->setScale(std::max(sx, sy));
        }

        // beginWithClear: without it, the blur shader blending against an
        // uninitialized FBO produces white LevelCells for thumbnails with
        // translucent alpha.
        rt->beginWithClear(0.f, 0.f, 0.f, 0.f);
        currentSprite->visit();
        rt->end();

        rt->getSprite()->getTexture()->setTexParameters(&linearParams);
        mips.push_back({rt, nextSize});

        currentSprite = CCSprite::createWithTexture(rt->getSprite()->getTexture());
        currentSprite->setAnchorPoint({0.5f, 0.5f});
        currentSize = nextSize;
    }

    if (mips.empty()) return srcSprite;

    for (int i = static_cast<int>(mips.size()) - 1; i >= 0; --i) {
        CCSize upSize = (i > 0) ? mips[i - 1].size : blurSize;

        auto rt = CCRenderTexture::create(
            static_cast<int>(upSize.width),
            static_cast<int>(upSize.height)
        );
        if (!rt) break;

        currentSprite->setShaderProgram(blurUp);
        blurUp->use();
        blurUp->setUniformsForBuiltins();
        blurUp->setUniformLocationWith2f(
            blurUp->getUniformLocationForName("u_halfpixel"),
            0.5f / currentSize.width,
            0.5f / currentSize.height
        );

        float sx = upSize.width / currentSprite->getContentSize().width;
        float sy = upSize.height / currentSprite->getContentSize().height;
        currentSprite->setScale(std::max(sx, sy));
        currentSprite->setPosition(upSize * 0.5f);

        rt->beginWithClear(0.f, 0.f, 0.f, 0.f);
        currentSprite->visit();
        rt->end();

        rt->getSprite()->getTexture()->setTexParameters(&linearParams);

        currentSprite = CCSprite::createWithTexture(rt->getSprite()->getTexture());
        currentSprite->setAnchorPoint({0.5f, 0.5f});
        currentSize = upSize;
    }

    auto finalSprite = currentSprite;
    finalSprite->getTexture()->setTexParameters(&linearParams);
    return finalSprite;
}

CCGLProgram* getBgShaderProgram(std::string const& shaderName) {
    if (shaderName.empty() || shaderName == "none") return nullptr;
    // Static shaders (no u_time animation)
    if (shaderName == "grayscale") return paimon::shaders::loadShader("layerbg-gray"_spr, "cell_vertex.glsl", "grayscale.glsl", nullptr, nullptr);
    if (shaderName == "sepia") return paimon::shaders::loadShader("layerbg-sepia"_spr, "cell_vertex.glsl", "sepia.glsl", nullptr, nullptr);
    if (shaderName == "vignette") return paimon::shaders::loadShader("layerbg-vignette"_spr, "cell_vertex.glsl", "vignette.glsl", nullptr, nullptr);
    if (shaderName == "bloom") return paimon::shaders::loadShader("layerbg-bloom"_spr, "cell_vertex.glsl", "bloom.glsl", nullptr, nullptr);
    if (shaderName == "chromatic") return paimon::shaders::loadShader("layerbg-chromatic"_spr, "cell_vertex.glsl", "chromatic.glsl", nullptr, nullptr);
    if (shaderName == "pixelate") return paimon::shaders::loadShader("layerbg-pixelate"_spr, "cell_vertex.glsl", "pixelate.glsl", nullptr, nullptr);
    if (shaderName == "posterize") return paimon::shaders::loadShader("layerbg-posterize"_spr, "cell_vertex.glsl", "posterize.glsl", nullptr, nullptr);
    if (shaderName == "scanlines") return paimon::shaders::loadShader("layerbg-scanlines"_spr, "cell_vertex.glsl", "scanlines.glsl", nullptr, nullptr);
    // Dynamic/animated shaders (use u_time + u_cursor like LevelInfoLayer)
    if (shaderName == "rain") return paimon::shaders::loadShader("layerbg-rain-dyn"_spr, "cell_vertex.glsl", "rain_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "matrix") return paimon::shaders::loadShader("layerbg-matrix-dyn"_spr, "cell_vertex.glsl", "matrix_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "neon-pulse") return paimon::shaders::loadShader("layerbg-neon-pulse-dyn"_spr, "cell_vertex.glsl", "neon_pulse_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "wave-distortion") return paimon::shaders::loadShader("layerbg-wave-dyn"_spr, "cell_vertex.glsl", "wave_distortion_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "crt") return paimon::shaders::loadShader("layerbg-crt-dyn"_spr, "cell_vertex.glsl", "crt_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "glitch") return paimon::shaders::loadShader("layerbg-glitch-dyn"_spr, "cell_vertex.glsl", "glitch_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "radial-blur") return paimon::shaders::loadShader("layerbg-radial-dyn"_spr, "cell_vertex.glsl", "radial_blur_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "shockwave") return paimon::shaders::loadShader("layerbg-shockwave-dyn"_spr, "cell_vertex.glsl", "shockwave_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "vortex") return paimon::shaders::loadShader("layerbg-vortex-dyn"_spr, "cell_vertex.glsl", "vortex_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "magnetic") return paimon::shaders::loadShader("layerbg-magnetic-dyn"_spr, "cell_vertex.glsl", "magnetic_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "spotlight") return paimon::shaders::loadShader("layerbg-spotlight-dyn"_spr, "cell_vertex.glsl", "spotlight_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "ripple") return paimon::shaders::loadShader("layerbg-ripple-dyn"_spr, "cell_vertex.glsl", "ripple_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "plasma-cursor") return paimon::shaders::loadShader("layerbg-plasma-cursor-dyn"_spr, "cell_vertex.glsl", "plasma_cursor_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "freeze") return paimon::shaders::loadShader("layerbg-freeze-dyn"_spr, "cell_vertex.glsl", "freeze_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "pixelate-cursor") return paimon::shaders::loadShader("layerbg-pixelate-cursor-dyn"_spr, "cell_vertex.glsl", "pixelate_cursor_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "kaleidoscope") return paimon::shaders::loadShader("layerbg-kaleidoscope-dyn"_spr, "cell_vertex.glsl", "kaleidoscope_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "sonar") return paimon::shaders::loadShader("layerbg-sonar-dyn"_spr, "cell_vertex.glsl", "sonar_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "electric-arc") return paimon::shaders::loadShader("layerbg-electric-arc-dyn"_spr, "cell_vertex.glsl", "electric_arc_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "prism-split") return paimon::shaders::loadShader("layerbg-prism-split-dyn"_spr, "cell_vertex.glsl", "prism_split_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "gravity-well") return paimon::shaders::loadShader("layerbg-gravity-well-dyn"_spr, "cell_vertex.glsl", "gravity_well_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "shatter") return paimon::shaders::loadShader("layerbg-shatter-dyn"_spr, "cell_vertex.glsl", "shatter_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "heat-haze") return paimon::shaders::loadShader("layerbg-heat-haze-dyn"_spr, "cell_vertex.glsl", "heat_haze_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "liquify") return paimon::shaders::loadShader("layerbg-liquify-dyn"_spr, "cell_vertex.glsl", "liquify_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "ink-spread") return paimon::shaders::loadShader("layerbg-ink-spread-dyn"_spr, "cell_vertex.glsl", "ink_spread_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "hologram") return paimon::shaders::loadShader("layerbg-hologram-dyn"_spr, "cell_vertex.glsl", "hologram_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "time-warp") return paimon::shaders::loadShader("layerbg-time-warp-dyn"_spr, "cell_vertex.glsl", "time_warp_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "underwater") return paimon::shaders::loadShader("layerbg-underwater-dyn"_spr, "cell_vertex.glsl", "underwater_dynamic.glsl", nullptr, nullptr);
    if (shaderName == "neon-trail") return paimon::shaders::loadShader("layerbg-neon-trail-dyn"_spr, "cell_vertex.glsl", "neon_trail_dynamic.glsl", nullptr, nullptr);

    // Beat-reactive postprocess shaders
    // These transform u_texture (the existing background) using audio FFT
    // uniforms (u_bass / u_mid / u_treble / u_beat / u_energy). They become
    // active when ShaderBgSprite::m_audioReactive > 0 (set by
    // BeatShaderManager). Otherwise the audio uniforms are pushed as 0.0
    // and they degrade gracefully into a static look.
    if (shaderName == "glitch-beat")      return paimon::shaders::loadShader("beat-glitch"_spr,      "cell_vertex.glsl", "glitch_beat.glsl",      nullptr, nullptr);
    if (shaderName == "wave-beat")        return paimon::shaders::loadShader("beat-wave"_spr,        "cell_vertex.glsl", "wave_beat.glsl",        nullptr, nullptr);
    if (shaderName == "chromatic-beat")   return paimon::shaders::loadShader("beat-chromatic"_spr,   "cell_vertex.glsl", "chromatic_beat.glsl",   nullptr, nullptr);
    if (shaderName == "pixelate-beat")    return paimon::shaders::loadShader("beat-pixelate"_spr,    "cell_vertex.glsl", "pixelate_beat.glsl",    nullptr, nullptr);
    if (shaderName == "shockwave-beat")   return paimon::shaders::loadShader("beat-shockwave"_spr,   "cell_vertex.glsl", "shockwave_beat.glsl",   nullptr, nullptr);
    if (shaderName == "rgb-split-beat")   return paimon::shaders::loadShader("beat-rgb-split"_spr,   "cell_vertex.glsl", "rgb_split_beat.glsl",   nullptr, nullptr);
    if (shaderName == "kaleidoscope-beat")return paimon::shaders::loadShader("beat-kaleidoscope"_spr,"cell_vertex.glsl", "kaleidoscope_beat.glsl",nullptr, nullptr);
    if (shaderName == "zoom-pulse-beat")  return paimon::shaders::loadShader("beat-zoom-pulse"_spr,  "cell_vertex.glsl", "zoom_pulse_beat.glsl",  nullptr, nullptr);
    if (shaderName == "scanlines-beat")   return paimon::shaders::loadShader("beat-scanlines"_spr,   "cell_vertex.glsl", "scanlines_beat.glsl",   nullptr, nullptr);
    if (shaderName == "vortex-beat")      return paimon::shaders::loadShader("beat-vortex"_spr,      "cell_vertex.glsl", "vortex_beat.glsl",      nullptr, nullptr);
    if (shaderName == "edge-pulse-beat")  return paimon::shaders::loadShader("beat-edge-pulse"_spr,  "cell_vertex.glsl", "edge_pulse_beat.glsl",  nullptr, nullptr);
    if (shaderName == "hue-shift-beat")   return paimon::shaders::loadShader("beat-hue-shift"_spr,   "cell_vertex.glsl", "hue_shift_beat.glsl",   nullptr, nullptr);
    if (shaderName == "liquid-beat")      return paimon::shaders::loadShader("beat-liquid"_spr,      "cell_vertex.glsl", "liquid_beat.glsl",      nullptr, nullptr);
    if (shaderName == "mosaic-beat")      return paimon::shaders::loadShader("beat-mosaic"_spr,      "cell_vertex.glsl", "mosaic_beat.glsl",      nullptr, nullptr);
    if (shaderName == "dream-beat")       return paimon::shaders::loadShader("beat-dream"_spr,       "cell_vertex.glsl", "dream_beat.glsl",       nullptr, nullptr);

    return nullptr;
}

CCGLProgram* getProceduralBgShaderProgram(std::string const& shaderName) {
    // All procedural backgrounds migrated to .glsl files with inline fallback
    if (shaderName == "aurora") return paimon::shaders::loadShader("layerbg-proc-aurora"_spr, "cell_vertex.glsl", "aurora_bg.glsl", nullptr, nullptr);
    if (shaderName == "nebula") return paimon::shaders::loadShader("layerbg-proc-nebula"_spr, "cell_vertex.glsl", "nebula_bg.glsl", nullptr, nullptr);
    if (shaderName == "plasma") return paimon::shaders::loadShader("layerbg-proc-plasma"_spr, "cell_vertex.glsl", "plasma_bg.glsl", nullptr, nullptr);
    if (shaderName == "grid") return paimon::shaders::loadShader("layerbg-proc-grid"_spr, "cell_vertex.glsl", "grid_bg.glsl", nullptr, nullptr);
    if (shaderName == "spiral") return paimon::shaders::loadShader("layerbg-proc-spiral"_spr, "cell_vertex.glsl", "spiral_bg.glsl", nullptr, nullptr);
    if (shaderName == "warp") return paimon::shaders::loadShader("layerbg-proc-warp"_spr, "cell_vertex.glsl", "warp_bg.glsl", nullptr, nullptr);
    if (shaderName == "lava") return paimon::shaders::loadShader("layerbg-proc-lava"_spr, "cell_vertex.glsl", "lava_bg.glsl", nullptr, nullptr);
    if (shaderName == "clouds") return paimon::shaders::loadShader("layerbg-proc-clouds"_spr, "cell_vertex.glsl", "clouds_bg.glsl", nullptr, nullptr);
    if (shaderName == "rings") return paimon::shaders::loadShader("layerbg-proc-rings"_spr, "cell_vertex.glsl", "rings_bg.glsl", nullptr, nullptr);
    if (shaderName == "waves") return paimon::shaders::loadShader("layerbg-proc-waves"_spr, "cell_vertex.glsl", "waves_bg.glsl", nullptr, nullptr);
    if (shaderName == "hex") return paimon::shaders::loadShader("layerbg-proc-hex"_spr, "cell_vertex.glsl", "hex_bg.glsl", nullptr, nullptr);
    if (shaderName == "fireflies") return paimon::shaders::loadShader("layerbg-proc-fireflies"_spr, "cell_vertex.glsl", "fireflies_bg.glsl", nullptr, nullptr);
    if (shaderName == "ripple") return paimon::shaders::loadShader("layerbg-proc-ripple"_spr, "cell_vertex.glsl", "ripple_bg.glsl", nullptr, nullptr);
    if (shaderName == "starfield") return paimon::shaders::loadShader("layerbg-proc-starfield"_spr, "cell_vertex.glsl", "starfield_bg.glsl", nullptr, nullptr);
    if (shaderName == "tunnel") return paimon::shaders::loadShader("layerbg-proc-tunnel"_spr, "cell_vertex.glsl", "tunnel_bg.glsl", nullptr, nullptr);
    if (shaderName == "checker") return paimon::shaders::loadShader("layerbg-proc-checker"_spr, "cell_vertex.glsl", "checker_bg.glsl", nullptr, nullptr);
    if (shaderName == "digital-rain") return paimon::shaders::loadShader("layerbg-proc-digital-rain"_spr, "cell_vertex.glsl", "digital_rain_bg.glsl", nullptr, nullptr);
    if (shaderName == "horizon") return paimon::shaders::loadShader("layerbg-proc-horizon"_spr, "cell_vertex.glsl", "horizon_bg.glsl", nullptr, nullptr);
    if (shaderName == "fractal") return paimon::shaders::loadShader("layerbg-proc-fractal"_spr, "cell_vertex.glsl", "fractal_bg.glsl", nullptr, nullptr);
    if (shaderName == "gradient-flow") return paimon::shaders::loadShader("layerbg-proc-gradient-flow"_spr, "cell_vertex.glsl", "gradient_flow_bg.glsl", nullptr, nullptr);
    if (shaderName == "bubbles") return paimon::shaders::loadShader("layerbg-proc-bubbles"_spr, "cell_vertex.glsl", "bubbles_bg.glsl", nullptr, nullptr);
    if (shaderName == "lightning") return paimon::shaders::loadShader("layerbg-proc-lightning"_spr, "cell_vertex.glsl", "lightning_bg.glsl", nullptr, nullptr);
    if (shaderName == "moire") return paimon::shaders::loadShader("layerbg-proc-moire"_spr, "cell_vertex.glsl", "moire_bg.glsl", nullptr, nullptr);
    if (shaderName == "crystal") return paimon::shaders::loadShader("layerbg-proc-crystal"_spr, "cell_vertex.glsl", "crystal_bg.glsl", nullptr, nullptr);
    if (shaderName == "embers") return paimon::shaders::loadShader("layerbg-proc-embers"_spr, "cell_vertex.glsl", "embers_bg.glsl", nullptr, nullptr);
    if (shaderName == "prism") return paimon::shaders::loadShader("layerbg-proc-prism"_spr, "cell_vertex.glsl", "prism_bg.glsl", nullptr, nullptr);
    if (shaderName == "soft-noise") return paimon::shaders::loadShader("layerbg-proc-soft-noise"_spr, "cell_vertex.glsl", "soft_noise_bg.glsl", nullptr, nullptr);
    if (shaderName == "pulse") return paimon::shaders::loadShader("layerbg-proc-pulse"_spr, "cell_vertex.glsl", "pulse_bg.glsl", nullptr, nullptr);
    if (shaderName == "topo") return paimon::shaders::loadShader("layerbg-proc-topo"_spr, "cell_vertex.glsl", "topo_bg.glsl", nullptr, nullptr);
    if (shaderName == "bloom-field") return paimon::shaders::loadShader("layerbg-proc-bloom-field"_spr, "cell_vertex.glsl", "bloom_field_bg.glsl", nullptr, nullptr);
    if (shaderName == "synthwave") return paimon::shaders::loadShader("layerbg-proc-synthwave"_spr, "cell_vertex.glsl", "synthwave_bg.glsl", nullptr, nullptr);
    if (shaderName == "neon-city") return paimon::shaders::loadShader("layerbg-proc-neon-city"_spr, "cell_vertex.glsl", "neon_city_bg.glsl", nullptr, nullptr);
    if (shaderName == "vortex") return paimon::shaders::loadShader("layerbg-proc-vortex"_spr, "cell_vertex.glsl", "vortex_bg.glsl", nullptr, nullptr);
    if (shaderName == "ocean") return paimon::shaders::loadShader("layerbg-proc-ocean"_spr, "cell_vertex.glsl", "ocean_bg.glsl", nullptr, nullptr);
    if (shaderName == "galaxy") return paimon::shaders::loadShader("layerbg-proc-galaxy"_spr, "cell_vertex.glsl", "galaxy_bg.glsl", nullptr, nullptr);
    return nullptr;
}

CCGLProgram* getBeatShaderProgram(std::string const& shaderName) {
    if (shaderName.empty() || shaderName == "none") return nullptr;
    if (shaderName == "beat-bars")     return paimon::shaders::loadShader("beat-bars"_spr,     "cell_vertex.glsl", "beat_bars.glsl",     nullptr, nullptr);
    if (shaderName == "beat-circles")  return paimon::shaders::loadShader("beat-circles"_spr,  "cell_vertex.glsl", "beat_circles.glsl",  nullptr, nullptr);
    if (shaderName == "beat-grid")     return paimon::shaders::loadShader("beat-grid"_spr,     "cell_vertex.glsl", "beat_grid.glsl",     nullptr, nullptr);
    if (shaderName == "freq-spectrum") return paimon::shaders::loadShader("freq-spectrum"_spr, "cell_vertex.glsl", "freq_spectrum.glsl", nullptr, nullptr);
    if (shaderName == "beat-tunnel")   return paimon::shaders::loadShader("beat-tunnel"_spr,   "cell_vertex.glsl", "beat_tunnel.glsl",   nullptr, nullptr);
    if (shaderName == "audio-aurora")  return paimon::shaders::loadShader("audio-aurora"_spr,  "cell_vertex.glsl", "audio_aurora.glsl",  nullptr, nullptr);
    return nullptr;
}

namespace {

void runStaggeredPrewarm(
    char const* label,
    std::vector<std::function<void()>> steps,
    float delaySec,
    size_t stepsPerTick
) {
    if (steps.empty()) return;

    struct State {
        std::vector<std::function<void()>> steps;
        size_t index = 0;
        size_t perTick = 2;
        float delay = 0.08f;
    };

    auto state = std::make_shared<State>(State{
        std::move(steps),
        0,
        std::max<size_t>(1, stepsPerTick),
        std::max(0.05f, delaySec),
    });

    geode::log::info("[Shaders] Stagger prewarm '{}' ({} steps, {} per tick)",
        label, state->steps.size(), state->perTick);

    auto tick = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weakTick = tick;
    *tick = [state, label, weakTick]() {
        if (paimon::isRuntimeShuttingDown()) return;

        size_t done = 0;
        while (state->index < state->steps.size() && done < state->perTick) {
            if (state->steps[state->index]) state->steps[state->index]();
            ++state->index;
            ++done;
        }

        if (state->index < state->steps.size()) {
            // Strong ref only in the pending continuation; the closure holds a weak
            // self-ref to avoid a self-owning shared_ptr cycle (permanent leak).
            if (auto strong = weakTick.lock()) {
                paimon::scheduleMainThreadDelay(state->delay, [strong]() { (*strong)(); });
            }
        } else {
            geode::log::info("[Shaders] Stagger prewarm '{}' complete", label);
        }
    };

    (*tick)();
}

} // namespace

void prewarmLevelInfoShaders() {
    prewarmLevelInfoShadersStaggered();
}

void prewarmLevelInfoShadersStaggered() {
    static bool started = false;
    if (started) return;
    started = true;

    std::vector<std::function<void()>> steps;
    steps.push_back([] { paimon::shaders::loadShader("grayscale"_spr, "cell_vertex.glsl", "grayscale.glsl", nullptr, nullptr); });
    steps.push_back([] { paimon::shaders::loadShader("sepia"_spr, "cell_vertex.glsl", "sepia.glsl", nullptr, nullptr); });
    steps.push_back([] { paimon::shaders::loadShader("vignette"_spr, "cell_vertex.glsl", "vignette.glsl", nullptr, nullptr); });
    steps.push_back([] { paimon::shaders::loadShader("scanlines"_spr, "cell_vertex.glsl", "scanlines.glsl", nullptr, nullptr); });
    steps.push_back([] { paimon::shaders::loadShader("bloom"_spr, "cell_vertex.glsl", "bloom.glsl", nullptr, nullptr); });
    steps.push_back([] { paimon::shaders::loadShader("chromatic-v2"_spr, "cell_vertex.glsl", "chromatic.glsl", nullptr, nullptr); });
    steps.push_back([] { paimon::shaders::loadShader("radial-blur-v2"_spr, "cell_vertex.glsl", "radial_blur.glsl", nullptr, nullptr); });
    steps.push_back([] { paimon::shaders::loadShader("glitch-v2"_spr, "cell_vertex.glsl", "glitch.glsl", nullptr, nullptr); });
    steps.push_back([] { paimon::shaders::loadShader("posterize"_spr, "cell_vertex.glsl", "posterize.glsl", nullptr, nullptr); });
    steps.push_back([] { paimon::shaders::loadShader("rain"_spr, "cell_vertex.glsl", "rain.glsl", nullptr, nullptr); });
    steps.push_back([] { paimon::shaders::loadShader("matrix"_spr, "cell_vertex.glsl", "matrix.glsl", nullptr, nullptr); });
    steps.push_back([] { paimon::shaders::loadShader("neon-pulse"_spr, "cell_vertex.glsl", "neon_pulse.glsl", nullptr, nullptr); });
    steps.push_back([] { paimon::shaders::loadShader("wave-distortion"_spr, "cell_vertex.glsl", "wave_distortion.glsl", nullptr, nullptr); });
    steps.push_back([] { paimon::shaders::loadShader("crt"_spr, "cell_vertex.glsl", "crt.glsl", nullptr, nullptr); });
    steps.push_back([] { paimon::shaders::loadShader("pixelate"_spr, "cell_vertex.glsl", "pixelate.glsl", nullptr, nullptr); });
    steps.push_back([] { (void)paimon::shaders::getBlurHorizontalShader(); });
    steps.push_back([] { (void)paimon::shaders::getBlurVerticalShader(); });
    steps.push_back([] { (void)paimon::shaders::getBlurSinglePassShader(); });
    steps.push_back([] { (void)paimon::shaders::getKawaseDownShader(); });
    steps.push_back([] { (void)paimon::shaders::getKawaseUpShader(); });
    steps.push_back([] { (void)paimon::shaders::getKawaseRealtimeShader(); });
    steps.push_back([] { (void)paimon::shaders::getBlurCellShader(); });
    steps.push_back([] { (void)paimon::shaders::getBlurFastShader(); });
    steps.push_back([] { (void)paimon::shaders::getYUVShader(); });
    steps.push_back([] { (void)paimon::shaders::getYUVBlitShader(); });
    steps.push_back([] { (void)paimon::shaders::getDominantColorsDownsampleShader(); });

    runStaggeredPrewarm("LevelInfo", std::move(steps), 0.08f, 2);
}

void prewarmConfiguredBackgroundShaders() {
    prewarmConfiguredBackgroundShadersStaggered();
}

void prewarmConfiguredBackgroundShadersStaggered() {
    static bool started = false;
    if (started) return;
    started = true;

    std::vector<std::string> shaderNames;
    for (auto const& [key, _name] : LayerBackgroundManager::LAYER_OPTIONS) {
        auto cfg = LayerBackgroundManager::get().resolveConfig(key);
        if (cfg.shader.empty() || cfg.shader == "none") continue;
        if (std::find(shaderNames.begin(), shaderNames.end(), cfg.shader) != shaderNames.end()) {
            continue;
        }
        shaderNames.push_back(cfg.shader);
    }

    if (shaderNames.empty()) return;

    std::vector<std::function<void()>> steps;
    steps.reserve(shaderNames.size());
    for (auto const& name : shaderNames) {
        steps.push_back([name]() { (void)getBgShaderProgram(name); });
    }

    runStaggeredPrewarm("configured-bg", std::move(steps), 0.1f, 1);
}

// ProgressiveBlurJob — multi-frame blur engine

ProgressiveBlurJob::~ProgressiveBlurJob() {
    cancel();
}

CCSprite* ProgressiveBlurJob::capSourceTexture(CCTexture2D* texture, CCSize const& targetSize) {
    // If the source texture is larger than targetSize, pre-render to targetSize so
    // blur cost is independent of the original resolution.
    float texW = texture->getContentSize().width;
    float texH = texture->getContentSize().height;

    if (texW <= targetSize.width && texH <= targetSize.height) {
        // texture is already <= target, no cap needed
        auto spr = CCSprite::createWithTexture(texture);
        if (!spr) return nullptr;
        float sx = targetSize.width / texW;
        float sy = targetSize.height / texH;
        spr->setScale(std::max(sx, sy));
        spr->setAnchorPoint({0.5f, 0.5f});
        spr->setPosition(targetSize * 0.5f);
        spr->setFlipY(true);
        return spr;
    }

    // pre-render to targetSize
    auto srcSprite = CCSprite::createWithTexture(texture);
    if (!srcSprite) return nullptr;
    float sx = targetSize.width / texW;
    float sy = targetSize.height / texH;
    srcSprite->setScale(std::max(sx, sy));
    srcSprite->setAnchorPoint({0.5f, 0.5f});
    srcSprite->setPosition(targetSize * 0.5f);
    srcSprite->setFlipY(true);

    ccTexParams linearParams{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    texture->setTexParameters(&linearParams);

    auto rt = CCRenderTexture::create(
        static_cast<int>(targetSize.width),
        static_cast<int>(targetSize.height));
    if (!rt) return srcSprite; // fallback

    rt->beginWithClear(0.f, 0.f, 0.f, 0.f);
    srcSprite->visit();
    rt->end();

    rt->getSprite()->getTexture()->setTexParameters(&linearParams);

    auto capped = CCSprite::createWithTexture(rt->getSprite()->getTexture());
    capped->setFlipY(true);
    capped->setAnchorPoint({0.5f, 0.5f});
    capped->setPosition(targetSize * 0.5f);
    return capped;
}

ProgressiveBlurJob* ProgressiveBlurJob::createGaussian(
    CCTexture2D* texture, CCSize const& targetSize,
    float intensity, CompletionCallback onComplete)
{
    auto job = new ProgressiveBlurJob();
    if (job && job->initGaussian(texture, targetSize, intensity, std::move(onComplete))) {
        job->autorelease();
        return job;
    }
    delete job;
    return nullptr;
}

bool ProgressiveBlurJob::initGaussian(
    CCTexture2D* texture, CCSize const& targetSize,
    float intensity, CompletionCallback onComplete)
{
    if (!texture || targetSize.width <= 0 || targetSize.height <= 0) return false;

    m_blurType = BlurType::Gaussian;
    m_targetSize = targetSize;
    m_intensity = intensity;
    m_onComplete = std::move(onComplete);
    m_sourceTexture = texture;
    m_phase = Phase::Setup;

    return true;
}

ProgressiveBlurJob* ProgressiveBlurJob::createPaimonBlur(
    CCTexture2D* texture, CCSize const& targetSize,
    float intensity, CompletionCallback onComplete)
{
    auto job = new ProgressiveBlurJob();
    if (job && job->initPaimonBlur(texture, targetSize, intensity, std::move(onComplete))) {
        job->autorelease();
        return job;
    }
    delete job;
    return nullptr;
}

bool ProgressiveBlurJob::initPaimonBlur(
    CCTexture2D* texture, CCSize const& targetSize,
    float intensity, CompletionCallback onComplete)
{
    if (!texture || targetSize.width <= 0 || targetSize.height <= 0) return false;

    m_blurType = BlurType::PaimonBlur;
    m_targetSize = targetSize;
    m_intensity = intensity;
    m_onComplete = std::move(onComplete);
    m_sourceTexture = texture;
    m_phase = Phase::Setup;

    return true;
}

void ProgressiveBlurJob::start() {
    if (m_started || m_cancelled || m_done) return;
    auto* director = CCDirector::get();
    if (!director || !director->getScheduler()) return;
    m_started = true;
    retain(); // prevent dealloc while scheduled
    director->getScheduler()->scheduleSelector(
        schedule_selector(ProgressiveBlurJob::tick), this, 0.0f, false);

    // Fast mode: running the first tick synchronously removes ~16ms of latency.
    // Only for single-shot jobs (priority blur); kept off for batch (list cells)
    // so work spreads across frames instead of spiking when N cells queue blur.
    if (m_fastMode && !m_cancelled && !m_done) {
        tick(0.0f);
    }
}

void ProgressiveBlurJob::cancel() {
    if (m_cancelled) return;
    m_cancelled = true;
    if (m_started && !m_done) {
        if (auto* director = CCDirector::get()) {
            if (auto* scheduler = director->getScheduler()) {
                scheduler->unscheduleSelector(
                    schedule_selector(ProgressiveBlurJob::tick), this);
            }
        }
        release(); // balance the retain from start()
    }
    // free resources
    m_onComplete = nullptr;
    m_currentSprite = nullptr;
    m_rtA = nullptr;
    m_rtB = nullptr;
    m_mips.clear();
    m_sourceTexture = nullptr;
}

void ProgressiveBlurJob::finish(CCSprite* result) {
    m_done = true;
    if (auto* director = CCDirector::get()) {
        if (auto* scheduler = director->getScheduler()) {
            scheduler->unscheduleSelector(
                schedule_selector(ProgressiveBlurJob::tick), this);
        }
    }

    if (m_onComplete) {
        auto cb = std::move(m_onComplete);
        m_onComplete = nullptr;
        cb(result);
    }

    // free intermediate resources
    m_currentSprite = nullptr;
    m_rtA = nullptr;
    m_rtB = nullptr;
    m_mips.clear();
    m_sourceTexture = nullptr;

    release(); // balance the retain from start()
}

void ProgressiveBlurJob::tick(float dt) {
    if (m_cancelled || m_done) return;

    if (m_blurType == BlurType::PaimonBlur)
        tickPaimonBlur();
    else
        tickGaussian();
}

void ProgressiveBlurJob::tickGaussian() {
    ccTexParams linearParams{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};

    if (m_phase == Phase::Setup) {
        m_blurH = paimon::shaders::getBlurHorizontalShader();
        m_blurV = paimon::shaders::getBlurVerticalShader();
        if (!m_blurH || !m_blurV) {
            // fallback: return an unblurred sprite
            auto fallback = capSourceTexture(m_sourceTexture, m_targetSize);
            finish(fallback);
            return;
        }

        m_rtA = CCRenderTexture::create(m_targetSize.width, m_targetSize.height);
        m_rtB = CCRenderTexture::create(m_targetSize.width, m_targetSize.height);
        if (!m_rtA || !m_rtB) {
            auto fallback = capSourceTexture(m_sourceTexture, m_targetSize);
            finish(fallback);
            return;
        }

        m_currentSprite = capSourceTexture(m_sourceTexture, m_targetSize);
        if (!m_currentSprite) {
            finish(nullptr);
            return;
        }
        m_sourceTexture->setTexParameters(&linearParams);

        m_radius = intensityToBlurRadius(m_intensity);
        m_phase = Phase::GaussianH1;
        if (!m_fastMode) return; 
    }

    if (m_phase == Phase::GaussianH1) {
        applyBlurPass(m_currentSprite, m_rtA, m_blurH, m_targetSize, m_radius);

        auto midSprite = CCSprite::createWithTexture(m_rtA->getSprite()->getTexture());
        midSprite->setFlipY(true);
        midSprite->setAnchorPoint({0.5f, 0.5f});
        midSprite->setPosition(m_targetSize * 0.5f);
        midSprite->getTexture()->setTexParameters(&linearParams);
        m_currentSprite = midSprite;

        m_phase = Phase::GaussianV1;
        return;
    }

    if (m_phase == Phase::GaussianV1) {
        applyBlurPass(m_currentSprite, m_rtB, m_blurV, m_targetSize, m_radius);

        // Low intensity (< 4.0): single H+V pass is visually sufficient.
        // Skip the second pair to halve GPU work and reduce frame latency.
        if (m_intensity < 4.0f) {
            auto finalSprite = CCSprite::createWithTexture(m_rtB->getSprite()->getTexture());
            finalSprite->setAnchorPoint({0.5f, 0.5f});
            finalSprite->setFlipY(true);
            finalSprite->getTexture()->setTexParameters(&linearParams);
            finish(finalSprite);
            return;
        }

        auto mid2 = CCSprite::createWithTexture(m_rtB->getSprite()->getTexture());
        mid2->setFlipY(true);
        mid2->setAnchorPoint({0.5f, 0.5f});
        mid2->setPosition(m_targetSize * 0.5f);
        mid2->getTexture()->setTexParameters(&linearParams);
        m_currentSprite = mid2;

        m_phase = Phase::GaussianH2;
        return;
    }

    if (m_phase == Phase::GaussianH2) {
        applyBlurPass(m_currentSprite, m_rtA, m_blurH, m_targetSize, m_radius * 0.8f);

        auto mid3 = CCSprite::createWithTexture(m_rtA->getSprite()->getTexture());
        mid3->setFlipY(true);
        mid3->setAnchorPoint({0.5f, 0.5f});
        mid3->setPosition(m_targetSize * 0.5f);
        mid3->getTexture()->setTexParameters(&linearParams);
        m_currentSprite = mid3;

        m_phase = Phase::GaussianV2;
        return;
    }

    if (m_phase == Phase::GaussianV2) {
        applyBlurPass(m_currentSprite, m_rtB, m_blurV, m_targetSize, m_radius * 0.8f);

        auto finalSprite = CCSprite::createWithTexture(m_rtB->getSprite()->getTexture());
        finalSprite->setAnchorPoint({0.5f, 0.5f});
        finalSprite->setFlipY(true);
        finalSprite->getTexture()->setTexParameters(&linearParams);
        finish(finalSprite);
        return;
    }
}

void ProgressiveBlurJob::tickPaimonBlur() {
    ccTexParams linearParams{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};

    if (m_phase == Phase::Setup) {
        m_blurDown = paimon::shaders::getKawaseDownShader();
        m_blurUp = paimon::shaders::getKawaseUpShader();
        if (!m_blurDown || !m_blurUp) {
            auto fallback = capSourceTexture(m_sourceTexture, m_targetSize);
            finish(fallback);
            return;
        }

        // passes tuned to avoid excessive blur and square artifacts
        m_totalPasses = std::clamp(static_cast<int>(m_intensity * 0.55f), 3, 5);
        m_currentPass = 0;
        m_mips.clear();
        m_mips.reserve(m_totalPasses);

        m_currentSprite = capSourceTexture(m_sourceTexture, m_targetSize);
        if (!m_currentSprite) {
            finish(nullptr);
            return;
        }
        m_sourceTexture->setTexParameters(&linearParams);
        m_currentSize = CCSize{std::round(m_targetSize.width), std::round(m_targetSize.height)};

        m_phase = Phase::Downsample;
        if (!m_fastMode) return; // yield — in batch mode setup consumes the whole tick
        // fast mode: continue to the next phase in the same tick
    }

    if (m_phase == Phase::Downsample) {
        // Run N downsample passes. Adaptive budget: fast mode (priority single-shot)
        // does more per tick; batch mode (list cells) does 1 op/tick to spread load
        // across frames and avoid spikes.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
        const int kOpsBudget = m_fastMode ? 2 : 1;
#else
        const int kOpsBudget = m_fastMode ? 3 : 1;
#endif
        int opsThisTick = 0;
        while (m_currentPass < m_totalPasses && opsThisTick < kOpsBudget) {
            CCSize nextSize = {
                std::max(std::round(m_currentSize.width * 0.7f), 32.f),
                std::max(std::round(m_currentSize.height * 0.7f), 32.f)
            };

            auto rt = CCRenderTexture::create(
                static_cast<int>(nextSize.width),
                static_cast<int>(nextSize.height));
            if (!rt) break;

            m_currentSprite->setShaderProgram(m_blurDown);
            m_blurDown->use();
            m_blurDown->setUniformsForBuiltins();
            m_blurDown->setUniformLocationWith2f(
                m_blurDown->getUniformLocationForName("u_halfpixel"),
                0.5f / m_currentSize.width,
                0.5f / m_currentSize.height);

            // scale the sprite to fill the smaller render target
            if (m_currentPass == 0) {
                float downScale = std::max(
                    nextSize.width / m_currentSprite->getContentSize().width,
                    nextSize.height / m_currentSprite->getContentSize().height);
                m_currentSprite->setScale(downScale);
                m_currentSprite->setPosition(nextSize * 0.5f);
            } else {
                float prevW = m_currentSprite->getContentSize().width;
                float prevH = m_currentSprite->getContentSize().height;
                float ssx = nextSize.width / prevW;
                float ssy = nextSize.height / prevH;
                m_currentSprite->setScale(std::max(ssx, ssy));
                m_currentSprite->setPosition(nextSize * 0.5f);
            }

            rt->beginWithClear(0.f, 0.f, 0.f, 0.f);
            m_currentSprite->visit();
            rt->end();

            rt->getSprite()->getTexture()->setTexParameters(&linearParams);
            m_mips.push_back({rt, nextSize});

            auto nextSprite = CCSprite::createWithTexture(rt->getSprite()->getTexture());
            nextSprite->setFlipY(true);
            nextSprite->setAnchorPoint({0.5f, 0.5f});

            m_currentSprite = nextSprite;
            m_currentSize = nextSize;
            m_currentPass++;
            opsThisTick++;

            // No "free ops" for small passes: even 128x128 passes cost ~0.1-0.2ms
            // in driver state changes, and batching several per frame causes
            // micro-stutter at high fps.
        }

        if (m_currentPass >= m_totalPasses || m_mips.empty()) {
            // move to upsample
            m_currentPass = static_cast<int>(m_mips.size()) - 1;
            m_phase = Phase::Upsample;
            // Only chain into upsample in fast mode; in batch mode yield the tick
            // to avoid ~20 FBO passes in one frame when several jobs run in parallel.
            if (!m_fastMode) return;
        } else {
            return;
        }
    }

    if (m_phase == Phase::Upsample) {
        // run upsample passes per tick; adaptive budget
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
        const int kUpOpsBudget = m_fastMode ? 2 : 1;
#else
        const int kUpOpsBudget = m_fastMode ? 3 : 1;
#endif
        int opsThisTick = 0;
        while (m_currentPass >= 0 && opsThisTick < kUpOpsBudget) {
            CCSize upSize = (m_currentPass > 0) ? m_mips[m_currentPass - 1].size : CCSize{std::round(m_targetSize.width), std::round(m_targetSize.height)};

            auto rt = CCRenderTexture::create(
                static_cast<int>(upSize.width),
                static_cast<int>(upSize.height));
            if (!rt) break;

            m_currentSprite->setShaderProgram(m_blurUp);
            m_blurUp->use();
            m_blurUp->setUniformsForBuiltins();
            m_blurUp->setUniformLocationWith2f(
                m_blurUp->getUniformLocationForName("u_halfpixel"),
                0.5f / m_currentSize.width,
                0.5f / m_currentSize.height);

            float ssx = upSize.width / m_currentSprite->getContentSize().width;
            float ssy = upSize.height / m_currentSprite->getContentSize().height;
            m_currentSprite->setScale(std::max(ssx, ssy));
            m_currentSprite->setPosition(upSize * 0.5f);

            rt->beginWithClear(0.f, 0.f, 0.f, 0.f);
            m_currentSprite->visit();
            rt->end();

            rt->getSprite()->getTexture()->setTexParameters(&linearParams);

            auto upSprite = CCSprite::createWithTexture(rt->getSprite()->getTexture());
            upSprite->setFlipY(true);
            upSprite->setAnchorPoint({0.5f, 0.5f});

            m_currentSprite = upSprite;
            m_currentSize = upSize;
            m_currentPass--;
            opsThisTick++;

            // no batching of small passes (360fps)
        }

        if (m_currentPass < 0) {
            // done
            auto finalSprite = m_currentSprite.data();
            finalSprite->getTexture()->setTexParameters(&linearParams);
            finish(finalSprite);
        }
    }
}

// ShaderBgSprite implementation
// Pushes per-frame uniforms to whatever shader is currently bound to the
// sprite. Audio uniforms (u_bass / u_mid / u_treble / u_beat / u_energy)
// are gated by m_audioReactive — when the beat-shaders feature is
// disabled the values are pushed as 0.0 and the shader behaves as if no
// audio were attached. PaimonAudio::update is called from updateShaderTime
// when audio is active so the FFT spectrum stays current.

namespace {
    // Per-frame guard: many ShaderBgSprite instances can exist in the same
    // scene (e.g. during a layer transition). They all want u_bass / etc to
    // be up-to-date but PaimonAudio::update only needs to run once per
    // frame; running it repeatedly compounds dt smoothing.
    uint64_t g_lastShaderAudioFrame = 0;

    // Global beat-shaders feature flag, set by BeatShaderManager whenever the
    // user toggles the feature. ShaderBgSprite reads it every draw() so newly
    // created sprites pick up the audio-reactive state without anybody
    // having to hunt them down and flip m_audioReactive manually.
    std::atomic<bool> g_beatShadersGloballyEnabled{false};

    uint64_t currentFrameForAudio() {
        auto* dir = CCDirector::get();
        return dir ? static_cast<uint64_t>(dir->getTotalFrames()) : 0;
    }
}

namespace ShaderBgSpriteAudioGate {
    void setEnabled(bool e) { g_beatShadersGloballyEnabled.store(e, std::memory_order_relaxed); }
    bool isEnabled()        { return g_beatShadersGloballyEnabled.load(std::memory_order_relaxed); }
}

void ShaderBgSprite::draw() {
    auto* shader = getShaderProgram();
    if (shader) {
        shader->use();
        shader->setUniformsForBuiltins();

        GLint loc;
        loc = shader->getUniformLocationForName("u_intensity");
        if (loc != -1) shader->setUniformLocationWith1f(loc, m_shaderIntensity);

        loc = shader->getUniformLocationForName("u_screenSize");
        if (loc != -1) shader->setUniformLocationWith2f(loc, m_screenW, m_screenH);

        loc = shader->getUniformLocationForName("u_time");
        if (loc != -1) shader->setUniformLocationWith1f(loc, m_shaderTime);

        loc = shader->getUniformLocationForName("u_texSize");
        if (loc != -1) {
            auto* t = getTexture();
            float tw = t ? static_cast<float>(t->getPixelsWide()) : 1.f;
            float th = t ? static_cast<float>(t->getPixelsHigh()) : 1.f;
            shader->setUniformLocationWith2f(loc, tw, th);
        }

        // Cursor / click — kept for the existing dynamic shaders.
        loc = shader->getUniformLocationForName("u_cursor");
        if (loc != -1) shader->setUniformLocationWith2f(loc, m_cursorX, 1.0f - m_cursorY);

        loc = shader->getUniformLocationForName("u_click");
        if (loc != -1) shader->setUniformLocationWith1f(loc, m_clickState);

        // Audio reactivity — gated by the global beat-shaders flag. We
        // intentionally ignore m_audioReactive here so that ShaderBgSprites
        // created by LayerBackgroundManager (which never sets that field)
        // still react to the music when the user has the feature enabled.
        bool audioGate = g_beatShadersGloballyEnabled.load(std::memory_order_relaxed);
        float bass = 0.f, mid = 0.f, treble = 0.f, beat = 0.f, energy = 0.f;
        if (audioGate) {
            auto& audio = PaimonAudio::get();
            // Pump audio analysis here too as a safety net — updateShaderTime
            // also calls update() but we cover the case where the sprite is
            // drawn before its scheduler tick (or scheduling failed).
            static uint64_t s_lastDrawAudioFrame = 0;
            auto* dir = CCDirector::get();
            if (!dir) return;
            auto frame = static_cast<uint64_t>(dir->getTotalFrames());
            if (frame != s_lastDrawAudioFrame) {
                s_lastDrawAudioFrame = frame;
                audio.update(dir->getDeltaTime());
            }
            bass   = audio.bass()      * m_bassMult;
            mid    = audio.mid()       * m_midMult;
            treble = audio.treble()    * m_trebleMult;
            beat   = audio.beatPulse() * m_beatMult;
            energy = audio.energy()    * m_energyMult;
            // Soft clamp — peaks above 2 / 1 are visually unstable.
            if (bass   > 2.f) bass   = 2.f;
            if (mid    > 2.f) mid    = 2.f;
            if (treble > 2.f) treble = 2.f;
            if (beat   > 1.f) beat   = 1.f;
            if (energy > 2.f) energy = 2.f;
        }

        loc = shader->getUniformLocationForName("u_bass");
        if (loc != -1) shader->setUniformLocationWith1f(loc, bass);

        loc = shader->getUniformLocationForName("u_mid");
        if (loc != -1) shader->setUniformLocationWith1f(loc, mid);

        loc = shader->getUniformLocationForName("u_treble");
        if (loc != -1) shader->setUniformLocationWith1f(loc, treble);

        loc = shader->getUniformLocationForName("u_beat");
        if (loc != -1) shader->setUniformLocationWith1f(loc, beat);

        loc = shader->getUniformLocationForName("u_energy");
        if (loc != -1) shader->setUniformLocationWith1f(loc, energy);
    }
    CCSprite::draw();
}

void ShaderBgSprite::updateShaderTime(float dt) {
    m_shaderTime += dt;

    // CCEGLView::getMousePosition is not exposed by Geode's Cocos bindings on
    // every target, so keep cursor-driven shader motion desktop-only.
#if defined(GEODE_IS_WINDOWS)
    auto* director = CCDirector::get();
    auto* glView = director ? director->getOpenGLView() : nullptr;
    if (glView) {
        auto mousePos = glView->getMousePosition();
        auto winSize = director->getWinSize();
        if (winSize.width > 0.f && winSize.height > 0.f) {
            float nx = mousePos.x / winSize.width;
            float ny = mousePos.y / winSize.height;
            m_cursorX = nx < 0.f ? 0.f : (nx > 1.f ? 1.f : nx);
            m_cursorY = ny < 0.f ? 0.f : (ny > 1.f ? 1.f : ny);
        }
    }
#endif

    // Drive PaimonAudio once per frame globally — multiple ShaderBgSprite
    // instances coexist during transitions, but only one update is needed.
    if (g_beatShadersGloballyEnabled.load(std::memory_order_relaxed)) {
        uint64_t frame = currentFrameForAudio();
        if (frame != g_lastShaderAudioFrame) {
            g_lastShaderAudioFrame = frame;
            PaimonAudio::get().update(dt);

            // Periodic diagnostic — once per second emit current audio
            // levels so the user can verify the FFT is producing data.
            // Using static counters keeps state local.
            static int s_logAccum = 0;
            ++s_logAccum;
            if (s_logAccum >= 60) {
                s_logAccum = 0;
                auto& a = PaimonAudio::get();
                geode::log::info(
                    "[ShaderBgSprite] audio: bass={:.2f} mid={:.2f} treble={:.2f} beat={:.2f} energy={:.2f} active={}",
                    a.bass(), a.mid(), a.treble(), a.beatPulse(), a.energy(), a.isActive()
                );
            }
        }
    }
}

} // namespace Shaders

