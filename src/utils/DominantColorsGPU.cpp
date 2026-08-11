#include "DominantColorsGPU.hpp"
#include "DominantColors.hpp"
#include "GLSLLoader.hpp"
#include "ImageConverter.hpp"

#include <Geode/Geode.hpp>
#include <Geode/cocos/platform/CCGL.h>
#include <Geode/cocos/CCDirector.h>
#include <Geode/cocos/textures/CCTexture2D.h>
#include <Geode/cocos/sprite_nodes/CCSprite.h>
#include <Geode/cocos/misc_nodes/CCRenderTexture.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

using namespace cocos2d;

namespace {

// Shared readback FBO; kept at file scope so context reload can invalidate it.
GLuint s_readbackFBO = 0;

// K-means on GPU-reduced LAB data (32×32 = 1024 pixels max).

struct LABPixel {
    float L, a, b;
};

static float labDistSq(LABPixel const& a, LABPixel const& b) {
    float dL = a.L - b.L;
    float da = a.a - b.a;
    float db = a.b - b.b;
    return dL * dL + da * da + db * db;
}

static DCColor labToRGB(float L, float a, float b) {
    // LAB → XYZ → linear RGB → sRGB.
    const float Xn = 0.95047f, Yn = 1.0f, Zn = 1.08883f;
    float fy = (L + 16.0f) / 116.0f;
    float fx = a / 500.0f + fy;
    float fz = fy - b / 200.0f;

    auto finv = [](float t) -> float {
        const float delta = 6.0f / 29.0f;
        return (t > delta) ? (t * t * t) : (3.0f * delta * delta * (t - 4.0f / 29.0f));
    };

    float X = Xn * finv(fx);
    float Y = Yn * finv(fy);
    float Z = Zn * finv(fz);

    float R = X *  3.2404542f + Y * -1.5371385f + Z * -0.4985314f;
    float G = X * -0.9692660f + Y *  1.8760108f + Z *  0.0415560f;
    float B = X *  0.0556434f + Y * -0.2040259f + Z *  1.0572252f;

    auto gammaInv = [](float v) -> float {
        return (v <= 0.0031308f) ? (12.92f * v) : (1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f);
    };

    R = gammaInv(R);
    G = gammaInv(G);
    B = gammaInv(B);

    auto clamp8 = [](float v) -> uint8_t {
        return static_cast<uint8_t>(std::clamp(v * 255.0f, 0.0f, 255.0f));
    };

    return DCColor{clamp8(R), clamp8(G), clamp8(B)};
}

struct Cluster {
    LABPixel centroid;
    uint32_t count = 0;
    float sumL = 0, sumA = 0, sumB = 0;
};

static std::pair<DCColor, DCColor> runMiniKMeans(std::vector<LABPixel> const& pixels) {
    if (pixels.empty()) return {DCColor{40, 40, 40}, DCColor{60, 60, 60}};

    const int K = std::min(5, static_cast<int>(pixels.size() / 20));
    if (K < 2) {
        // Too few samples: return the average.
        float sL = 0, sA = 0, sB = 0;
        for (auto const& p : pixels) { sL += p.L; sA += p.a; sB += p.b; }
        float n = static_cast<float>(pixels.size());
        DCColor avg = labToRGB(sL / n, sA / n, sB / n);
        return {avg, avg};
    }

    std::mt19937 rng(42);
    std::vector<Cluster> clusters(K);

    std::uniform_int_distribution<size_t> dist(0, pixels.size() - 1);
    clusters[0].centroid = pixels[dist(rng)];

    for (int k = 1; k < K; ++k) {
        std::vector<float> distances(pixels.size());
        float totalDist = 0.0f;
        for (size_t i = 0; i < pixels.size(); ++i) {
            float minD = std::numeric_limits<float>::max();
            for (int j = 0; j < k; ++j) {
                float d = labDistSq(pixels[i], clusters[j].centroid);
                minD = std::min(minD, d);
            }
            distances[i] = minD;
            totalDist += minD;
        }
        if (totalDist == 0.0f) break;

        std::uniform_real_distribution<float> prob(0.0f, totalDist);
        float target = prob(rng);
        float cumulative = 0.0f;
        for (size_t i = 0; i < pixels.size(); ++i) {
            cumulative += distances[i];
            if (cumulative >= target) {
                clusters[k].centroid = pixels[i];
                break;
            }
        }
    }

    for (int iter = 0; iter < 10; ++iter) {
        for (auto& c : clusters) { c.count = 0; c.sumL = c.sumA = c.sumB = 0; }

        for (auto const& p : pixels) {
            float minD = std::numeric_limits<float>::max();
            int best = 0;
            for (int k = 0; k < K; ++k) {
                float d = labDistSq(p, clusters[k].centroid);
                if (d < minD) { minD = d; best = k; }
            }
            clusters[best].count++;
            clusters[best].sumL += p.L;
            clusters[best].sumA += p.a;
            clusters[best].sumB += p.b;
        }

        bool converged = true;
        for (auto& c : clusters) {
            if (c.count == 0) continue;
            LABPixel newC{c.sumL / c.count, c.sumA / c.count, c.sumB / c.count};
            if (labDistSq(c.centroid, newC) > 1.0f) converged = false;
            c.centroid = newC;
        }
        if (converged) break;
    }

    std::sort(clusters.begin(), clusters.end(),
        [](Cluster const& a, Cluster const& b) { return a.count > b.count; });

    DCColor color1 = labToRGB(clusters[0].centroid.L, clusters[0].centroid.a, clusters[0].centroid.b);

    // Prefer a second cluster with a useful perceptual distance.
    const float DELTA_THRESHOLD_SQ = 20.0f * 20.0f; // deltaE² ≈ 400
    DCColor color2 = color1;
    for (int i = 1; i < K; ++i) {
        if (clusters[i].count == 0) continue;
        float dSq = labDistSq(clusters[0].centroid, clusters[i].centroid);
        if (dSq >= DELTA_THRESHOLD_SQ) {
            color2 = labToRGB(clusters[i].centroid.L, clusters[i].centroid.a, clusters[i].centroid.b);
            break;
        }
    }

    if (color2.r == color1.r && color2.g == color1.g && color2.b == color1.b) {
        float maxD = 0;
        for (int i = 1; i < K; ++i) {
            if (clusters[i].count == 0) continue;
            float d = labDistSq(clusters[0].centroid, clusters[i].centroid);
            if (d > maxD) {
                maxD = d;
                color2 = labToRGB(clusters[i].centroid.L, clusters[i].centroid.a, clusters[i].centroid.b);
            }
        }
        if (maxD < 100.0f) {
            LABPixel lab2 = clusters[0].centroid;
            if (std::abs(lab2.a) > std::abs(lab2.b)) {
                lab2.b += (lab2.b > 0) ? 25.0f : -25.0f;
                lab2.a *= 0.6f;
            } else {
                lab2.a += (lab2.a > 0) ? 25.0f : -25.0f;
                lab2.b *= 0.6f;
            }
            lab2.L = std::clamp(lab2.L + ((lab2.L < 50.0f) ? 15.0f : -15.0f), 0.0f, 100.0f);
            color2 = labToRGB(lab2.L, lab2.a, lab2.b);
        }
    }

    return {color1, color2};
}

// Render the texture into a 32×32 LAB FBO.

static constexpr int kDownsampleSize = 32;

static std::pair<DCColor, DCColor> gpuExtract(CCTexture2D* texture) {
    if (!texture) return {DCColor{0, 0, 0}, DCColor{0, 0, 0}};

    auto* shader = paimon::shaders::getDominantColorsDownsampleShader();
    if (!shader) return {DCColor{0, 0, 0}, DCColor{0, 0, 0}};

    auto* sprite = CCSprite::createWithTexture(texture);
    if (!sprite) return {DCColor{0, 0, 0}, DCColor{0, 0, 0}};

    CCSize dstSize(static_cast<float>(kDownsampleSize), static_cast<float>(kDownsampleSize));

    float sx = dstSize.width / texture->getContentSize().width;
    float sy = dstSize.height / texture->getContentSize().height;
    sprite->setScale(std::max(sx, sy));
    sprite->setAnchorPoint({0.5f, 0.5f});
    sprite->setPosition(dstSize * 0.5f);
    sprite->setFlipY(true);

    sprite->setShaderProgram(shader);

    ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    texture->setTexParameters(&params);

    auto* rt = CCRenderTexture::create(kDownsampleSize, kDownsampleSize);
    if (!rt) return {DCColor{0, 0, 0}, DCColor{0, 0, 0}};

    rt->beginWithClear(0.f, 0.f, 0.f, 0.f);
    sprite->visit();
    rt->end();

    auto* rtSprite = rt->getSprite();
    if (!rtSprite || !rtSprite->getTexture()) return {DCColor{0, 0, 0}, DCColor{0, 0, 0}};

    if (s_readbackFBO == 0) {
        glGenFramebuffers(1, &s_readbackFBO);
    }
    GLint oldFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, s_readbackFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           rtSprite->getTexture()->getName(), 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
        return {DCColor{0, 0, 0}, DCColor{0, 0, 0}};
    }

    std::vector<uint8_t> pixels(kDownsampleSize * kDownsampleSize * 4);
    glReadPixels(0, 0, kDownsampleSize, kDownsampleSize, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);

    std::vector<LABPixel> labPixels;
    labPixels.reserve(kDownsampleSize * kDownsampleSize);

    for (int i = 0; i < kDownsampleSize * kDownsampleSize; ++i) {
        uint8_t r = pixels[i * 4 + 0];
        uint8_t g = pixels[i * 4 + 1];
        uint8_t b = pixels[i * 4 + 2];
        uint8_t a = pixels[i * 4 + 3];

        // Transparent pixels are filtered out by the shader.
        if (a < 128) continue;

        // Decode the shader's normalized LAB channels.
        float L = (r / 255.0f) * 100.0f;           // [0,1] → [0,100]
        float la = (g / 255.0f) * 255.0f - 128.0f; // [0,1] → [-128,127]
        float lb = (b / 255.0f) * 255.0f - 128.0f; // [0,1] → [-128,127]

        labPixels.push_back({L, la, lb});
    }

    if (labPixels.size() < 20) {
        return {DCColor{0, 0, 0}, DCColor{0, 0, 0}};
    }

    return runMiniKMeans(labPixels);
}

}

namespace DominantColorsGPU {

bool isAvailable() {
    static int cached = -1;
    if (cached >= 0) return cached != 0;

    auto* director = CCDirector::get();
    if (!director || !director->getOpenGLView()) {
        cached = 0;
        return false;
    }

    auto* shader = paimon::shaders::getDominantColorsDownsampleShader();
    cached = (shader != nullptr) ? 1 : 0;
    return cached != 0;
}

void onGLContextReload() {
    // Delete while the old context is active; the FBO is recreated lazily.
    if (s_readbackFBO != 0) {
        glDeleteFramebuffers(1, &s_readbackFBO);
        s_readbackFBO = 0;
    }
}

std::pair<DCColor, DCColor> extractFromTexture(CCTexture2D* texture) {
    if (!texture) return {DCColor{0, 0, 0}, DCColor{0, 0, 0}};

    if (!isAvailable()) {
        return {DCColor{40, 40, 40}, DCColor{60, 60, 60}};
    }

    auto result = gpuExtract(texture);

    if (result.first.r == 0 && result.first.g == 0 && result.first.b == 0 &&
        result.second.r == 0 && result.second.g == 0 && result.second.b == 0) {
        return {DCColor{40, 40, 40}, DCColor{60, 60, 60}};
    }

    return result;
}

std::pair<DCColor, DCColor> extractFromRGB(const uint8_t* rgb, int width, int height) {
    if (!rgb || width <= 0 || height <= 0) return {DCColor{0, 0, 0}, DCColor{0, 0, 0}};

    if (!isAvailable()) {
        return DominantColors::extract(rgb, width, height);
    }

    // Upload RGB as a temporary RGBA texture.
    size_t pixelCount = static_cast<size_t>(width) * height;
    std::vector<uint8_t> rgba(pixelCount * 4);
    ImageConverter::rgbToRgbaFast(rgb, rgba.data(), pixelCount);

    auto* texture = new CCTexture2D();
    if (!texture->initWithData(rgba.data(), kCCTexture2DPixelFormat_RGBA8888,
                               width, height, CCSize(static_cast<float>(width), static_cast<float>(height)))) {
        texture->release();
        return DominantColors::extract(rgb, width, height);
    }

    auto result = gpuExtract(texture);
    texture->release();

    if (result.first.r == 0 && result.first.g == 0 && result.first.b == 0 &&
        result.second.r == 0 && result.second.g == 0 && result.second.b == 0) {
        return DominantColors::extract(rgb, width, height);
    }

    return result;
}

std::pair<DCColor, DCColor> extractFromRGBA(const uint8_t* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) return {DCColor{0, 0, 0}, DCColor{0, 0, 0}};

    if (!isAvailable()) {
        size_t pixelCount = static_cast<size_t>(width) * height;
        std::vector<uint8_t> rgb(pixelCount * 3);
        ImageConverter::rgbaToRgbFast(rgba, rgb.data(), pixelCount);
        return DominantColors::extract(rgb.data(), width, height);
    }

    auto* texture = new CCTexture2D();
    if (!texture->initWithData(rgba, kCCTexture2DPixelFormat_RGBA8888,
                               width, height, CCSize(static_cast<float>(width), static_cast<float>(height)))) {
        texture->release();
        size_t pixelCount = static_cast<size_t>(width) * height;
        std::vector<uint8_t> rgb(pixelCount * 3);
        ImageConverter::rgbaToRgbFast(rgba, rgb.data(), pixelCount);
        return DominantColors::extract(rgb.data(), width, height);
    }

    auto result = gpuExtract(texture);
    texture->release();

    if (result.first.r == 0 && result.first.g == 0 && result.first.b == 0 &&
        result.second.r == 0 && result.second.g == 0 && result.second.b == 0) {
        size_t pixelCount = static_cast<size_t>(width) * height;
        std::vector<uint8_t> rgb(pixelCount * 3);
        ImageConverter::rgbaToRgbFast(rgba, rgb.data(), pixelCount);
        return DominantColors::extract(rgb.data(), width, height);
    }

    return result;
}

}
