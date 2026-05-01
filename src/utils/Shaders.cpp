#include "Shaders.hpp"
#include <Geode/Geode.hpp>
#include <Geode/loader/Log.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace Shaders {

CCGLProgram* getOrCreateShader(char const* key, char const* vertexSrc, char const* fragmentSrc) {
    auto shaderCache = CCShaderCache::sharedShaderCache();
    if (auto program = shaderCache->programForKey(key)) {
        return program;
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
    program->setUniformLocationWith2f(
        program->getUniformLocationForName("u_screenSize"),
        size.width, size.height
    );
    program->setUniformLocationWith1f(
        program->getUniformLocationForName("u_radius"),
        radius
    );

    output->begin();
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

    // ── Optimization: downscale before blurring ──
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

    auto blurH = getOrCreateShader("blur-h-v2"_spr, vertexShaderCell, fragmentShaderHorizontal);
    auto blurV = getOrCreateShader("blur-v-v2"_spr, vertexShaderCell, fragmentShaderVertical);

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

    if (!useDirectRadius) {
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

CCGLProgram* getBlurCellShader() {
    return getOrCreateShader("paimon_cell_blur", vertexShaderCell, fragmentShaderBlurCell);
}

CCGLProgram* getBlurSinglePassShader() {
    return getOrCreateShader("blur-single"_spr, vertexShaderCell, fragmentShaderBlurSinglePass);
}

CCGLProgram* getPaimonBlurShader() {
    return getOrCreateShader("paimonblur-rt"_spr, vertexShaderCell, fragmentShaderPaimonBlurRT);
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

    float scaleX = targetSize.width / texture->getContentSize().width;
    float scaleY = targetSize.height / texture->getContentSize().height;
    float scale = std::max(scaleX, scaleY);
    srcSprite->setScale(scale);
    srcSprite->setAnchorPoint({0.5f, 0.5f});
    srcSprite->setPosition(targetSize * 0.5f);

    auto blurDown = getOrCreateShader("paimonblur-down"_spr, vertexShaderCell, fragmentShaderPaimonBlurDown);
    auto blurUp   = getOrCreateShader("paimonblur-up"_spr,   vertexShaderCell, fragmentShaderPaimonBlurUp);
    if (!blurDown || !blurUp) {
        return srcSprite;
    }

    int passes = std::clamp(static_cast<int>(intensity * 0.8f), 3, 7);

    struct MipLevel {
        CCRenderTexture* rt;
        CCSize size;
    };
    std::vector<MipLevel> mips;
    mips.reserve(passes);

    CCSize currentSize = targetSize;
    CCSprite* currentSprite = srcSprite;

    for (int i = 0; i < passes; ++i) {
        CCSize nextSize = {
            std::max(currentSize.width * 0.7f, 32.f),
            std::max(currentSize.height * 0.7f, 32.f)
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

        rt->begin();
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
        CCSize upSize = (i > 0) ? mips[i - 1].size : targetSize;

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

        rt->begin();
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
    CCGLProgram* p = nullptr;
    if (shaderName == "grayscale") p = getOrCreateShader("layerbg-gray"_spr, vertexShaderCell, fragmentShaderGrayscale);
    else if (shaderName == "sepia") p = getOrCreateShader("layerbg-sepia"_spr, vertexShaderCell, fragmentShaderSepia);
    else if (shaderName == "vignette") p = getOrCreateShader("layerbg-vignette"_spr, vertexShaderCell, fragmentShaderVignette);
    else if (shaderName == "bloom") p = getOrCreateShader("layerbg-bloom"_spr, vertexShaderCell, fragmentShaderBloom);
    else if (shaderName == "chromatic") p = getOrCreateShader("layerbg-chromatic"_spr, vertexShaderCell, fragmentShaderChromatic);
    else if (shaderName == "pixelate") p = getOrCreateShader("layerbg-pixelate"_spr, vertexShaderCell, fragmentShaderPixelate);
    else if (shaderName == "posterize") p = getOrCreateShader("layerbg-posterize"_spr, vertexShaderCell, fragmentShaderPosterize);
    else if (shaderName == "scanlines") p = getOrCreateShader("layerbg-scanlines"_spr, vertexShaderCell, fragmentShaderScanlines);
    return p;
}

void prewarmLevelInfoShaders() {
    static bool warmed = false;
    if (warmed) return;
    warmed = true;

    geode::log::info("[Shaders] Pre-warming LevelInfoLayer shaders...");

    // 14 shaders de kShaderTable en LevelInfoLayer.cpp
    getOrCreateShader("grayscale"_spr,       vertexShaderCell, fragmentShaderGrayscale);
    getOrCreateShader("sepia"_spr,           vertexShaderCell, fragmentShaderSepia);
    getOrCreateShader("vignette"_spr,        vertexShaderCell, fragmentShaderVignette);
    getOrCreateShader("scanlines"_spr,       vertexShaderCell, fragmentShaderScanlines);
    getOrCreateShader("bloom"_spr,           vertexShaderCell, fragmentShaderBloom);
    getOrCreateShader("chromatic-v2"_spr,    vertexShaderCell, fragmentShaderChromatic);
    getOrCreateShader("radial-blur-v2"_spr,  vertexShaderCell, fragmentShaderRadialBlur);
    getOrCreateShader("glitch-v2"_spr,       vertexShaderCell, fragmentShaderGlitch);
    getOrCreateShader("posterize"_spr,       vertexShaderCell, fragmentShaderPosterize);
    getOrCreateShader("rain"_spr,            vertexShaderCell, fragmentShaderRain);
    getOrCreateShader("matrix"_spr,          vertexShaderCell, fragmentShaderMatrix);
    getOrCreateShader("neon-pulse"_spr,      vertexShaderCell, fragmentShaderNeonPulse);
    getOrCreateShader("wave-distortion"_spr, vertexShaderCell, fragmentShaderWaveDistortion);
    getOrCreateShader("crt"_spr,             vertexShaderCell, fragmentShaderCRT);

    // Pixelate (para estilo "pixel" con GIFs)
    getOrCreateShader("pixelate"_spr,        vertexShaderCell, fragmentShaderPixelate);

    // Blur shaders (para estilos "blur" y "paimonblur")
    getOrCreateShader("blur-h-v2"_spr,  vertexShaderCell, fragmentShaderHorizontal);
    getOrCreateShader("blur-v-v2"_spr,    vertexShaderCell, fragmentShaderVertical);
    getOrCreateShader("blur-single"_spr,      vertexShaderCell, fragmentShaderBlurSinglePass);
    getOrCreateShader("paimonblur-down"_spr,  vertexShaderCell, fragmentShaderPaimonBlurDown);
    getOrCreateShader("paimonblur-up"_spr,    vertexShaderCell, fragmentShaderPaimonBlurUp);
    getOrCreateShader("paimonblur-rt"_spr,    vertexShaderCell, fragmentShaderPaimonBlurRT);

    geode::log::info("[Shaders] Pre-warm complete (21 shaders compiled)");
}

// ============================================================================
// ProgressiveBlurJob — motor de blur multi-frame
// ============================================================================

ProgressiveBlurJob::~ProgressiveBlurJob() {
    cancel();
}

CCSprite* ProgressiveBlurJob::capSourceTexture(CCTexture2D* texture, CCSize const& targetSize) {
    // Si la textura fuente es más grande que targetSize, pre-renderizar a targetSize
    // para normalizar el costo de blur independiente de la resolución original.
    float texW = texture->getContentSize().width;
    float texH = texture->getContentSize().height;

    if (texW <= targetSize.width && texH <= targetSize.height) {
        // Textura ya es <= target, no hace falta cap
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

    // Pre-render a targetSize
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

    rt->begin();
    srcSprite->visit();
    rt->end();

    rt->getSprite()->getTexture()->setTexParameters(&linearParams);

    auto capped = CCSprite::createWithTexture(rt->getSprite()->getTexture());
    capped->setFlipY(true);
    capped->setAnchorPoint({0.5f, 0.5f});
    capped->setPosition(targetSize * 0.5f);
    return capped;
}

// ---- Gaussian factory ----

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

// ---- PaimonBlur factory ----

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

// ---- Lifecycle ----

void ProgressiveBlurJob::start() {
    if (m_started || m_cancelled || m_done) return;
    m_started = true;
    retain(); // prevent dealloc while scheduled
    CCDirector::sharedDirector()->getScheduler()->scheduleSelector(
        schedule_selector(ProgressiveBlurJob::tick), this, 0.0f, false);
}

void ProgressiveBlurJob::cancel() {
    if (m_cancelled) return;
    m_cancelled = true;
    if (m_started && !m_done) {
        CCDirector::sharedDirector()->getScheduler()->unscheduleSelector(
            schedule_selector(ProgressiveBlurJob::tick), this);
        release(); // balance the retain from start()
    }
    // Liberar recursos
    m_onComplete = nullptr;
    m_currentSprite = nullptr;
    m_rtA = nullptr;
    m_rtB = nullptr;
    m_mips.clear();
    m_sourceTexture = nullptr;
}

void ProgressiveBlurJob::finish(CCSprite* result) {
    m_done = true;
    CCDirector::sharedDirector()->getScheduler()->unscheduleSelector(
        schedule_selector(ProgressiveBlurJob::tick), this);

    if (m_onComplete) {
        auto cb = std::move(m_onComplete);
        m_onComplete = nullptr;
        cb(result);
    }

    // Liberar recursos intermedios
    m_currentSprite = nullptr;
    m_rtA = nullptr;
    m_rtB = nullptr;
    m_mips.clear();
    m_sourceTexture = nullptr;

    release(); // balance the retain from start()
}

// ---- Tick dispatcher ----

void ProgressiveBlurJob::tick(float dt) {
    if (m_cancelled || m_done) return;

    if (m_blurType == BlurType::PaimonBlur)
        tickPaimonBlur();
    else
        tickGaussian();
}

// ---- Gaussian tick ----

void ProgressiveBlurJob::tickGaussian() {
    ccTexParams linearParams{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};

    if (m_phase == Phase::Setup) {
        m_blurH = getOrCreateShader("blur-h-v2"_spr, vertexShaderCell, fragmentShaderHorizontal);
        m_blurV = getOrCreateShader("blur-v-v2"_spr, vertexShaderCell, fragmentShaderVertical);
        if (!m_blurH || !m_blurV) {
            // Fallback: devolver sprite sin blur
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
        return; // yield — setup done, blur starts next frame
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

        // Siempre hacer doble-pasada para mayor calidad
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
    }
}

// ---- PaimonBlur tick ----

void ProgressiveBlurJob::tickPaimonBlur() {
    ccTexParams linearParams{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};

    if (m_phase == Phase::Setup) {
        m_blurDown = getOrCreateShader("paimonblur-down"_spr, vertexShaderCell, fragmentShaderPaimonBlurDown);
        m_blurUp = getOrCreateShader("paimonblur-up"_spr, vertexShaderCell, fragmentShaderPaimonBlurUp);
        if (!m_blurDown || !m_blurUp) {
            auto fallback = capSourceTexture(m_sourceTexture, m_targetSize);
            finish(fallback);
            return;
        }

        // Pasadas ajustadas para evitar blur excesivo y artefactos cuadrados
        m_totalPasses = std::clamp(static_cast<int>(m_intensity * 0.8f), 3, 7);
        m_currentPass = 0;
        m_mips.clear();
        m_mips.reserve(m_totalPasses);

        m_currentSprite = capSourceTexture(m_sourceTexture, m_targetSize);
        if (!m_currentSprite) {
            finish(nullptr);
            return;
        }
        m_sourceTexture->setTexParameters(&linearParams);
        m_currentSize = m_targetSize;

        m_phase = Phase::Downsample;
        return; // yield
    }

    if (m_phase == Phase::Downsample) {
        // Ejecutar N pasadas de downsample, o múltiples si la resolución es pequeña.
        // Desktop: 10 ops/tick => el blur se completa en 1 frame. Mobile: 3 ops/tick.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
        constexpr int kOpsBudget = 3;
#else
        constexpr int kOpsBudget = 10;
#endif
        int opsThisTick = 0;
        while (m_currentPass < m_totalPasses && opsThisTick < kOpsBudget) {
            CCSize nextSize = {
                std::max(m_currentSize.width * 0.7f, 32.f),
                std::max(m_currentSize.height * 0.7f, 32.f)
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

            // Escalar sprite para llenar render target más pequeño
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

            rt->begin();
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

            // Si la resolución es muy pequeña, agrupar más pasadas en este tick
            if (nextSize.width <= 64.f && nextSize.height <= 64.f) {
                opsThisTick = 0; // reset — pasadas pequeñas son gratis
            }
        }

        if (m_currentPass >= m_totalPasses || m_mips.empty()) {
            // Pasar a upsample
            m_currentPass = static_cast<int>(m_mips.size()) - 1;
            m_phase = Phase::Upsample;
        }
        return;
    }

    if (m_phase == Phase::Upsample) {
        // Ejecutar 1-2 pasadas de upsample por tick
        int opsThisTick = 0;
        while (m_currentPass >= 0 && opsThisTick < 2) {
            CCSize upSize = (m_currentPass > 0) ? m_mips[m_currentPass - 1].size : m_targetSize;

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

            rt->begin();
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

            // Pasadas pequeñas se agrupan
            if (upSize.width <= 64.f && upSize.height <= 64.f) {
                opsThisTick = 0;
            }
        }

        if (m_currentPass < 0) {
            // Terminado
            auto finalSprite = m_currentSprite.data();
            finalSprite->getTexture()->setTexParameters(&linearParams);
            finish(finalSprite);
        }
    }
}

} // namespace Shaders


